// Frame pacing (PLAN 4.7) over VK_GOOGLE_display_timing. See warden_frame_pacing.h.
//
// The thread that presents is the GPU command processor's (paint mode
// kGuestOutputThreadImmediately) and the GPU is nearly always busy, so nothing here may
// make that thread wait - no sleeping until a refresh, as Swappy does. Instead each
// present carries an id and, in the paced mode, a time before which the compositor must
// not show it (desiredPresentTime). After each present, vkGetPastPresentationTimingGOOGLE
// gives the frames whose fate is known: when each reached the panel (actualPresentTime),
// the earliest refresh it could have (earliestPresentTime), and how long it waited,
// finished, for the compositor to take it (presentMargin, less the refreshes already
// counted into the earliest time).
//
// Paced: each frame is held for the refresh nearest its place on the title's own
// timeline, which advances by the title's measured frame period - near two vblanks of its
// 60 Hz at its 30 fps cap (33.42 ms at the King's Camp: a frame now and then takes three).
// The Thor's "120 Hz" panel refreshes every 8.45 ms (SurfaceFlinger's vsync model and the
// panel's own times, run 279), so 30 fps cannot be four refreshes apart throughout (that
// is 29.6 fps): on the timeline it is four, and three about every twentieth frame, spread
// evenly - never five and then three, which is the judder an unpaced frame finished a
// little late makes. The slack the timeline keeps ahead of the frames - its offset - tracks
// a late-frame rate: half a refresh later for a frame late for its slot, and a small step
// earlier for each frame on time, so that about one frame in two hundred is late; a frame
// held two refreshes or more pulls it in by a sixteenth of the excess at once. That is the
// least slack for the cadence, the title's own frames a vblank late now and then included:
// whole-refresh moves kept three times as much (runs 283-284). Once the offset has found
// its edge, a steady climb or fall of it means the timeline's period is off, and goes into
// it. A timeline starts at the latency the unpaced frames showed (their 90th percentile,
// four refreshes until one is measured), and starts again when the title pauses or changes
// its rate.
//
// The mode is the files/frame-pacing marker's (warden_gpu.cc passes it on as
// WARDEN_FRAME_PACING): "off" - each frame shown as soon as it is finished, as before -
// "paced", or "alternate": a minute of each in turn from the first present, to compare the
// two in one run - or "phases": paced throughout, the desired time 4, 3, 2 and 1 eighths of
// a refresh after the slot a minute each. Without the marker, the frame rate's
// (FramePacingFollowFrameRate): paced at the title's own 30 - at the King's Camp 6.8% of
// frames off the four-refresh cadence against 11.1% off, for 8 ms more from the title's
// swap to the panel (run 285); 6.2-6.9% with the title's frames even (run 298), where
// 4.1% is the floor - and off above it.
#include "warden_frame_pacing.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "warden_frame_rate.h"
#include "warden_frame_times.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_api.h"

// warden_frame_generation.cc: the frames generated between each two of the game's.
extern "C" uint32_t warden_frame_generation_count();

// libvulkan's own export: the runtime's vulkan_api.h declares no prototypes.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                       const char* name);

