// The GPU: Xenia's Vulkan graphics system driven by the title's own Direct3D. Task 5
// (STATUS.md §9, session 28) - the renderer that has been waiting since Phase 3.
//
// What the title does, and what answers it here:
//   VdInitializeRingBuffer        the PM4 ring in physical memory  -> the command processor
//   VdEnableRingBufferRPtrWriteBack where to publish the read pointer -> the command processor
//   VdSetGraphicsInterruptCallback its interrupt handler              -> the graphics system,
//                                 whose vsync worker raises the vblank (source 0) and whose
//                                 command processor raises the title's own PM4 INTERRUPT
//                                 packets (source 1), both through xe::dao_host::DispatchInterrupt
//   stw to 0x7FC80000 + 4*0x01C5  the ring's write pointer           -> UpdateWritePointer,
//                                 through the PPC_MM_STORE_U32 hook; every other register
//                                 write lands in the window (plain guest memory) and in
//                                 GraphicsSystem::WriteRegister
//   VdSwap                        the frame is done                  -> the swap packet into
//                                 the title's buffer, as Xenia's VdSwap writes it
//
// This is DAORecomp's design (DAORecomp/gpu/xenia_host.cpp, video.cpp,
// DAORecompLib/mmio_hook.h) with the Direct3D 12 graphics system replaced by the
// Vulkan one the graft produced (docs/GRAFT.md) and the window by the app's surface.
// The graphics system owns its Vulkan provider and presenter; the runtime hands the
// presenter to the window once this starts.

#include "warden_gpu.h"

#include <android/data_space.h>
#include <android/native_window.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "warden_deck.h"
#include "warden_frame_pacing.h"
#include "warden_frame_rate.h"
#include "warden_frame_times.h"
#include "warden_fsr3.h"
#include "warden_priority_probe.h"
#include "warden_guest.h"
#include "warden_gpu_memory.h"
#include "warden_guest_registers.h"
#include "warden_suspend.h"
#include "warden_thermal.h"
#include "warden_threads.h"
#include "warden_trace.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/dao_host.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gpu_flags.h"

DECLARE_bool(vulkan_sparse_shared_memory);  // xenia/gpu/vulkan/vulkan_shared_memory.cc
// BUG-112: the host's waits for the GPU (gpu_completion_timeline.h's edit): how many and
// how long since the last status tick, and the longest - a driver that signals its fences
// late, or a query the game waits on, shows here.
static std::atomic<uint32_t> g_gpu_waits{0};
static std::atomic<double> g_gpu_wait_ms{0.0};
static std::atomic<double> g_gpu_wait_max_ms{0.0};
// BUG-112: and the host's time inside vkQueueSubmit and vkQueuePresentKHR.
static std::atomic<uint32_t> g_gpu_submits{0};
static std::atomic<double> g_gpu_submit_ms{0.0};
static std::atomic<double> g_gpu_submit_max_ms{0.0};
static std::atomic<uint32_t> g_gpu_presents{0};
static std::atomic<double> g_gpu_present_ms{0.0};
static std::atomic<double> g_gpu_present_max_ms{0.0};
static void AddTime(std::atomic<uint32_t>& count, std::atomic<double>& total,
                    std::atomic<double>& longest, double milliseconds) {
  count.fetch_add(1, std::memory_order_relaxed);
  double sum = total.load(std::memory_order_relaxed);
  while (!total.compare_exchange_weak(sum, sum + milliseconds)) {
  }
  double most = longest.load(std::memory_order_relaxed);
  while (most < milliseconds && !longest.compare_exchange_weak(most, milliseconds)) {
  }
}
// BUG-112: the pipeline barriers and render passes executed since the last tick.
static std::atomic<uint32_t> g_gpu_barriers{0};
static std::atomic<uint32_t> g_gpu_image_barriers{0};
static std::atomic<uint32_t> g_gpu_buffer_barriers{0};
static std::atomic<uint32_t> g_gpu_passes{0};
extern "C" void warden_gpu_barrier_note(uint32_t image_barriers, uint32_t buffer_barriers) {
  g_gpu_barriers.fetch_add(1, std::memory_order_relaxed);
  g_gpu_image_barriers.fetch_add(image_barriers, std::memory_order_relaxed);
  g_gpu_buffer_barriers.fetch_add(buffer_barriers, std::memory_order_relaxed);
}
extern "C" void warden_gpu_pass_note() { g_gpu_passes.fetch_add(1, std::memory_order_relaxed); }
extern "C" void warden_gpu_submit_note(double milliseconds) {
  AddTime(g_gpu_submits, g_gpu_submit_ms, g_gpu_submit_max_ms, milliseconds);
}
extern "C" void warden_gpu_present_note(double milliseconds) {
  AddTime(g_gpu_presents, g_gpu_present_ms, g_gpu_present_max_ms, milliseconds);
}
extern "C" void warden_gpu_wait_note(double milliseconds) {
  g_gpu_waits.fetch_add(1, std::memory_order_relaxed);
  double total = g_gpu_wait_ms.load(std::memory_order_relaxed);
  while (!g_gpu_wait_ms.compare_exchange_weak(total, total + milliseconds)) {
  }
  double longest = g_gpu_wait_max_ms.load(std::memory_order_relaxed);
  while (longest < milliseconds && !g_gpu_wait_max_ms.compare_exchange_weak(longest, milliseconds)) {
  }
}

DECLARE_bool(vulkan_validation);            // xenia/ui/vulkan/vulkan_provider.cc
DECLARE_bool(debug_msaa_2x_as_4x);          // xenia/gpu/render_target_cache.cc
DECLARE_int32(draw_resolution_scale_x);       // xenia/gpu/texture_cache.cc
DECLARE_int32(draw_resolution_scale_y);       // xenia/gpu/texture_cache.cc
DECLARE_uint32(draw_resolution_scale_threshold);  // xenia/gpu/render_target_cache.cc
#include "xenia/gpu/d3d12/dao_frame_trace.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/vulkan_api.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/windowed_app_context.h"

#include "third_party/stb/stb_image_write.h"

extern "C" uint32_t warden_watched_write_faults();  // warden_memory.cc
extern "C" uint32_t warden_watched_write_races();   // warden_memory.cc, BUG-044
extern "C" uint32_t warden_frame_paints();          // warden_window.cc
extern "C" uint32_t warden_paint_requests();
extern "C" uint32_t warden_frame_callbacks_posted();
extern "C" uint32_t warden_deferred_paint_requests_dropped();
extern "C" uint32_t warden_deferred_paint_requests();

namespace warden {
bool VideoFourByThree();  // warden_video.cc: the 4:3 test
}  // namespace warden

// BUG-117: the GPU's progress - a count the command processor bumps each time it writes the
// ring's read pointer back to the title (command_processor.cc's edit), a futex word for the
// render thread's fence wait to sleep on with Heat guard's GPU wait on (warden_game.cc's
// override of the title's wait) instead of spinning a big core. Woken only while someone waits.
namespace {
std::atomic<uint32_t> g_gpu_progress{0};
std::atomic<uint32_t> g_gpu_progress_waiters{0};
}  // namespace

extern "C" void warden_gpu_progress_note() {
  g_gpu_progress.fetch_add(1, std::memory_order_release);
  if (g_gpu_progress_waiters.load(std::memory_order_acquire)) {
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(&g_gpu_progress), FUTEX_WAKE_PRIVATE,
            INT32_MAX, nullptr, nullptr, 0);
  }
}

extern "C" uint32_t warden_gpu_progress() {
  return g_gpu_progress.load(std::memory_order_acquire);
}

// Until the progress count is not `seen` any more, or `timeout_us` passes.
extern "C" void warden_gpu_progress_wait(uint32_t seen, uint32_t timeout_us) {
  g_gpu_progress_waiters.fetch_add(1, std::memory_order_acq_rel);
  if (g_gpu_progress.load(std::memory_order_acquire) == seen) {
    timespec timeout = {0, long(timeout_us) * 1000};
    syscall(SYS_futex, reinterpret_cast<uint32_t*>(&g_gpu_progress), FUTEX_WAIT_PRIVATE, seen,
            &timeout, nullptr, 0);
  }
  g_gpu_progress_waiters.fetch_sub(1, std::memory_order_acq_rel);
}

