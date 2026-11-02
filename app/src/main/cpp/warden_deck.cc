// PLAN 8.3: the second screen's state mirror - the feed the game's own UI already gets.
//
// The UI is Scaleform (ActionScript 2), and its movies do not have data pushed into them:
// they observe the engine's data model by token path ("GUI.SelectedChar.Attributes.",
// a party member's creature token ...) through the TokenManager class every movie
// shares. A movie queues (observer id, token path, update delay) in
// TokenManager.AddTokenObserverBatchData; the engine reads the queue and registers each
// (tu6_sub_825DD278 -> tu6_sub_825DC418(scene, id, path, delay)); whenever an observed
// value changes the engine queues (id, value as text) on the movie's scene object and
// pushes them as TokenManager.BatchTokenReplacementCallbackIDs and ...Values, then calls
// TokenManager.PerformBatchTokenReplacements (tu6_sub_825DCFF0, once a frame). Found
// from the image's ASCII "TokenManager.*" paths and the code that loads them
// (session 37; TU7 addresses, the build's "tu6_" names).
//
// Two hooks, read-only: the first names each (scene, id) by its path, the second reads
// the queued (id, value) pairs before the engine sends them on. Nothing is written into
// the game and nothing of the game's is called. So the mirror is what the game's own UI
// shows - already resolved, formatted and localised - by a name that does not move
// between builds the way a structure's offsets do (docs/SECOND-SCREEN.md §3).
//
// Off the game's path (8.7): the hooks update the mirror's map under a lock only the
// guest's threads take; at most every 100 ms the map is published as one text snapshot,
// with a try-lock - the game never waits on the deck. The deck (another process) pulls
// the snapshot through DeckService ten times a second.
//
// files/deck-trace (a test switch) logs every path registered and every value change
// (bounded), for finding the paths a tab wants.
#include "warden_deck.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "warden_fsr3.h"
#include "xenia/base/logging.h"

WARDEN_GAME_FUNC(__imp__tu6_sub_825DC418);
WARDEN_GAME_FUNC(__imp__tu6_sub_825DCFF0);