namespace warden {

namespace {

// The paced modes differ only in how far after its slot a frame's desired time is, in
// eighths of a refresh: "paced" a quarter of a refresh; "phase4" to "phase1" four to one
// eighths, for the "phases" setting to compare. Run 298 compared them (named "paced",
// "paced3", "paced2" and "paced1" then): no frame shown early in any, 6.2-6.9% off the
// four-refresh cadence in all; a quarter leaves the compositor's times room either way.
enum Mode : int { kOff, kPaced, kPhase4, kPhase3, kPhase2, kPhase1, kModeCount };
const char* const kModeNames[kModeCount] = {"off",    "paced",  "phase4",
                                            "phase3", "phase2", "phase1"};
constexpr int64_t kEighthsAfter[kModeCount] = {0, 2, 4, 3, 2, 1};

constexpr int64_t kSecond = 1000000000;
constexpr int64_t kVblank = kSecond / 60;  // the title's vblank (graphics_system.cc)
constexpr int64_t kAlternatePeriod = 60 * kSecond;
// The late-frame rate the offset tracks: one frame in kLateRateInverse. A late frame is a
// judder (five refreshes, then three).
constexpr int64_t kLateRateInverse = 200;

int64_t Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// A present whose timing has not come back yet.
struct Sent {
  uint32_t id = 0;
  int64_t cpu = 0;   // when it was presented
  int64_t swap = 0;  // when the title swapped it, or 0
  int64_t slot = 0;  // paced: the refresh it was held for; 0 unpaced
  int mode = kOff;
  int role = 0;      // frame generation's pair: 0 none, 1 its generated, 2 its real
};

// Per mode, since the previous summary.
struct ModeStats {
  std::vector<int64_t> panel;    // between consecutive frames on the panel
  std::vector<int64_t> latency;  // present -> panel
  std::vector<int64_t> swap_latency;  // the title's swap -> panel
  std::vector<int64_t> acquire;       // the presenter's wait for a swapchain image
  int ready_earlier[4] = {};     // finished 0, 1, 2, 3+ refreshes before it was taken
  int lost = 0;                  // never shown (replaced in the mailbox) or no times
  int late = 0;                  // paced: on the panel a refresh or more after its slot
  int held = 0;                  // ... of them finished in time: held by the desired time
  int early = 0;                 // paced: on the panel a refresh or more before its slot
  int pushes = 0;                // paced: the offset pushed later for one
  int grids = 0;                 // paced: the timeline started again
  int grids_ahead = 0;           // ... of them because it had run too far ahead of the frames
  int slots[3] = {};             // paced: slots 3 or fewer, 4, 5 or more refreshes apart
  std::vector<int64_t> pair_gap;  // frame generation: a pair's generated -> real frame
  std::vector<int64_t> pair_target;  // ... what was asked
  int pair_late = 0;             // pair presents on the panel a refresh or more late
};

std::mutex g_mutex;
bool g_initialized = false;
VkDevice g_device = VK_NULL_HANDLE;
PFN_vkGetPastPresentationTimingGOOGLE g_get_past = nullptr;
PFN_vkGetRefreshCycleDurationGOOGLE g_get_refresh = nullptr;
bool g_alternate = false;
bool g_phases = false;
bool g_explicit = false;  // files/frame-pacing chose the mode
int g_fixed_mode = kOff;
// PLAN 9.14: frame generation (FramePacingFrameGeneration). Each guest output with a
// generated frame is presented as a pair at once (dao_frame_pacer_android.cc names each
// present's role: warden_present_role) - both paints run on the GPU right after the
// title's frame: on one queue, a paint made later would wait behind the next frame's
// work (run 427). So the spacing is the compositor's, by each present's desired time,
// on one timeline: the generated frame half a title frame after the pair before's real
// one, but never before it can be ready (its present plus the pairs' readiness, below)
// nor more than half a frame after that; the real frame half a frame after its
// generated one. Every time later than the one before: Android's buffer queue drops a
// queued frame when the one behind it is due too - which, with the generated frames due
// at once, dropped every real frame (run 428). The readiness tracks the late frames as
// the paced mode's offset does: half a refresh more for a frame on the panel a refresh
// or more after its target (once a burst), a little less for each on time.
// BUG-092: a guest output without a generated frame in a short gap between groups is a
// group of one (role 4): its real frame two steps after the group before's - where its
// title frame falls - and taking its swap, so the timeline goes on unbroken.
bool g_frame_generation = false;
int g_role = 0;                  // the next present's role
int64_t g_ready = 0;             // a pair's present -> the refresh it can make (0: unknown)
uint32_t g_ready_settled_after = 0;  // late pairs up to this id were on an older readiness
int64_t g_pair_last_target = 0;  // the previous pair present's target (0: none)
uint32_t g_pair_id = 0;          // the latest group present's id
int64_t g_pair_actual = 0;       // ... on the panel (feedback), with its id
uint32_t g_pair_actual_id = 0;
// files/frame-generation-timing (a test switch): "off" presents the pairs with no
// desired times, "alternate" changes between timed and not every kPairTimingPeriod.
int g_pair_timing = 0;  // 0 timed, 1 off, 2 alternate
bool g_pairs_timed = true;  // the latest pair's
constexpr int64_t kPairTimingPeriod = 10 * kSecond;
int64_t g_start = 0;
int g_last_mode = kOff;
int64_t g_mode_since = 0;

VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
int64_t g_refresh = 0;  // the panel's refresh period, as Android reports it
int64_t g_measured = 0;  // ... as the panel's own times say it (a moving average)
int64_t g_refresh_checked = 0;
uint32_t g_next_id = 1;
std::array<Sent, 64> g_sent;  // by id % 64
int64_t g_vsync = 0;          // the latest refresh a frame reached the panel at
int64_t g_last_actual = 0;
int64_t g_period_sum = 0;     // the panel's own period: intervals between frames on it
int64_t g_period_refreshes = 0;  // ... over the refreshes they span

// The paced timeline.
int64_t g_ideal = 0;           // the previous frame's place on the title's timeline
int64_t g_vblanks = 2;         // the title's frame period, in its vblanks
int64_t g_lead = 0;            // present -> panel, for a new timeline
int64_t g_offset = 0;          // the slack kept ahead of the frames, tracking kLateRateInverse
uint32_t g_settled_after = 0;  // late frames up to this id were on an older offset
int64_t g_last_slot = 0;       // the previous paced frame's slot
bool g_integrating = false;    // the offset has met its edge since the timeline started
// The period's correction: the title's mean interval over 64 frames is a noisy measure of
// its rate, and a timeline a few hundredths of a millisecond a frame fast wears its slack
// away in seconds (run 282). The offset then climbs steadily; a 512th of each of its steps
// goes into the period.
int64_t g_bias = 0;
std::vector<int64_t> g_unpaced_latency;  // the off mode's present -> panel, toward g_lead

ModeStats g_stats[kModeCount];

VkPresentTimeGOOGLE g_time = {};
VkPresentTimesInfoGOOGLE g_times = {};

// The refresh period the pacing works in: the panel's own, once its times have said it -
// Android's figure is 8.333 ms most of the time on the Thor, the panel's 8.445 (run 280).
int64_t Refresh() { return g_measured ? g_measured : g_refresh; }

// Without files/frame-pacing, the frame rate's mode: paced at the title's own 30. Above
// it each frame is shown as soon as it is finished: the paced timeline expects the
// title's steady period, and where the GPU holds the title under the rate (44 fps in the
// Korcari Wilds at 60) it held frames two refreshes and then five (p95 42 ms); unpaced
// they take two or three, the 1% low 39.4 fps, 9 ms less latency (runs 360-361).
int ModeForFrameRate() { return TitleFrameRate() == 30.0 ? kPaced : kOff; }

int CurrentMode(int64_t now) {
  const int64_t period = (now - g_start) / kAlternatePeriod;
  if (g_phases) {
    return kPhase4 + int(period % (kModeCount - kPhase4));
  }
  return g_alternate ? int(period % 2) : g_fixed_mode;
}

// The refresh nearest to `t` on the panel's grid, g_vsync + k * Refresh(): in
// (t - refresh / 2, t + refresh / 2].
int64_t NearestRefresh(int64_t t) {
  const int64_t r = Refresh();
  int64_t refresh = g_vsync + (t - g_vsync) / r * r;
  while (refresh > t + r / 2) {
    refresh -= r;
  }
  while (refresh + r / 2 <= t) {
    refresh += r;
  }
  return refresh;
}

int64_t RefreshAtOrAfter(int64_t t) {
  const int64_t refresh = NearestRefresh(t);
  return refresh < t ? refresh + Refresh() : refresh;
}

// The refresh frame `id`, presented at `now`, is held for: the one nearest the frame's place
// on the title's timeline.
int64_t PacedSlot(int64_t now, uint32_t id, int mode) {
  const int64_t r = Refresh();
  ModeStats& stats = g_stats[mode];
  const int64_t frame = RecentGuestFrameInterval();
  const int64_t vblanks =
      frame > 0 ? std::clamp<int64_t>((frame + kVblank / 2) / kVblank, 1, 4) : 2;
  // The timeline moves at the title's measured rate, not its nominal one: at its 30 fps cap
  // the title makes 29.9 (a frame now and then takes three vblanks), and a timeline at 30
  // runs ahead of the frames by a refresh every three seconds (run 280).
  const int64_t nominal = vblanks * kVblank;
  const int64_t mean = RecentGuestMeanInterval();
  const int64_t period =
      (mean ? std::clamp(mean, nominal * 9 / 10, nominal * 11 / 10) : nominal) + g_bias;
  if (!g_lead) {
    g_lead = 4 * r;  // until the unpaced frames have measured it
  }
  int64_t slot = 0;
  if (g_ideal && vblanks == g_vblanks) {
    g_ideal += period;
    // The panel's own grid each time, so a refresh period a little off the reported one
    // cannot walk the slots off the refreshes.
    slot = NearestRefresh(g_ideal + g_offset);
  }
  // A new timeline: the first paced frame, the title's rate changed, the frames fell behind
  // it (the title paused - a load, the pause menu) or it ran a period ahead of them. After
  // a pause the title's catch-up frames, a vblank apart, each move the timeline a whole
  // period: with two periods allowed (to run 300), the catch-up after three stalls left the
  // frames 72 ms from present to panel for good, the offset pinned at its floor (run 299).
  const bool ahead = slot > now + g_lead + g_offset + period;
  if (!slot || slot < now + r || ahead) {
    slot = RefreshAtOrAfter(now + g_lead);
    g_ideal = slot;
    g_offset = 0;
    g_settled_after = id - 1;
    g_integrating = false;
    ++stats.grids;
    stats.grids_ahead += ahead;
  } else if (g_last_slot) {
    const int64_t refreshes = (slot - g_last_slot + r / 2) / r;
    ++stats.slots[refreshes <= 3 ? 0 : refreshes == 4 ? 1 : 2];
  }
  g_last_slot = slot;
  if (vblanks != g_vblanks) {
    g_bias = 0;  // another rate: what was learned about the old one does not apply
  }
  g_vblanks = vblanks;
  return slot;
}

void Feedback(const VkPastPresentationTimingGOOGLE& timing) {
  const Sent& sent = g_sent[timing.presentID % g_sent.size()];
  if (sent.id != timing.presentID) {
    return;  // from before the swapchain was made again
  }
  ModeStats& stats = g_stats[sent.mode];
  const int64_t actual = int64_t(timing.actualPresentTime);
  if (!actual) {
    ++stats.lost;
    return;
  }
  const int64_t r = Refresh();
  g_vsync = actual;
  if (g_last_actual && actual > g_last_actual) {
    stats.panel.push_back(actual - g_last_actual);
    const int64_t refreshes = (actual - g_last_actual + r / 2) / r;
    if (refreshes >= 1 && refreshes <= 8) {
      g_period_sum += actual - g_last_actual;
      g_period_refreshes += refreshes;
      const int64_t period = (actual - g_last_actual) / refreshes;
      g_measured = g_measured ? g_measured + (period - g_measured) / 64 : period;
    }
  }
  g_last_actual = actual;
  const int64_t latency = actual - sent.cpu;
  stats.latency.push_back(latency);
  if (sent.swap) {
    stats.swap_latency.push_back(actual - sent.swap);
  }
  // How long the finished frame waited for the compositor to take it: the margin, plus the
  // refreshes Android already took off it to name an earlier possible refresh. A frame
  // taken before the GPU had finished it has a negative margin (wrapped); none, here.
  const int64_t waited =
      std::max<int64_t>(0, int64_t(timing.presentMargin)) +
      std::max<int64_t>(0, actual - int64_t(timing.earliestPresentTime));
  const int ready_earlier = int(std::min<int64_t>(3, waited / r));
  ++stats.ready_earlier[ready_earlier];

  if (sent.role) {
    // The gaps between a group's frames on the panel.
    if (sent.role != 1 && g_pair_actual && g_pair_actual_id + 1 == sent.id) {
      stats.pair_gap.push_back(actual - g_pair_actual);
    }
    g_pair_actual = actual;
    g_pair_actual_id = sent.id;
    if (sent.slot && g_ready) {
      // As the paced mode's offset: a frame late but finished a refresh or more before it
      // was taken was held by the compositor's phase, not its readiness (run 431: counted
      // late, they wound the readiness up to 101 ms, every image held and the title made
      // to wait for one); one that waited two refreshes or more pulls it in by a sixteenth
      // of the excess at once.
      const bool late = actual >= sent.slot + r / 2;
      const bool held = late && waited >= r;
      stats.pair_late += late && !held;
      if (!late) {
        g_ready -= waited >= 2 * r ? (waited - 3 * r / 2) / 16
                                   : (r / 2) / (kLateRateInverse - 1);
      } else if (!held && sent.id > g_ready_settled_after) {
        g_ready += r / 2;
        g_ready_settled_after = g_next_id - 1;
      }
      g_ready = std::clamp<int64_t>(g_ready, r, 12 * r);
    }
    return;
  }
  if (sent.mode == kOff) {
    g_unpaced_latency.push_back(latency);
    if (g_unpaced_latency.size() >= 64) {
      auto p90 = g_unpaced_latency.begin() + g_unpaced_latency.size() * 9 / 10;
      std::nth_element(g_unpaced_latency.begin(), p90, g_unpaced_latency.end());
      g_lead = *p90;
      g_unpaced_latency.clear();
    }
    return;
  }
  if (!sent.slot) {
    return;
  }
  const bool late = actual >= sent.slot + r / 2;
  // Late, but finished a refresh or more before the compositor took it: held past its
  // slot's refresh by its desired time - the phase's doing, not the slack's (run 296: 2.9%
  // of frames, ready in time, a refresh later than the rest). The other edge of the phase:
  // shown a refresh or more before the slot.
  const bool held = late && waited >= r;
  stats.late += late;
  stats.held += held;
  stats.early += actual <= sent.slot - r / 2;
  if (!g_ideal || held) {
    return;
  }
  int64_t step = 0;
  if (!late) {
    if (waited >= 2 * r) {
      // Held two refreshes or more: far from the edge - a new timeline's lead, or a lighter
      // stretch. A sixteenth of the excess at once: the frames in flight (three or four)
      // see a quarter of it before this comes back.
      step = -(waited - 3 * r / 2) / 16;
    } else {
      step = -(r / 2) / (kLateRateInverse - 1);
    }
  } else if (timing.presentID > g_settled_after) {
    // Once a burst: the frames already presented were held on the old offset.
    step = r / 2;
    g_settled_after = g_next_id - 1;
    g_integrating = true;
    ++stats.pushes;
  }
  g_offset = std::clamp<int64_t>(g_offset + step, -2 * r, 6 * r);
  // Every step goes into the period once the offset has found its edge, the pull-ins too:
  // without them the pushes alone wound it up - +0.1 ms a frame by the end of run 296, the
  // offset pinned at its floor and the timeline started again 19 times in 11 minutes - and
  // with the pull-ins cut down to an on-time frame's step, again (run 300: +0.13 ms).
  if (g_integrating) {
    g_bias = std::clamp<int64_t>(g_bias + step / 512, -r / 16, r / 16);
  }
}

void Poll(VkDevice device) {
  for (int round = 0; round < 4; ++round) {
    VkPastPresentationTimingGOOGLE timings[16];
    uint32_t count = 16;
    const VkResult result = g_get_past(device, g_swapchain, &count, timings);
    if (result < 0) {
      return;
    }
    for (uint32_t i = 0; i < count; ++i) {
      Feedback(timings[i]);
    }
    if (result != VK_INCOMPLETE) {
      return;
    }
  }
}

// For each device that presents: the bring-up provider's presents first (Runtime::Startup),
// before warden_gpu.cc has read the marker, and the graphics system's own device after it.
void Initialize(VkDevice device) {
  g_initialized = true;
  g_device = device;
  g_swapchain = VK_NULL_HANDLE;
  g_get_past = reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
      vkGetDeviceProcAddr(device, "vkGetPastPresentationTimingGOOGLE"));
  g_get_refresh = reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(
      vkGetDeviceProcAddr(device, "vkGetRefreshCycleDurationGOOGLE"));
  const char* setting = std::getenv("WARDEN_FRAME_PACING");
  g_alternate = setting && strcmp(setting, "alternate") == 0;
  g_phases = setting && strcmp(setting, "phases") == 0;
  g_explicit = setting != nullptr;
  g_fixed_mode = !setting                      ? ModeForFrameRate()
                 : strcmp(setting, "off") == 0 ? kOff
                                               : kPaced;
  g_start = Now();
  if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
    if (FILE* file = std::fopen((std::string(dir) + "/frame-generation-timing").c_str(),
                                "r")) {
      char word[16] = {};
      if (std::fscanf(file, "%15s", word) == 1) {
        g_pair_timing = strcmp(word, "off") == 0 ? 1 : strcmp(word, "alternate") == 0 ? 2 : 0;
      }
      std::fclose(file);
      XELOGW("frame pacing: frame generation's pairs {} (files/frame-generation-timing)",
             g_pair_timing == 1 ? "untimed" : g_pair_timing == 2 ? "timed and untimed in turn"
                                                                : "timed");
    }
  }
  g_last_mode = CurrentMode(g_start);
  g_mode_since = g_start;
  XELOGI("frame pacing: {}, {}",
         g_get_past && g_get_refresh ? "the panel's times through VK_GOOGLE_display_timing"
                                     : "no display timing functions",
         g_phases      ? "paced with the desired time 4, 3, 2 and 1 eighths of a refresh "
                         "after the slot, a minute each"
         : g_alternate ? "off and paced a minute each"
                       : kModeNames[g_fixed_mode]);
}

}  // namespace