namespace warden::gpu {

namespace {

std::mutex g_mutex;
std::unique_ptr<xe::gpu::vulkan::VulkanGraphicsSystem> g_graphics;
xe::Memory* g_memory = nullptr;

constexpr uint32_t kRegisterWindow = 0x7FC80000;
constexpr uint32_t kRegWritePointer = 0x01C5;  // CP_RB_WPTR

// What the title and the command processor have told each other, for the status
// line every few seconds: the last write pointer the title kicked and how many
// kicks, where the read pointer is written back, and the title's Direct3D device
// (the interrupt callback's argument), whose counters say what its present is
// waiting for. Offsets from the device as the title's own code reads them
// (sub_82D9DFE0, sub_82D9E0F8, sub_82D8E218, sub_82D9E780).
std::atomic<uint32_t> g_last_write_pointer{0};
std::atomic<uint32_t> g_write_pointer_kicks{0};
// Kicks the title stored into the register window without the hooked store, found
// by the poller below (BUG-042).
std::atomic<uint32_t> g_unhooked_kicks{0};
// BUG-047: the last kicks - the write pointer, from the hook or the poller, when and from
// which thread - for the log when the command processor meets a packet it cannot execute.
struct Kick {
  uint32_t value = 0;
  bool polled = false;
  int64_t time_us = 0;
  std::string thread;
};
std::mutex g_kicks_mutex;
std::array<Kick, 16> g_kicks;
uint32_t g_kicks_recorded = 0;
thread_local bool t_polling = false;  // the poller's own WriteRegister

int64_t NowMicroseconds() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
std::atomic<uint32_t> g_writeback_physical{0};
std::atomic<uint32_t> g_ring_physical{0};
std::atomic<uint32_t> g_ring_size_log2{0};  // the ring is 1 << (size_log2 + 1) dwords
std::atomic<uint32_t> g_device{0};
std::atomic<uint32_t> g_swap_packets{0};
std::once_flag g_status_once;
std::string g_capture_dir;

// warden_memory.cc: write-watch faults per probed range (BUG-039).
extern "C" void warden_probe_range_set(unsigned index, uint32_t address, uint32_t length);
extern "C" uint32_t warden_probe_range_faults(unsigned index);
std::atomic<uint32_t> g_last_front_buffer{0};  // physical, from the last swap packet
std::atomic<uint32_t> g_last_front_width{0};
std::atomic<uint32_t> g_last_front_height{0};

// The presenter's guest output - the frame the GPU last produced, before the panel -
// as a PNG, so "black panel" can be told from "black frame".
void CaptureGuestOutput(xe::gpu::GraphicsSystem* graphics, unsigned index) {
  xe::ui::Presenter* presenter = graphics->presenter();
  if (!presenter || g_capture_dir.empty()) {
    return;
  }
  xe::ui::RawImage image;
  if (!presenter->CaptureGuestOutput(image) || !image.width || !image.height) {
    XELOGW("GPU: guest output capture {}: nothing to capture", index);
    return;
  }
  // Tightly packed RGBX rows for the writer.
  std::vector<uint8_t> packed(size_t(image.width) * image.height * 4);
  for (uint32_t y = 0; y < image.height; ++y) {
    std::memcpy(packed.data() + size_t(y) * image.width * 4,
                image.data.data() + size_t(y) * image.stride, size_t(image.width) * 4);
  }
  // The front buffer in guest memory - what the resolve wrote, read back - beside it.
  const uint32_t front = g_last_front_buffer.load();
  const uint32_t front_bytes = g_last_front_width.load() * g_last_front_height.load() * 4;
  // Only meaningful when resolves are read back (SetReadbackResolveMode in Start).
  if (front && front_bytes && g_memory &&
      xe::gpu::GetReadbackResolveMode() != xe::gpu::ReadbackResolveMode::kDisabled) {
    const uint8_t* bytes = g_memory->TranslatePhysical(front);
    uint64_t nonzero = 0;
    for (uint32_t i = 0; i < front_bytes; i += 4) {
      nonzero += (bytes[i] | bytes[i + 1] | bytes[i + 2] | bytes[i + 3]) ? 1 : 0;
    }
    XELOGI("GPU: front buffer in guest memory at {:08X} ({} bytes): {} of {} pixels "
           "non-zero", front, front_bytes, nonzero, front_bytes / 4);
    // The raw bytes too, for reading on the host: tiling, byte order and channels are
    // easier to see in a file than to guess in a log.
    if (index == 2 && !g_capture_dir.empty()) {
      const std::string raw = fmt::format("{}/front-buffer-{}.raw", g_capture_dir, index);
      if (FILE* file = fopen(raw.c_str(), "wb")) {
        fwrite(bytes, 1, front_bytes, file);
        fclose(file);
        XELOGI("GPU: front buffer raw bytes -> {}", raw);
      }
    }
  }
  // A quick read of the content, logged beside the file: how many pixels are not
  // black, and the mean of the rest.
  uint64_t lit = 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < packed.size(); i += 4) {
    const unsigned v = packed[i] + packed[i + 1] + packed[i + 2];
    sum += v;
    lit += v > 24 ? 1 : 0;
  }
  const std::string path = fmt::format("{}/guest-output-{}.png", g_capture_dir, index);
  const int ok = stbi_write_png(path.c_str(), int(image.width), int(image.height), 4,
                                packed.data(), int(image.width) * 4);
  XELOGI("GPU: guest output capture {}: {}x{}, {} of {} pixels lit, mean {:.1f} -> {} ({})",
         index, image.width, image.height, lit, packed.size() / 4,
         double(sum) / double(packed.size() / 4 * 3), path, ok ? "written" : "NOT written");
}

uint32_t ReadGuest32(uint8_t* base, uint32_t address) {
  if (!address) {
    return 0;
  }
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return __builtin_bswap32(value);
}

// Guest physical ranges to summarise every status tick, from files/probe.txt - one
// "address length" pair per line, hex - so memory the GPU reads (a texture, a movie
// plane) can be checked against what it shows without a rebuild. Bring-up only.
void ProbeMemory() {
  if (g_capture_dir.empty() || !g_memory) {
    return;
  }
  FILE* file = fopen((g_capture_dir + "/probe.txt").c_str(), "r");
  if (!file) {
    return;
  }
  // Per range: a hash of the bytes, kept between ticks, says whether the title wrote
  // them; the write-watch fault count (warden_memory.cc) says whether the GPU was told.
  static uint64_t last_hash[8] = {};
  unsigned address = 0;
  unsigned length = 0;
  unsigned index = 0;
  while (index < 8 && fscanf(file, "%x %x", &address, &length) == 2) {
    if (!length || length > 0x1000000u) {
      continue;
    }
    // Addresses below 0x20000000 are physical (GPU memory, write-watched); others are
    // guest virtual (the module's globals, heaps). A range of 256 bytes or fewer is
    // printed in full, as big-endian words, for reading a title's state.
    const bool virtual_range = address >= 0x20000000u;
    const uint8_t* bytes = virtual_range ? g_memory->TranslateVirtual(address)
                                         : g_memory->TranslatePhysical(address);
    if (!bytes) {
      continue;
    }
    if (!virtual_range) {
      warden_probe_range_set(index, address, length);
    }
    if (length <= 256) {
      std::string words;
      for (uint32_t i = 0; i + 4 <= length; i += 4) {
        words += fmt::format("{}{:02X}{:02X}{:02X}{:02X}", i % 32 ? " " : "\n    ", bytes[i],
                             bytes[i + 1], bytes[i + 2], bytes[i + 3]);
      }
      XELOGI("probe {:08X}+{:X}:{}", address, length, words);
      ++index;
      continue;
    }
    uint32_t histogram[4] = {0, 0, 0, 0};  // 0, 1-63, 64-191, 192-255
    uint64_t sum = 0;
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t i = 0; i < length; ++i) {
      const uint8_t b = bytes[i];
      sum += b;
      hash = (hash ^ b) * 1099511628211ull;
      histogram[b == 0 ? 0 : b < 64 ? 1 : b < 192 ? 2 : 3]++;
    }
    const bool changed = hash != last_hash[index];
    last_hash[index] = hash;
    XELOGI("GPU: probe {:08X}+{:X}: mean {:.1f}, bytes 0 {} / 1-63 {} / 64-191 {} / "
           "192-255 {}, first {:02X} {:02X} {:02X} {:02X}; {} since the last probe, {} "
           "write-watch faults in the range so far",
           address, length, double(sum) / length, histogram[0], histogram[1], histogram[2],
           histogram[3], bytes[0], bytes[1], bytes[2], bytes[3],
           changed ? "changed" : "unchanged", warden_probe_range_faults(index));
    ++index;
  }
  fclose(file);
}

// The register window is plain guest memory, and only the stores XenonRecomp marked
// as MMIO (an eieio-fenced store in Direct3D's ring kick) reach WriteRegister through
// the hook. When the title reached its main menu it kicked the ring from somewhere
// else - the word in the window changed, the GPU never heard, and the frame stopped
// (BUG-042). So the window's write pointer word is polled at 1 kHz: a value the hook
// has not delivered is a kick.
//
// But the hooked store writes the window before it calls the hook, and a poll falling
// between the two kicked too - some 0.4 a second at the King's Camp; of the kicks logged
// in runs 250-297, 397 three words past the last hooked value, 94 equal to it, three
// behind it - from this thread, ahead of the release in the hook that makes the packets
// the title wrote visible to the command processor: XenonRecomp's eieio, lwsync and sync
// are no-ops, and ARM64 may show another core the window's word before the packets
// (BUG-047, run 297). A value behind the hooked one sends the command processor round the
// whole ring again. So only a value still unhooked a poll later, and not behind the
// hooked one, is a kick.
void WritePointerPoller() {
  uint32_t seen = UINT32_MAX;  // an unhooked value from the previous poll
  for (;;) {
    // At 20 Hz while the guest is suspended (warden_suspend.cc): its threads are held,
    // and a thousand wake-ups a second would cost a put-away handheld its battery. Not
    // held outright - the vblank handler, which still runs, could leave a kick behind.
    std::this_thread::sleep_for(std::chrono::milliseconds(GuestSuspended() ? 50 : 1));
    uint8_t* base = GuestMemoryBase();
    if (!base || !g_graphics || !g_ring_physical.load()) {
      continue;
    }
    uint32_t word;
    std::memcpy(&word, base + kRegisterWindow + kRegWritePointer * 4, sizeof(word));
    std::atomic_thread_fence(std::memory_order_acquire);
    word = __builtin_bswap32(word);
    const uint32_t hooked = g_last_write_pointer.load();
    const uint32_t ring_dwords = uint32_t(1) << (g_ring_size_log2.load() + 1);
    if (word == hooked || word >= 0x10000 ||
        ((hooked - word) & (ring_dwords - 1)) < ring_dwords / 2) {
      seen = UINT32_MAX;
      continue;
    }
    if (word != seen) {
      seen = word;
      continue;
    }
    seen = UINT32_MAX;
    const uint32_t count = g_unhooked_kicks.fetch_add(1) + 1;
    if (count <= 8 || (count & (count - 1)) == 0) {
      XELOGW("GPU: ring write pointer {:08X} in the register window without a hooked "
             "store (last hooked {:08X}); kicked ({} so far)",
             word, g_last_write_pointer.load(), count);
    }
    t_polling = true;
    WriteRegister(kRegisterWindow + kRegWritePointer * 4, word);
    t_polling = false;
  }
}

