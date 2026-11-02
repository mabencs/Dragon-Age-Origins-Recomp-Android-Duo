// xe::ui::Window over a SurfaceView's ANativeWindow. Phase 4.2a.
//
// Xenia's Presenter cannot connect to a surface without a Window:
// SetWindowSurfaceFromUIThread asserts that a surface cannot exist without one, and
// calls window_->RequestPaint() when the guest output thread needs the UI thread to
// paint. So presenting through Xenia's presenter requires this class to exist.
//
// Xenia's own AndroidWindow is not used - it casts app_context() to
// AndroidWindowedAppContext and asks that for the activity's surface, so it is bound
// to Xenia's Java activity rather than ours. The four pure virtuals are small enough
// that implementing them against our own surface is less work than adapting to theirs,
// and far less surprising to read.
//
// Everything here runs on the Android main thread except RequestPaintImpl, which the
// guest output thread may call and which routes itself back to the main thread.
#ifndef WARDEN_WINDOW_H_
#define WARDEN_WINDOW_H_

#include <android/choreographer.h>
#include <android/native_window.h>

#include <cstdint>
#include <memory>

#include "warden_app_context.h"
#include "xenia/ui/window.h"

namespace warden {

class Window final : public xe::ui::Window {
 public:
  Window(AppContext& app_context, uint32_t width, uint32_t height);
  ~Window() override;

  // The handheld's panels are 1920x1080 and 1240x1080 at a size where 160 dpi - the
  // Android "medium" density - keeps Xenia's logical and physical sizes equal. The
  // game renders at physical resolution; nothing here should be scaled by density.
  uint32_t GetMediumDpi() const override { return 160; }

  // Called from the app's SurfaceHolder callbacks, main thread only. Acquires its own
  // reference on `window`; pass nullptr when the surface goes away.
  void SetNativeWindow(ANativeWindow* window);
  void SetSize(uint32_t width, uint32_t height);

  // Paint now rather than on the next display frame. For the cases where there is no
  // point waiting: a surface that has just appeared, or a resize. OnPaint is protected
  // in the base class, hence this.
  void PaintNow(bool force_paint) { OnPaint(force_paint); }

  ANativeWindow* native_window() const { return native_window_; }

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;
  std::unique_ptr<xe::ui::Surface> CreateSurfaceImpl(
      xe::ui::Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  // Paints on the next display frame rather than immediately. A paint request can
  // arrive several times between two frames; the display will only show one.
  void PostFrameCallback();
  static void OnFrame(int64_t frame_time_nanos, void* data);

  AppContext& context_;
  ANativeWindow* native_window_ = nullptr;
  uint32_t width_;
  uint32_t height_;
  // Choreographer instances are per-thread; this one belongs to the main thread.
  AChoreographer* choreographer_ = nullptr;
  bool frame_callback_pending_ = false;
};

}  // namespace warden

#endif  // WARDEN_WINDOW_H_