std::string FramePacingSummary() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized || !g_get_past) {
    return std::string();
  }
  const int64_t now = Now();
  std::string text = fmt::format(
      "mode {} for {} s, refresh {:.3f} ms (measured {:.3f}), period {} vblanks, bias {:+.4f} ms, "
      "lead {:.1f} ms, offset {:.1f} ms",
      kModeNames[CurrentMode(now)], (now - g_mode_since) / kSecond, double(g_refresh) / 1e6,
      g_period_refreshes ? double(g_period_sum) / double(g_period_refreshes) / 1e6 : 0.0,
      g_vblanks, double(g_bias) / 1e6, double(g_lead) / 1e6, double(g_offset) / 1e6);
  g_period_sum = 0;
  g_period_refreshes = 0;
  for (int mode = 0; mode < kModeCount; ++mode) {
    ModeStats& stats = g_stats[mode];
    if (stats.latency.empty() && !stats.lost) {
      continue;
    }
    auto percentiles = [](std::vector<int64_t> values) {
      if (values.empty()) {
        return std::string("none");
      }
      std::sort(values.begin(), values.end());
      auto at = [&](double p) {
        return double(values[std::min(values.size() - 1,
                                      size_t(p * double(values.size() - 1) + 0.5))]) /
               1e6;
      };
      return fmt::format("p50 {:.1f} p95 {:.1f} max {:.1f}", at(0.50), at(0.95),
                         double(values.back()) / 1e6);
    };
    text += fmt::format(
        " | {} {}; latency ms {}; swap to panel ms {}; acquire ms {}; ready earlier 0:{} 1:{} "
        "2:{} 3+:{}; lost {}",
        kModeNames[mode], DescribeIntervals("panel", std::move(stats.panel), Refresh(), "refreshes"),
        percentiles(std::move(stats.latency)), percentiles(std::move(stats.swap_latency)),
        percentiles(std::move(stats.acquire)),
        stats.ready_earlier[0], stats.ready_earlier[1], stats.ready_earlier[2],
        stats.ready_earlier[3], stats.lost);
    if (!stats.pair_gap.empty()) {
      int64_t target_sum = 0;
      for (int64_t target : stats.pair_target) {
        target_sum += target;
      }
      text += fmt::format(
          "; frame generation's groups, asked {:.1f} ms apart, readiness {:.1f} ms, {} late: "
          "{}",
          stats.pair_target.empty()
              ? 0.0
              : double(target_sum) / double(stats.pair_target.size()) / 1e6,
          double(g_ready) / 1e6, stats.pair_late,
          DescribeIntervals("within a group", std::move(stats.pair_gap), Refresh(),
                            "refreshes"));
    }
    if (mode != kOff) {
      text += fmt::format(
          "; late {}; held {}; early {}; pushed {}; new {}; ahead {}; slots 3x{} 4x{} 5x{}",
          stats.late, stats.held, stats.early, stats.pushes, stats.grids, stats.grids_ahead,
          stats.slots[0], stats.slots[1], stats.slots[2]);
    }
    stats = ModeStats();
  }
  return text;
}