void StatusLoop() {
  unsigned ticks = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    GuestSuspendPoint("GPU status");  // nothing to report while the guest is held
    uint8_t* base = GuestMemoryBase();
    xe::gpu::GraphicsSystem* graphics = g_graphics.get();
    if (!base || !graphics) {
      continue;
    }
    ++ticks;
    ProbeMemory();
    // 3.7: the Vulkan memory held, by the code that allocated it, when it has moved by
    // 16 MB (warden_gpu_memory.cc).
    if (const std::string memory = GpuMemoryReport(); !memory.empty()) {
      XELOGI("{}", memory);
    }
    // 4.9: files/hack-gpu-loss - the host GPU reported lost at 60 s, through the graphics
    // system's own path, to try the app's answer (a message, then the end of the process)
    // without a real loss.
    if (ticks == 12 && std::getenv("DAO_HACK_GPU_LOSS")) {
      XELOGW("GPU: experiment - reporting the host GPU lost");
      graphics->OnHostGpuLossFromAnyThread(true);
    }
    // At 10, 20, 40, 60, 80, 100, 120 and 150 s - the boot and the menus, the same eight
    // files at every start (BUG-079: no longer every minute after, 4 MB each without end,
    // in every session of play). files/capture-every holding N captures every N ticks
    // (5 s each) instead, all the way - for reading menus that a scripted run steps through
    // (files/route, files/inject-pad).
    static const unsigned capture_every = [] {
      unsigned n = 0;
      if (FILE* file = fopen((g_capture_dir + "/capture-every").c_str(), "r")) {
        if (fscanf(file, "%u", &n) != 1) {
          n = 0;
        }
        fclose(file);
      }
      return n;
    }();
    if (capture_every ? ticks % capture_every == 0
                      : (ticks == 2 || ticks == 4 || ticks == 8 || ticks == 12 || ticks == 16 ||
                         ticks == 20 || ticks == 24 || ticks == 30)) {
      CaptureGuestOutput(graphics, ticks);
    }
    std::string line = fmt::format(
        "GPU: write pointer {:08X} after {} kicks, {} swap packets, vblanks {}, {} "
        "watched writes served ({} races retried), paints: {} requested ({} from another "
        "thread, {} dropped), {} posted, {} painted",
        g_last_write_pointer.load(), g_write_pointer_kicks.load(), g_swap_packets.load(),
        graphics->command_processor() ? graphics->command_processor()->counter() : 0,
        warden_watched_write_faults(), warden_watched_write_races(), warden_paint_requests(),
        warden_deferred_paint_requests(), warden_deferred_paint_requests_dropped(),
        warden_frame_callbacks_posted(), warden_frame_paints());
    // DAORecomp's per-swap draw counts (dao_frame_trace.h, always on): what the title
    // submitted, what the backend issued to the host GPU, what failed or was predicated
    // away - for the last few swaps. A black frame with draws issued is a rendering
    // problem; with none submitted it is the title not drawing yet.
    {
      const uint32_t swaps = xe::gpu::dao_trace::SwapCount();
      std::string stats;
      for (uint32_t back = 1; back <= 3 && back <= swaps; ++back) {
        xe::gpu::dao_trace::FrameStats frame;
        if (xe::gpu::dao_trace::GetFrameStats(swaps - back, frame)) {
          stats += fmt::format("{}[{} submitted, {} issued, {} failed, {} viz-skipped]",
                               stats.empty() ? "" : " ", frame.submitted, frame.issued,
                               frame.failed, frame.viz_skipped);
        }
      }
      line += fmt::format("; GPU swaps {}, draws per recent swap: {}", swaps,
                          stats.empty() ? "none recorded" : stats);
      // BUG-112: the host's waits for the GPU since the last tick (gpu_completion_timeline.h).
      line += fmt::format("; GPU waits {} for {:.1f} ms (longest {:.1f})",
                          g_gpu_waits.exchange(0), g_gpu_wait_ms.exchange(0.0),
                          g_gpu_wait_max_ms.exchange(0.0));
      line += fmt::format("; passes {}, barriers {} ({} image, {} buffer)",
                          g_gpu_passes.exchange(0), g_gpu_barriers.exchange(0),
                          g_gpu_image_barriers.exchange(0), g_gpu_buffer_barriers.exchange(0));
      line += fmt::format("; submits {} for {:.1f} ms (longest {:.1f}); presents {} for {:.1f} ms (longest {:.1f})",
                          g_gpu_submits.exchange(0), g_gpu_submit_ms.exchange(0.0),
                          g_gpu_submit_max_ms.exchange(0.0), g_gpu_presents.exchange(0),
                          g_gpu_present_ms.exchange(0.0), g_gpu_present_max_ms.exchange(0.0));
    }
    // PLAN 10.2: the temperatures and the GPU's thermal cap, and the heat guard's step
    // when it is on (warden_thermal.cc).
    line += "; " + warden::thermal::Tick();
    const uint32_t writeback = g_writeback_physical.load();
    if (writeback && g_memory) {
      uint32_t value;
      std::memcpy(&value, g_memory->TranslatePhysical(writeback), sizeof(value));
      line += fmt::format("; read pointer written back {:08X}", __builtin_bswap32(value));
    }
    // Once: the ring's first words, raw, so the packets the title submitted can be read
    // off the log when the command processor stops on one of them.
    static bool ring_dumped = false;
    const uint32_t ring = g_ring_physical.load();
    if (!ring_dumped && ring && g_memory && g_last_write_pointer.load()) {
      ring_dumped = true;
      std::string words;
      const uint32_t count = 64;
      for (uint32_t i = 0; i < count; ++i) {
        uint32_t value;
        std::memcpy(&value, g_memory->TranslatePhysical(ring + 4 * i), sizeof(value));
        words += fmt::format("{}{:08X}", i ? " " : "", __builtin_bswap32(value));
      }
      XELOGI("GPU: ring at physical {:08X}, words 0..{}: {}", ring, count - 1, words);
    }
    const uint32_t device = g_device.load();
    if (device) {
      const uint32_t rptr_pointer = ReadGuest32(base, device + 10896);
      line += fmt::format(
          "; D3D device {:08X}: rptr at {:08X} = {:08X} (+60 {:08X}), counters +16560 {} "
          "+16564 {} +16568 {}, swap queue +16700 {} +16704 {}, +10888 {:08X} +11008 {}, "
          "flags +10941 {:02X}, ring write index +11012 {:08X}, ring +14896 {:08X} "
          "+14900 {:08X}, hook +21532 {:08X}",
          device, rptr_pointer, rptr_pointer ? ReadGuest32(base, rptr_pointer) : 0,
          rptr_pointer ? ReadGuest32(base, rptr_pointer + 60) : 0,
          ReadGuest32(base, device + 16560), ReadGuest32(base, device + 16564),
          ReadGuest32(base, device + 16568), ReadGuest32(base, device + 16700),
          ReadGuest32(base, device + 16704), ReadGuest32(base, device + 10888),
          ReadGuest32(base, device + 11008), base[device + 10941],
          ReadGuest32(base, device + 11012), ReadGuest32(base, device + 14896),
          ReadGuest32(base, device + 14900), ReadGuest32(base, device + 21532));
    }
    XELOGI("{}", line);
    // 6.7: how evenly the presenter queued frames and the title produced them, since the
    // previous report (warden_frame_times); 4.7: when they reached the panel, per pacing
    // mode (warden_frame_pacing).
    XELOGI("frames: {}", FrameTimesSummary());
    const std::string pacing = FramePacingSummary();
    if (!pacing.empty()) {
      XELOGI("pacing: {}", pacing);
    }
  }
}

xe::gpu::GraphicsSystem* Graphics() {
  // Read without the lock on the hot paths: the pointer is set once, before guest
  // code runs, and cleared only at shutdown.
  return g_graphics.get();
}

