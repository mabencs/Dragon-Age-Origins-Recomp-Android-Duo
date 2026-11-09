#include "warden_runtime.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "warden_affinity.h"
#include "warden_audio.h"
#include "warden_game_library.h"
#include "warden_camera_fov.h"
#include "warden_debug_mode.h"
#include "warden_motion_blur.h"

namespace warden {
// PLAN 10.14: warden_game.cc - a cheat at the next frame of play.
void RequestCheat(const std::string& name, const std::string& value);
}  // namespace warden
#include "warden_autosave.h"
#include "warden_extract.h"
#include "warden_frame_pacing.h"
#include "warden_frame_rate.h"
#include "warden_fs.h"
#include "warden_glyphs.h"
#include "warden_gpu.h"
#include "warden_guest_info.h"
#include "warden_imports.h"
#include "warden_options.h"
#include "warden_suspend.h"
#include "warden_thermal.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

// warden_frame_generation.cc: its setting read again as the game boots.
extern "C" void warden_frame_generation_reread();

namespace warden {

namespace {

// The owner (2026-11-02): the log says plainly when mods are loaded - a report from a
// modded game is read differently. A mod's name from its mod.toml (`name = "..."`), else
// its folder's.
std::string ModName(const std::string& folder) {
  std::string name = std::filesystem::path(folder).filename().string();
  if (FILE* file = fopen((folder + "/mod.toml").c_str(), "r")) {
    char line[512];
    while (fgets(line, sizeof(line), file)) {
      const std::string text(line);
      const size_t eq = text.find('=');
      if (eq == std::string::npos || text.compare(0, 4, "name") != 0) {
        continue;
      }
      const size_t open = text.find('"', eq);
      const size_t close = open == std::string::npos ? open : text.rfind('"');
      if (open != std::string::npos && close > open) {
        name = text.substr(open + 1, close - open - 1);
      }
      break;
    }
    fclose(file);
  }
  return name;
}

// Every mod loaded, and every add-in installed from a mod (PLAN 10.19: content/00000002/
// mod-<id>, off when files/dlc-off names it), as the game starts.
void LogMods(const std::string& data_dir, const std::vector<std::string>& mods) {
  std::vector<std::pair<std::string, bool>> addins;  // "title (package)", on
  std::vector<std::string> off;
  if (FILE* file = fopen((data_dir + "/dlc-off").c_str(), "r")) {
    char word[128];
    while (fscanf(file, "%127s", word) == 1) {
      off.push_back(word);
    }
    fclose(file);
  }
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::directory_iterator(data_dir + "/content/00000002", ec)) {
    const std::string package = entry.path().filename().string();
    if (!entry.is_directory(ec) || package.compare(0, 4, "mod-") != 0) {
      continue;
    }
    std::string title;
    if (FILE* file = fopen((entry.path().string() + ".name").c_str(), "rb")) {
      unsigned char name[256] = {};
      const size_t read = fread(name, 1, sizeof(name), file);
      fclose(file);
      for (size_t i = 0; i + 1 < read && (name[i] || name[i + 1]); i += 2) {
        title += name[i] ? '?' : char(name[i + 1]);  // UTF-16 big-endian, ASCII kept
      }
    }
    addins.emplace_back((title.empty() ? package : title) + " (" + package + ")",
                        std::find(off.begin(), off.end(), package) == off.end());
  }
  // Both answers on one marker, at the same level, so a log is searched for it alone and
  // says either way (the owner, 2026-11-02: a report from a modded game may not be looked
  // into - README).
  if (mods.empty() && addins.empty()) {
    XELOGW("MODS LOADED: none - the game as on the disc");
    return;
  }
  const size_t addins_on = size_t(std::count_if(
      addins.begin(), addins.end(), [](const auto& addin) { return addin.second; }));
  XELOGW("MODS LOADED: yes - {} mod{} loaded (their files laid over the game's), {} add-in{} "
         "from mods on, {} off",
         mods.size(), mods.size() == 1 ? "" : "s", addins_on, addins_on == 1 ? "" : "s",
         addins.size() - addins_on);
  for (size_t i = 0; i < mods.size(); ++i) {
    XELOGW("MODS LOADED: {}. {}{} - {}", i + 1, ModName(mods[i]),
           i == 0 && mods.size() > 1 ? " (wins over those below)" : "", mods[i]);
  }
  for (const auto& [title, on] : addins) {
    XELOGW("MODS LOADED: add-in {} - {}", title,
           on ? "on" : "off (its mod turned off: not loaded)");
  }
}

// Small formatting helper: the report is read off the handheld's own screen, so it
// wants fixed columns rather than prose.
void Linef(std::string& out, const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  out += buffer;
  out += '\n';
}

const char* YesNo(bool value) { return value ? "yes" : "no"; }

// The report is also the capture artifact (tools/appbringup/run.sh greps it out of
// logcat), so it goes out a line at a time under a recognisable prefix. Xenia's logger
// prepends its own decoration, which is why the prefix is needed to find the lines
// again rather than relying on position.
void LogReport(const std::string& text) {
  std::string::size_type begin = 0;
  while (begin < text.size()) {
    std::string::size_type end = text.find('\n', begin);
    if (end == std::string::npos) {
      end = text.size();
    }
    XELOGI("[report] {}", std::string_view(text).substr(begin, end - begin));
    begin = end + 1;
  }
}

const char* PresentModeName(VkPresentModeKHR mode) {
  switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
      return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:
      return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:
      return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
      return "FIFO_RELAXED";
    default:
      return "other";
  }
}