namespace warden::deck {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kPublishEvery = std::chrono::milliseconds(100);
constexpr size_t kMaxPaths = 20000;
constexpr int kTraceLines = 4000;

void AppendUtf8(std::string& out, uint32_t c) {
  if (c < 0x80) {
    out += char(c);
  } else if (c < 0x800) {
    out += char(0xC0 | (c >> 6));
    out += char(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += char(0xE0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
  } else {
    out += char(0xF0 | (c >> 18));
    out += char(0x80 | ((c >> 12) & 0x3F));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
  }
}

bool Guest(uint32_t address) { return address >= 0x10000 && address < 0xF0000000u; }

// An engine string (warden_game.cc's notes): UTF-16 with a buffer of 8 characters -
// capacity at +24 (8 or more: the characters behind a pointer at +4, else inline at +4),
// length at +20. False if it does not look like one.
bool EngineString(uint8_t* base, uint32_t object, std::string* out) {
  out->clear();
  if (!Guest(object)) {
    return false;
  }
  const uint32_t length = PPC_LOAD_U32(object + 20);
  const uint32_t capacity = PPC_LOAD_U32(object + 24);
  if (length > 4096 || capacity < 7 || length > capacity) {
    return false;
  }
  const uint32_t chars = capacity >= 8 ? PPC_LOAD_U32(object + 4) : object + 4;
  if (!Guest(chars)) {
    return false;
  }
  for (uint32_t i = 0; i < length; ++i) {
    uint32_t c = PPC_LOAD_U16(chars + i * 2);
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < length) {
      const uint32_t low = PPC_LOAD_U16(chars + (i + 1) * 2);
      if (low >= 0xDC00 && low < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      }
    }
    if (c == 0) {
      continue;
    }
    // Tabs and line ends are the snapshot's separators; beyond the BMP, JNI's modified
    // UTF-8 would need surrogates - none in the game's text.
    AppendUtf8(*out, c == '\t' || c == '\n' || c == '\r' ? ' ' : c >= 0x10000 ? '?' : c);
  }
  return true;
}

bool LooksLikePath(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  for (char c : text) {
    if (static_cast<unsigned char>(c) < 0x20) {
      return false;
    }
  }
  return true;
}

struct Mirror {
  // Guest threads only.
  std::mutex mutex;
  std::unordered_map<uint64_t, std::string> paths;  // (scene << 32 | id) -> token path
  std::unordered_map<std::string, std::string> values;
  bool dirty = false;
  Clock::time_point last_publish;
  // The published snapshot: the deck's side takes this lock, the game's only tries it.
  std::mutex published_mutex;
  std::shared_ptr<const std::string> published;
  bool trace = false;
  // files/deck-reads-staged (a test switch): the deck's read patterns switched on one at
  // a time, 15 s apart, to find one that changes what the game does.
  bool staged = false;
  std::atomic<int> trace_lines{0};
  std::atomic<int> odd_strings{0};
};

Mirror& State() {
  static Mirror* mirror = [] {
    auto* m = new Mirror();
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    if (dir) {
      if (FILE* file = std::fopen((std::string(dir) + "/deck-trace").c_str(), "r")) {
        std::fclose(file);
        m->trace = true;
        XELOGW("deck: tracing the UI's token paths and values (files/deck-trace)");
      }
      if (FILE* file = std::fopen((std::string(dir) + "/deck-reads-staged").c_str(), "r")) {
        std::fclose(file);
        m->staged = true;
        XELOGW("deck: the read patterns switched on one at a time (files/deck-reads-staged)");
      }
    }
    return m;
  }();
  return *mirror;
}

bool Trace(Mirror& m) {
  return m.trace && m.trace_lines.fetch_add(1, std::memory_order_relaxed) < kTraceLines;
}

// PLAN 8.8: the pending quick-slot press. The quick slots' buttons on the 360 build,
// read off the HUD and confirmed in run 544: slot 0 is X (left), slot 1 Y (top - Y
// toggled Alistair's Shield Cover, slot 1's Status 0 -> 1); 3-5 the same three with RT
// held (the HUD brings the second set forward while RT is down). B (right) is the
// healing potion on the HUD, whatever slot 2 holds - so the deck's third tile is the
// potion, and its tap presses B. X_INPUT masks: X 4000, Y 8000, B 2000.
constexpr uint16_t kQuickslotButtons[3] = {0x4000, 0x8000, 0x2000};
constexpr int64_t kTriggerLeadNanos = 120'000'000;   // RT down this long before the button
constexpr int64_t kButtonHoldNanos = 150'000'000;    // the button held this long
std::atomic<int> g_quick_slot{-1};
std::atomic<int64_t> g_quick_at{0};  // when the press starts (steady clock, ns)

// Under m.mutex: the map as one text, handed over if the deck's side is not reading.
void MaybePublish(Mirror& m) {
  const auto now = Clock::now();
  if (!m.dirty || now - m.last_publish < kPublishEvery) {
    return;
  }
  auto text = std::make_shared<std::string>();
  text->reserve(m.values.size() * 48);
  for (const auto& [path, value] : m.values) {
    *text += path;
    *text += '\t';
    *text += value;
    *text += '\n';
  }
  if (m.published_mutex.try_lock()) {
    // PLAN 8.8: files/deck-dump (a test switch) - the snapshot written once to
    // files/deck-snapshot.txt, the marker removed.
    static const std::string dir = [] {
      const char* d = std::getenv("WARDEN_FILES_DIR");
      return d ? std::string(d) : std::string();
    }();
    if (!dir.empty()) {
      const std::string marker = dir + "/deck-dump";
      if (std::FILE* probe = std::fopen(marker.c_str(), "r")) {
        std::fclose(probe);
        std::remove(marker.c_str());
        if (std::FILE* out = std::fopen((dir + "/deck-snapshot.txt").c_str(), "w")) {
          std::fwrite(text->data(), 1, text->size(), out);
          std::fclose(out);
          XELOGI("deck: snapshot written to files/deck-snapshot.txt ({} paths)", m.values.size());
        }
      }
    }
    m.published = std::move(text);
    m.published_mutex.unlock();
    m.dirty = false;
    m.last_publish = now;
  }
}

// Frames for the performance tab and overlay: lock-free, written by the threads that
// count them. 1,000 intervals - about 16 s at 60 - so the 1% low has ten frames to be.
constexpr size_t kIntervals = 1000;
std::atomic<uint64_t> g_guest_swaps{0};
// When the deck last pulled a snapshot: the deck's own reads only while it does.
std::atomic<int64_t> g_last_pull{0};
std::atomic<uint64_t> g_presents{0};
std::atomic<int64_t> g_last_swap{0};
std::array<std::atomic<int64_t>, kIntervals> g_intervals{};

}  // namespace

std::string QueueQuickslot(int slot) {
  if (slot < 0 || slot > 5) {
    return "no such slot";
  }
  // The engine's own mode, not the HUD's copy (GameModeController.CurrentGameMode): the
  // HUD does not see the pause menu, and a tap there went through (run 544).
  const int mode = warden::GameMode();
  if (mode != 0 && mode != 1) {
    XELOGI("deck: quick slot {} refused - the game is in mode {}, not exploring or fighting",
           slot, mode);
    return "only while exploring or fighting";
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          Clock::now().time_since_epoch()).count();
  if (g_quick_slot.load(std::memory_order_relaxed) >= 0 &&
      now - g_quick_at.load(std::memory_order_relaxed) < kTriggerLeadNanos + kButtonHoldNanos) {
    return "a press is still being made";
  }
  g_quick_at.store(now, std::memory_order_relaxed);
  g_quick_slot.store(slot, std::memory_order_release);
  XELOGI("deck: quick slot {} pressed from the deck ({}{:04X})", slot, slot >= 3 ? "RT + " : "",
         kQuickslotButtons[slot % 3]);
  return "";
}

bool ApplyQuickslot(uint16_t& buttons, uint8_t& right_trigger) {
  const int slot = g_quick_slot.load(std::memory_order_acquire);
  if (slot < 0) {
    return false;
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          Clock::now().time_since_epoch()).count();
  const int64_t since = now - g_quick_at.load(std::memory_order_relaxed);
  // Slots 3-5: RT first (the HUD swaps to the second set), then the button, RT held over it.
  const int64_t lead = slot >= 3 ? kTriggerLeadNanos : 0;
  if (since >= lead + kButtonHoldNanos + 40'000'000) {
    g_quick_slot.store(-1, std::memory_order_relaxed);
    return false;
  }
  if (slot >= 3) {
    right_trigger = 255;
  }
  if (since >= lead && since < lead + kButtonHoldNanos) {
    buttons |= kQuickslotButtons[slot % 3];
  }
  return true;
}

std::string Snapshot() {
  g_last_pull.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
  Mirror& m = State();
  std::shared_ptr<const std::string> published;
  {
    std::lock_guard<std::mutex> lock(m.published_mutex);
    published = m.published;
  }
  return published ? *published : std::string();
}

void NoteGuestSwap(int64_t now_nanos) {
  const uint64_t index = g_guest_swaps.fetch_add(1, std::memory_order_relaxed);
  const int64_t last = g_last_swap.exchange(now_nanos, std::memory_order_relaxed);
  if (last) {
    g_intervals[index % kIntervals].store(now_nanos - last, std::memory_order_relaxed);
  }
}

void NotePresent() { g_presents.fetch_add(1, std::memory_order_relaxed); }

FrameStats Frames() {
  FrameStats stats;
  stats.now_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now().time_since_epoch())
                        .count();
  stats.guest_swaps = g_guest_swaps.load(std::memory_order_relaxed);
  stats.presents = g_presents.load(std::memory_order_relaxed);
  const uint64_t count = std::min<uint64_t>(stats.guest_swaps > 0 ? stats.guest_swaps - 1 : 0,
                                            kIntervals);
  stats.intervals.reserve(count);
  for (uint64_t i = 0; i < count; ++i) {
    // Oldest first: the slot after the newest, round.
    const uint64_t slot = (stats.guest_swaps + kIntervals - count + i) % kIntervals;
    stats.intervals.push_back(g_intervals[slot].load(std::memory_order_relaxed));
  }
  return stats;
}

