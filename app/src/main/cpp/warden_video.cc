// The video-driver imports: what the title asks the kernel about the display and the
// GPU before it touches the command processor. Task 5.1h found them: once startup had
// threads, the Update thread went straight into Direct3D device creation.
//
// None of these drives the GPU yet - that is the command-processor work after first
// light - but their answers decide whether the title believes it has a display at
// all. Values follow Xenia's xboxkrnl_video.cc and xam_video.cc, where a title's
// expectations have already been found out the hard way.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "ppc_recomp_shared.h"
#include "warden_frame_rate.h"
#include "warden_gpu.h"
#include "warden_guest.h"
#include "warden_guest_heap.h"
#include "warden_threads.h"
#include "warden_trace.h"
#include "xenia/base/logging.h"

namespace {

// The GPU's register file is mapped at 0x7FC80000 on the console and the title reads
// and writes it directly: the interrupt callback tests the vblank bit of the interrupt
// status register before it does anything, the vblank handler writes the front buffer
// address, Direct3D kicks the command processor by writing the ring buffer's write
// pointer. Here the window is plain guest memory (the 4 GiB reservation is read-write),
// so a read returns what was last stored - and what the title expects to find is stored
// here first, with the values Xenia's GraphicsSystem::ReadRegister answers. Trapping
// the writes (the write pointer) is the command processor's work, after first light.
constexpr uint32_t kGpuRegisterBase = 0x7FC80000;
constexpr uint32_t kRegEdramTiming = 0x0F00;        // RB_EDRAM_TIMING
constexpr uint32_t kRegBcControl = 0x0F01;          // RB_BC_CONTROL
constexpr uint32_t kRegHsioAligner = 0x0F2D;        // RB_HSIO_INTERFACE_ALIGNER_VALUE
constexpr uint32_t kRegInterruptStatus = 0x1951;    // bit 0: a vblank is pending
constexpr uint32_t kRegViewportSize = 0x1961;       // AVIVO_D1MODE_VIEWPORT_SIZE

uint32_t DisplayWidth();  // the mode the title is told, below
uint32_t DisplayHeight();

void PresetGpuRegisters(uint8_t* base) {
  PPC_STORE_U32(kGpuRegisterBase + kRegEdramTiming * 4, 0x08100748);
  PPC_STORE_U32(kGpuRegisterBase + kRegBcControl * 4, 0x0000200E);
  PPC_STORE_U32(kGpuRegisterBase + kRegHsioAligner * 4, 0x00BBBBBB);
  PPC_STORE_U32(kGpuRegisterBase + kRegInterruptStatus * 4, 1);
  PPC_STORE_U32(kGpuRegisterBase + kRegViewportSize * 4,
                (DisplayWidth() << 16) | DisplayHeight());  // 1280x720: 0x050002D0
}

// The mode the title is told it is running in: 1280x720, the console's standard HD
// output and what this title rendered at. The presenter scales it to the panel.
// PLAN 9.4's experiment: files/hack-video-mode holding WxH tells the title that mode
// instead, to see whether it renders its frames at another size.
struct VideoSize {
  uint32_t width = 1280;
  uint32_t height = 720;
};

const VideoSize& DisplaySize() {
  static const VideoSize size = [] {
    VideoSize told;
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    if (FILE* file = dir ? std::fopen((std::string(dir) + "/hack-video-mode").c_str(), "r")
                         : nullptr) {
      unsigned width = 0, height = 0;
      if (std::fscanf(file, "%ux%u", &width, &height) == 2 && width >= 320 && height >= 180 &&
          width <= 1920 && height <= 1080) {
        told.width = width;
        told.height = height;
      }
      std::fclose(file);
      XELOGW("video mode: the title is told {}x{} (files/hack-video-mode)", told.width,
             told.height);
    }
    return told;
  }();
  return size;
}
uint32_t DisplayWidth() { return DisplaySize().width; }
uint32_t DisplayHeight() { return DisplaySize().height; }
constexpr uint32_t kVideoStandardNtscM = 1;

std::mutex g_video_mutex;
// VdSetGraphicsInterruptCallback: the title's handler for vblank and command
// processor interrupts, and the value it wants passed back. For DispatchInterrupt
// (warden_dao_host.cc) once the GPU runs.
uint32_t g_interrupt_callback = 0;
uint32_t g_interrupt_user_data = 0;
// VdSetSystemCommandBufferGpuIdentifierAddress: where the title keeps its command
// buffer identifier. Remembered, not used yet.
uint32_t g_gpu_identifier_address = 0;

// The graphics interrupt. On the console the GPU raises one at every vblank and one
// when a swap it was asked for completes; the kernel runs the title's callback as a
// DPC with the source - 0 vblank, 1 swap - and the title's own argument. The title's
// callback (sub_82DA12C8) clears its pending-swap bits on a swap and, on a vblank with
// the status register's bit set, runs its vblank handler, which counts frames and lets
// Direct3D's worker go. Without either, that worker times out every 30 ms and the
// Update thread waits for a frame that never completes (BUG-036).
//
// Delivered from a host thread attached to the guest, at 60 Hz, as Xenia's vsync
// worker does (GraphicsSystem::MarkVblank). A VdSwap marks a swap pending and the
// next tick reports it complete before the vblank, which is the console's order. The
// thread starts with the first callback registration.
std::atomic<bool> g_swap_pending{false};
std::atomic<uint64_t> g_vblanks{0};
std::atomic<uint64_t> g_swaps{0};
std::once_flag g_interrupt_thread_once;

void InterruptThread() {
  if (!warden::AttachGuestThreadForHost("gpu-interrupt")) {
    XELOGE("gpu-interrupt: could not attach to the guest; no vblank will ever fire");
    return;
  }
  uint8_t* base = warden::GuestMemoryBase();
  PPCContext* ctx = warden::CurrentGuestContext();
  PresetGpuRegisters(base);
  constexpr auto kPeriod = std::chrono::nanoseconds(1000000000 / 60);
  auto next = std::chrono::steady_clock::now();
  uint32_t last_missing = 0;
  for (;;) {
    next += kPeriod;
    std::this_thread::sleep_until(next);
    uint32_t callback;
    uint32_t user_data;
    {
      std::lock_guard<std::mutex> lock(g_video_mutex);
      callback = g_interrupt_callback;
      user_data = g_interrupt_user_data;
    }
    if (!callback) {
      continue;
    }
    PPCFunc* function = PPC_LOOKUP_FUNC(base, callback);
    if (!function) {
      if (callback != last_missing) {
        XELOGE("gpu-interrupt: callback {:08X} is not a translated function", callback);
        last_missing = callback;
      }
      continue;
    }
    // The vblank bit stays set, as Xenia's register read always answers 1; the title
    // tests it and does not clear it.
    PPC_STORE_U32(kGpuRegisterBase + kRegInterruptStatus * 4, 1);
    if (g_swap_pending.exchange(false)) {
      ctx->r3.u64 = 1;
      ctx->r4.u64 = user_data;
      function(*ctx, base);
      const uint64_t swaps = ++g_swaps;
      if (swaps <= 3 || swaps % 600 == 0) {
        XELOGI("gpu-interrupt: swap {} reported complete", swaps);
      }
    }
    ctx->r3.u64 = 0;
    ctx->r4.u64 = user_data;
    function(*ctx, base);
    const uint64_t vblanks = ++g_vblanks;
    if (vblanks <= 2 || vblanks % 3600 == 0) {
      XELOGI("gpu-interrupt: vblank {} delivered to {:08X}", vblanks, callback);
    }
  }
}

void EnsureInterruptThread() {
  std::call_once(g_interrupt_thread_once, [] { std::thread(InterruptThread).detach(); });
}

// X_VIDEO_MODE, 48 bytes, big-endian: display_width, display_height, is_interlaced,
// is_widescreen, is_hi_def, refresh_rate (float), video_standard, pixel_rate,
// widescreen_flag, reserved[3].
void WriteVideoMode(uint8_t* base, uint32_t address) {
  std::memset(base + address, 0, 48);
  PPC_STORE_U32(address + 0, DisplayWidth());
  PPC_STORE_U32(address + 4, DisplayHeight());
  PPC_STORE_U32(address + 8, 0);   // not interlaced
  PPC_STORE_U32(address + 12, 1);  // widescreen
  PPC_STORE_U32(address + 16, 1);  // hi-def
  const float refresh = 60.0f;
  uint32_t refresh_bits;
  std::memcpy(&refresh_bits, &refresh, sizeof(refresh_bits));
  PPC_STORE_U32(address + 20, refresh_bits);
  PPC_STORE_U32(address + 24, kVideoStandardNtscM);
  PPC_STORE_U32(address + 28, 0x8A);  // pixel rate, as Xenia reports it
  PPC_STORE_U32(address + 32, 1);     // widescreen flag: 1 widescreen, 3 not
}

}  // namespace