// Returned for enum values this probe does not name. Static buffers rather than
// std::string so the callers can stay printf-style; one per kind, since a single
// line never prints two unknowns of the same kind.
const char* UnknownEnum(char* buffer, size_t size, const char* kind, int value) {
  std::snprintf(buffer, size, "%s(%d)", kind, value);
  return buffer;
}

thread_local char t_unknown_format[32];
thread_local char t_unknown_colorspace[32];

const char* FormatName(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
      return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
      return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
      return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB:
      return "B8G8R8A8_SRGB";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      return "A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
      return "A2R10G10B10_UNORM_PACK32";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
      return "R5G6B5_UNORM_PACK16";
    // Adreno reports R8_UNORM + PASS_THROUGH among the surface formats. Named only
    // so the list has no holes - a single-channel pass-through surface is no use for
    // presenting a game.
    case VK_FORMAT_R8_UNORM:
      return "R8_UNORM";
    default:
      return UnknownEnum(t_unknown_format, sizeof(t_unknown_format), "VkFormat",
                         int(format));
  }
}

const char* ColorSpaceName(VkColorSpaceKHR space) {
  switch (space) {
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
      return "SRGB_NONLINEAR";
    case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
      return "DISPLAY_P3_NONLINEAR";
    case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
      return "EXTENDED_SRGB_LINEAR";
    case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
      return "DISPLAY_P3_LINEAR";
    case VK_COLOR_SPACE_BT709_LINEAR_EXT:
      return "BT709_LINEAR";
    case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
      return "BT709_NONLINEAR";
    case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
      return "BT2020_LINEAR";
    case VK_COLOR_SPACE_HDR10_ST2084_EXT:
      return "HDR10_ST2084";
    case VK_COLOR_SPACE_HDR10_HLG_EXT:
      return "HDR10_HLG";
    case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:
      return "EXTENDED_SRGB_NONLINEAR";
    case VK_COLOR_SPACE_PASS_THROUGH_EXT:
      return "PASS_THROUGH";
    default:
      return UnknownEnum(t_unknown_colorspace, sizeof(t_unknown_colorspace),
                         "VkColorSpace", int(space));
  }
}

}  // namespace

Runtime& Runtime::Get() {
  static Runtime instance;
  return instance;
}