void FramePacingFrameGeneration(bool on) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (on != g_frame_generation) {
    g_frame_generation = on;
    XELOGI("frame pacing: {}", on ? "frame generation's pairs - the real frame held for "
                                       "half a title frame after the generated one"
                                  : "as before frame generation");
  }
}

}  // namespace warden

// BUG-092: whether guest outputs are paced as frame generation's groups now - the
// presenter then hands one without a generated frame to the groups' thread as a group of
// one (dao_frame_pacer_android.cc).
extern "C" bool warden_frame_pacing_groups() {
  std::lock_guard<std::mutex> lock(warden::g_mutex);
  return warden::g_frame_generation;
}

namespace warden {

void FramePacingFollowFrameRate() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_explicit || g_alternate || g_phases) {
    return;
  }
  const int mode = ModeForFrameRate();
  if (mode != g_fixed_mode) {
    g_fixed_mode = mode;
    XELOGI("frame pacing: {} at {} fps", kModeNames[mode], FrameRate());
  }
}

}  // namespace warden

extern "C" const void* warden_present_timing(VkDevice device, VkSwapchainKHR swapchain,
                                              bool display_timing) {
  using namespace warden;
  if (!display_timing) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (device != g_device) {
    Initialize(device);
  }
  if (!g_get_past || !g_get_refresh) {
    return nullptr;
  }
  const int64_t now = Now();
  if (swapchain != g_swapchain) {
    g_swapchain = swapchain;
    g_sent = {};
    g_vsync = 0;
    g_last_actual = 0;
    g_ideal = 0;
    g_last_slot = 0;
    g_refresh_checked = 0;
  }
  if (!g_refresh_checked || now - g_refresh_checked >= kSecond) {
    g_refresh_checked = now;
    VkRefreshCycleDurationGOOGLE cycle = {};
    if (g_get_refresh(device, swapchain, &cycle) == VK_SUCCESS && cycle.refreshDuration) {
      const int64_t refresh = int64_t(cycle.refreshDuration);
      // Android's figure moves between the mode's nominal period and its vsync model's
      // (8.333 and 8.447 ms on the Thor, run 279): only a new display mode - a change of
      // more than 5% - starts a new grid.
      if (!g_refresh || std::abs(refresh - g_refresh) * 20 > g_refresh) {
        g_ideal = 0;
        g_measured = 0;
        XELOGI("frame pacing: the panel refreshes every {:.3f} ms", double(refresh) / 1e6);
      }
      g_refresh = refresh;
    }
  }
  const int mode = g_frame_generation ? kOff : CurrentMode(now);
  if (mode != g_last_mode) {
    g_last_mode = mode;
    g_mode_since = now;
  }
  const uint32_t id = g_next_id++;
  const int role = g_frame_generation ? g_role : 0;
  int64_t slot = 0;
  if (mode != kOff && g_refresh > 0 && g_vsync > 0) {
    slot = PacedSlot(now, id, mode);
  } else {
    g_ideal = 0;
    g_last_slot = 0;
  }
  // Frame generation's pair: its present's target on the panel (above, g_frame_generation).
  if (role == 1 || role == 4) {
    g_pairs_timed = g_pair_timing == 0 ||
                    (g_pair_timing == 2 && (now - g_start) / kPairTimingPeriod % 2 == 0);
  }
  if (role && g_pairs_timed) {
    // The group's frames the title's frame interval apart divided among them: half of it
    // with 2x, a quarter with 4x.
    const int64_t r = Refresh() > 0 ? Refresh() : kSecond / 120;
    const int64_t interval = RecentGuestFrameInterval();
    const int64_t step = (interval > 0 ? interval : kSecond / 30) /
                         int64_t(std::max(warden_frame_generation_count(), 1u) + 1);
    if (!g_ready) {
      g_ready = 4 * r;
    }
    const int64_t ready = now + g_ready;
    if (role == 1 || role == 4) {
      // A group's first: a step after the group before's real frame, never before it can
      // be ready nor more than a step after that. A group of one (BUG-092): its real frame
      // where a group's would be, two steps after.
      const int64_t steps = role == 4 ? 2 * step : step;
      slot = g_pair_last_target
                 ? std::clamp<int64_t>(g_pair_last_target + steps, ready, ready + steps)
                 : ready;
    } else {
      // Its later frames: a step after the one before.
      const bool grouped = g_pair_id + 1 == id;
      slot = grouped ? std::max<int64_t>(g_pair_last_target + step, ready) : ready;
      if (grouped && g_stats[mode].pair_target.size() < 4096) {
        g_stats[mode].pair_target.push_back(slot - g_pair_last_target);
      }
    }
    g_pair_id = id;
    if (g_vsync > 0 && Refresh() > 0) {
      slot = NearestRefresh(slot);
    }
    g_pair_last_target = slot;
  } else if (g_frame_generation) {
    g_pair_last_target = 0;
  }
  // A group's generated frames take no swap: the title's frame is its real one.
  g_sent[id % g_sent.size()] =
      Sent{id, now, role == 1 || role == 3 ? 0 : TakeOldestGuestSwap(), slot, mode, role};
  g_time.presentID = id;
  // A frame reaches the panel a refresh before the one its desired time is nearest, by the
  // panel's own times: the compositor's expected present times run about a refresh ahead
  // of them. With the desired time half a refresh before the slot (runs 279-297) every
  // frame was shown a refresh before its slot (run 297), so a frame shown at its slot was
  // in fact a refresh late - and never counted late, never pushing the offset: the five-
  // then-three judder of run 296. So a quarter of a refresh after the slot (or the mode's
  // eighths) - the slot's refresh, by the panel's times. Unpaced, the present's own time -
  // due at once, as with no time at all, and no older time left in force for this frame.
  g_time.desiredPresentTime =
      uint64_t(slot ? slot + Refresh() * (role ? 2 : kEighthsAfter[mode]) / 8 : now);
  g_times.sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
  g_times.pNext = nullptr;
  g_times.swapchainCount = 1;
  g_times.pTimes = &g_time;
  return &g_times;
}

