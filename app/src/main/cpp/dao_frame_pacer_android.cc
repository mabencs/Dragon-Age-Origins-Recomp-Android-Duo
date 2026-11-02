// Presenter::Dao* - frame generation's presentation (PLAN 9.14), in place of DAORecomp's
// xenia/ui/dao_frame_pacer.cc.
//
// Upstream's file is excluded from this build (tools/compile_runtime.py, EXCLUDE_PLATFORM):
// it waits with Win32 waitable timers and events, and its timing does not fit the Thor
// (session 37, runs 427-438). Upstream hands each guest output with a generated frame to a
// pacer thread that paints the generated frame at once and the real one half a frame
// later, by the CPU's clock. On a PC the GPU keeps up, so a paint runs when it is
// submitted. Here the game is GPU-bound and everything runs on one queue in order: the
// real frame's paint, submitted half a frame later, ran right after the generated one
// anyway, behind the title's frame, and the pacer, its paints held up behind the frames in
// flight, judged the panel "behind" and kept falling back to real frames only (run 427).
//
// What is kept is upstream's reason for the thread: the guest output thread - the GPU
// command processor's - must not wait for presentation. In FIFO, Android's buffer queue
// takes a second frame only once the first has been latched, which needs the GPU to have
// finished it, so presenting the pair from the guest output thread cost it about one GPU
// frame at every guest output (runs 432-438: the real frame's present 35 ms, the title at
// 22 fps, the GPU idle a fifth of the time). So the guest output thread only hands each
// guest output with a generated frame over, and this file's thread paints the group as soon
// as it gets it - the generated frames (one with 2x, up to three with 4x), then the real
// one - and takes the queue's waits. The spacing is the compositor's: each present carries
// a desired time on one timeline, the group's frames the title's frame interval apart
// divided among them (warden_frame_pacing.cc, VK_GOOGLE_display_timing; this file names
// each present's role).
// A guest output handed over while the thread is still presenting the one before replaces
// any not yet started (the newest is shown, as the mailbox does).
//
// The thread-local stage below tells the presenter's paint which image to show
// (DaoPacerPainting) and to keep the guest output its generated frame's paint took for the
// real frame's (DaoKeepAcquired).

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <system_error>

#include "xenia/base/logging.h"
#include "xenia/ui/presenter.h"

// warden_frame_pacing.cc: the next present's role in a group - 0 none, 1 its first
// generated frame, 3 a later one, 2 the real frame.
extern "C" void warden_present_role(int role);
// warden_frame_generation.cc: the frames generated between each two of the game's.
extern "C" uint32_t warden_frame_generation_count();
// warden_frame_pacing.cc: whether guest outputs are paced as groups now (BUG-092).
extern "C" bool warden_frame_pacing_groups();