bool Runtime::Startup(const std::string& data_dir) {
  if (started_) {
    return provider_ != nullptr;
  }
  started_ = true;
  data_dir_ = data_dir;
  // The marker and settings files are read from here by code that has no runtime object
  // in hand - frame generation's, as the first swapchain is made, before the title loads.
  setenv("WARDEN_FILES_DIR", data_dir_.c_str(), 1);

  // PLAN 11.1: Debugging Mode's log file (and the last run's kept), before logging starts.
  debug_mode::Prepare(data_dir_);

  // Routes XELOG* into logcat - logging.cc has an Android sink built on
  // __android_log_write. Without this, everything the runtime has to say about a
  // failure is silently dropped.
  xe::InitializeLogging("warden");
  XELOGI("Dragon Age Origins Recomp Android Duo: native runtime starting, data dir {}", data_dir);
  debug_mode::LogHeader(data_dir_);

  // Guest memory first. It reserves 4 GiB of address space and installs a fault
  // handler, and if either fails there is no point creating a Vulkan device.
  memory_ = std::make_unique<xe::Memory>();
  device_lines_ = CheckMemory(*memory_);
  device_lines_ += "\n";

  // with_presentation is true, unlike the Phase 3.2 command-line probe: there is a
  // real window now, so the instance needs VK_KHR_surface, VK_KHR_android_surface and
  // VK_EXT_swapchain_colorspace enabled.
  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, true);
  if (!provider_) {
    XELOGE("VulkanProvider::Create failed - this device cannot run Xenos emulation");
    device_lines_ +=
        "VulkanProvider::Create(gpu_emulation, presentation) failed.\n"
        "The reason is in logcat under the tag 'warden'.\n";
    return false;
  }
  if (debug_mode::On()) {
    // PLAN 11.1: the GPU and its driver, for the report's header.
    const auto& properties = provider_->vulkan_device()->properties();
    XELOGI("Debugging Mode: GPU {}, Vulkan {}.{}.{}, driver {:08X} (vendor {:04X}, device "
           "{:08X})",
           properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
           VK_API_VERSION_MINOR(properties.apiVersion),
           VK_API_VERSION_PATCH(properties.apiVersion), properties.driverVersion,
           properties.vendorID, properties.deviceID);
  }

  // Must be constructed on this thread: the base class takes the constructing thread
  // as the UI thread, and the looper comes from it.
  app_context_ = std::make_unique<AppContext>();
  if (!app_context_->valid()) {
    XELOGE("app context could not attach to the main thread looper");
  }

  presenter_ = provider_->CreatePresenter();
  if (!presenter_) {
    XELOGE("CreatePresenter failed");
    device_lines_ += "CreatePresenter failed.\n";
    return false;
  }

  const xe::ui::vulkan::VulkanInstance* instance = provider_->vulkan_instance();
  const xe::ui::vulkan::VulkanDevice* device = provider_->vulkan_device();
  const auto& props = device->properties();

  Linef(device_lines_, "DEVICE");
  Linef(device_lines_, "  %-34s %s", "name", props.deviceName);
  Linef(device_lines_, "  %-34s %u.%u.%u", "api version",
        VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
        VK_VERSION_PATCH(props.apiVersion));
  Linef(device_lines_, "  %-34s 0x%08X", "driver version", props.driverVersion);
  Linef(device_lines_, "  %-34s %u", "graphics+compute queue family",
        device->queue_family_graphics_compute());
  Linef(device_lines_, "PRESENTATION SUPPORT");
  Linef(device_lines_, "  %-34s %s", "VK_KHR_surface",
        YesNo(instance->extensions().ext_KHR_surface));
  Linef(device_lines_, "  %-34s %s", "VK_KHR_android_surface",
        YesNo(instance->extensions().ext_KHR_android_surface));
  Linef(device_lines_, "  %-34s %s", "VK_KHR_swapchain",
        YesNo(device->extensions().ext_KHR_swapchain));
  // Reported because its absence is invisible but total: without it the spec allows
  // surface format queries to return only VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, so a
  // Display-P3 panel looks exactly like an sRGB one. Enabled by a graft edit - see
  // tools/assemble_runtime.py, "request VK_EXT_swapchain_colorspace".
  Linef(device_lines_, "  %-34s %s", "VK_EXT_swapchain_colorspace",
        YesNo(instance->extensions().ext_EXT_swapchain_colorspace));
  Linef(device_lines_, "  %-34s %s", "presenter created", YesNo(true));

  // Topology and whether pinning is permitted at all on this device. Measured now
  // rather than in Phase 5, because a vendor that silently refuses
  // sched_setaffinity would otherwise show up only as an unexplained slow game.
  device_lines_ += "\n";
  device_lines_ += Affinity::Get().Report();

  XELOGI("Vulkan device ready: {}", props.deviceName);
  return true;
}

void Runtime::Shutdown() {
  if (!started_) {
    return;
  }
  // Stop the extraction and wait for it. It holds a file descriptor and is writing
  // into a directory; letting it run on past teardown would leave a .part file behind
  // and a thread writing into a process that is going away.
  Extractor::Get().Cancel();
  Extractor::Get().Join();

  // Order matters and is the reverse of construction: the window holds the surface
  // the presenter paints to, so the presenter is detached before the window goes, and
  // the window before the provider that owns the device behind it.
  if (window_) {
    window_->SetPresenter(nullptr);
    window_.reset();
  }
  // The graphics system owns the presenter the window was showing once the title
  // booted, and its own Vulkan device; its threads are guest threads, so before the
  // memory goes.
  gpu::Shutdown();
  presenter_.reset();
  app_context_.reset();
  provider_.reset();
  // Last: it owns the address space everything above was pointing into.
  memory_.reset();
  XELOGI("native runtime stopped after {} painted frames", frames_painted_);
  xe::ShutdownLogging();
  started_ = false;
}