// ---- the deck's faces: the HUD's party portraits, read back from the GPU ----
//
// The HUD's portraits are renders (BUG-052): the game draws each party member's head
// into its own 146x146 k_8_8_8_8 target and resolves it to guest memory (destination
// pitch 009200A0, 0x19000 bytes - 160x160 tiled, 32 bits a pixel), in a burst of one
// per member at an area load, slot by slot (the HUD names them portrait_<tag><slot>),
// and again one at a time when a member's look changes. The Vulkan backend reads each
// one back while the deck pulls (assemble_runtime.py), and it lands here: untiled into
// RGBA, held until the burst is over, then given to the slots - a burst of two or more
// in order, a lone redraw to the slot its address had (else the first).
namespace {

constexpr int kPortraitSide = 146;
constexpr int kPortraitPitch = 160;
constexpr uint32_t kPortraitBytes = kPortraitPitch * kPortraitPitch * 4;
constexpr int kPortraitSlots = 4;
constexpr int64_t kPortraitBurstGapNanos = 500'000'000;

struct PendingPortrait {
  uint32_t address = 0;
  std::vector<uint8_t> rgba;
};

struct Portraits {
  std::mutex mutex;
  // The faces by their portrait's name (the owner, 2026-11-02: faces on the wrong rows).
  // The game names each portrait as it makes it (warden_game.cc: NotePortraitMade) and
  // draws it right after, to a texture of its own: a resolve to an address not seen
  // before is the oldest portrait made and not yet drawn; a redraw comes to the same
  // address again. A slot then shows the face whose name the HUD gives it
  // (Party.Members.N.Portrait), whatever order the faces came in. The by-slot faces below
  // are the fallback for a face no name is known for.
  struct Named {
    std::vector<uint8_t> rgba;
    uint32_t generation = 0;
  };
  std::map<std::string, Named> by_name;
  std::map<uint32_t, std::string> name_at;  // a resolve address: whose texture it is
  std::deque<std::string> made;              // portraits made, not drawn yet
  std::vector<uint8_t> rgba[kPortraitSlots];
  uint32_t generation[kPortraitSlots] = {};
  uint32_t address[kPortraitSlots] = {};
  // The slots' portrait names at the last burst.
  std::array<std::string, kPortraitSlots> names;
  uint32_t next_generation = 1;
  std::vector<PendingPortrait> burst;
  int64_t last_nanos = 0;
};

Portraits& PortraitState() {
  static Portraits* state = new Portraits();
  return *state;
}

int64_t NowNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             Clock::now().time_since_epoch())
      .count();
}

// texture_address::Tiled2D for 32-bit texels (tools/resolve_to_png.py's tiled_offset).
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t pitch_aligned) {
  const uint32_t outer_blocks = ((y >> 5) * (pitch_aligned >> 5) + (x >> 5)) << 6;
  const uint32_t inner_blocks = (((y >> 1) & 7) << 3) | (x & 7);
  const uint32_t outer_inner = (outer_blocks | inner_blocks) << 2;
  const uint32_t bank = (y >> 4) & 1;
  const uint32_t pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
  return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (outer_inner & 15) |
         (((outer_inner >> 4) & 1) << 5) | (((outer_inner >> 5) & 7) << 8) |
         ((outer_inner >> 8) << 12);
}

// Each party slot's portrait name as the HUD was last told it (Party.Members.N.Portrait,
// "portrait_gen00fl_alistair2" - the render target's own name), from the published
// snapshot: the game names a slot's target as it renders a new face into it.
std::array<std::string, kPortraitSlots> PortraitNames() {
  std::array<std::string, kPortraitSlots> names;
  Mirror& m = State();
  std::shared_ptr<const std::string> published;
  {
    std::lock_guard<std::mutex> lock(m.published_mutex);
    published = m.published;
  }
  if (!published) {
    return names;
  }
  for (int s = 0; s < kPortraitSlots; ++s) {
    const std::string key = "Party.Members." + std::to_string(s) + ".Portrait\t";
    size_t at = published->find(key);
    // A line's start: the key at the front or after a line break.
    while (at != std::string::npos && at != 0 && (*published)[at - 1] != '\n') {
      at = published->find(key, at + 1);
    }
    if (at != std::string::npos) {
      const size_t begin = at + key.size();
      const size_t end = published->find('\n', begin);
      names[s] = published->substr(begin, end == std::string::npos ? std::string::npos
                                                                     : end - begin);
    }
  }
  return names;
}

// The burst is over: its faces to their slots. Under the lock. A burst redraws only the
// members whose faces changed - a member joining redraws that member alone - so the
// slots are the ones whose portrait name changed since the last burst, in slot order,
// when there are as many; else the slots with a name, when the burst redrew them all;
// else each face to the slot its address had, or the first changed or unknown slot.
void SettlePortraits(Portraits& p) {
  if (p.burst.empty()) {
    return;
  }
  const std::array<std::string, kPortraitSlots> names = PortraitNames();
  std::vector<int> changed, named;
  for (int s = 0; s < kPortraitSlots; ++s) {
    if (names[s].empty()) {
      continue;
    }
    named.push_back(s);
    if (names[s] != p.names[s]) {
      changed.push_back(s);
    }
  }
  const size_t count = std::min(p.burst.size(), size_t(kPortraitSlots));
  std::vector<int> slots;
  const char* how;
  if (changed.size() == count) {
    slots = changed;
    how = "the slots renamed";
  } else if (named.size() == count) {
    slots = named;
    how = "every named slot";
  } else {
    how = "by address";
    std::vector<int> spare = changed;
    for (int s = 0; s < kPortraitSlots; ++s) {
      if (!p.address[s] && std::find(spare.begin(), spare.end(), s) == spare.end()) {
        spare.push_back(s);
      }
    }
    for (size_t i = 0; i < count; ++i) {
      int slot = -1;
      for (int s = 0; s < kPortraitSlots; ++s) {
        if (p.address[s] == p.burst[i].address) {
          slot = s;
        }
      }
      if (slot < 0) {
        slot = !spare.empty() ? spare.front() : int(i);
      }
      spare.erase(std::remove(spare.begin(), spare.end(), slot), spare.end());
      slots.push_back(slot);
    }
  }
  for (size_t i = 0; i < count; ++i) {
    const int slot = slots[i];
    // An address another slot had is that slot's no longer.
    for (int s = 0; s < kPortraitSlots; ++s) {
      if (s != slot && p.address[s] == p.burst[i].address) {
        p.address[s] = 0;
      }
    }
    p.rgba[slot] = std::move(p.burst[i].rgba);
    p.address[slot] = p.burst[i].address;
    p.generation[slot] = p.next_generation++;
  }
  p.names = names;
  XELOGI("Deck: {} portrait(s) drawn, placed {} - slots {:08X} {:08X} {:08X} {:08X}",
         p.burst.size(), how, p.address[0], p.address[1], p.address[2], p.address[3]);
  p.burst.clear();
}

}  // namespace

void NotePortraitMade(const std::string& name) {
  Portraits& p = PortraitState();
  std::lock_guard<std::mutex> lock(p.mutex);
  p.made.push_back(name);
  while (p.made.size() > 8) {
    p.made.pop_front();
  }
  XELOGI("Deck: portrait made: {}", name);
}

bool WantsPortraits() {
  // Once the deck has pulled at all (not only lately): every portrait's first draw must
  // be seen to know whose its texture is - a deck opened again after the game was put
  // away (Home) misses none.
  return g_last_pull.load(std::memory_order_relaxed) != 0;
}