extern "C" int64_t warden_acquire_begin() { return warden::Now(); }

// PLAN 9.14, a test switch: the frames the command processor lets the GPU be behind (the
// graft's frame wait in vulkan_command_processor.cc) - 3, Xenia's own, unless
// files/frames-in-flight says 2 or 1; "alternate" takes 3, 2 and 1 in turn, 15 s each,
// to compare them in one run (the pacing's summaries every 5 s say what each costs).
extern "C" uint32_t warden_frames_in_flight() {
  static const int setting = [] {
    int value = 3;
    if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
      if (FILE* file = std::fopen((std::string(dir) + "/frames-in-flight").c_str(), "r")) {
        char text[16] = {};
        if (std::fscanf(file, "%15s", text) == 1) {
          value = std::strcmp(text, "alternate") == 0 ? 0 : std::clamp(std::atoi(text), 1, 3);
        }
        std::fclose(file);
        XELOGW("frames in flight: {} (files/frames-in-flight)",
               value ? std::to_string(value) : std::string("3, 2 and 1 in turn"));
      }
    }
    return value;
  }();
  if (setting) {
    return uint32_t(setting);
  }
  static const auto start = std::chrono::steady_clock::now();
  const uint32_t frames =
      3 - uint32_t((std::chrono::steady_clock::now() - start) / std::chrono::seconds(15) % 3);
  static uint32_t last = 0;
  if (frames != last) {
    last = frames;
    XELOGI("frames in flight: now {}", frames);
  }
  return frames;
}

extern "C" void warden_acquire_end(int64_t start) {
  using namespace warden;
  const int64_t wait = Now() - start;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized && g_stats[g_last_mode].acquire.size() < 4096) {
    g_stats[g_last_mode].acquire.push_back(wait);
  }
}

extern "C" void warden_presented(VkDevice device, VkSwapchainKHR swapchain) {
  using namespace warden;
  RecordPresent(Now());
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_get_past && device == g_device && swapchain == g_swapchain && Refresh() > 0) {
    Poll(device);
  }
}

// PLAN 9.14: the next present's role in frame generation's pair - 0 none, 1 the
// generated frame, 2 the real one (dao_frame_pacer_android.cc, around its paints).
extern "C" void warden_present_role(int role) {
  std::lock_guard<std::mutex> lock(warden::g_mutex);
  warden::g_role = role;
}

// PLAN 9.14: whether the latest pair was timed (files/frame-generation-timing).
extern "C" bool warden_pairs_timed() {
  std::lock_guard<std::mutex> lock(warden::g_mutex);
  return warden::g_pairs_timed;
}