void Runtime::SurfaceCreated(ANativeWindow* window) {
  if (!provider_ || !presenter_ || !window) {
    return;
  }
  gpu::SetWindow(window);  // PLAN 9.2: for tagging the swapchain's buffers HDR10
  surface_width_ = uint32_t(ANativeWindow_getWidth(window));
  surface_height_ = uint32_t(ANativeWindow_getHeight(window));

  // Measure before the presenter owns the window, not after.
  MeasureSurface(window);

  if (!window_) {
    window_ = std::make_unique<Window>(*app_context_, surface_width_, surface_height_);
    window_->SetNativeWindow(window);
    if (!window_->Open()) {
      XELOGE("window failed to open");
      window_.reset();
      return;
    }
    // Creates the Xenia Surface and connects the paint path - see
    // Window::SetPresenter in window.cc. The graphics system's presenter once the
    // title has booted (the surface can come back after a pause), the bring-up one
    // before.
    window_->SetPresenter(gpu::Presenter() ? gpu::Presenter() : presenter_.get());
  } else {
    window_->SetNativeWindow(window);
  }

  // Force the first paint rather than waiting for a frame callback: there is no guest
  // output yet, so nothing else will ask for one.
  window_->PaintNow(true);
  ++frames_painted_;

  uint32_t painted_width = 0;
  uint32_t painted_height = 0;
  (gpu::Presenter() ? gpu::Presenter() : presenter_.get())
      ->GetPaintSurfaceSize(painted_width, painted_height);
  const bool connected = painted_width != 0 && painted_height != 0;
  present_lines_.clear();
  Linef(present_lines_, "PRESENT");
  Linef(present_lines_, "  %-34s %s", "window open", YesNo(window_ != nullptr));
  // A non-zero paint-connection size means the presenter established a paint
  // connection and built a swapchain - it only records a size once it has one. This
  // is what distinguishes "a frame was presented" from "paint was called".
  Linef(present_lines_, "  %-34s %ux%u", "paint connection size", painted_width,
        painted_height);
  Linef(present_lines_, "  %-34s %s", "swapchain connected", YesNo(connected));
  Linef(present_lines_, "  %-34s %u", "frames painted", frames_painted_);
  LogReport(Report());
}

void Runtime::SurfaceChanged(uint32_t width, uint32_t height) {
  surface_width_ = width;
  surface_height_ = height;
  XELOGI("surface now {}x{}", width, height);
  if (window_) {
    window_->SetSize(width, height);
    window_->PaintNow(true);
    ++frames_painted_;
  }
}

void Runtime::SurfaceDestroyed() {
  if (window_) {
    // Disconnects the presenter from the surface and releases the window reference
    // before returning, which is what the JNI contract promises Java.
    window_->SetNativeWindow(nullptr);
  }
  gpu::SetWindow(nullptr);
  surface_width_ = 0;
  surface_height_ = 0;
}

// PLAN 11.1: a bug report's fresh save - the game's own autosave, whatever Autosave when
// put away says: 1 written, 0 none possible now (the game not exploring, or held), -1 not
// written within 6 s. Blocks: never on the UI thread.
int Runtime::SaveForReport() {
  const uint64_t written = AutosavesWritten();
  if (held_ || !RequestAutosave(true)) {
    XELOGI("Debugging Mode: no save for the report ({})",
           held_ ? "the game is held" : "the game allows none now");
    return 0;
  }
  const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (AutosavesWritten() == written && std::chrono::steady_clock::now() < give_up) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  const bool saved = AutosavesWritten() != written;
  XELOGI("Debugging Mode: the report's save {}", saved ? "written" : "not written within 6 s");
  return saved ? 1 : -1;
}