namespace xe {
namespace ui {

namespace {

// The pair thread's nice value: above the game's threads (a late present is a visible
// stutter), as Android's display threads are.
constexpr int kPairThreadNice = -8;

// What the calling thread is painting: a Presenter::DaoPacerStage (protected
// there), and of which guest output.
thread_local int t_pacer_stage = 0;
thread_local uint64_t t_pacer_serial = 0;
// Which of the guest output's generated frames (the presenter's paint picks its image).
thread_local uint32_t t_pacer_generated_index = 0;
// BUG-092: the real frame painted is a group of one - a guest output without a generated
// frame in a short gap between groups - so the paint takes the newest guest output
// rather than keeping the one a generated frame's paint took.
thread_local bool t_pacer_lone = false;
// ... and the group handed to the thread is one (set with the serial, under its mutex).
bool g_pacer_lone = false;

}  // namespace

Presenter::DaoPacerStage Presenter::DaoPacerPainting() {
  return DaoPacerStage(t_pacer_stage);
}

bool Presenter::DaoKeepAcquired(const GuestOutputProperties& acquired) {
  // The group's later frames: the guest output its first generated frame's paint took,
  // just before on this thread - even if a newer one is ready by now (it is the next
  // group's).
  (void)acquired;
  return (DaoPacerStage(t_pacer_stage) == DaoPacerStage::kReal && !t_pacer_lone) ||
         (DaoPacerStage(t_pacer_stage) == DaoPacerStage::kGenerated &&
          t_pacer_generated_index > 0);
}

bool Presenter::DaoPacerOutdated(const GuestOutputProperties& consumed) {
  // The pair thread shows the newest guest output it takes; nothing is skipped as outdated.
  (void)consumed;
  return false;
}

bool Presenter::DaoGeneratedFramesShown() const {
  // Upstream stops making generated frames for a while when its pacer falls behind; the
  // compositor paces them here, and at 120 Hz the Thor's panel shows twice the title's
  // frame rate up to its 60.
  return true;
}

void Presenter::DaoResetFramePacing() {}

bool Presenter::DaoQueueFramePaints(const GuestOutputProperties& properties) {
  // Not the pacer's business, but this is the one place our code sees every guest
  // frame the command processor hands the presenter, with the presenter's own state
  // in reach: how it will paint (0 none, 1 from the UI thread on request, 2 from
  // this thread at once) and whether its surface connection can be painted to. Once
  // a second, so a black panel can be read as "no frames", "no paint mode" or
  // "no connection" (BUG-038's fourth act).
  static std::atomic<uint32_t> refreshes{0};
  const uint32_t count = refreshes.fetch_add(1, std::memory_order_relaxed) + 1;
  if (count <= 3 || count % 60 == 0) {
    XELOGI("presenter: guest frame {} refreshed ({}x{}, active {}), paint mode {}, "
           "surface connection state {}{}",
           count, properties.frontbuffer_width, properties.frontbuffer_height,
           properties.IsActive(), int(paint_mode_),
           int(surface_paint_connection_state_),
           properties.dao_generated ? ", with a generated frame" : "");
  }
  // BUG-092: without a generated frame, a guest output in a short gap between groups
  // goes to the groups' thread too, as a group of one; otherwise it is painted here, as
  // without frame generation.
  const bool lone = !properties.dao_generated;
  if (lone && !warden_frame_pacing_groups()) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(dao_pacer_mutex_);
    if (dao_pacer_shutdown_ || (lone && !dao_pacer_thread_.joinable())) {
      return false;
    }
    if (!dao_pacer_thread_.joinable()) {
      try {
        dao_pacer_thread_ = std::thread(&Presenter::DaoPacerThread, this);
      } catch (const std::system_error& error) {
        XELOGE("Frame generation: no thread for the pairs ({}), showing real frames",
               error.what());
        dao_pacer_shutdown_ = true;
        return false;
      }
    }
    // A pair to present; one not yet started is replaced.
    dao_pacer_serial_ = properties.dao_serial;
    dao_pacer_stage_ = DaoPacerStage::kGenerated;
    g_pacer_lone = lone;
  }
  dao_pacer_cv_.notify_one();
  return true;
}

// The UI thread's paints show the newest guest output's real frame, as without frame
// generation: the stage stays kNone there.
void Presenter::DaoBeginUIThreadPaint() {}

void Presenter::DaoEndUIThreadPaint() {}

void Presenter::DaoStopPacer() {
  {
    std::lock_guard<std::mutex> lock(dao_pacer_mutex_);
    dao_pacer_shutdown_ = true;
  }
  dao_pacer_cv_.notify_one();
  if (dao_pacer_thread_.joinable()) {
    dao_pacer_thread_.join();
  }
}

void Presenter::DaoPacerThread() {
  const int nice_result = setpriority(PRIO_PROCESS, gettid(), kPairThreadNice);
  XELOGI("Frame generation: the groups' thread is up{}",
         nice_result == 0 ? fmt::format(", nice {}", kPairThreadNice)
                          : std::string(" (at the default priority)"));
  // Its time in the pairs' presents, logged every 300 pairs.
  double present_ms[2] = {}, present_max_ms[2] = {};
  uint32_t pairs = 0;
  for (;;) {
    uint64_t serial;
    {
      std::unique_lock<std::mutex> lock(dao_pacer_mutex_);
      dao_pacer_cv_.wait(lock, [this] {
        return dao_pacer_shutdown_ || dao_pacer_stage_ != DaoPacerStage::kNone;
      });
      if (dao_pacer_shutdown_) {
        break;
      }
      serial = dao_pacer_serial_;
      dao_pacer_stage_ = DaoPacerStage::kNone;
      t_pacer_lone = g_pacer_lone;
    }

    PaintResult paint_result = PaintResult::kNotPresented;
    double ms[2] = {};
    {
      std::lock_guard<std::mutex> paint_mode_lock(paint_mode_mutex_);
      switch (paint_mode_) {
        case PaintMode::kNone:
          break;
        case PaintMode::kUIThreadOnRequest:
          // Painted there, the real frame only (as without frame generation).
          RequestPaintOrConnectionRecoveryViaWindow(true);
          break;
        case PaintMode::kGuestOutputThreadImmediately:
          if (surface_paint_connection_state_ !=
              SurfacePaintConnectionState::kConnectedPaintable) {
            break;
          }
          t_pacer_serial = serial;
          {
            const uint32_t generated = std::max(warden_frame_generation_count(), 1u);
            auto last = std::chrono::steady_clock::now();
            bool painting = true;
            // The generated frames, then the real one - or a group of one's real frame
            // alone (BUG-092).
            for (uint32_t i = t_pacer_lone ? generated : 0; i <= generated && painting; ++i) {
              const bool real = i == generated;
              t_pacer_stage =
                  int(real ? DaoPacerStage::kReal : DaoPacerStage::kGenerated);
              t_pacer_generated_index = i;
              warden_present_role(t_pacer_lone ? 4 : real ? 2 : i == 0 ? 1 : 3);
              paint_result = PaintAndPresent(false);
              const auto now = std::chrono::steady_clock::now();
              ms[real ? 1 : 0] += std::chrono::duration<double, std::milli>(now - last).count();
              last = now;
              painting = surface_paint_connection_state_ ==
                             SurfacePaintConnectionState::kConnectedPaintable &&
                         paint_result != PaintResult::kGpuLostResponsible &&
                         paint_result != PaintResult::kGpuLostExternally;
            }
          }
          warden_present_role(0);
          t_pacer_stage = int(DaoPacerStage::kNone);
          t_pacer_lone = false;
          if (surface_paint_connection_state_ ==
              SurfacePaintConnectionState::kConnectedOutdated) {
            RequestPaintOrConnectionRecoveryViaWindow(true);
          }
          break;
      }
    }
    if (host_gpu_loss_callback_) {
      if (paint_result == PaintResult::kGpuLostResponsible) {
        host_gpu_loss_callback_(true, false);
      } else if (paint_result == PaintResult::kGpuLostExternally) {
        host_gpu_loss_callback_(false, false);
      }
    }
    for (int i = 0; i < 2; ++i) {
      present_ms[i] += ms[i];
      present_max_ms[i] = std::max(present_max_ms[i], ms[i]);
    }
    if (++pairs == 300) {
      XELOGI("Frame generation: the groups' thread - the generated frames' paints and "
             "presents {:.2f} ms (max {:.2f}), the real one's {:.2f} ms (max {:.2f})",
             present_ms[0] / pairs, present_max_ms[0], present_ms[1] / pairs,
             present_max_ms[1]);
      pairs = 0;
      present_ms[0] = present_ms[1] = present_max_ms[0] = present_max_ms[1] = 0.0;
    }
  }
}

}  // namespace ui
}  // namespace xe

// The generated frame the calling thread paints (vulkan_presenter.cc, the graft).
extern "C" uint32_t warden_pacer_generated_index() {
  return xe::ui::t_pacer_generated_index;
}