// BUG-079: the captures past the first eight left by earlier sessions (one a minute while
// the game ran, before), unless files/capture-every asks for captures throughout.
void RemoveLateCaptures() {
  std::error_code ec;
  if (std::filesystem::exists(g_capture_dir + "/capture-every", ec)) {
    return;
  }
  std::vector<std::filesystem::path> late;
  uintmax_t bytes = 0;
  for (const auto& entry : std::filesystem::directory_iterator(g_capture_dir, ec)) {
    unsigned index = 0;
    char extension[8] = {};
    const std::string name = entry.path().filename().string();
    if (std::sscanf(name.c_str(), "guest-output-%u.%7s", &index, extension) == 2 &&
        std::strcmp(extension, "png") == 0 && index > 30) {
      bytes += entry.file_size(ec);
      late.push_back(entry.path());
    }
  }
  size_t removed = 0;
  for (const auto& path : late) {
    removed += std::filesystem::remove(path, ec) ? 1 : 0;
  }
  if (removed) {
    XELOGI("GPU: {} guest output captures from earlier sessions removed ({} MB)", removed,
           bytes >> 20);
  }
}

}  // namespace

bool Start(xe::Memory& memory, xe::ui::WindowedAppContext* app_context,
           const std::string& capture_dir) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_graphics) {
    return true;
  }
  g_capture_dir = capture_dir;
  RemoveLateCaptures();
  // PLAN 9.16: the Rendering resolution option above 1280x720 - every render target at
  // twice the game's size (Xenia's draw resolution scale; the Thor has the sparse binding
  // it needs). Surfaces 160 pixels wide or less stay at the game's size: the HUD's
  // portraits (146x146), which the deck reads back from memory (warden_deck.cc).
  if (const uint32_t scale = warden::fsr3::RenderScale(); scale > 1) {
    cvars::draw_resolution_scale_x = int32_t(scale);
    cvars::draw_resolution_scale_y = int32_t(scale);
    cvars::draw_resolution_scale_threshold = 160;
    uint32_t width = 0, height = 0;
    warden::fsr3::RenderSize(width, height);
    XELOGI("GPU: rendering at {}x{} - the game's {}x{} at {}x scale", width * scale,
           height * scale, width, height, scale);
  }
  // DAORecomp's one-frame draw trace (dao_frame_trace.h): every draw of the frame
  // after this swap - shaders, render targets, textures, the resolves and the swap -
  // under the "FT" prefix. Bring-up only; ten seconds in, once the title is past
  // its first screens.
  if (!getenv("DAO_GPU_FRAME_TRACE")) {
    // The swap to trace: files/trace-swap holds a number, else 600 (about 10 s).
    // files/trace-swap: a swap number, or "busy" for the first frame after one with
    // 60 or more draws (the first frame of a fuller scene, wherever it falls).
    std::string swap = "600";
    if (FILE* file = fopen((capture_dir + "/trace-swap").c_str(), "r")) {
      char word[16] = {};
      if (fscanf(file, "%15s", word) == 1 && word[0]) {
        swap = word;
      }
      fclose(file);
    }
    setenv("DAO_GPU_FRAME_TRACE", swap.c_str(), 0);
  }
  // files/probe.txt's ranges are also the ranges whose GPU uploads are always logged
  // ("FT upload" lines, see UploadLogged in dao_frame_trace.h).
  if (FILE* file = fopen((capture_dir + "/probe.txt").c_str(), "r")) {
    std::string ranges;
    unsigned address = 0;
    unsigned length = 0;
    while (fscanf(file, "%x %x", &address, &length) == 2) {
      ranges += fmt::format("{}{:X}+{:X}", ranges.empty() ? "" : ",", address, length);
    }
    fclose(file);
    if (!ranges.empty()) {
      setenv("DAO_LOG_UPLOADS", ranges.c_str(), 1);
      XELOGI("GPU: uploads into {} are logged", ranges);
    }
  }
  // files/sample-threads: a thread name (substring) whose guest call chain the 5 s
  // report logs every time, for a spin the trace ring cannot attribute.
  if (FILE* file = fopen((capture_dir + "/sample-threads").c_str(), "r")) {
    char name[64] = {};
    if (fscanf(file, "%63s", name) == 1 && name[0]) {
      setenv("WARDEN_SAMPLE_THREADS", name, 1);
      XELOGI("GPU: sampling the call chain of threads named *{}* every report", name);
    }
    fclose(file);
  }
  // files/frame-pacing (4.7): "off", "paced", "alternate" (a minute of each in turn) or
  // "phases" (paced, four desired-time phases a minute each - run 298), for
  // warden_frame_pacing.cc; paced without it.
  // files/hack-log-bindings (BUG-049): a pixel shader hash (hex) - the texture bindings
  // of that shader's draws are logged as a traced frame's are, with the texture's state.
  if (FILE* file = fopen((capture_dir + "/hack-log-bindings").c_str(), "r")) {
    char hash[32] = {};
    if (fscanf(file, "%31s", hash) == 1 && hash[0]) {
      setenv("DAO_HACK_LOG_BINDINGS_PS", hash, 1);
      XELOGW("GPU: the texture bindings of pixel shader {}'s draws are logged", hash);
    }
    fclose(file);
  }
  if (FILE* file = fopen((capture_dir + "/frame-pacing").c_str(), "r")) {
    char mode[16] = {};
    if (fscanf(file, "%15s", mode) == 1 && mode[0]) {
      setenv("WARDEN_FRAME_PACING", mode, 1);
      XELOGI("GPU: frame pacing {}", mode);
    }
    fclose(file);
  }
  // BUG-039 bring-up: resolves are copied back to guest memory (Xenia's default is
  // not to), so the front buffer the swap reads can be inspected there - black in
  // guest memory means the draw or the resolve; content there and black on the panel
  // means the swap or the presenter. "full" waits for the GPU; accuracy over speed
  // while the first frame is being found.
  // xe::gpu::SetReadbackResolveMode("full");  // on when the front buffer must be read
  // files/dump-shaders: every translated shader (guest microcode and SPIR-V) under
  // files/shaders, to read what the driver compiles (BUG-039, BUG-045).
  if (std::filesystem::exists(std::filesystem::path(capture_dir) / "dump-shaders")) {
    cvars::dump_shaders = std::filesystem::path(capture_dir) / "shaders";
    XELOGW("GPU: translated shaders are dumped to {}", cvars::dump_shaders.string());
  }
  // PLAN 9.6, the settings' Texture filtering: the anisotropic filtering Xenia's texture
  // cache gives every mipmapped, linearly filtered texture (its anisotropic_override, as
  // DAORecomp's Graphics tab sets it). 16x unless files/anisotropic says 2, 4 or 8, or
  // "off" for the game's own filtering: at the King's Camp 16x costs nothing measurable
  // (the GPU 93% busy with it and without, 30 fps) and the receding ground stays sharp
  // (runs 355-357).
  {
    int level = 5;
    if (FILE* file = fopen((std::filesystem::path(capture_dir) / "anisotropic").string().c_str(), "r")) {
      char text[16] = {};
      if (fscanf(file, "%15s", text) == 1) {
        // xenos::AnisoFilter: 2:1 is 2, 4:1 3, 8:1 4, 16:1 5.
        level = strcmp(text, "off") == 0 ? -1
                : strcmp(text, "2") == 0 ? 2
                : strcmp(text, "4") == 0 ? 3
                : strcmp(text, "8") == 0 ? 4
                                         : 5;
      }
      fclose(file);
    }
    cvars::anisotropic_override = level;
    XELOGI("GPU: texture filtering {}", level < 0 ? std::string("the game's own")
                                                  : fmt::format("{}x anisotropic", 1 << (level - 1)));
  }
  // PLAN 9.1, 10.1, the settings' Frame rate: files/frame-rate - 30 (the title's own),
  // 45, 60 or 120; 60 without the file (warden_frame_rate.cc). The panel's frame pacing
  // follows it - paced at 30 only - unless files/frame-pacing chose a mode
  // (warden_frame_pacing.cc).
  {
    int frame_rate = 60;
    if (FILE* file = fopen((std::filesystem::path(capture_dir) / "frame-rate").string().c_str(), "r")) {
      if (fscanf(file, "%d", &frame_rate) != 1) {
        frame_rate = 60;
      }
      fclose(file);
    }
    warden::SetFrameRate(frame_rate);
    warden::FramePacingFollowFrameRate();
    XELOGI("GPU: frame rate {}{}", warden::FrameRate(),
           warden::TitleFrameRate() == 30.0 ? ", the game's own 30" : " where the GPU keeps up");
  }
  // BUG-039 experiments: a marker file files/hack-<name> sets DAO_HACK_<NAME>.
  // BUG-049, BUG-052: every pipeline is created when it is first drawn with, not drawn
  // with a placeholder pixel shader while the real one compiles in the background
  // (upstream's async_shader_compilation): a draw made once - the save thumbnail's
  // quad, the HUD portraits rendered at an area load - drew the placeholder, and the
  // thumbnail came out empty and the portraits without faces (runs 306-313).
  // files/hack-async-pipelines brings upstream's default back.
  cvars::async_shader_compilation = false;
  // PLAN 10.9: Options -> Shader math (files/shader-math), Exact unless chosen (the owner,
  // 2026-10-31). Fast: IEEE multiplication instead of the Xenos rule that zero times
  // anything is +0 (the translator's DAO_HACK_NO_MUL_ZERO) - not exact, 0 * inf becomes
  // NaN and 0 * -x -0 - about 40% fewer ALU instructions in the heaviest pixel shaders,
  // the King's Camp 22 -> 24 fps (run 500). From the next start (the shaders are
  // translated then; the driver compiles each pipeline once more). files/hack-no-mul-zero
  // still decides when it is there.
  if (FILE* file = fopen((capture_dir + "/shader-math").c_str(), "r")) {
    char word[16] = {};
    if (fscanf(file, "%15s", word) == 1 && strcmp(word, "fast") == 0) {
      setenv("DAO_HACK_NO_MUL_ZERO", "ieee", 0);
      XELOGI("GPU: shader math fast - IEEE multiplication (files/shader-math)");
    }
    fclose(file);
  }
  for (const char* hack : {"rt-readback", "no-sparse", "vk-validation", "resolve-readback",
                           "no-cull", "no-depth-test", "depth-always", "msaa-2x-as-4x",
                           "ps-red", "no-a0", "no-nmin", "vs-probe", "skip-vs",
                           "resolve-dump", "ps-paramgen", "rect-in-vs",
                           "no-float-controls", "tiny-scissor", "no-sample-mask",
                           "split-pass", "implicit-lod", "simple-fetch", "no-snap",
                           "no-integer-scale", "no-mul-zero", "no-transfers",
                           "block-render-load", "gpu-loss", "memexport-readback",
                           "no-resolve-direct", "async-pipelines", "pipeline-stats",
                           "precreate-pipelines", "fetch-raw"}) {
    if (std::filesystem::exists(std::filesystem::path(capture_dir) / (std::string("hack-") + hack))) {
      std::string name = std::string("DAO_HACK_") + hack;
      for (char& c : name) c = c == '-' ? '_' : char(toupper(c));
      // rt-readback and resolve-dump write files into the files directory: the
      // traced frame's first colour render target (rt-readback.raw), and every
      // resolve of the traced frame (resolve-<n>-<address>.raw, BUG-045).
      setenv(name.c_str(),
             strcmp(hack, "rt-readback") == 0 || strcmp(hack, "resolve-dump") == 0
                 ? capture_dir.c_str()
                 : "1",
             1);
      XELOGW("GPU: experiment {} is on", name);
      if (strcmp(hack, "ps-red") == 0 || strcmp(hack, "vs-probe") == 0 ||
          strcmp(hack, "skip-vs") == 0 || strcmp(hack, "ps-paramgen") == 0 ||
          strcmp(hack, "no-float-controls") == 0) {
        // The file's first word becomes the variable's value (BUG-045). For the first
        // four, shader ucode hashes (hex, as the FT lines print them; up to 8, comma
        // separated). ps-red: that pixel shader's colour outputs become solid red.
        // vs-probe: those vertex shaders' guest positions for vertices 0..4095 are read
        // back from the GPU for up to 8 draws of the traced frame and logged as screen
        // extents ("FT VS probe"). skip-vs: the draws of those vertex shaders are
        // dropped. ps-paramgen: those pixel shaders' colour 0 shows their raw
        // gl_FragCoord. no-float-controls: the float-control modes dropped from every
        // shader - n, d, r, or empty for all three (docs/TESTING.md).
        std::string hash;
        if (FILE* file = fopen((std::filesystem::path(capture_dir) / (std::string("hack-") + hack)).string().c_str(), "r")) {
          char text[256] = {};  // vs-probe: up to 8 hashes, comma separated
          if (fscanf(file, "%255s", text) == 1) {
            hash = text;
          }
          fclose(file);
        }
        setenv(name.c_str(), hash.c_str(), 1);
        XELOGW("GPU: {} applies to shader {}", name, hash);
      }
      if (strcmp(hack, "tiny-scissor") == 0) {
        // 3.8: a first word "<first>:<last>" limits the 8x8 scissor to those draws of
        // the traced frame (counted from 0); without one, every draw, as before.
        if (FILE* file = fopen((std::filesystem::path(capture_dir) / "hack-tiny-scissor").string().c_str(), "r")) {
          char text[32] = {};
          if (fscanf(file, "%31s", text) == 1 && strchr(text, ':')) {
            setenv("DAO_HACK_TINY_SCISSOR", text, 1);
            XELOGW("GPU: the 8x8 scissor applies to the traced frame's draws {}", text);
          }
          fclose(file);
        }
      }
      if (strcmp(hack, "split-pass") == 0) {
        // 3.8: the file's first word is n - in the traced frame a render pass is ended
        // every n draws, so the GPU pass timing (files/gpu-timing.txt) times each piece
        // of a long pass (default 1: every draw).
        if (FILE* file = fopen((std::filesystem::path(capture_dir) / "hack-split-pass").string().c_str(), "r")) {
          char text[16] = {};
          if (fscanf(file, "%15s", text) == 1) {
            setenv("DAO_HACK_SPLIT_PASS", text, 1);
          }
          fclose(file);
        }
        XELOGW("GPU: traced render passes split every {} draws", getenv("DAO_HACK_SPLIT_PASS"));
      }
      if (strcmp(hack, "no-mul-zero") == 0) {
        // 3.8: the first word picks the zero rule's form instead of the default
        // (|a| == 0) || (|b| == 0) - "nmin" (upstream's), "fmin", "parity" (even
        // ucode hashes "or", odd "fmin"), or IEEE multiplication (no rule) for
        // anything else, "vs" or "ps" in that stage only.
        if (FILE* file = fopen((std::filesystem::path(capture_dir) / "hack-no-mul-zero").string().c_str(), "r")) {
          char text[16] = {};
          if (fscanf(file, "%15s", text) == 1) {
            setenv("DAO_HACK_NO_MUL_ZERO", text, 1);
          }
          fclose(file);
        }
        XELOGW("GPU: the multiplication zero rule: {}", getenv("DAO_HACK_NO_MUL_ZERO"));
      }
      if (strcmp(hack, "resolve-dump") == 0) {
        // BUG-052: the file's first word, if any, is a resolve destination pitch
        // (RB_COPY_DEST_PITCH, hex, as the "GPU: resolve to" lines print it); every
        // resolve with that pitch is dumped too, traced frame or not - the HUD
        // portraits' is 009200A0. BUG-049: the second, a destination address (hex):
        // resolves written from there are dumped too - the save thumbnail's
        // 010001C0 and the scene copy it samples, 00DC3000.
        if (FILE* file = fopen((std::filesystem::path(capture_dir) / "hack-resolve-dump").string().c_str(), "r")) {
          char text[32] = {};
          if (fscanf(file, "%31s", text) == 1) {
            setenv("DAO_HACK_RESOLVE_DUMP_PITCH", text, 1);
            XELOGW("GPU: resolves with destination pitch {} are dumped too", text);
            char address[32] = {};
            if (fscanf(file, "%31s", address) == 1) {
              setenv("DAO_HACK_RESOLVE_DUMP_ADDRESS", address, 1);
              XELOGW("GPU: resolves written from {} are dumped too", address);
              // A third word, "base-only": the texture at that address is sampled at
              // its base level alone, whatever mips its fetch constant declares (run
              // 305: no change). "reload": with files/hack-log-bindings, before every
              // second draw of that pixel shader the texture there loads again.
              char option[32] = {};
              if (fscanf(file, "%31s", option) == 1) {
                if (strcmp(option, "base-only") == 0) {
                  setenv("DAO_HACK_BASE_ONLY_ADDRESS", address, 1);
                  XELOGW("GPU: experiment - the texture at {} has its base level only",
                         address);
                } else if (strcmp(option, "reload") == 0) {
                  setenv("DAO_HACK_RELOAD_ADDRESS", address, 1);
                  XELOGW("GPU: experiment - the texture at {} reloads for every second "
                         "logged draw", address);
                }
              }
            }
          }
          fclose(file);
        }
      }
      if (strcmp(hack, "msaa-2x-as-4x") == 0) {
        // Xenia's own switch: the title's 2x MSAA render targets become 4x on the host
        // (BUG-045: the 3D passes are the 2x MSAA ones, and they leave nothing).
        cvars::debug_msaa_2x_as_4x = true;
      }
      if (strcmp(hack, "async-pipelines") == 0) {
        // Upstream's default back: a new pipeline drawn with a placeholder pixel
        // shader while the real one compiles in the background (see below).
        cvars::async_shader_compilation = true;
      }
      if (strcmp(hack, "memexport-readback") == 0) {
        // BUG-049: what shaders write to memory (memory export) is copied back to guest
        // memory too, as resolves are with resolve-readback - "a huge performance
        // impact" upstream, so only for finding which writes the save thumbnail needs.
        xe::gpu::SaveGPUSetting(xe::gpu::GPUSetting::ReadbackMemexport, 1);
      }
      if (strcmp(hack, "resolve-readback") == 0) {
        // Every resolve's result is copied back to guest memory, so the memory probe
        // (files/probe.txt) and the front-buffer capture read what the GPU wrote
        // (BUG-045: the resolved depth and colour buffers). Costs most of the frame
        // rate (~10 Hz).
        xe::gpu::SetReadbackResolveMode("full");
      }
      if (strcmp(hack, "vk-validation") == 0) {
        // Xenia asks for VK_LAYER_KHRONOS_validation and VK_EXT_debug_utils, and
        // writes the layer's messages to its log. The layer itself ships in the
        // debug APK (jniLibs, never committed) and is enabled for this app through
        // the enable_gpu_debug_layers / gpu_debug_app / gpu_debug_layers settings.
        cvars::vulkan_validation = true;
      }
      if (strcmp(hack, "no-sparse") == 0) {
        // The shared memory as one plainly bound 512 MB buffer instead of a sparse
        // one bound page by page (BUG-039: no draw's fragments reach the render
        // target - zero vertex positions would do that).
        cvars::vulkan_sparse_shared_memory = false;
      }
    }
  }
  auto graphics = std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
  // with_presentation: a presenter for the window. The command processor starts its
  // thread here, and the vsync worker its 60 Hz, both attaching to the guest.
  const xe::X_STATUS status = graphics->Setup(&memory, app_context, true);
  if (status != 0) {
    XELOGE("GPU: the Vulkan graphics system did not set up ({:08X})", uint32_t(status));
    return false;
  }
  g_memory = &memory;
  // The 4:3 test (files/hack-video-mode "WxH 4:3"): the presenter's letterbox at 4:3, as the
  // title's output then is.
  if (warden::VideoFourByThree()) {
    graphics->SetScaledAspectRatio(4, 3);
    XELOGW("GPU: the presenter shows the title at 4:3 (files/hack-video-mode)");
  }
  g_graphics = std::move(graphics);
  // PLAN 9.4: FSR 3.1 brought up on the game's device - a test while it is being built
  // (files/fsr3-probe).
  if (std::filesystem::exists(std::filesystem::path(capture_dir) / "fsr3-probe")) {
    auto* provider = static_cast<xe::ui::vulkan::VulkanProvider*>(g_graphics->provider());
    const xe::ui::vulkan::VulkanDevice* device = provider->vulkan_device();
    warden::fsr3::Probe(device->physical_device(), device->device());
  }
  // PLAN 9.21: whether a second device of higher global priority gets its work through
  // while the game renders (files/hack-priority-probe: the seconds to wait first).
  if (FILE* file = fopen((capture_dir + "/hack-priority-probe").c_str(), "r")) {
    int delay = 240;
    if (fscanf(file, "%d", &delay) != 1) {
      delay = 240;
    }
    fclose(file);
    auto* provider = static_cast<xe::ui::vulkan::VulkanProvider*>(g_graphics->provider());
    const xe::ui::vulkan::VulkanDevice* device = provider->vulkan_device();
    XELOGI("GPU: the priority probe in {} s (files/hack-priority-probe)", delay);
    warden::priority_probe::Start(device, delay);
  }
  // PLAN 10.3: Xenia's shader and pipeline storage, under the files directory - the
  // pipelines of earlier sessions created again at startup, in the background, so an
  // area's first load and its first seconds wait for fewer of them (pipelines are
  // created when first drawn with: BUG-049, BUG-052). files/hack-no-pipeline-storage
  // turns it off. kPipelineStorageVersion is part of the path: bump it when the shader
  // translation's modification bits change meaning, or stored pipelines would be made
  // again with the old meaning.
  constexpr uint32_t kPipelineStorageVersion = 1;
  if (!std::filesystem::exists(std::filesystem::path(capture_dir) /
                               "hack-no-pipeline-storage")) {
    const std::filesystem::path cache_root =
        std::filesystem::path(capture_dir) /
        fmt::format("pipeline-storage-v{}", kPipelineStorageVersion);
    g_graphics->InitializeShaderStorage(cache_root, xe::dao_host::TitleId(), false);
    XELOGI("GPU: shader and pipeline storage at {}", cache_root.string());
  }
  std::call_once(g_status_once, [] {
    std::thread(StatusLoop).detach();
    std::thread(WritePointerPoller).detach();
  });
  XELOGI("GPU: Xenia's Vulkan graphics system is up - command processor and vsync "
         "worker running, presenter {}", g_graphics->presenter() ? "ready" : "missing");
  return true;
}

void Shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_graphics) {
    return;
  }
  g_graphics->Shutdown();
  g_graphics.reset();
  g_memory = nullptr;
}

bool Active() { return Graphics() != nullptr; }

xe::ui::Presenter* Presenter() {
  xe::gpu::GraphicsSystem* graphics = Graphics();
  return graphics ? graphics->presenter() : nullptr;
}

// PLAN 9.2, the settings' HDR: HDR10 where the display takes it. The Thor's panel does
// (Display.HdrCapabilities: HDR10 and HLG, 420 nits; its composer: hdr10=true), but its
// Vulkan swapchain offers no PQ colour space (BUG-025) - so the swapchain is the 10-bit
// sRGB one (warden_surface_format) and, once it is made, its window's buffers are tagged
// BT.2020 PQ directly (warden_swapchain_created), which Android's composer shows as an
// HDR10 layer. Meanwhile the presenter's final pass draws with its HDR variant
// (warden_hdr_paint_shader, warden_hdr_key; the graft's edits to vulkan_presenter.cc),
// which encodes the game's picture for it (shaders/warden_hdr_output.glsli): the
// highlights expanded toward the display's peak, white at the paper white
// (files/hdr-white, 200 nits without it), Vivid's primaries with files/colour's.
// files/hdr holding "on" asks for it; the options panel switches it while the game runs
// - the swapchain made again, and the final pass's pipeline with it.
namespace {
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_hdr_bilinear_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_hdr_cas_resample_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_hdr_cas_sharpen_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_hdr_fsr_rcas_ps.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_hdr_fxaa_ps.h"

std::atomic<ANativeWindow*> g_window{nullptr};
std::atomic<float> g_display_peak{0.0f};    // nits; 0: the display takes no HDR10
std::atomic<bool> g_hdr_for_next{false};    // warden_surface_format chose HDR10's swapchain
std::atomic<bool> g_hdr_active{false};      // the swapchain in use is tagged HDR10
std::atomic<uint32_t> g_hdr_generation{1};  // the HDR state's version: pipelines follow it

// A settings file's first word, or "" without it.
std::string SettingWord(const char* name) {
  char text[32] = {};
  if (FILE* file = fopen((g_capture_dir + "/" + name).c_str(), "r")) {
    if (fscanf(file, "%31s", text) != 1) {
      text[0] = 0;
    }
    fclose(file);
  }
  return text;
}

bool HdrWanted() { return g_display_peak.load() > 0.0f && SettingWord("hdr") == "on"; }

float HdrPaperWhite() {
  const int nits = atoi(SettingWord("hdr-white").c_str());
  return nits >= 80 && nits <= 400 ? float(nits) : 200.0f;
}
}  // namespace