void NotePortrait(uint32_t address, const uint8_t* data, uint32_t length) {
  if (!data || length < kPortraitBytes) {
    return;
  }
  PendingPortrait face;
  face.address = address;
  face.rgba.resize(size_t(kPortraitSide) * kPortraitSide * 4);
  uint32_t lit = 0;
  uint32_t covered = 0;
  for (uint32_t y = 0; y < uint32_t(kPortraitSide); ++y) {
    for (uint32_t x = 0; x < uint32_t(kPortraitSide); ++x) {
      // The portraits' bytes are R G B A (unlike the front buffer's B G R A: run 546's
      // first faces came out blue).
      const uint8_t* texel = data + TiledOffset(x, y, kPortraitPitch);
      uint8_t* out = face.rgba.data() + (size_t(y) * kPortraitSide + x) * 4;
      out[0] = texel[0];
      out[1] = texel[1];
      out[2] = texel[2];
      out[3] = texel[3];
      lit += (texel[0] | texel[1] | texel[2]) != 0;
      covered += texel[3] != 0;
    }
  }
  // The alpha is the head's mask when it covers part of the picture and not all of it
  // (the HUD lays the head over its disc); otherwise it says nothing and the picture is
  // opaque. Android's bitmaps are premultiplied.
  const uint32_t pixels = uint32_t(kPortraitSide) * kPortraitSide;
  const bool masked = covered > pixels / 20 && covered < pixels - pixels / 20;
  for (size_t i = 0; i < face.rgba.size(); i += 4) {
    uint8_t* out = face.rgba.data() + i;
    if (!masked) {
      out[3] = 255;
    } else if (out[3] != 255) {
      out[0] = uint8_t(out[0] * out[3] / 255);
      out[1] = uint8_t(out[1] * out[3] / 255);
      out[2] = uint8_t(out[2] * out[3] / 255);
    }
  }
  const int64_t now = NowNanos();
  const std::array<std::string, kPortraitSlots> party = PortraitNames();
  Portraits& p = PortraitState();
  std::lock_guard<std::mutex> lock(p.mutex);
  // Whose face this is: the address's portrait; a new address, the oldest portrait made
  // and not drawn; an address whose portrait has left the party while a new one waits,
  // the new one (a freed texture's memory given to it).
  std::string name;
  const auto bound = p.name_at.find(address);
  const bool in_party =
      bound != p.name_at.end() &&
      std::any_of(party.begin(), party.end(), [&](const std::string& slot) {
        return !slot.empty() && slot.find(bound->second) != std::string::npos;
      });
  if (bound != p.name_at.end() && (in_party || p.made.empty())) {
    name = bound->second;
  } else if (!p.made.empty()) {
    name = p.made.front();
    p.made.pop_front();
    p.name_at[address] = name;
  }
  if (!name.empty()) {
    Portraits::Named& named = p.by_name[name];
    named.rgba = std::move(face.rgba);
    named.generation = p.next_generation++;
    // Faces of portraits no longer in the party, beyond a few, let go.
    if (p.by_name.size() > 12) {
      for (auto it = p.by_name.begin(); it != p.by_name.end();) {
        const bool kept = std::any_of(party.begin(), party.end(), [&](const std::string& slot) {
          return slot.find(it->first) != std::string::npos;
        });
        it = kept || it->first == name ? std::next(it) : p.by_name.erase(it);
      }
    }
    XELOGI("Deck: portrait render at {:08X}, {} of {} pixels lit{} - {}", address, lit, pixels,
           masked ? ", the head's mask" : "", name);
    return;
  }
  if (now - p.last_nanos > kPortraitBurstGapNanos) {
    SettlePortraits(p);
  }
  p.last_nanos = now;
  XELOGI("Deck: portrait render at {:08X}, {} of {} pixels lit, {} with alpha{} (burst {})",
         address, lit, pixels, covered, masked ? ", the head's mask" : "", p.burst.size());
  p.burst.push_back(std::move(face));
}

namespace {
// A slot's face: the one named for it, else the by-slot fallback. Under the lock.
const Portraits::Named* NamedFace(Portraits& p, const std::string& slot_name) {
  if (slot_name.empty()) {
    return nullptr;
  }
  for (const auto& [name, face] : p.by_name) {
    if (slot_name.find(name) != std::string::npos) {
      return &face;
    }
  }
  return nullptr;
}
}  // namespace

std::vector<uint32_t> PortraitGenerations() {
  const std::array<std::string, kPortraitSlots> party = PortraitNames();
  Portraits& p = PortraitState();
  std::lock_guard<std::mutex> lock(p.mutex);
  if (!p.burst.empty() && NowNanos() - p.last_nanos > kPortraitBurstGapNanos) {
    SettlePortraits(p);
  }
  std::vector<uint32_t> generations(kPortraitSlots);
  for (int s = 0; s < kPortraitSlots; ++s) {
    const Portraits::Named* named = NamedFace(p, party[s]);
    generations[s] = named ? named->generation : p.generation[s];
  }
  return generations;
}

// ---- what the frames go through, for the performance tab and overlay ----

namespace {
std::mutex g_render_mutex;
std::string g_render_notes[4] = {"Off", "", "Bilinear", ""};
}  // namespace

void SetRenderNote(RenderNote which, const std::string& value) {
  std::lock_guard<std::mutex> lock(g_render_mutex);
  g_render_notes[int(which)] = value;
}

std::string RenderReport() {
  uint32_t width = 0, height = 0;
  warden::fsr3::RenderSize(width, height);
  width *= warden::fsr3::RenderScale();
  height *= warden::fsr3::RenderScale();
  std::lock_guard<std::mutex> lock(g_render_mutex);
  const std::string& fsr3 = g_render_notes[int(RenderNote::kFsr3)];
  std::string out;
  out += "Render.FrameGeneration\t" + g_render_notes[int(RenderNote::kFrameGeneration)] + "\n";
  // FSR 3.1 rebuilds the picture at the output's size, and the presenter's pass then
  // has nothing left to scale; without it, the presenter's pass is the upscaler.
  out += "Render.Upscaler\t" +
         (fsr3.empty() ? g_render_notes[int(RenderNote::kPaint)] : "FSR 3.1 " + fsr3) + "\n";
  out += "Render.Size\t" + std::to_string(width) + "x" + std::to_string(height) + "\n";
  if (!g_render_notes[int(RenderNote::kOutput)].empty()) {
    out += "Render.Output\t" + g_render_notes[int(RenderNote::kOutput)] + "\n";
  }
  return out;
}