// void XGetVideoMode(X_VIDEO_MODE* mode)  - XAM's, the same answer as the kernel's.
PPC_FUNC(__imp__XGetVideoMode) {
  if (ctx.r3.u32) {
    WriteVideoMode(base, ctx.r3.u32);
  }
}

// void VdQueryVideoMode(X_VIDEO_MODE* mode)
PPC_FUNC(__imp__VdQueryVideoMode) {
  if (ctx.r3.u32) {
    WriteVideoMode(base, ctx.r3.u32);
  }
}

// DWORD VdQueryVideoFlags()  - 1 widescreen, 2 at least 720p, 4 at least 1080p.
PPC_FUNC(__imp__VdQueryVideoFlags) {
  (void)base;
  ctx.r3.u64 = 1 | 2 | (DisplayWidth() >= 1920 ? 4 : 0);  // HD whatever the experiment tells
}

// void VdGetCurrentDisplayGamma(DWORD* type, float* power)
//
// 2 is the TV (BT.709) curve, with the power Xenia uses beside it; the title's gamma
// ramp code reads both.
PPC_FUNC(__imp__VdGetCurrentDisplayGamma) {
  if (ctx.r3.u32) {
    PPC_STORE_U32(ctx.r3.u32, 2);
  }
  if (ctx.r4.u32) {
    const float power = 2.22222233f;
    uint32_t bits;
    std::memcpy(&bits, &power, sizeof(bits));
    PPC_STORE_U32(ctx.r4.u32, bits);
  }
}

