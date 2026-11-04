// PLAN 10.2: the Thor's temperatures and the GPU's thermal limit, read from sysfs by the
// game's own process, and the heat guard that acts on them.
//
// Android's thermal service has no HAL on the Thor ("HAL Ready: false" in dumpsys
// thermalservice, its status always 0), so PowerManager's thermal status and headroom say
// nothing; the kernel's thermal zones and the Adreno's KGSL nodes do, and the app may read
// them:
//   /sys/class/thermal/thermal_zone*/  type gpuss-0..7 (the GPU), cpu-1-0..10 (the big
//                                      cores), xo-therm (the board); temp in millidegrees
//   /sys/class/kgsl/kgsl-3d0/          gpuclk (Hz), thermal_pwrlevel (0: no thermal cap;
//                                      N: capped at gpu_available_frequencies' Nth)
// The zones are found once by their types; every read is a small file, every 5 s.
//
// Measured (run 475): 14 minutes at the King's Camp with Lossless 2x over FSR 3.1, the
// GPU at 680 MHz and thermal level 0 throughout - no throttling - the GPU settling at
// 82 C, the big cores at 94-95, the board at 61 (the fan in the Thor's mode 4).
//
// The heat guard - an option, off by default (the owner, 2026-10-31: nothing changes the
// frame rate on its own unless asked). On, it holds the rate on the screen a step lower
// (warden::SetFrameRateCap: 120 -> 60 -> 45 -> 30) when the kernel caps the GPU, the GPU
// reaches 75 C or the hottest big core 88 C, a step at most every 30 s, and lifts it a step
// at a time after a minute with no cap, the GPU at 62 C or less and the big cores at 72 or
// less. With Android's battery saver on, it holds 30. The player's own choice stays as it
// is; the guard's limit lies over it. files/thermal-guard-test holding two numbers ("75
// 74") sets the GPU's hot and cool temperatures instead - a test switch, to see the guard
// work at the temperatures a run reaches.
//
// BUG-117 (the owner, 2026-11-04): it stepped only at 90 C on the GPU, which a Thor with its
// fan running never reaches (the camp uncapped: the GPU 78 C, the big cores 84 on average and
// 94 at the hottest - run 721), so it never acted. A lower frame rate cools the Thor only
// with the game's waits resting the processor (the guard's other half, warden_threads.cc and
// warden_game.cc): at 30 the big cores 71 C without, 54 with (runs 718-720).

#include "warden_thermal.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <sstream>
#include <vector>

#include "warden_frame_pacing.h"
#include "warden_frame_rate.h"
#include "xenia/base/logging.h"

namespace warden::thermal {

namespace {

constexpr const char* kKgsl = "/sys/class/kgsl/kgsl-3d0/";

struct Zones {
  std::vector<std::string> gpu;
  std::vector<std::string> big;
  std::string board;
  std::vector<long> frequencies;  // MHz, the highest first (power level 0)
};

struct Reading {
  double gpu = -1, big = -1, board = -1;  // degrees; -1 unread
  int gpu_error = 0, big_error = 0, board_error = 0;
  long clock = -1, level = -1;  // MHz; the thermal power level
  int clock_error = 0, level_error = 0;
};

bool ReadLong(const std::string& path, long& value, int& error) {
  FILE* file = std::fopen(path.c_str(), "r");
  if (!file) {
    error = errno;
    return false;
  }
  const bool ok = std::fscanf(file, "%ld", &value) == 1;
  std::fclose(file);
  if (!ok) {
    error = EIO;
  }
  return ok;
}

std::string ReadText(const std::string& path) {
  std::string text;
  if (FILE* file = std::fopen(path.c_str(), "r")) {
    char buffer[256];
    const size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    text.assign(buffer, read);
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
      text.pop_back();
    }
  }
  return text;
}

const Zones& TheZones() {
  static const Zones zones = [] {
    Zones z;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/thermal", ec)) {
      const std::string dir = entry.path().string();
      if (dir.find("thermal_zone") == std::string::npos) {
        continue;
      }
      const std::string type = ReadText(dir + "/type");
      if (type.rfind("gpuss-", 0) == 0) {
        z.gpu.push_back(dir + "/temp");
      } else if (type.rfind("cpu-1-", 0) == 0) {
        z.big.push_back(dir + "/temp");
      } else if (type == "xo-therm") {
        z.board = dir + "/temp";
      }
    }
    std::istringstream frequencies(ReadText(std::string(kKgsl) + "gpu_available_frequencies"));
    long hz;
    while (frequencies >> hz) {
      z.frequencies.push_back(hz / 1000000);
    }
    XELOGI("thermal: {} GPU, {} big-core and {} board sensors, {} GPU clocks (the top {} MHz){}",
           z.gpu.size(), z.big.size(), z.board.empty() ? 0 : 1, z.frequencies.size(),
           z.frequencies.empty() ? 0 : z.frequencies.front(),
           ec ? " - the zones could not be listed: " + ec.message() : std::string());
    return z;
  }();
  return zones;
}