std::vector<uint8_t> Portrait(int slot) {
  if (slot < 0 || slot >= kPortraitSlots) {
    return {};
  }
  const std::array<std::string, kPortraitSlots> party = PortraitNames();
  Portraits& p = PortraitState();
  std::lock_guard<std::mutex> lock(p.mutex);
  const Portraits::Named* named = NamedFace(p, party[slot]);
  return named ? named->rgba : p.rgba[slot];
}

}  // namespace warden::deck

using warden::deck::EngineString;
using warden::deck::Guest;
using warden::deck::LooksLikePath;
using warden::deck::Mirror;
using warden::deck::State;
using warden::deck::Trace;

// An observer registered: r3 the movie's scene object, r4 the observer id (the movie's
// own number), r5 the token path (an engine string), f1 the update delay.
WARDEN_OVERRIDE(tu6_sub_825DC418) {
  const uint32_t scene = ctx.r3.u32;
  const uint32_t id = ctx.r4.u32;
  std::string path;
  if (EngineString(base, ctx.r5.u32, &path) && LooksLikePath(path)) {
    Mirror& m = State();
    std::lock_guard<std::mutex> lock(m.mutex);
    if (m.paths.size() < warden::deck::kMaxPaths) {
      m.paths[(uint64_t(scene) << 32) | id] = path;
    }
    if (Trace(m)) {
      XELOGI("deck: scene {:08X} observes {} as {}", scene, path, id);
    }
  } else if (State().odd_strings.fetch_add(1, std::memory_order_relaxed) < 8) {
    XELOGW("deck: an observer's path is not an engine string (r5 {:08X}: length {}, "
           "capacity {})",
           ctx.r5.u32, Guest(ctx.r5.u32) ? PPC_LOAD_U32(ctx.r5.u32 + 20) : 0,
           Guest(ctx.r5.u32) ? PPC_LOAD_U32(ctx.r5.u32 + 24) : 0);
  }
  __imp__tu6_sub_825DC418(ctx, base);
}

// ---- reading token paths the UI only pulls (the journal's, the inventory's ...) ----
//
// The data model as the UI's own ExternalInterface handlers read it (GetValue,
// tu6_sub_825E5080; GetNumMembers, tu6_sub_825E54C8; ListMemberNames, tu6_sub_825E5580):
//   sub_8261CB80(string, UTF-8)     an engine string made from the path
//   sub_8224E240()                  the data model's root
//   sub_82250208(root, string)      the path's node: 0, or 0x83240658 for "none"
//   node vtable +48 (out, node)     its value as text, into an engine string
//   node vtable +68 (node)          how many members it has
//   sub_82255AF8(iterator, node)    its members: the iterator's object at +8 -
//                                   vtable +8 valid?, +4 next, +0 (1) delete;
//                                   sub_82255BD8(iterator) the member's name
//   sub_82253270(string, 1, 0)      an engine string let go
// Getters only, called on the game's own thread from the hook below, with scratch room
// on the guest stack under the hook's frame.
WARDEN_GAME_FUNC(__imp__tu6_sub_8261CB80);
WARDEN_GAME_FUNC(__imp__tu6_sub_8224E240);
WARDEN_GAME_FUNC(__imp__tu6_sub_82250208);
WARDEN_GAME_FUNC(__imp__tu6_sub_82253270);
WARDEN_GAME_FUNC(__imp__tu6_sub_82255AF8);
WARDEN_GAME_FUNC(__imp__tu6_sub_82255BD8);
// The talk table, as the UI's GetTlkString (tu6_sub_825E8740) reads it:
// sub_82626120(result, string id) fills a result whose byte +0 says it is there (else
// sub_826263F8(result) tries again, twice), byte +37 that it was found, and the text is
// an engine string at +8.
WARDEN_GAME_FUNC(__imp__tu6_sub_82626120);
WARDEN_GAME_FUNC(__imp__tu6_sub_826263F8);
// The 2DA tables, as the UI's Get2DAValue (tu6_sub_825E7E18) reads them: the game object
// (sub_826CB6A8) ready (sub_826CBB30); the tables' holder at 0x8324247C, its +4 to
// sub_8262BFB0(holder, table) for the table; the table's vtable +80 (table, row, column,
// &type) the cell's type - 1 an integer - and +40 (table, row, column, &value) it.
WARDEN_GAME_FUNC(__imp__tu6_sub_826CB6A8);
WARDEN_GAME_FUNC(__imp__tu6_sub_826CBB30);
WARDEN_GAME_FUNC(__imp__tu6_sub_8262BFB0);

namespace warden::deck {
namespace {

constexpr uint32_t kNoNode = 0x83240658;

class TokenReader {
 public:
  // `base`, as the PPC_LOAD/PPC_STORE macros name the guest memory.
  TokenReader(const PPCContext& ctx, uint8_t* memory) : base(memory), probe_(ctx) {
    // Under the hook's frame: the path, three engine strings' room, the iterator; the
    // callees' frames below that.
    area_ = (ctx.r1.u32 - 0x2000) & ~0xFu;
    probe_.r1.u64 = area_ - 0x200;
  }

  bool Text(const std::string& path, std::string* out) {
    const uint32_t node = Node(path);
    return node && Text(node, out);
  }

  bool Text(uint32_t node, std::string* out) {
    const uint32_t text = area_ + 0x440;
    for (uint32_t i = 0; i < 32; i += 4) {
      PPC_STORE_U32(text + i, 0);
    }
    probe_.r3.u64 = text;
    probe_.r4.u64 = node;
    Virtual(node, 48);
    const bool ok = EngineString(base, text, out);
    Free(text);
    return ok;
  }

  int Count(const std::string& path) {
    const uint32_t node = Node(path);
    if (!node) {
      return -1;
    }
    probe_.r3.u64 = node;
    Virtual(node, 68);
    return probe_.r3.s32;
  }

  std::vector<std::string> Members(const std::string& path, size_t limit) {
    const uint32_t node = Node(path);
    return node ? Members(node, limit) : std::vector<std::string>();
  }

  std::vector<std::string> Members(uint32_t node, size_t limit) {
    std::vector<std::string> names;
    const uint32_t iterator = area_ + 0x480;
    for (uint32_t i = 0; i < 32; i += 4) {
      PPC_STORE_U32(iterator + i, 0);
    }
    probe_.r3.u64 = iterator;
    probe_.r4.u64 = node;
    __imp__tu6_sub_82255AF8(probe_, base);
    const uint32_t object = PPC_LOAD_U32(iterator + 8);
    if (!Guest(object)) {
      return names;
    }
    std::string name;
    while (names.size() < limit) {
      probe_.r3.u64 = object;
      Virtual(object, 8);
      if (!(probe_.r3.u32 & 0xFF)) {
        break;
      }
      probe_.r3.u64 = iterator;
      __imp__tu6_sub_82255BD8(probe_, base);
      if (EngineString(base, probe_.r3.u32, &name)) {
        names.push_back(name);
      }
      probe_.r3.u64 = object;
      Virtual(object, 4);
    }
    probe_.r3.u64 = object;
    probe_.r4.u64 = 1;
    Virtual(object, 0);
    return names;
  }