void SetWindow(ANativeWindow* window) {
  g_window.store(window);
  // The picture's final size, for the performance tab and overlay.
  if (window) {
    const int32_t width = ANativeWindow_getWidth(window);
    const int32_t height = ANativeWindow_getHeight(window);
    if (width > 0 && height > 0) {
      warden::deck::SetRenderNote(warden::deck::RenderNote::kOutput,
                                  std::to_string(width) + "x" + std::to_string(height));
    }
  }
}

void SetDisplayHdrPeak(float nits) {
  g_display_peak.store(nits > 0.0f ? nits : 0.0f);
  if (nits > 0.0f) {
    XELOGI("GPU: the display takes HDR10, {:.0f} nits at its peak", nits);
  } else {
    XELOGI("GPU: the display takes no HDR10");
  }
}

void HdrSettingsChanged() { g_hdr_generation.fetch_add(1); }

// The final pass's HDR10 variant, by xe::ui::Presenter::GuestOutputPaintEffect's order:
// bilinear, its dithered variant (the app's FXAA - warden_paint_shader), CAS sharpening
// and its dithered one, CAS resampling and its dithered one, EASU (never the last
// pass), RCAS and its dithered one. A dithered one gets the plain HDR variant: the HDR10
// swapchain is 10-bit.
extern "C" bool warden_hdr_paint_shader(int effect, size_t* code_size, const uint32_t** code) {
  switch (effect) {
    case 0:
      *code_size = sizeof(warden_hdr_bilinear_ps);
      *code = warden_hdr_bilinear_ps;
      return true;
    case 1:
      *code_size = sizeof(warden_hdr_fxaa_ps);
      *code = warden_hdr_fxaa_ps;
      return true;
    case 2:
    case 3:
      *code_size = sizeof(warden_hdr_cas_sharpen_ps);
      *code = warden_hdr_cas_sharpen_ps;
      return true;
    case 4:
    case 5:
      *code_size = sizeof(warden_hdr_cas_resample_ps);
      *code = warden_hdr_cas_resample_ps;
      return true;
    case 7:
    case 8:
      *code_size = sizeof(warden_hdr_fsr_rcas_ps);
      *code = warden_hdr_fsr_rcas_ps;
      return true;
    default:
      return false;
  }
}

// The swapchain's pipeline is made again when this changes (the graft's edit to the
// presenter's paint): 0 while the output is standard range, the HDR state's version while
// it is HDR10. With `specialization`, the HDR variant's constants as well (null for
// standard range) - the paper white, the display's peak and Vivid, in the order of
// warden_hdr_output.glsli's constant ids - for the pipeline being made on this thread.
extern "C" uint32_t warden_hdr_key(const VkSpecializationInfo** specialization) {
  if (specialization) {
    *specialization = nullptr;
  }
  if (!g_hdr_active.load()) {
    return 0;
  }
  const uint32_t key = g_hdr_generation.load();
  if (specialization) {
    struct Constants {
      float paper_white;
      float peak;
      int32_t vivid;
    };
    thread_local Constants constants;
    thread_local const VkSpecializationMapEntry entries[3] = {
        {0, offsetof(Constants, paper_white), sizeof(float)},
        {1, offsetof(Constants, peak), sizeof(float)},
        {2, offsetof(Constants, vivid), sizeof(int32_t)}};
    thread_local VkSpecializationInfo info;
    constants.paper_white = HdrPaperWhite();
    constants.peak = g_display_peak.load();
    constants.vivid = SettingWord("colour") == "vivid" ? 1 : 0;
    info.mapEntryCount = 3;
    info.pMapEntries = entries;
    info.dataSize = sizeof(constants);
    info.pData = &constants;
    *specialization = &info;
    XELOGI("GPU: the final pass in HDR10 - white at {:.0f} nits, the peak {:.0f}, {} primaries",
           constants.paper_white, constants.peak,
           constants.vivid ? "Display-P3's (Vivid)" : "Rec.709's");
  }
  return key;
}