// The hottest of the sensors in degrees, or -1 with the error.
double Hottest(const std::vector<std::string>& paths, int& error) {
  long most = -1;
  for (const std::string& path : paths) {
    long value;
    if (ReadLong(path, value, error)) {
      most = std::max(most, value);
    }
  }
  return most < 0 ? -1.0 : most / 1000.0;
}

Reading Read() {
  const Zones& zones = TheZones();
  Reading r;
  r.gpu = Hottest(zones.gpu, r.gpu_error);
  r.big = Hottest(zones.big, r.big_error);
  std::vector<std::string> board_path;
  if (!zones.board.empty()) {
    board_path.push_back(zones.board);
  }
  r.board = Hottest(board_path, r.board_error);
  long hz;
  if (ReadLong(std::string(kKgsl) + "gpuclk", hz, r.clock_error)) {
    r.clock = hz / 1000000;
  }
  long level;
  if (ReadLong(std::string(kKgsl) + "thermal_pwrlevel", level, r.level_error)) {
    r.level = level;
  }
  return r;
}

std::string Degrees(const char* name, double value, int error) {
  char text[64];
  if (value >= 0) {
    std::snprintf(text, sizeof(text), "%s %.1f C", name, value);
  } else {
    std::snprintf(text, sizeof(text), "%s unread (%s)", name,
                  error ? std::strerror(error) : "no sensor");
  }
  return text;
}

std::string Summary(const Reading& r) {
  std::string line = "thermal: " + Degrees("GPU", r.gpu, r.gpu_error) + ", " +
                     Degrees("big cores", r.big, r.big_error) + ", " +
                     Degrees("board", r.board, r.board_error);
  char text[160];
  if (r.clock >= 0) {
    std::snprintf(text, sizeof(text), ", GPU clock %ld MHz", r.clock);
  } else {
    std::snprintf(text, sizeof(text), ", GPU clock unread (%s)", std::strerror(r.clock_error));
  }
  line += text;
  if (r.level >= 0) {
    const std::vector<long>& frequencies = TheZones().frequencies;
    const long cap = size_t(r.level) < frequencies.size() ? frequencies[size_t(r.level)] : 0;
    std::snprintf(text, sizeof(text), " (thermal level %ld, cap %ld MHz)", r.level, cap);
  } else {
    std::snprintf(text, sizeof(text), " (thermal level unread: %s)",
                  std::strerror(r.level_error));
  }
  line += text;
  return line;
}

// The guard.
constexpr int kTicksBetweenSteps = 6;    // 30 s at a tick every 5 s
constexpr int kTicksCoolBeforeLift = 12; // a minute

std::mutex g_guard_mutex;
int g_guard = -1;          // -1 until files/thermal-guard is read; then 0 or 1
bool g_power_save = false;
int g_heat_cap = 0;        // the guard's step for heat: 0, 60, 45 or 30
int g_applied = 0;         // the limit last given to the frame rate
int g_ticks_since_step = kTicksBetweenSteps;
int g_ticks_cool = 0;

struct Temperatures {
  double hot = 75.0;   // the hottest GPU sensor
  double cool = 62.0;
  double big_hot = 88.0;   // the hottest big core
  double big_cool = 72.0;
};

const Temperatures& TheTemperatures() {
  static const Temperatures temperatures = [] {
    Temperatures t;
    if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
      if (FILE* file = std::fopen((std::string(dir) + "/thermal-guard-test").c_str(), "r")) {
        double hot, cool;
        if (std::fscanf(file, "%lf %lf", &hot, &cool) == 2 && cool < hot) {
          t.hot = hot;
          t.cool = cool;
          XELOGW("heat guard: hot at {:.1f} C, cool at {:.1f} (files/thermal-guard-test)",
                 hot, cool);
        }
        std::fclose(file);
      }
    }
    return t;
  }();
  return temperatures;
}

// Under g_guard_mutex.
bool GuardOn() {
  if (g_guard < 0) {
    bool on = false;
    if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
      if (FILE* file = std::fopen((std::string(dir) + "/thermal-guard").c_str(), "r")) {
        char text[16] = {};
        on = std::fscanf(file, "%15s", text) == 1 && std::strcmp(text, "on") == 0;
        std::fclose(file);
      }
    }
    g_guard = on ? 1 : 0;
    XELOGI("heat guard: {}", on ? "on" : "off");
  }
  return g_guard == 1;
}

// The guard's steps; the chosen rate is the top one.
constexpr int kSteps[] = {30, 45, 60};