  // A string of the talk table, by its id.
  bool TlkString(uint32_t id, std::string* out) {
    const uint32_t result = area_ + 0x500;
    for (uint32_t i = 0; i < 64; i += 4) {
      PPC_STORE_U32(result + i, 0);
    }
    probe_.r3.u64 = result;
    probe_.r4.u64 = id;
    __imp__tu6_sub_82626120(probe_, base);
    for (int retry = 0; retry < 2 && !PPC_LOAD_U8(result); ++retry) {
      probe_.r3.u64 = result;
      __imp__tu6_sub_826263F8(probe_, base);
    }
    const bool found = PPC_LOAD_U8(result + 37) && EngineString(base, result + 8, out);
    Free(result + 8);
    return found;
  }

  // An integer of a 2DA table: its id, the row, the column's hash.
  bool TwoDaInt(uint32_t table, uint32_t row, uint32_t column, int32_t* out) {
    __imp__tu6_sub_826CB6A8(probe_, base);
    __imp__tu6_sub_826CBB30(probe_, base);
    if (!(probe_.r3.u32 & 0xFF)) {
      return false;
    }
    const uint32_t holder = PPC_LOAD_U32(0x8324247Cu);
    if (!Guest(holder) || !Guest(PPC_LOAD_U32(holder + 4))) {
      return false;
    }
    probe_.r3.u64 = PPC_LOAD_U32(holder + 4);
    probe_.r4.u64 = table;
    __imp__tu6_sub_8262BFB0(probe_, base);
    const uint32_t object = probe_.r3.u32;
    if (!Guest(object)) {
      return false;
    }
    const uint32_t slot = area_ + 0x560;
    PPC_STORE_U32(slot, 0xFFFFFFFFu);
    probe_.r3.u64 = object;
    probe_.r4.u64 = row;
    probe_.r5.u64 = column;
    probe_.r6.u64 = slot;
    Virtual(object, 80);
    if (PPC_LOAD_U32(slot) != 1) {
      return false;
    }
    PPC_STORE_U32(slot, 0);
    probe_.r3.u64 = object;
    probe_.r4.u64 = row;
    probe_.r5.u64 = column;
    probe_.r6.u64 = slot;
    Virtual(object, 40);
    *out = int32_t(PPC_LOAD_U32(slot));
    return true;
  }

  // The path's node, or 0.
  uint32_t Node(const std::string& path) {
    if (path.empty() || path.size() > 500) {
      return 0;
    }
    const uint32_t chars = area_;
    for (size_t i = 0; i < path.size(); ++i) {
      PPC_STORE_U8(chars + uint32_t(i), uint8_t(path[i]));
    }
    PPC_STORE_U8(chars + uint32_t(path.size()), 0);
    const uint32_t name = area_ + 0x400;
    probe_.r3.u64 = name;
    probe_.r4.u64 = chars;
    __imp__tu6_sub_8261CB80(probe_, base);
    __imp__tu6_sub_8224E240(probe_, base);
    probe_.r4.u64 = name;
    __imp__tu6_sub_82250208(probe_, base);
    const uint32_t node = probe_.r3.u32;
    Free(name);
    return Guest(node) && node != kNoNode ? node : 0;
  }

 private:
  void Free(uint32_t string) {
    probe_.r3.u64 = string;
    probe_.r4.u64 = 1;
    probe_.r5.u64 = 0;
    __imp__tu6_sub_82253270(probe_, base);
  }

  // A virtual call on `object` at the vtable's `offset`, the registers as set.
  void Virtual(uint32_t object, uint32_t offset) {
    const uint32_t function = PPC_LOAD_U32(PPC_LOAD_U32(object) + offset);
    PPCFunc* host = PPC_LOOKUP_FUNC(base, function);
    if (host) {
      host(probe_, base);
    }
  }