void Runtime::SetPaused(bool paused) {
  if (paused_ == paused) {
    return;
  }
  paused_ = paused;
  XELOGI("{}", paused ? "paused" : "resumed");
  // In the background the title's threads hold and its clocks stop, so the game comes
  // back where it was put away (warden_suspend.cc, BUG-057); the sound stops with it.
  // Neither call blocks the main thread.
  //
  // PLAN 9.13: with an autosave asked for as the game is put away (warden_autosave.h),
  // the hold waits on a thread of its own until the autosave is written - 1.5-2.5 s put
  // away (Home, the lid, the lock; BUG-100) - or 6 s have passed, inside the seconds
  // Android gives a put-away app before freezing it; coming back before then cancels the
  // hold (a new generation).
  std::lock_guard<std::mutex> lock(pause_mutex_);
  const uint64_t generation = ++pause_generation_;
  if (paused && held_) {
    // The options panel already holds the game (10.1): it cannot make an autosave.
    XELOGI("autosave on pause: the options panel holds the game; none");
  } else if (paused) {
    const uint64_t written = AutosavesWritten();
    if (RequestAutosave()) {
      std::thread([this, generation, written] {
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (AutosavesWritten() == written && std::chrono::steady_clock::now() < give_up) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const bool saved = AutosavesWritten() != written;
        std::lock_guard<std::mutex> hold(pause_mutex_);
        if (pause_generation_ != generation) {
          XELOGI("autosave on pause: back before the hold; {}", saved ? "saved" : "not saved");
          return;
        }
        XELOGI("autosave on pause: {}; the guest holds",
               saved ? "written" : "not written within 6 s");
        SuspendGuest();
      }).detach();
    } else {
      SuspendGuest();
    }
  } else if (!held_) {
    ResumeGuest();
  }
  SetAudioOutputPaused(paused || held_);
}

void Runtime::SetHeld(bool held) {
  std::lock_guard<std::mutex> lock(pause_mutex_);
  if (held_ == held) {
    return;
  }
  held_ = held;
  XELOGI("options panel {}", held ? "open: the game holds" : "closed");
  if (held) {
    SuspendGuest();
  } else if (!paused_) {
    ResumeGuest();
  }
  SetAudioOutputPaused(held || paused_);
}

bool Runtime::ApplyOption(const std::string& name, const std::string& value) {
  bool live = true;
  bool repaint = false;
  if (name == "frame-rate") {
    SetFrameRate(atoi(value.c_str()));
    FramePacingFollowFrameRate();
  } else if (name == "upscaling") {
    gpu::ApplyPaintSettings();
    repaint = true;
  } else if (name == "colour" || name == "hdr") {
    // The swapchain made again, in the format the files now name (warden_surface_format;
    // HDR10's tagged as it is made): the surface let go and taken back, as when the app
    // is put away and comes back. In HDR10, Vivid changes the final pass's primaries.
    gpu::HdrSettingsChanged();
    ANativeWindow* window = window_ ? window_->native_window() : nullptr;
    if (window && gpu::Presenter()) {
      ANativeWindow_acquire(window);
      window_->SetNativeWindow(nullptr);
      window_->SetNativeWindow(window);
      ANativeWindow_release(window);
      repaint = true;
    } else {
      live = false;
    }
  } else if (name == "hdr-white") {
    gpu::HdrSettingsChanged();  // the final pass made again with the new paper white
    repaint = true;
  } else if (name == "shadows") {
    SetDynamicShadows(value != "off");
  } else if (name == "post-processing") {
    SetPostProcessing(value != "off");
  } else if (name == "vignette") {
    SetVignette(value != "off");  // PLAN 9.21, from the next frame
  } else if (name == "headphones") {
    SetHeadphoneSurround(value == "surround");
  } else if (name == "volume-boost") {
    SetVolumeBoost(value);
  } else if (name == "autosave-on-pause") {
    SetAutosaveOnPause(value == "on");
  } else if (name == "thermal-guard") {
    thermal::SetGuard(value == "on");  // PLAN 10.2
  } else if (name == "heat-cpu-rest") {
    thermal::SetCpuRest(value == "on");  // BUG-117, from the next wait
  } else if (name == "heat-gpu-wait") {
    thermal::SetGpuWait(value == "on");  // BUG-117, from the next wait
  } else if (name.rfind("cheat-", 0) == 0) {
    RequestCheat(name, value);  // PLAN 10.14, at the next frame of play
  } else if (name == "dof") {
    SetDepthOfField(value == "on");  // PLAN 9.19, from the next upscaled frame
  } else if (name == "motion-blur") {
    // PLAN 9.18: from the next upscaled frame.
    SetMotionBlur(value == "high" ? 2 : value == "low" ? 1 : 0);
  } else if (name == "fov" || name == "fov-conversation") {
    SetCameraFov(name, atoi(value.c_str()));  // PLAN 9.17, from the next frame
  } else if (name == "power-save") {
    thermal::SetPowerSave(value == "on");  // Android's battery saver (GameActivity)
  } else {
    live = false;
  }
  XELOGI("options: {} is now {}{}", name, value, live ? "" : ", from the next start");
  // The panel holds the game, so no new frame comes: the last one painted again shows
  // the change around the panel.
  if (repaint && window_) {
    window_->PaintNow(true);
    ++frames_painted_;
  }
  return live;
}

