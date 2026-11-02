// The host boundary the grafted runtime needs filled in, stubbed for the Phase 3.2
// device probe.
//
// DAORecomp implements these outside the Xenia subset it vendors - in its own
// runtime, which this project has not written yet. Linking the runtime enumerates
// them exactly, and that list is the contract Phase 4's Android shell has to satisfy:
//
//   Presenter::Dao*   DAORecomp's frame pacer (ui/dao_frame_pacer.cc, built on
//                     Windows waitable timers). Android's replacement is Swappy,
//                     task 4.7.
//   Window::*         Xenia's window layer, excluded because it needs ImGui and
//                     because Android presents through a SurfaceView instead.
//                     Task 4.2.
//
// Every one of these aborts rather than returning a plausible value. The probe only
// creates a Vulkan device and never presents, so none should be reached - and if one
// is, the probe's result would be meaningless, so failing loudly is the point. A
// quiet `return false` here would be the difference between "the device works" and
// "the device appears to work".

#include <cstdio>
#include <cstdlib>

#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"

namespace {

[[noreturn]] void NotImplemented(const char* what) {
  std::printf("\nFATAL: the device probe reached %s.\n", what);
  std::printf("It should not: the probe creates a Vulkan device and never presents.\n");
  std::printf("This is the Phase 4 host boundary - see tools/vkdevice/host_stubs.cc\n");
  std::fflush(stdout);
  std::abort();
}

}  // namespace

namespace xe {
namespace ui {

// ---- DAORecomp's frame pacer (Swappy replaces this on Android, task 4.7) ----

bool Presenter::DaoKeepAcquired(const GuestOutputProperties& acquired) {
  (void)acquired;
  NotImplemented("Presenter::DaoKeepAcquired");
}

void Presenter::DaoStopPacer() { NotImplemented("Presenter::DaoStopPacer"); }

bool Presenter::DaoQueueFramePaints(const GuestOutputProperties& properties) {
  (void)properties;
  NotImplemented("Presenter::DaoQueueFramePaints");
}

void Presenter::DaoBeginUIThreadPaint() {
  NotImplemented("Presenter::DaoBeginUIThreadPaint");
}

void Presenter::DaoEndUIThreadPaint() {
  NotImplemented("Presenter::DaoEndUIThreadPaint");
}

// ---- Xenia's window layer (Android uses a SurfaceView instead, task 4.2) ----

void Window::SetPresenter(Presenter* presenter) {
  (void)presenter;
  NotImplemented("Window::SetPresenter");
}

void Window::OnSurfaceChanged(bool new_surface_potentially_exists) {
  (void)new_surface_potentially_exists;
  NotImplemented("Window::OnSurfaceChanged");
}

void Window::OnPaint(bool force_paint) {
  (void)force_paint;
  NotImplemented("Window::OnPaint");
}

}  // namespace ui
}  // namespace xe
