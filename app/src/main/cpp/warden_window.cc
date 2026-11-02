#include "warden_window.h"

#include "warden_frame_times.h"
#include "xenia/base/logging.h"
#include "xenia/ui/surface_android.h"

namespace warden {

Window::Window(AppContext& app_context, uint32_t width, uint32_t height)
    : xe::ui::Window(app_context, "Dragon Age Origins Recomp Android Duo", width, height),
      context_(app_context),
      width_(width),
      height_(height) {}

Window::~Window() {
  // Must be first: it moves the window to the deleting phase and disconnects the
  // presenter from the surface, so nothing is left pointing at a half-destroyed
  // window or at an ANativeWindow this destructor is about to release.
  EnterDestructor();
  if (native_window_) {
    ANativeWindow_release(native_window_);
    native_window_ = nullptr;
  }
}

void Window::SetNativeWindow(ANativeWindow* window) {
  if (native_window_ == window) {
    return;
  }
  if (native_window_) {
    // Tell the window layer the surface is gone *before* dropping the reference, so
    // the presenter has already let go of it by the time it is released.
    OnSurfaceChanged(false);
    ANativeWindow_release(native_window_);
    native_window_ = nullptr;
  }
  native_window_ = window;
  if (native_window_) {
    ANativeWindow_acquire(native_window_);
    OnSurfaceChanged(true);
  }
}

void Window::SetSize(uint32_t width, uint32_t height) {
  if (width_ == width && height_ == height) {
    return;
  }
  width_ = width;
  height_ = height;
  OnDesiredLogicalSizeUpdate(SizeToLogical(width), SizeToLogical(height));
  xe::ui::Window::WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(width, height, WindowResizeAction::kManual,
                     destruction_receiver);
}

bool Window::OpenImpl() {
  // Nothing to open. On every other platform OpenImpl creates an OS window; here the
  // window already exists - Android made it for the SurfaceView before this class was
  // constructed. All that is owed is the initial size, which the base class needs
  // before it will consider the window usable.
  xe::ui::Window::WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(width_, height_, WindowResizeAction::kManual,
                     destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return false;
  }
  XELOGI("window opened at {}x{}", width_, height_);
  // 6.7: the display's refresh period, for the frame-time histogram (warden_frame_times)
  // - registered here because the guest output thread presents by itself (paint mode
  // kGuestOutputThreadImmediately), so frame callbacks may never be posted.
  if (!choreographer_) {
    choreographer_ = AChoreographer_getInstance();
  }
  if (choreographer_) {
    AChoreographer_registerRefreshRateCallback(
        choreographer_,
        [](int64_t vsync_period_nanos, void*) { SetDisplayRefreshPeriod(vsync_period_nanos); },
        nullptr);
  } else {
    XELOGW("window: no Choreographer on this thread; frame times without refreshes");
  }
  return true;
}

void Window::RequestCloseImpl() {
  // Closing is Java's decision, as in Xenia's own AndroidWindow: finishing the
  // activity here would quit the whole app, which is not what closing a window means
  // on other platforms. Run the close notifications and leave the activity alone.
  xe::ui::Window::WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
  OnAfterClose();
}

std::unique_ptr<xe::ui::Surface> Window::CreateSurfaceImpl(
    xe::ui::Surface::TypeFlags allowed_types) {
  if (!(allowed_types & xe::ui::Surface::kTypeFlag_AndroidNativeWindow)) {
    return nullptr;
  }
  if (!native_window_) {
    return nullptr;
  }
  return std::make_unique<xe::ui::AndroidNativeWindowSurface>(native_window_);
}

// How far a paint request gets, for the GPU status line: requested by the presenter
// (and how many of those crossed from another thread, through the app context's pipe -
// the path that hung the main thread before BUG-056), posted to the Choreographer,
// painted in its frame callback.
std::atomic<uint32_t> g_paint_requests{0};
std::atomic<uint32_t> g_deferred_requests{0};
std::atomic<uint32_t> g_frame_callbacks_posted{0};
std::atomic<uint32_t> g_deferred_requests_dropped{0};

void Window::RequestPaintImpl() {
  g_paint_requests.fetch_add(1, std::memory_order_relaxed);
  if (context_.IsInUIThread()) {
    PostFrameCallback();
    return;
  }
  // The guest output thread asks for paints too. Choreographer instances belong to
  // the thread that obtained them, so the request has to cross to the main thread
  // first - which is exactly what the app context's pipe is for.
  g_deferred_requests.fetch_add(1, std::memory_order_relaxed);
  if (!context_.CallInUIThreadDeferred([this]() { PostFrameCallback(); })) {
    g_deferred_requests_dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

void Window::PostFrameCallback() {
  if (frame_callback_pending_) {
    // Several requests between two display frames are one paint. Coalescing them is
    // the point of going through the Choreographer rather than painting inline.
    return;
  }
  g_frame_callbacks_posted.fetch_add(1, std::memory_order_relaxed);
  if (!choreographer_) {
    choreographer_ = AChoreographer_getInstance();
    if (!choreographer_) {
      XELOGE("no Choreographer on the UI thread; painting immediately instead");
      OnPaint(false);
      return;
    }
  }
  frame_callback_pending_ = true;
  AChoreographer_postFrameCallback64(choreographer_, OnFrame, this);
}

// Paints through the frame callback, for the GPU status line: the presenter asks for
// one per guest frame it refreshes, so this says whether frames reach the panel.
std::atomic<uint32_t> g_frame_paints{0};

void Window::OnFrame(int64_t frame_time_nanos, void* data) {
  (void)frame_time_nanos;
  Window* window = static_cast<Window*>(data);
  window->frame_callback_pending_ = false;
  g_frame_paints.fetch_add(1, std::memory_order_relaxed);
  window->OnPaint(false);
}

}  // namespace warden

extern "C" uint32_t warden_frame_paints() {
  return warden::g_frame_paints.load(std::memory_order_relaxed);
}
extern "C" uint32_t warden_deferred_paint_requests_dropped() {
  return warden::g_deferred_requests_dropped.load(std::memory_order_relaxed);
}
extern "C" uint32_t warden_paint_requests() {
  return warden::g_paint_requests.load(std::memory_order_relaxed);
}
extern "C" uint32_t warden_deferred_paint_requests() {
  return warden::g_deferred_requests.load(std::memory_order_relaxed);
}
extern "C" uint32_t warden_frame_callbacks_posted() {
  return warden::g_frame_callbacks_posted.load(std::memory_order_relaxed);
}
