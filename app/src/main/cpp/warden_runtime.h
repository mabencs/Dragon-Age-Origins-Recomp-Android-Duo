// The native runtime object behind the JNI bridge.
//
// Phase 4.2. One instance, owned by the process, driven entirely from the Android main
// thread - which is "the UI thread" as far as Xenia's presenter is concerned. The
// emulation and GPU threads arrive in Phase 5; until then everything here is
// single-threaded on purpose, so that ordering bugs cannot hide behind a race.
#ifndef WARDEN_RUNTIME_H_
#define WARDEN_RUNTIME_H_

#include <android/native_window.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <string>

#include "warden_app_context.h"
#include "warden_boot.h"
#include "warden_guest.h"
#include "warden_boot.h"
#include "warden_guest.h"
#include "warden_memory_check.h"
#include "warden_window.h"
#include "xenia/memory.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace warden {

class Runtime {
 public:
  static Runtime& Get();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // Brings up logging, a Vulkan device and a presenter. Returns false if this device
  // cannot run Xenos emulation, in which case the report says what was missing.
  bool Startup(const std::string& data_dir);
  void Shutdown();

  // Takes a reference on `window`; the caller's reference is its own to release.
  void SurfaceCreated(ANativeWindow* window);
  void SurfaceChanged(uint32_t width, uint32_t height);
  // Releases everything referring to the window before returning. Android frees the
  // window's buffers the moment the Java callback returns, so this cannot be deferred
  // to another thread.
  void SurfaceDestroyed();

  void SetPaused(bool paused);

  // PLAN 10.1: the options panel holds the game while it is open - its threads and its
  // sound, as a pause does, without the autosave a pause can ask for.
  void SetHeld(bool held);
  // PLAN 11.1: a bug report's fresh save (warden_runtime.cc).
  int SaveForReport();

  // PLAN 10.1: an option changed in the options panel, its file already written. Taken
  // at once where the game can take it while running - the frame rate, the upscaling,
  // the colour and HDR, the engine's switches, the headphone mix, the autosave - and true then;
  // false for the options the game takes only as it starts.
  bool ApplyOption(const std::string& name, const std::string& value);

  // Loads the title image. Called once the game data directory is known; safe to call
  // again, which does nothing.
  void LoadTitle(const std::string& game_data_dir);

  // The button prompts' set (PLAN Phase 7), as files/glyphs names it - "nintendo",
  // "playstation", or the game's own Xbox ones - written into the override tree at the
  // title's load (warden_glyphs.h); the settings change the file, for the next start.
  // False before the title is loaded, or if the set could not be written.
  bool ApplyGlyphs();

  // Starts guest code - the game - on a thread of its own. Returns false if it is not
  // ready (BootReport says why), or has already been started.
  bool BootGuest();

  // Human-readable, for the on-screen overlay and for logcat.
  std::string Report() const;

  // The report's title image and guest boot sections: why the game did not start.
  std::string BootReport() const;

 private:
  Runtime() = default;

  // Measures what a swapchain on this window could do, before the presenter takes it
  // over. Creates and destroys its own VkSurfaceKHR: the presenter owns the one it
  // presents through, and two owners of one surface is how a teardown goes wrong.
  void MeasureSurface(ANativeWindow* window);

  bool started_ = false;
  bool paused_ = false;
  // The options panel is open (10.1): the guest holds whatever the activity does.
  // Guarded by pause_mutex_.
  bool held_ = false;
  // PLAN 9.13: the hold waits for an autosave on its own thread; a pause or a resume
  // since it began (a new generation) cancels it. Guarded by pause_mutex_.
  std::mutex pause_mutex_;
  uint64_t pause_generation_ = 0;
  std::string data_dir_;

  // Guest memory. Created before the provider, because the GPU takes a reference to
  // it and the 4 GiB reservation is the thing most likely to fail outright - better
  // to find that out before a Vulkan device exists to clean up.
  std::unique_ptr<xe::Memory> memory_;
  std::unique_ptr<xe::ui::vulkan::VulkanProvider> provider_;
  std::unique_ptr<AppContext> app_context_;
  std::unique_ptr<xe::ui::Presenter> presenter_;
  std::unique_ptr<Window> window_;

  uint32_t surface_width_ = 0;
  uint32_t surface_height_ = 0;
  uint32_t frames_painted_ = 0;

  // The title image, once the game data directory is known. Java supplies that path
  // when it asks whether extraction is complete, which is the only moment either side
  // knows it.
  BootImage boot_image_;
  GuestBoot guest_boot_;
  std::thread boot_thread_;
  std::string game_data_dir_;

  std::string device_lines_;
  std::string surface_lines_;
  std::string present_lines_;
};

}  // namespace warden

#endif  // WARDEN_RUNTIME_H_