void Runtime::LoadTitle(const std::string& game_data_dir) {
  if (boot_image_.loaded || !memory_ || game_data_dir.empty()) {
    return;
  }
  game_data_dir_ = game_data_dir;
  // D:\ is the extracted disc. Known now, and needed before any guest code runs.
  SetGuestFileRoot(game_data_dir);
  // Laid over it, the loose-file tree in app storage (PLAN 7.3), and in the tree the
  // button prompts' set (Phase 7) - now, before the title lists its patch folder at
  // its start.
  SetGuestOverrideRoot(data_dir_ + "/override");
  // PLAN 10.12: under it, the enabled mods - the launcher's Mods page writes their
  // folders to files/mods-enabled.txt, one a line, the one that wins first.
  {
    std::vector<std::string> mods;
    if (FILE* file = fopen((data_dir_ + "/mods-enabled.txt").c_str(), "r")) {
      char line[1024];
      while (fgets(line, sizeof(line), file)) {
        std::string path(line);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) {
          path.pop_back();
        }
        if (!path.empty()) {
          mods.push_back(path);
        }
      }
      fclose(file);
    }
    SetGuestModRoots(mods);
    LogMods(data_dir_, mods);
  }
  ApplyGlyphs();
  // Opt-in, as in DAORecomp: a file named skip_intro_logos in the app's files
  // directory hides the three logo movies, and the title skips them - the settings'
  // Intro logos (PLAN 9.11) writes "skip" or "show" into it; an empty one, the marker
  // the capture scripts make, skips too.
  {
    bool skip = false;
    if (FILE* file = fopen((data_dir_ + "/skip_intro_logos").c_str(), "r")) {
      char text[16] = {};
      skip = fscanf(file, "%15s", text) != 1 || strcmp(text, "show") != 0;
      fclose(file);
    }
    SetSkipIntroLogos(skip);
  }
  // The other marker files (inject-pad, the GPU's) are read from the same directory
  // by code that has no runtime object in hand.
  setenv("WARDEN_FILES_DIR", data_dir_.c_str(), 1);
  // Guest memory has to exist first: the image is written straight into it at its own
  // base address rather than staged and copied later.
  // Where a title update might be: beside the game data, and in the app's own
  // storage, which is where adb or a file manager can drop one.
  const std::vector<std::string> search_dirs = {game_data_dir,
                                                game_data_dir + "/..", data_dir_};
  // PLAN 12.2: the game's code, from the app's files - made on the device from the
  // player's own disc, or put there from the computer while developing. The image is
  // checked against its function table below, so it comes first; without it there is
  // nothing to start, and the report says why.
  {
    std::string error;
    if (!game::Load(game::LibraryPath(data_dir_), &error)) {
      XELOGE("boot: {}", error);
      boot_image_.detail = error;
      guest_boot_.detail = "not attempted - " + error;
      return;
    }
  }
  boot_image_ = LoadTitleImage(game_data_dir, search_dirs, *memory_);
  // Every guest thread's TLS block is cut from this; before any thread attaches.
  SetGuestTlsInfo(boot_image_.tls_slot_count, boot_image_.tls_raw_data_address,
                  boot_image_.tls_data_size, boot_image_.tls_raw_data_size);

  // Only if the image is the right generation. Building the indirect-call table from
  // a mismatched image would fill it with pointers to functions whose addresses mean
  // something else entirely.
  if (boot_image_.loaded && boot_image_.matches_build) {
    guest_boot_ = StartGuest(boot_image_.entry_point, *memory_);
  } else if (boot_image_.loaded) {
    guest_boot_.detail = "not attempted - the image does not match the linked build";
  }
}

bool Runtime::ApplyGlyphs() {
  if (game_data_dir_.empty()) {
    return false;
  }
  glyphs::Set set = glyphs::Set::kXbox;
  if (FILE* file = fopen((data_dir_ + "/glyphs").c_str(), "r")) {
    char text[32] = {};
    if (fgets(text, sizeof(text), file)) {
      if (auto parsed = glyphs::ParseSet(text)) {
        set = *parsed;
      } else {
        XELOGW("glyphs: \"{}\" is not a set - xbox, nintendo or playstation", text);
      }
    }
    fclose(file);
  }
  std::string error;
  return glyphs::Apply(game_data_dir_, data_dir_ + "/override",
                       data_dir_ + "/glyphs-applied.txt", set, &error);
}