// Each swapchain the presenter makes (the graft's edit, after vkCreateSwapchainKHR): the
// one warden_surface_format chose for HDR10 gets its window's buffers tagged BT.2020 PQ;
// a standard-range one after it, the tag its own colour space gives (Android's Vulkan
// sets that as the swapchain is made; again here, to be sure).
extern "C" void warden_swapchain_created(VkFormat format, VkColorSpaceKHR color_space) {
  const bool wanted = g_hdr_for_next.exchange(false) &&
                      format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
                      color_space == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ANativeWindow* window = g_window.load();
  bool active = false;
  if (wanted) {
    const int32_t result =
        window ? ANativeWindow_setBuffersDataSpace(window, ADATASPACE_BT2020_PQ) : -1;
    active = result == 0;
    if (active) {
      XELOGI("GPU: HDR10 - the swapchain's buffers tagged BT.2020 PQ");
    } else {
      XELOGW("GPU: HDR10 - the buffers could not be tagged BT.2020 PQ ({}); standard range",
             result);
    }
  } else if (g_hdr_active.load() && window) {
    ANativeWindow_setBuffersDataSpace(
        window, color_space == VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT ? ADATASPACE_DISPLAY_P3
                                                                      : ADATASPACE_SRGB);
  }
  if (g_hdr_active.exchange(active) != active) {
    g_hdr_generation.fetch_add(1);
  }
}

// PLAN 9.3, the settings' Colour: the swapchain Xenia's presenter asks for (the graft's
// edit to vulkan_presenter.cc calls warden_surface_format before its own choice). The
// panel runs in Display-P3 - SurfaceFlinger's current colour mode, the decision D8
// took for granted - and a frame tagged sRGB is converted into it by Android, which is
// how the game's own colours reach it.
//   "accurate" (no file): 10-bit, tagged sRGB - the game's colours, converted from ten
//                         bits rather than eight (the upscaling's in-between values kept);
//   "vivid": 10-bit, tagged Display-P3 - the same values read as P3's, so every colour
//            stretched to the panel's wider gamut, more saturated than the game was made;
//   "8bit": Xenia's own choice, 8-bit sRGB, for comparing.
// Before gpu::Start - no files directory yet - and without the format, Xenia chooses.
extern "C" bool warden_surface_format(const VkSurfaceFormatKHR* formats, uint32_t count,
                                      VkSurfaceFormatKHR* chosen) {
  if (g_capture_dir.empty()) {
    return false;
  }
  // PLAN 9.2: HDR10 first - the 10-bit sRGB swapchain, its buffers tagged once it is made.
  g_hdr_for_next.store(false);
  if (HdrWanted()) {
    for (uint32_t i = 0; i < count; ++i) {
      if (formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
          formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        *chosen = formats[i];
        g_hdr_for_next.store(true);
        XELOGI("GPU: colour HDR10 - 10-bit, its buffers to be tagged BT.2020 PQ");
        return true;
      }
    }
    XELOGW("GPU: HDR10 - the surface has no 10-bit sRGB format; standard range");
  }
  char text[16] = "accurate";
  if (FILE* file = fopen((g_capture_dir + "/colour").c_str(), "r")) {
    if (fscanf(file, "%15s", text) != 1) {
      strcpy(text, "accurate");
    }
    fclose(file);
  }
  if (strcmp(text, "8bit") == 0) {
    XELOGI("GPU: colour - 8-bit sRGB, Xenia's own choice");
    return false;
  }
  const bool vivid = strcmp(text, "vivid") == 0;
  const VkColorSpaceKHR space =
      vivid ? VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  for (uint32_t i = 0; i < count; ++i) {
    if (formats[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
        formats[i].colorSpace == space) {
      *chosen = formats[i];
      XELOGI("GPU: colour {} - 10-bit, tagged {}", vivid ? "vivid" : "accurate",
             vivid ? "Display-P3 (the game's colours stretched to the panel's gamut)"
                   : "sRGB (Android converts it into the panel's Display-P3)");
      return true;
    }
  }
  XELOGW("GPU: colour {} - the surface has no 10-bit {} format; Xenia chooses",
         vivid ? "vivid" : "accurate", vivid ? "Display-P3" : "sRGB");
  return false;
}

// PLAN 9.7, 10.1, the settings' Upscaling -> FXAA: the shader of the presenter's
// dithered bilinear pass (the graft's edit to vulkan_presenter.cc asks here as it makes
// that module) is the app's FXAA (shaders/warden_fxaa.ps.glsl: NVIDIA's FXAA 3.11 and the
// bilinear upscale in one pass, with the bilinear shader's inputs). Nothing here asks
// Xenia to dither - its dithering is for 8-bit output, and the swapchain is 10-bit - so
// FXAA is the bilinear effect with dithering on (ApplyPaintSettings), and switches while
// the game runs like the other effects.
namespace {
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_fxaa_ps.h"
}  // namespace

extern "C" void warden_paint_shader(size_t* code_size, const uint32_t** code) {
  *code_size = sizeof(warden_fxaa_ps);
  *code = warden_fxaa_ps;
}

void ApplyPaintSettings() {
  xe::ui::Presenter* presenter = Presenter();
  if (!presenter) {
    return;
  }
  // PLAN 9.5, the settings' Upscaling: how the title's 1280x720 reaches the 1920x1080
  // panel - Xenia's own presenter effects: "fsr" (FidelityFX Super Resolution 1, EASU's
  // upscaling then RCAS's sharpening), "cas" (FidelityFX CAS) or "bilinear".
  using Effect = xe::ui::Presenter::GuestOutputPaintConfig::Effect;
  Effect effect = Effect::kBilinear;
  bool fxaa = false;
  const char* name = "bilinear";
  const char* label = "Bilinear";  // the option's own word, for the performance tab
  if (FILE* file = fopen((g_capture_dir + "/upscaling").c_str(), "r")) {
    char text[16] = {};
    if (fscanf(file, "%15s", text) == 1) {
      if (strcmp(text, "fsr") == 0) {
        effect = Effect::kFsr;
        name = "FSR 1 (EASU, RCAS)";
        label = "FSR 1";
      } else if (strcmp(text, "cas") == 0) {
        effect = Effect::kCas;
        name = "CAS";
        label = "CAS";
      } else if (strcmp(text, "fxaa") == 0) {
        // The dithered bilinear pass, its shader the app's FXAA (warden_paint_shader).
        fxaa = true;
        name = "FXAA 3.11 with the bilinear upscale";
        label = "FXAA";
      }
    }
    fclose(file);
  }
  xe::ui::Presenter::GuestOutputPaintConfig config =
      presenter->GetGuestOutputPaintConfigFromUIThread();
  config.SetEffect(effect);
  config.SetDither(fxaa);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);
  warden::deck::SetRenderNote(warden::deck::RenderNote::kPaint, label);
  XELOGI("GPU: upscaling {}", name);
}

void InitializeRingBuffer(uint32_t physical_address, uint32_t size_log2) {
  if (auto* graphics = Graphics()) {
    g_ring_physical.store(physical_address & 0x1FFFFFFF);
    g_ring_size_log2.store(size_log2);
    graphics->InitializeRingBuffer(physical_address, size_log2);
  }
}

void EnableReadPointerWriteBack(uint32_t address, uint32_t block_size_log2) {
  g_writeback_physical.store(address & 0x1FFFFFFF);
  if (auto* graphics = Graphics()) {
    graphics->EnableReadPointerWriteBack(address, block_size_log2);
  }
}

void SetInterruptCallback(uint32_t callback, uint32_t user_data) {
  g_device.store(user_data);
  if (auto* graphics = Graphics()) {
    graphics->SetInterruptCallback(callback, user_data);
  }
}