  uint8_t* base;
  PPCContext probe_;
  uint32_t area_ = 0;
};

// ---- the deck's own reads: what the UI only pulls while its screens are open ----
//
// Patterns over the data model (session 37's dumps, runs 459-460): a segment is a name,
// "*" (each member of the node there) or "{a,b}" (each of these names). Each pattern is
// read again at its own period, its leaves' text kept by path; the keys a pass no longer
// finds are dropped, so an item sold or a quest done leaves the deck too. Spread over
// frames: at most kReadBudget of reading in each kReadWindow, only while the deck pulls
// and the game explores, fights, is in its menus or a conversation.
constexpr auto kReadWindow = std::chrono::milliseconds(16);
constexpr auto kReadBudget = std::chrono::microseconds(500);
constexpr size_t kMembersLimit = 128;

struct Segment {
  bool all = false;                // "*"
  std::vector<std::string> names;  // a name, or "{a,b}"'s
};

struct ReadJob {
  std::vector<Segment> segments;
  std::chrono::milliseconds period;
  Clock::time_point due;
  std::unordered_map<std::string, bool> owned;  // the keys its last pass wrote
};

std::vector<Segment> ParsePattern(const std::string& pattern) {
  std::vector<Segment> segments;
  size_t start = 0;
  while (start <= pattern.size()) {
    size_t end = start;
    int depth = 0;
    while (end < pattern.size() && (pattern[end] != '.' || depth > 0)) {
      depth += pattern[end] == '{' ? 1 : pattern[end] == '}' ? -1 : 0;
      ++end;
    }
    const std::string part = pattern.substr(start, end - start);
    Segment segment;
    if (part == "*") {
      segment.all = true;
    } else if (part.size() > 2 && part.front() == '{' && part.back() == '}') {
      size_t from = 1;
      while (from < part.size() - 1) {
        size_t comma = part.find(',', from);
        if (comma == std::string::npos || comma > part.size() - 1) {
          comma = part.size() - 1;
        }
        segment.names.push_back(part.substr(from, comma - from));
        from = comma + 1;
      }
    } else {
      segment.names.push_back(part);
    }
    segments.push_back(std::move(segment));
    start = end + 1;
  }
  return segments;
}

struct Reads {
  std::vector<ReadJob> jobs;
  // The pass under way: its job, the paths still to visit (with the segment next), what
  // it has read.
  size_t current = SIZE_MAX;
  std::vector<std::pair<std::string, size_t>> pending;
  std::unordered_map<std::string, std::string> staged;
  Clock::time_point window_start;
  Clock::duration window_used{};
};

Reads& ReadState() {
  static Reads* reads = [] {
    auto* r = new Reads();
    const struct {
      const char* pattern;
      int period_ms;
    } kPatterns[] = {
        // Party: who, what they are, where.
        {"Party.Members.*.{Name,Tag,IsDead,InCombat,CanLevelUp,WorldPositionX,WorldPositionY,"
         "WorldOrientationY}",
         1000},
        {"Party.Members.*.Stats.{ClassName,RaceName}", 5000},
        {"Party.{InCamp,BackpackSize,BackpackMaxSize,MaxMembers}", 5000},
        // Items: what each wears and carries, and the purse.
        {"Party.Members.*.Equipment.*.{Name,Icon,StackSize,Description}", 5000},
        {"Party.Backpack.*.{Name,Icon,StackSize}", 5000},
        {"Party.Money.*", 5000},
        // Quests and the map: the journal's plots, the givers, the area's notes.
        {"Journal.{ActivePlot,ChanterBoard}", 5000},
        {"Journal.PlotList.*.*", 5000},
        {"Journal.PlotList.*.*.*.*", 5000},
        {"Journal.PlotGivers.*.{PlaceableName,WorldPositionX,WorldPositionY}", 5000},
        {"MapNoteList.MapNotes.*.{MapNote,Name,WorldPositionX,WorldPositionY,Icon}", 5000},
        // The area: its name, its minimap's texture and where its edges are in the world
        // (the markers' coordinates), the share of the texture the map fills.
        {"Area.{Name,ResRef,Minimap,MinimapLeft,MinimapRight,MinimapTop,MinimapBottom,"
         "MinimapWidth,MinimapHeight,North}",
         5000},
        // The journal's conversations, the codex.
        {"Journal.Conversations.*.ConversationLines.*.*", 5000},
        {"Journal.CodexGroups.*.{Name,NumberUnlocked}", 20000},
        {"Journal.CodexGroups.*.CodexList.*.{Name,Locked,Updated,Descriptions}", 20000},
        // Tactics: each member's slots.
        {"Party.Members.*.Tactics.{Enabled,PresetType}", 10000},
        {"Party.Members.*.Tactics.TacticsList.*.{Enabled,Condition,ConditionID,Command,"
         "CommandParam,Target,TargetID,ItemName}",
         10000},
    };
    for (const auto& p : kPatterns) {
      ReadJob job;
      job.segments = ParsePattern(p.pattern);
      job.period = std::chrono::milliseconds(p.period_ms);
      r->jobs.push_back(std::move(job));
    }
    return r;
  }();
  return *reads;
}

// Under m.mutex, on the game's thread: some of the deck's reads, within the budget.
void ReadSome(Mirror& m, TokenReader& reader) {
  Reads& r = ReadState();
  const auto now = Clock::now();
  if (now - r.window_start > kReadWindow) {
    r.window_start = now;
    r.window_used = {};
  }
  if (r.window_used >= kReadBudget) {
    return;
  }
  const auto started = now;
  while (Clock::now() - started < kReadBudget - r.window_used) {
    if (r.current == SIZE_MAX) {
      // The next job due (staged: only those switched on so far).
      static Clock::time_point first_read = now;
      size_t switched_on = r.jobs.size();
      if (m.staged) {
        switched_on = std::min(r.jobs.size(),
                               size_t((now - first_read) / std::chrono::seconds(15)) + 1);
        static size_t logged = 0;
        while (logged < switched_on) {
          XELOGI("deck: read pattern {} on", logged);
          ++logged;
        }
      }
      for (size_t i = 0; i < switched_on; ++i) {
        if (r.jobs[i].due <= now) {
          r.current = i;
          r.pending.clear();
          r.pending.emplace_back(std::string(), 0);
          r.staged.clear();
          break;
        }
      }
      if (r.current == SIZE_MAX) {
        break;
      }
    }
    ReadJob& job = r.jobs[r.current];
    if (r.pending.empty()) {
      // The pass done: its keys in, the ones it no longer found out.
      for (auto& [key, seen] : job.owned) {
        seen = false;
      }
      for (auto& [key, value] : r.staged) {
        std::string& held = m.values[key];
        if (held != value) {
          held = value;
          m.dirty = true;
        }
        job.owned[key] = true;
      }
      for (auto it = job.owned.begin(); it != job.owned.end();) {
        if (!it->second) {
          m.values.erase(it->first);
          m.dirty = true;
          it = job.owned.erase(it);
        } else {
          ++it;
        }
      }
      job.due = Clock::now() + job.period;
      r.current = SIZE_MAX;
      continue;
    }
    auto [path, index] = std::move(r.pending.back());
    r.pending.pop_back();
    if (index == job.segments.size()) {
      std::string text;
      const uint32_t node = reader.Node(path);
      if (node && reader.Text(node, &text)) {
        r.staged[path] = text;
        // A conversation's line and its speaker are the talk table's ids: their text
        // beside them, as the journal shows it.
        const size_t dot = path.rfind('.');
        const std::string leaf = dot == std::string::npos ? path : path.substr(dot + 1);
        if (leaf == "Line" || leaf == "Speaker") {
          const unsigned long id = std::strtoul(text.c_str(), nullptr, 10);
          std::string resolved;
          if (id && id < 0xFFFFFFFFul && reader.TlkString(uint32_t(id), &resolved)) {
            r.staged[path + "Text"] = resolved;
          }
        }
        // A tactic's condition, command and target: their names as the tactics screen
        // shows them - the 2DA tables 137, 141 and 140, their column 1080246718 the talk
        // table's id (tactics.gfx's GetConditionName, GetActionName, GetTargetName).
        if (path.find(".TacticsList.") != std::string::npos &&
            (leaf == "Condition" || leaf == "Command" || leaf == "Target")) {
          const uint32_t table = leaf == "Condition" ? 137 : leaf == "Command" ? 141 : 140;
          const long row = std::strtol(text.c_str(), nullptr, 10);
          int32_t strref = 0;
          std::string resolved;
          if (row >= 0 && reader.TwoDaInt(table, uint32_t(row), 1080246718u, &strref) &&
              strref > 0 && reader.TlkString(uint32_t(strref), &resolved)) {
            r.staged[path + "Name"] = resolved;
          }
        }
        // Its command's ability ("Use ability", "Activate mode"): the abilities' table
        // (M2DA 1, read as the UI's GetM2DAValue does - the same lookup) and its column
        // 846460685, the talk table's id (tactics.gfx's GetAbilityName). Items have
        // their own name (ItemName).
        if (path.find(".TacticsList.") != std::string::npos && leaf == "CommandParam") {
          const long ability = std::strtol(text.c_str(), nullptr, 10);
          int32_t strref = 0;
          std::string resolved;
          if (ability > 0 && reader.TwoDaInt(1, uint32_t(ability), 846460685u, &strref) &&
              strref > 0 && reader.TlkString(uint32_t(strref), &resolved)) {
            r.staged[path + "Name"] = resolved;
          }
        }
      }
      continue;
    }
    const Segment& segment = job.segments[index];
    const auto join = [&](const std::string& name) {
      return path.empty() ? name : path + "." + name;
    };
    if (segment.all) {
      const uint32_t node = reader.Node(path);
      if (node) {
        std::vector<std::string> members = reader.Members(node, kMembersLimit);
        // Visited in order: the stack takes them last first.
        for (auto it = members.rbegin(); it != members.rend(); ++it) {
          r.pending.emplace_back(join(*it), index + 1);
        }
      }
    } else {
      for (auto it = segment.names.rbegin(); it != segment.names.rend(); ++it) {
        r.pending.emplace_back(join(*it), index + 1);
      }
    }
  }
  r.window_used += Clock::now() - started;
}

// files/deck-trace: once the game has explored for 10 s, the data model's tree under
// these roots in the log - to find what a tab can read.
void DumpTree(TokenReader& reader, const std::string& path, int depth, size_t limit,
              int* budget) {
  if (*budget <= 0) {
    return;
  }
  std::string text;
  const bool has_text = reader.Text(path, &text);
  const int count = reader.Count(path);
  --*budget;
  XELOGI("deck: tree {} = {}{}", path, has_text ? text : "-",
         count > 0 ? fmt::format(" ({} members)", count) : "");
  if (depth <= 0 || count <= 0) {
    return;
  }
  for (const std::string& member : reader.Members(path, limit)) {
    DumpTree(reader, path + "." + member, depth - 1, limit, budget);
  }
}

}  // namespace
}  // namespace warden::deck