// DWORD VdInitializeEngines(DWORD unknown, void* callback, void* callback_argument,
//                           DWORD* pfp_microcode, DWORD* me_microcode)
//
// 1 for success, as Xenia answers. The microcode pointers are the title's copies of
// the GPU's own firmware; nothing here runs them.
PPC_FUNC(__imp__VdInitializeEngines) {
  PresetGpuRegisters(base);
  XELOGI("VdInitializeEngines({:08X}, callback {:08X}, argument {:08X}, pfp {:08X}, "
         "me {:08X}) -> 1",
         ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  ctx.r3.u64 = 1;
}

// BOOL VdIsHSIOTrainingSucceeded()
//
// Whether the GPU's high-speed link trained at boot. 1, as Xenia and DAORecomp answer:
// the stub's 0 told Direct3D's device creation the link had failed, and from the
// second frame on it routed every submission through a hook object it never had
// instead of kicking the ring - the present storm of BUG-038.
PPC_FUNC(__imp__VdIsHSIOTrainingSucceeded) {
  (void)base;
  ctx.r3.u64 = 1;
}

// DWORD VdInitializeScalerCommandBuffer(source_xy, source_wh, output_xy, output_wh,
//     front_buffer_wh, vertical_filter_type, vertical_filter, horizontal_filter_type,
//     horizontal_filter, unknown, DWORD* dest, DWORD count)
//
// Twelve arguments; dest and count are the 11th and 12th, on the stack at r1 + 0x64
// and + 0x6C. The console fills the buffer with the display scaler's commands; it is
// only ever executed by the display hardware, so type-2 (no-op) packets do, as Xenia
// and DAORecomp write. Returns the count written.
PPC_FUNC(__imp__VdInitializeScalerCommandBuffer) {
  const uint32_t dest = PPC_LOAD_U32(ctx.r1.u32 + 0x64);
  const uint32_t count = PPC_LOAD_U32(ctx.r1.u32 + 0x6C);
  if (dest) {
    for (uint32_t i = 0; i < count; ++i) {
      PPC_STORE_U32(dest + 4 * i, 0x80000000u);
    }
  }
  ctx.r3.u64 = count;
}

// BOOL VdPersistDisplay(DWORD unknown, DWORD* out) - a 64-byte physical block, as Xenia.
PPC_FUNC(__imp__VdPersistDisplay) {
  if (ctx.r4.u32) {
    uint32_t block = 0;
    uint32_t size = 0;
    if (warden::AllocatePhysicalMemory(64, 0x1000, block, size) != 0) {
      block = 0;
    }
    PPC_STORE_U32(ctx.r4.u32, block);
  }
  ctx.r3.u64 = 1;
}

// DWORD VdSetDisplayMode(DWORD flags) - accepted; the mode is fixed at 1280x720.
PPC_FUNC(__imp__VdSetDisplayMode) {
  (void)base;
  ctx.r3.u64 = 0;
}

// void VdGetCurrentDisplayInformation(X_DISPLAY_INFO* info)
//
// 0x58 bytes, DAORecomp's layout (gpu/video.cpp): front buffer size, the scaler's
// source rectangle and output size, filters, overscan, display size and refresh rate.
PPC_FUNC(__imp__VdGetCurrentDisplayInformation) {
  const uint32_t info = ctx.r3.u32;
  if (!info) {
    return;
  }
  std::memset(base + info, 0, 0x58);
  PPC_STORE_U16(info + 0x00, DisplayWidth());   // front buffer width
  PPC_STORE_U16(info + 0x02, DisplayHeight());  // front buffer height
  PPC_STORE_U32(info + 0x10, DisplayWidth());   // scaler source rectangle: right
  PPC_STORE_U32(info + 0x14, DisplayHeight());  //                         bottom
  PPC_STORE_U32(info + 0x18, DisplayWidth());   // scaled output width
  PPC_STORE_U32(info + 0x1C, DisplayHeight());  // scaled output height
  PPC_STORE_U32(info + 0x20, 1);               // vertical filter type
  PPC_STORE_U32(info + 0x30, 1);               // horizontal filter type
  PPC_STORE_U16(info + 0x40, 320);             // overscan left, top, right, bottom
  PPC_STORE_U16(info + 0x42, 180);
  PPC_STORE_U16(info + 0x44, 320);
  PPC_STORE_U16(info + 0x46, 180);
  PPC_STORE_U16(info + 0x48, DisplayWidth());   // display width
  PPC_STORE_U16(info + 0x4A, DisplayHeight());  // display height
  const float refresh = 60.0f;
  uint32_t refresh_bits;
  std::memcpy(&refresh_bits, &refresh, sizeof(refresh_bits));
  PPC_STORE_U32(info + 0x4C, refresh_bits);
  PPC_STORE_U16(info + 0x56, DisplayWidth());   // actual display width
}

// DWORD VdRetrainEDRAM(...) / VdRetrainEDRAMWorker(...)  - 0: nothing to retrain.
PPC_FUNC(__imp__VdRetrainEDRAM) {
  (void)base;
  ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__VdRetrainEDRAMWorker) {
  (void)base;
  ctx.r3.u64 = 0;
}

// void VdShutdownEngines()
//
// Titles call an initialise/shutdown pair to probe, then initialise again; nothing to
// undo here.
PPC_FUNC(__imp__VdShutdownEngines) {
  (void)ctx;
  (void)base;
  XELOGI("VdShutdownEngines()");
}

// void VdSetGraphicsInterruptCallback(void* callback, void* user_data)
//
// The callback takes (source, user_data): 0 vblank, 1 command processor. Kept for
// DispatchInterrupt; a null callback unregisters, which device teardown does.
PPC_FUNC(__imp__VdSetGraphicsInterruptCallback) {
  (void)base;
  {
    std::lock_guard<std::mutex> lock(g_video_mutex);
    g_interrupt_callback = ctx.r3.u32;
    g_interrupt_user_data = ctx.r4.u32;
  }
  XELOGI("VdSetGraphicsInterruptCallback({:08X}, {:08X})", ctx.r3.u32, ctx.r4.u32);
  if (warden::gpu::Active()) {
    // The graphics system's vsync worker and command processor raise the interrupts.
    warden::gpu::SetInterruptCallback(ctx.r3.u32, ctx.r4.u32);
  } else if (ctx.r3.u32) {
    // No GPU: the vblank and a swap-complete per VdSwap still have to come from
    // somewhere, or the title's Direct3D waits forever.
    EnsureInterruptThread();
  }
}

// void VdInitializeRingBuffer(void* ring, DWORD size_log2)
//
// The PM4 ring buffer the title's Direct3D writes its commands into: a physical
// address and a size as a power of two (plus three, as the command processor counts it).
PPC_FUNC(__imp__VdInitializeRingBuffer) {
  (void)base;
  XELOGI("[{}] VdInitializeRingBuffer({:08X}, 2^{})", warden::CurrentGuestThreadName(),
         ctx.r3.u32, ctx.r4.u32);
  warden::gpu::InitializeRingBuffer(ctx.r3.u32, ctx.r4.u32);
}

// void VdEnableRingBufferRPtrWriteBack(void* address, DWORD block_size_log2)
//
// Where the command processor publishes how far it has read, which is what the title's
// Direct3D polls to know the GPU has caught up (sub_82D8E218 spins on it).
PPC_FUNC(__imp__VdEnableRingBufferRPtrWriteBack) {
  (void)base;
  XELOGI("[{}] VdEnableRingBufferRPtrWriteBack({:08X}, 2^{})",
         warden::CurrentGuestThreadName(), ctx.r3.u32, ctx.r4.u32);
  warden::gpu::EnableReadPointerWriteBack(ctx.r3.u32, ctx.r4.u32);
}

// void VdSwap(void* command_buffer, void* fetch, void* writeback, void* system_buffer,
//             void* system_buffer_identifier, DWORD* frontbuffer, DWORD* texture_format,
//             DWORD* color_space, DWORD* width, DWORD* height)
//
// Ten arguments, as Xenia's VdSwap_entry lists them; the last two are on the stack at
// r1 + 0x54 and r1 + 0x5C. With the GPU up, the swap packet goes into the 64 words the
// title reserved in its ring at command_buffer - the front buffer's fetch constant and
// an XE_SWAP - and the command processor presents when it executes it. Without the GPU
// the swap is complete when the fallback interrupt thread next runs: a present that
// took one frame and showed nothing.
PPC_FUNC(__imp__VdSwap) {
  const uint32_t command_buffer = ctx.r3.u32;
  const uint32_t fetch = ctx.r4.u32;
  const uint32_t frontbuffer_pointer = ctx.r8.u32;
  const uint32_t width_pointer = PPC_LOAD_U32(ctx.r1.u32 + 0x54);
  const uint32_t height_pointer = PPC_LOAD_U32(ctx.r1.u32 + 0x5C);
  static std::atomic<int> logged{0};
  const int call = logged.fetch_add(1, std::memory_order_relaxed);
  const bool log = call < 3;
  warden::FrameSwapped();  // PLAN 10.1: at 45 and 120 the render thread is paced here
  if (warden::gpu::Active() && command_buffer && fetch && frontbuffer_pointer &&
      width_pointer && height_pointer) {
    const uint32_t frontbuffer = PPC_LOAD_U32(frontbuffer_pointer);
    const uint32_t width = PPC_LOAD_U32(width_pointer);
    const uint32_t height = PPC_LOAD_U32(height_pointer);
    warden::gpu::WriteSwapPacket(command_buffer, fetch, frontbuffer, width, height);
    if (log) {
      XELOGI("[{}] VdSwap(command buffer {:08X}, fetch {:08X}, front buffer {:08X}, "
             "{}x{}) -> swap packet written", warden::CurrentGuestThreadName(),
             command_buffer, fetch, frontbuffer, width, height);
    }
    return;
  }
  g_swap_pending.store(true);
  if (log) {
    XELOGI("[{}] VdSwap(command buffer {:08X}, fetch {:08X}) -> no GPU; complete at the "
           "next vblank", warden::CurrentGuestThreadName(), command_buffer, fetch);
  }
}

// void VdSetSystemCommandBufferGpuIdentifierAddress(void* address)
PPC_FUNC(__imp__VdSetSystemCommandBufferGpuIdentifierAddress) {
  (void)base;
  std::lock_guard<std::mutex> lock(g_video_mutex);
  g_gpu_identifier_address = ctx.r3.u32;
}

// void VdGetSystemCommandBuffer(void** buffer, void** unknown)
//
// Xenia's answer: a 0x94-byte zeroed block whose first word is a marker, and a second
// marker word. Nothing has read either yet.
PPC_FUNC(__imp__VdGetSystemCommandBuffer) {
  if (ctx.r3.u32) {
    std::memset(base + ctx.r3.u32, 0, 0x94);
    PPC_STORE_U32(ctx.r3.u32, 0xBEEF0000);
  }
  if (ctx.r4.u32) {
    PPC_STORE_U32(ctx.r4.u32, 0xBEEF0001);
  }
}