// BUG-052: with the portrait investigation on (files/hack-resolve-dump), each kick's new
// ring words are parsed and the indirect buffers they point to searched for the skinned
// head's draw initiator (10,788 indices, a triangle list) and for the portraits' render
// target (RB_SURFACE_INFO 0x028000A0, the 160-pitch surface) - both together are a
// portrait. The kicking thread is the one that recorded it: its name and guest call
// chain (and, the first time, the functions it entered last) for the first six portrait
// kicks, in any mode - the loads' broken portraits and a fight's working redraw.
void DaoScanKickForHeadDraw(uint32_t from, uint32_t to) {
  static const bool on = std::getenv("DAO_HACK_RESOLVE_DUMP_PITCH") != nullptr;
  static std::atomic<int> reported{0};
  const uint32_t ring = g_ring_physical.load();
  if (!on || !ring || !g_memory || reported.load() >= 6) {
    return;
  }
  bool portrait_target = false;
  const uint32_t ring_dwords = uint32_t(1) << (g_ring_size_log2.load() + 1);
  auto read = [](uint32_t physical) {
    uint32_t value;
    std::memcpy(&value, g_memory->TranslatePhysical(physical), sizeof(value));
    return __builtin_bswap32(value);
  };
  const uint32_t submitted = (to - from) & (ring_dwords - 1);
  uint32_t found = 0;
  for (uint32_t consumed = 0; consumed < submitted && !found;) {
    const uint32_t header = read(ring + 4 * ((from + consumed) & (ring_dwords - 1)));
    uint32_t length = 1;
    switch (header >> 30) {
      case 0:
        length = 2 + ((header >> 16) & 0x3FFF);
        break;
      case 1:
        length = 3;
        break;
      case 3: {
        const uint32_t opcode = (header >> 8) & 0x7F;
        const uint32_t count = ((header >> 16) & 0x3FFF) + 1;
        if ((opcode == 0x3F || opcode == 0x37) && count >= 2) {  // INDIRECT_BUFFER(_PFD)
          const uint32_t ib =
              read(ring + 4 * ((from + consumed + 1) & (ring_dwords - 1))) & 0x1FFFFFFF;
          const uint32_t ib_dwords =
              read(ring + 4 * ((from + consumed + 2) & (ring_dwords - 1))) & 0xFFFFF;
          for (uint32_t k = 0; k < ib_dwords; ++k) {
            const uint32_t word = read(ib + 4 * k);
            portrait_target = portrait_target || word == 0x028000A0;
            if (!found && (word >> 16) == 10788 && (word & 0x3F) == 4) {
              found = ib + 4 * k;
            }
          }
        }
        length = 1 + count;
        break;
      }
      default:
        break;
    }
    consumed += length;
  }
  if (!found || !portrait_target) {
    return;
  }
  const int n = reported.fetch_add(1) + 1;
  XELOGI("BUG-052: kick {} (write pointer {:08X} -> {:08X}) carries a portrait's head draw "
         "(its initiator at {:08X}), kicked by [{}]; its chain follows", n, from, to, found,
         CurrentGuestThreadName());
  warden::LogGuestCallChainOnFault(GuestMemoryBase());
  if (n == 1) {
    warden::LogGuestTraceNow("the kick carrying a portrait's head draw");
  }
}

void WriteRegister(uint32_t address, uint32_t value) {
  xe::gpu::GraphicsSystem* graphics = Graphics();
  if (!graphics) {
    static bool said = false;
    if (!said) {
      said = true;
      XELOGW("GPU: register write {:08X} <- {:08X} with no graphics system; the title "
             "is driving a GPU that is not there", address, value);
    }
    return;
  }
  // GraphicsSystem::WriteRegister takes the register index from the low 16 bits of the
  // address and hands CP_RB_WPTR to the command processor.
  graphics->WriteRegister(address, value);
  if (((address & 0xFFFF) >> 2) == kRegWritePointer) {
    {
      std::lock_guard<std::mutex> lock(g_kicks_mutex);
      Kick& kick = g_kicks[g_kicks_recorded++ % g_kicks.size()];
      kick.value = value;
      kick.polled = t_polling;
      kick.time_us = NowMicroseconds();
      kick.thread = t_polling ? std::string("poller") : CurrentGuestThreadName();
    }
    const uint32_t previous = g_last_write_pointer.exchange(value);
    const uint32_t kicks = g_write_pointer_kicks.fetch_add(1) + 1;
    if (kicks <= 8) {
      XELOGI("GPU: ring write pointer <- {:08X} (kick {})", value, kicks);
    }
    DaoScanKickForHeadDraw(previous, value);  // BUG-052, with its marker only
    if (!t_polling) {
      warden::FrameKicked();  // PLAN 10.1: at 45 and 120 the render thread is held here
    }
  }
}

// BUG-047: the command processor met a packet it cannot execute - the words around it,
// the stretch it was executing, the last kicks and what the title and the command
// processor last told each other, before the trap.
void LogPacketFailure(bool primary, uint32_t buffer, uint32_t buffer_dwords, uint32_t read_index,
                      uint32_t write_index, uint32_t position) {
  uint8_t* base = GuestMemoryBase();
  if (!g_memory || !base || !buffer_dwords) {
    return;
  }
  const uint32_t physical = buffer & 0x1FFFFFFF;
  auto word = [&](uint32_t index) {
    uint32_t value;
    std::memcpy(&value, g_memory->TranslatePhysical(physical + 4 * index), sizeof(value));
    return __builtin_bswap32(value);
  };
  // The reader stops past the packet's header.
  const uint32_t header = (position + buffer_dwords - 1) % buffer_dwords;
  const uint32_t first =
      primary ? (header + buffer_dwords - 8) % buffer_dwords : (header >= 8 ? header - 8 : 0);
  std::string words;
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t index = first + i;
    if (primary) {
      index %= buffer_dwords;
    } else if (index >= buffer_dwords) {
      break;
    }
    words += fmt::format("{}{}{:08X}", i % 8 ? " " : "\n    ", index == header ? "*" : "",
                         word(index));
  }
  XELOGE("GPU: BUG-047 - the {} at physical {:08X} ({} words) stopped at the packet at word "
         "{:X}, executing words {:X} to {:X}; from word {:X} (* the packet):{}",
         primary ? "primary ring" : "indirect buffer", physical, buffer_dwords, header,
         read_index, write_index, first, words);
  std::string kicks;
  {
    std::lock_guard<std::mutex> lock(g_kicks_mutex);
    const int64_t now = NowMicroseconds();
    const uint32_t n = std::min<uint32_t>(g_kicks_recorded, uint32_t(g_kicks.size()));
    for (uint32_t i = 0; i < n; ++i) {
      const Kick& kick = g_kicks[(g_kicks_recorded - n + i) % g_kicks.size()];
      kicks += fmt::format("{}{:X} {} [{}] {} us ago", i ? "; " : "", kick.value,
                           kick.polled ? "polled" : "hooked", kick.thread, now - kick.time_us);
    }
  }
  uint32_t written_back = 0;
  if (const uint32_t writeback = g_writeback_physical.load()) {
    std::memcpy(&written_back, g_memory->TranslatePhysical(writeback), sizeof(written_back));
    written_back = __builtin_bswap32(written_back);
  }
  const uint32_t device = g_device.load();
  XELOGE("GPU: BUG-047 - the last kicks, oldest first: {}; the read pointer written back "
         "{:08X}; the title's own ring write index (device +11012) {:08X}",
         kicks, written_back, device ? ReadGuest32(base, device + 11012) : 0);
  xe::FlushLog();
}

void WriteSwapPacket(uint32_t buffer, uint32_t fetch_constant, uint32_t front_buffer_virtual,
                     uint32_t width, uint32_t height) {
  namespace xenos = xe::gpu::xenos;
  uint8_t* base = GuestMemoryBase();
  if (!base || !buffer || !fetch_constant) {
    return;
  }
  // The fetch constant comes from the Direct3D texture header, which holds a virtual
  // address; the GPU wants the physical one.
  xenos::xe_gpu_texture_fetch_t fetch;
  xe::copy_and_swap_32_unaligned(&fetch, base + fetch_constant, 6);
  uint32_t front_buffer_physical =
      g_memory ? g_memory->GetPhysicalAddress(front_buffer_virtual) : UINT32_MAX;
  if (front_buffer_physical == UINT32_MAX) {
    front_buffer_physical = front_buffer_virtual & 0x1FFFFFFF;
  }
  fetch.base_address = front_buffer_physical >> 12;

  auto* dwords = reinterpret_cast<xe::be<uint32_t>*>(base + buffer);
  uint32_t offset = 0;
  dwords[offset++] = xenos::MakePacketType0(xe::gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 6);
  dwords[offset++] = fetch.dword_0;
  dwords[offset++] = fetch.dword_1;
  dwords[offset++] = fetch.dword_2;
  dwords[offset++] = fetch.dword_3;
  dwords[offset++] = fetch.dword_4;
  dwords[offset++] = fetch.dword_5;
  dwords[offset++] = xenos::MakePacketType3(xenos::PM4_XE_SWAP, 4);
  dwords[offset++] = xenos::kSwapSignature;
  dwords[offset++] = front_buffer_physical;
  dwords[offset++] = width;
  dwords[offset++] = height;
  while (offset < 64) {
    dwords[offset++] = xenos::MakePacketType2();
  }
  g_swap_packets.fetch_add(1);
  RecordGuestSwap();  // 6.7: the title's frame times (warden_frame_times)
  g_last_front_buffer.store(front_buffer_physical);
  g_last_front_width.store(width);
  g_last_front_height.store(height);
}

}  // namespace warden::gpu

// The recompiled code's memory-mapped stores (eieio-fenced) into the register window.
extern "C" void warden_gpu_register_write(uint32_t address, uint32_t value) {
  warden::gpu::WriteRegister(address, value);
}

// pm4_command_processor_implement.h, before its trap on a packet it cannot execute (a
// recorded edit in tools/assemble_runtime.py; BUG-047).
extern "C" void warden_gpu_packet_failed(bool primary, uint32_t buffer, uint32_t buffer_dwords,
                                         uint32_t read_index, uint32_t write_index,
                                         uint32_t position) {
  warden::gpu::LogPacketFailure(primary, buffer, buffer_dwords, read_index, write_index,
                                position);
}
