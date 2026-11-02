// The frame rate (PLAN 9.1, 10.1): the title's own 30, or 45, 60 or 120.
//
// DAORecomp's way (DAORecomp/gpu/video.cpp, SwapInterruptHook and PaceFrames). Direct3D's
// swap interrupt handler, sub_82D9E0F8 in the title update's code, runs when the GPU
// reaches a swap; its argument packs the front buffer's address (the high bits), the
// presentation interval (bits 8-11) and the immediate-present threshold (the low byte),
// and the flip is queued for the vblank `interval` after the last one. The title presents
// its 3D scenes with interval 2 - 30 fps on its 60 Hz vblank.
//   60       the interval clamped to 1, a flip a vblank, and the threshold set to 100
//            (percent): a flip late for its vblank is shown at once rather than held for
//            the next one, so a frame a little over a vblank costs that little, not a
//            whole vblank.
//   45, 120  interval 0: each frame flipped at once, not at a vblank, and the title's
//            render thread held to the rate instead - once the frame is handed to the GPU
//            (its write of the ring buffer's write pointer just after VdSwap), so the
//            finished frame is not held back from it. A frame late by more than a frame
//            starts the schedule again. 0.5% faster than the rate: a display at about the
//            rate then paces the frames itself, rather than the two clocks drifting apart
//            and a refresh going without a new frame now and then.
// The guest's flip only chooses which frame is presented next; the presenting and its
// pacing on the panel are warden_frame_pacing.cc's.
//
// The setting is files/frame-rate - "30", "45", "60" or "120", 60 without the file - read
// as the GPU starts (warden_gpu.cc) and changed by the options panel while the game runs
// (Runtime::ApplyOption). PLAN 9.14 (the owner): it is the rate on the screen. With frame
// generation (warden_frame_generation.cc, one frame made between each two of the game's)
// the game itself runs at half of it - 60 is the title's own 30 then, 120 every vblank,
// and 45 and 30 the render thread held to 22.5 and 15 - and with 3x and 4x at a third or a
// quarter (120 with 4x: the title's own 30 again).
#include "warden_frame_rate.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "xenia/base/logging.h"

// warden_frame_generation.cc: the frames generated between each two of the game's (set
// at the start; 0 without frame generation).
extern "C" uint32_t warden_frame_generation_count();

namespace warden {

namespace {
std::atomic<int> g_frame_rate{60};
// PLAN 10.2: the heat guard's limit, 0 for none (SetFrameRateCap).
std::atomic<int> g_frame_rate_cap{0};

// The render thread's: a frame swapped and not yet handed to the GPU, and when the next
// may be.
thread_local bool t_frame_pending = false;
thread_local std::chrono::steady_clock::time_point t_next_frame;

// The frames on the screen for each of the game's: 2, 3 or 4 with frame generation.
int Multiplier() { return int(warden_frame_generation_count()) + 1; }

void PaceFrame() {
  using Clock = std::chrono::steady_clock;
  const double rate = TitleFrameRate();
  if (rate == 30.0 || rate == 60.0) {
    t_next_frame = {};
    return;
  }
  const auto period = std::chrono::nanoseconds(int64_t(1e9 / (rate * 1.005)));
  const auto now = Clock::now();
  if (t_next_frame == Clock::time_point{} || now - t_next_frame > period) {
    t_next_frame = now;
  } else if (now < t_next_frame) {
    std::this_thread::sleep_until(t_next_frame);
  }
  t_next_frame += period;
}
}  // namespace

void SetFrameRate(int frames_per_second) {
  const int rate = frames_per_second == 30 || frames_per_second == 45 ||
                           frames_per_second == 120
                       ? frames_per_second
                       : 60;
  g_frame_rate.store(rate, std::memory_order_relaxed);
  if (Multiplier() > 1) {
    XELOGI("frame rate: {} on the screen - the game at {:g}, {} frame(s) generated after "
           "each",
           rate, double(rate) / Multiplier(), Multiplier() - 1);
  }
}

int FrameRate() {
  const int rate = g_frame_rate.load(std::memory_order_relaxed);
  const int cap = g_frame_rate_cap.load(std::memory_order_relaxed);
  return cap && cap < rate ? cap : rate;
}

int ChosenFrameRate() { return g_frame_rate.load(std::memory_order_relaxed); }

void SetFrameRateCap(int frames_per_second) {
  const int cap =
      frames_per_second == 30 || frames_per_second == 45 || frames_per_second == 60
          ? frames_per_second
          : 0;
  if (g_frame_rate_cap.exchange(cap, std::memory_order_relaxed) == cap) {
    return;
  }
  XELOGI("frame rate: {} on the screen{} - the game at {:g}", FrameRate(),
         cap && cap < ChosenFrameRate() ? fmt::format(" (held there; {} chosen)", ChosenFrameRate())
                                        : std::string(),
         TitleFrameRate());
}

double TitleFrameRate() { return double(FrameRate()) / Multiplier(); }

void FrameSwapped() {
  // A frame not handed over by the next swap is paced then.
  if (t_frame_pending) {
    PaceFrame();
  }
  t_frame_pending = true;
}

void FrameKicked() {
  if (!t_frame_pending) {
    return;
  }
  t_frame_pending = false;
  PaceFrame();
}

}  // namespace warden

WARDEN_GAME_FUNC(__imp__tu6_sub_82D9E0F8);
WARDEN_OVERRIDE(tu6_sub_82D9E0F8) {
  const double rate = warden::TitleFrameRate();
  const uint32_t interval = (ctx.r3.u32 >> 8) & 0xF;
  if (rate == 60.0) {
    if (interval >= 1) {
      ctx.r3.u64 = (ctx.r3.u64 & ~uint64_t(0xFFF)) | 0x100 | 100;
    }
  } else if (rate != 30.0) {
    ctx.r3.u64 &= ~uint64_t(0xF00);
  }
  __imp__tu6_sub_82D9E0F8(ctx, base);
}