// A movie's changed values, before the engine sends them on: the ids at +164 (begin
// +168, end +172, 4 bytes each), the values at +180 (begin +184, end +188, engine
// strings of 28 bytes) - in the same order.
WARDEN_OVERRIDE(tu6_sub_825DCFF0) {
  {
    // The discovery dump (files/deck-trace), after ten seconds of exploring - twice at
    // most, each stretch of exploring once (the routes explore a test area first).
    static int dumps = 0;
    static bool dumped = false;
    static auto exploring_since = warden::deck::Clock::time_point();
    Mirror& m = State();
    if (m.trace && dumps < 2) {
      bool exploring;
      {
        std::lock_guard<std::mutex> lock(m.mutex);
        auto mode = m.values.find("GameModeController.CurrentGameMode");
        exploring = mode != m.values.end() && mode->second == "0";
      }
      const auto now = warden::deck::Clock::now();
      if (!exploring) {
        exploring_since = {};
        dumped = false;
      } else if (exploring_since == warden::deck::Clock::time_point()) {
        exploring_since = now;
      } else if (!dumped && now - exploring_since > std::chrono::seconds(10)) {
        dumped = true;
        ++dumps;
        warden::deck::TokenReader reader(ctx, base);
        int budget = 2500;
        // Each tab's candidates: the root, how deep, how many members a node.
        struct Root {
          const char* path;
          int depth;
          size_t limit;
        };
        for (const Root& root : {Root{"Party.Members.0", 1, 80}, Root{"Party.Members.0.Stats", 2, 40},
                                 Root{"GUI.SelectedChar", 1, 120},
                                 Root{"GUI.SelectedChar.Equipment", 3, 30},
                                 Root{"GUI.SelectedChar.Tactics", 3, 12},
                                 Root{"Journal.PlotList", 4, 20}, Root{"Journal.ActivePlot", 2, 20},
                                 Root{"Journal.PlotGivers", 3, 20},
                                 Root{"Journal.Conversations", 3, 10},
                                 Root{"Journal.CodexGroups", 1, 20},
                                 Root{"Journal.CodexGroups.0", 4, 5},
                                 Root{"MapNoteList", 3, 20}, Root{"Area", 2, 40},
                                 Root{"WorldMap", 1, 60}, Root{"GUIInventory", 2, 40},
                                 Root{"Party", 1, 60}, Root{"GUI", 1, 120}}) {
          warden::deck::DumpTree(reader, root.path, root.depth, root.limit, &budget);
        }
        XELOGI("deck: tree dumped ({} nodes left of the budget)", budget);
      }
    }
  }
  {
    // The deck's own reads (ReadSome), while it pulls and the game is in a mode whose
    // data model is settled: exploring, fighting, in the menus or a conversation.
    using warden::deck::Clock;
    const int64_t pulled = warden::deck::g_last_pull.load(std::memory_order_relaxed);
    if (pulled && Clock::now().time_since_epoch().count() - pulled <
                      std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(3))
                          .count()) {
      Mirror& m = State();
      std::lock_guard<std::mutex> lock(m.mutex);
      auto mode = m.values.find("GameModeController.CurrentGameMode");
      if (mode != m.values.end() && (mode->second == "0" || mode->second == "1" ||
                                     mode->second == "3" || mode->second == "4")) {
        warden::deck::TokenReader reader(ctx, base);
        warden::deck::ReadSome(m, reader);
        warden::deck::MaybePublish(m);
      }
    }
  }
  const uint32_t scene = ctx.r3.u32;
  if (warden::deck::Guest(scene)) {
    const uint32_t ids = PPC_LOAD_U32(scene + 168);
    const uint32_t ids_end = PPC_LOAD_U32(scene + 172);
    const uint32_t values = PPC_LOAD_U32(scene + 184);
    const uint32_t values_end = PPC_LOAD_U32(scene + 188);
    if (warden::deck::Guest(ids) && warden::deck::Guest(values) && ids_end > ids &&
        values_end > values) {
      const uint32_t count = std::min((ids_end - ids) / 4, (values_end - values) / 28);
      Mirror& m = State();
      std::lock_guard<std::mutex> lock(m.mutex);
      std::string value;
      for (uint32_t i = 0; i < count && i < 4096; ++i) {
        const uint32_t id = PPC_LOAD_U32(ids + i * 4);
        auto path = m.paths.find((uint64_t(scene) << 32) | id);
        if (path == m.paths.end() || !EngineString(base, values + i * 28, &value)) {
          continue;
        }
        std::string& held = m.values[path->second];
        if (held != value) {
          if (Trace(m)) {
            XELOGI("deck: {} = {}", path->second, value);
          }
          held = value;
          m.dirty = true;
        }
      }
      warden::deck::MaybePublish(m);
    }
  }
  __imp__tu6_sub_825DCFF0(ctx, base);
}
