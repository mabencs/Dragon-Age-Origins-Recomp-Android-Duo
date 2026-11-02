// WindowedAppContext over the Android main thread's looper. Phase 4.2a.
//
// Xenia's Window needs a WindowedAppContext: its "post this function to the UI thread"
// service, plus a way to wake the UI loop. On the platforms Xenia targets, the context
// also *owns* the loop - it runs it. Here it does not, and must not: Android owns the
// main thread's loop, and the activity is Java's.
//
// So this implementation does the one thing the interface genuinely requires and
// nothing more. Only two methods are pure virtual:
//
//   NotifyUILoopOfPendingFunctions  callable from any thread; must make the UI thread
//                                   notice. A pipe registered with the main thread's
//                                   ALooper - the pattern android_native_app_glue uses.
//   PlatformQuitFromUIThread        on other platforms, break out of the loop. Here,
//                                   the app's end (warden_lifecycle): only Java can
//                                   finish an activity, and it polls.
//
// Xenia's own AndroidWindowedAppContext is not used: it drives Xenia's Java activity
// through cached JNI method IDs and expects to own the surface. This app owns its
// activity, so it provides the context itself.
#ifndef WARDEN_APP_CONTEXT_H_
#define WARDEN_APP_CONTEXT_H_

#include <android/looper.h>

#include <atomic>

#include "xenia/ui/windowed_app_context.h"

namespace warden {

class AppContext final : public xe::ui::WindowedAppContext {
 public:
  // Must be constructed on the Android main thread: the base class records the
  // constructing thread as the UI thread, and the looper is taken from it.
  AppContext();
  ~AppContext() override;

  bool valid() const { return wake_write_ >= 0; }

  // Set if Xenia ever asks the app to quit; the request itself goes to
  // warden_lifecycle, which Java polls (task 4.9).
  bool quit_requested() const {
    return quit_requested_.load(std::memory_order_relaxed);
  }

 protected:
  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

 private:
  static int OnWake(int fd, int events, void* data);

  ALooper* looper_ = nullptr;
  int wake_read_ = -1;
  int wake_write_ = -1;
  std::atomic<bool> quit_requested_{false};
};

}  // namespace warden

#endif  // WARDEN_APP_CONTEXT_H_
