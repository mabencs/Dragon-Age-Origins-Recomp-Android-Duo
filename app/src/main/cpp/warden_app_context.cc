#include "warden_app_context.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

#include "warden_lifecycle.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {
// Any value unique among the fds this thread's looper watches. Android's own
// identifiers are small integers, so this is deliberately not one of those.
constexpr int kWakeIdent = 0x4441;  // 'DA'
}  // namespace

AppContext::AppContext() {
  looper_ = ALooper_forThread();
  if (!looper_) {
    // The Android main thread always has one, so this means the context was built on
    // the wrong thread - which would silently break every CallInUIThread later.
    XELOGE("AppContext constructed on a thread with no looper");
    return;
  }
  ALooper_acquire(looper_);

  // Non-blocking at both ends (BUG-056). OnWake drains until a read fails, which on
  // a blocking pipe is never: the first wake-up from another thread - the guest
  // output thread asking for a paint after a surface comes back - parked the main
  // thread in read() for good, and Android called the app not responding. The
  // writer's EAGAIN case below relies on it too.
  int fds[2];
  if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
    XELOGE("AppContext: pipe2() failed: {}", errno);
    return;
  }
  wake_read_ = fds[0];
  wake_write_ = fds[1];

  if (ALooper_addFd(looper_, wake_read_, kWakeIdent, ALOOPER_EVENT_INPUT, OnWake,
                    this) != 1) {
    XELOGE("AppContext: ALooper_addFd failed");
    close(wake_read_);
    close(wake_write_);
    wake_read_ = wake_write_ = -1;
    return;
  }
  XELOGI("AppContext attached to the main thread looper");
}

AppContext::~AppContext() {
  // Run whatever is queued before tearing the pipe down, not after: a queued function
  // dropped on the floor is a use-after-free waiting to happen in whatever was
  // expecting it to run.
  //
  // windowed_app_context.h says to call ExecutePendingFunctionsFromUIThread(true)
  // here, but that overload is private - only the no-argument public one is reachable
  // from a subclass, and it is what Xenia's own Win32 and Android contexts call. Safe
  // at this point: it may invoke NotifyUILoopOfPendingFunctions, which is still this
  // object's override with its pipe intact, because the base destructor has not run.
  ExecutePendingFunctionsFromUIThread();
  if (looper_ && wake_read_ >= 0) {
    ALooper_removeFd(looper_, wake_read_);
  }
  if (wake_read_ >= 0) {
    close(wake_read_);
  }
  if (wake_write_ >= 0) {
    close(wake_write_);
  }
  wake_read_ = wake_write_ = -1;
  if (looper_) {
    ALooper_release(looper_);
    looper_ = nullptr;
  }
}

void AppContext::NotifyUILoopOfPendingFunctions() {
  if (wake_write_ < 0) {
    return;
  }
  // One byte is enough: the reader drains whatever is there and runs the queue once,
  // so coalescing several notifications into one wake-up is correct, not a loss.
  const uint8_t token = 1;
  while (write(wake_write_, &token, sizeof(token)) < 0) {
    if (errno != EINTR) {
      // EAGAIN means the pipe is full, which means a wake-up is already pending -
      // the UI thread will run the queue. Nothing is lost by not writing.
      break;
    }
  }
}

void AppContext::PlatformQuitFromUIThread() {
  // Nothing to break out of: Android runs the main loop, and only Java can finish an
  // activity. The request goes where the title's own end goes (warden_lifecycle, task
  // 4.9): Java polls it, finishes the activity and ends the process.
  quit_requested_.store(true, std::memory_order_relaxed);
  RequestExit("the runtime asked to quit", false);
}

int AppContext::OnWake(int fd, int events, void* data) {
  (void)events;
  uint8_t drain[16];
  while (read(fd, drain, sizeof(drain)) > 0) {
    // Drain fully: the queue is run once below regardless of how many tokens arrived.
    // An empty pipe ends the loop with EAGAIN - it is non-blocking (BUG-056).
  }
  static_cast<AppContext*>(data)->ExecutePendingFunctionsFromUIThread();
  return 1;  // keep watching this fd
}

}  // namespace warden