bool Runtime::BootGuest() {
  if (!guest_boot_.entry_found || guest_boot_.called || !memory_) {
    return false;
  }
  // BUG-058: from here Xenia's timer queue and log writer answer at once again.
  GuestStarting();
  // The GPU first: Xenia's Vulkan graphics system over the guest memory, with its
  // command processor and vsync worker, which attach to the guest and so need the
  // heap the title's load set up. Its presenter replaces the bring-up one on the
  // window - the frames the title presents come out of it. Without it the title is not
  // started (BUG-059): it would only run blind, a black screen with its sound, and
  // until bring-up was over that was worth having; now the report on the screen, which
  // names the Vulkan failure, is the better answer.
  // Frame generation's setting as the game starts: the bring-up presenter's swapchain
  // read it at startup, and the pre-launch settings may have changed it since.
  warden_frame_generation_reread();
  if (!gpu::Start(*memory_, app_context_.get(), data_dir_)) {
    XELOGE("boot: the GPU did not start, so the title is not started");
    guest_boot_.detail = "not started: the GPU did not start";
    return false;
  }
  {
    xe::ui::Presenter* presenter = gpu::Presenter();
    if (window_ && presenter) {
      window_->SetPresenter(nullptr);
      window_->SetPresenter(presenter);
    }
    gpu::ApplyPaintSettings();
  }
  guest_boot_.called = true;
  guest_boot_.detail = "running";
  const uint32_t entry = guest_boot_.entry_point;
  xe::Memory* memory = memory_.get();
  // Detached: this thread does not come back. The title's entry point runs the game
  // and never returns - so there is nothing to join, and joining would hang shutdown
  // waiting for a thread that is gone.
  boot_thread_ = std::thread([entry, memory]() { CallEntryPoint(entry, *memory); });
  boot_thread_.detach();
  return true;
}