// The step below a rate, at least 30.
int StepBelow(int rate) {
  for (int i = int(std::size(kSteps)) - 1; i >= 0; --i) {
    if (kSteps[i] < rate) {
      return kSteps[i];
    }
  }
  return kSteps[0];
}

// The step above the guard's, or 0 when that is the chosen rate or above.
int StepAbove(int step, int chosen) {
  for (int s : kSteps) {
    if (s > step) {
      return s < chosen ? s : 0;
    }
  }
  return 0;
}

// Under g_guard_mutex: the limit as the guard and the battery saver have it now, given to
// the frame rate when it changes.
void Apply(const char* why) {
  int limit = 0;
  if (GuardOn()) {
    limit = g_heat_cap;
    if (g_power_save) {
      limit = 30;
    }
  }
  if (limit == g_applied) {
    return;
  }
  g_applied = limit;
  XELOGI("heat guard: {} - {}", why,
         limit ? fmt::format("the frame rate held to {} ({} chosen)", limit, ChosenFrameRate())
               : fmt::format("the frame rate back to {}", ChosenFrameRate()));
  SetFrameRateCap(limit);
  FramePacingFollowFrameRate();
}

void Guard(const Reading& r) {
  std::lock_guard<std::mutex> lock(g_guard_mutex);
  if (!GuardOn()) {
    return;
  }
  const Temperatures& t = TheTemperatures();
  ++g_ticks_since_step;
  const bool capped = r.level > 0;
  const bool hot = capped || r.gpu >= t.hot || r.big >= t.big_hot;
  const bool cool = r.level == 0 && r.gpu >= 0 && r.gpu <= t.cool && r.big <= t.big_cool;
  g_ticks_cool = cool ? g_ticks_cool + 1 : 0;
  const int chosen = ChosenFrameRate();
  const int showing = g_heat_cap && g_heat_cap < chosen ? g_heat_cap : chosen;
  char why[96];
  if (hot && showing > 30 && g_ticks_since_step >= kTicksBetweenSteps) {
    g_heat_cap = StepBelow(showing);
    g_ticks_since_step = 0;
    std::snprintf(why, sizeof(why), "hot (GPU %.1f C, big cores %.1f, thermal level %ld)",
                  r.gpu, r.big, r.level);
    Apply(why);
  } else if (g_heat_cap && g_ticks_cool >= kTicksCoolBeforeLift &&
             g_ticks_since_step >= kTicksBetweenSteps) {
    g_heat_cap = StepAbove(g_heat_cap, chosen);
    g_ticks_since_step = 0;
    g_ticks_cool = 0;
    std::snprintf(why, sizeof(why), "cool again (GPU %.1f C, big cores %.1f)", r.gpu, r.big);
    Apply(why);
  }
}

}  // namespace

std::string Tick() {
  const Reading reading = Read();
  Guard(reading);
  return Summary(reading);
}

namespace {
std::atomic<int> g_cpu_rest{-1};
std::atomic<int> g_gpu_wait{-1};

// On unless the file says "off" (the owner, 2026-11-04: both rests on by default - no frame
// rate lost, much cooler; BUG-117).
bool Switch(std::atomic<int>& value, const char* file) {
  int seen = value.load(std::memory_order_relaxed);
  if (seen < 0) {
    bool on = true;
    if (const char* dir = std::getenv("WARDEN_FILES_DIR")) {
      if (FILE* f = std::fopen((std::string(dir) + "/" + file).c_str(), "r")) {
        char text[16] = {};
        on = !(std::fscanf(f, "%15s", text) == 1 && std::strcmp(text, "off") == 0);
        std::fclose(f);
      }
    }
    seen = on ? 1 : 0;
    value.store(seen, std::memory_order_relaxed);
    XELOGI("heat guard: {} {}", file, on ? "on" : "off");
  }
  return seen == 1;
}
}  // namespace

bool CpuRest() { return Switch(g_cpu_rest, "heat-cpu-rest"); }
bool GpuWait() { return Switch(g_gpu_wait, "heat-gpu-wait"); }
void SetCpuRest(bool on) { g_cpu_rest.store(on ? 1 : 0, std::memory_order_relaxed); }
void SetGpuWait(bool on) { g_gpu_wait.store(on ? 1 : 0, std::memory_order_relaxed); }

void SetGuard(bool on) {
  std::lock_guard<std::mutex> lock(g_guard_mutex);
  g_guard = on ? 1 : 0;
  if (!on) {
    g_heat_cap = 0;
  }
  g_ticks_since_step = kTicksBetweenSteps;
  g_ticks_cool = 0;
  Apply(on ? "turned on" : "turned off");
}

void SetPowerSave(bool on) {
  std::lock_guard<std::mutex> lock(g_guard_mutex);
  g_power_save = on;
  Apply(on ? "battery saver on" : "battery saver off");
}

}  // namespace warden::thermal