void Runtime::MeasureSurface(ANativeWindow* window) {
  const xe::ui::vulkan::VulkanInstance* instance = provider_->vulkan_instance();
  const auto& ifn = instance->functions();
  if (!instance->extensions().ext_KHR_android_surface) {
    surface_lines_ = "VK_KHR_android_surface is not enabled; cannot present.\n";
    return;
  }

  // The same call vulkan_presenter.cc makes for kTypeIndex_AndroidNativeWindow, on a
  // surface of our own - so that what a swapchain *could* do here is recorded
  // independently of what the presenter chooses to do with it.
  VkAndroidSurfaceCreateInfoKHR create_info;
  create_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  create_info.pNext = nullptr;
  create_info.flags = 0;
  create_info.window = window;
  VkSurfaceKHR query_surface = VK_NULL_HANDLE;
  if (ifn.vkCreateAndroidSurfaceKHR(instance->instance(), &create_info, nullptr,
                                    &query_surface) != VK_SUCCESS) {
    surface_lines_ = "vkCreateAndroidSurfaceKHR failed.\n";
    XELOGE("vkCreateAndroidSurfaceKHR failed for the query surface");
    return;
  }

  VkPhysicalDevice physical_device = provider_->vulkan_device()->physical_device();
  uint32_t family = provider_->vulkan_device()->queue_family_graphics_compute();

  surface_lines_.clear();
  Linef(surface_lines_, "SURFACE  %ux%u", surface_width_, surface_height_);

  VkBool32 supported = VK_FALSE;
  ifn.vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, family, query_surface,
                                           &supported);
  Linef(surface_lines_, "  %-34s %s", "graphics queue can present",
        YesNo(supported == VK_TRUE));

  VkSurfaceCapabilitiesKHR caps;
  if (ifn.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, query_surface,
                                                    &caps) == VK_SUCCESS) {
    Linef(surface_lines_, "  %-34s %ux%u", "current extent",
          caps.currentExtent.width, caps.currentExtent.height);
    Linef(surface_lines_, "  %-34s %u..%u", "swapchain images", caps.minImageCount,
          caps.maxImageCount ? caps.maxImageCount : caps.minImageCount);
  }

  uint32_t mode_count = 0;
  ifn.vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, query_surface,
                                                &mode_count, nullptr);
  std::vector<VkPresentModeKHR> modes(mode_count);
  if (mode_count) {
    ifn.vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, query_surface,
                                                  &mode_count, modes.data());
  }
  std::string mode_list;
  bool has_mailbox = false;
  for (VkPresentModeKHR mode : modes) {
    if (!mode_list.empty()) {
      mode_list += " ";
    }
    mode_list += PresentModeName(mode);
    has_mailbox = has_mailbox || mode == VK_PRESENT_MODE_MAILBOX_KHR;
  }
  Linef(surface_lines_, "  %-34s %s", "present modes",
        mode_list.empty() ? "none" : mode_list.c_str());
  // MAILBOX is what lets a frame replace an undisplayed one instead of queueing
  // behind it. On a 120 Hz panel showing a 60 fps game, its absence would push all
  // the pacing work onto Swappy (task 4.7).
  Linef(surface_lines_, "  %-34s %s", "MAILBOX available", YesNo(has_mailbox));

  uint32_t format_count = 0;
  ifn.vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, query_surface,
                                           &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  if (format_count) {
    ifn.vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, query_surface,
                                             &format_count, formats.data());
  }
  Linef(surface_lines_, "  swapchain formats (%u)", format_count);
  bool has_p3 = false;
  bool has_hdr10 = false;
  bool has_10bit = false;
  for (const VkSurfaceFormatKHR& format : formats) {
    Linef(surface_lines_, "    %-28s %s", FormatName(format.format),
          ColorSpaceName(format.colorSpace));
    has_p3 = has_p3 || format.colorSpace == VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
    has_hdr10 = has_hdr10 || format.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
    has_10bit = has_10bit ||
                format.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                format.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
  }
  // These three decide whether D8 - wide-gamut SDR by default, HDR10 opt-in - is
  // actually reachable on this panel, or only a preference.
  Linef(surface_lines_, "  %-34s %s", "DISPLAY_P3_NONLINEAR", YesNo(has_p3));
  Linef(surface_lines_, "  %-34s %s", "10-bit swapchain format", YesNo(has_10bit));
  Linef(surface_lines_, "  %-34s %s", "HDR10_ST2084", YesNo(has_hdr10));

  ifn.vkDestroySurfaceKHR(instance->instance(), query_surface, nullptr);

  XELOGI("surface measured: {} present modes, {} formats, P3 {}, HDR10 {}",
         mode_count, format_count, has_p3, has_hdr10);
}

std::string Runtime::Report() const {
  // The status block first, because "what can this do right now" is the question a
  // wall of measurements does not answer.
  Status status;
  status.presenting = window_ != nullptr && provider_ != nullptr;
  // Answered by the side that knows where the extraction lives: Java asks
  // extractIsComplete at startup with the path from getExternalFilesDir, and the
  // extractor remembers the answer.
  status.game_data_ready = Extractor::KnownComplete();
  status.imports_declared = kImportCount;
  // Counted, not asserted: the one import with a strong definition is
  // PPCQueryTimebase, which is not in the stub table at all - so the honest number
  // here is how many of the declared imports have been given real bodies, which is
  // still zero.
  status.imports_implemented = 0;
  status.image_loaded = boot_image_.loaded;
  status.image_matches_build = boot_image_.matches_build;
  std::string out = StatusReport(status);
  out += "\n";
  out += device_lines_;
  if (!surface_lines_.empty()) {
    out += "\n";
    out += surface_lines_;
  } else {
    out += "\nSURFACE  not connected yet\n";
  }
  if (!present_lines_.empty()) {
    out += "\n";
    out += present_lines_;
  }
  if (boot_image_.loaded || !boot_image_.detail.empty()) {
    out += "\n";
    out += boot_image_.Describe();
  }
  if (guest_boot_.table_entries || !guest_boot_.detail.empty()) {
    out += "\n";
    out += guest_boot_.Describe();
  }
  // Last, and read live rather than cached: which kernel imports the title has
  // reached is the one thing in this report that changes as the game runs.
  out += "\n";
  out += ImportReport();
  return out;
}

std::string Runtime::BootReport() const {
  std::string out;
  if (boot_image_.loaded || !boot_image_.detail.empty()) {
    out += boot_image_.Describe();
  }
  if (guest_boot_.table_entries || !guest_boot_.detail.empty()) {
    out += out.empty() ? "" : "\n";
    out += guest_boot_.Describe();
  }
  return out.empty() ? std::string("TITLE IMAGE\n  not loaded\n") : out;
}

}  // namespace warden
