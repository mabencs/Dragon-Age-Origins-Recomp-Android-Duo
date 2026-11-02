// PLAN 11.1: Debugging Mode. An end user cannot reach logcat, so with the option on the
// run's log is also written to a file in the app's storage (Xenia's own FileLogSink,
// added in logging.cc when WARDEN_LOG_FILE is set - tools/assemble_runtime.py), with
// the budgets that cap the runtime's repetitive lines lifted (warden_game.cc's Budget,
// warden_fs.cc's ShouldLog; reads of a file are still sampled - the first few, powers of
// two, every 1024th - or a streamed file would fill the storage). The last run's log is
// kept beside it. And the save loaded is copied aside as it is read, so the report the
// options panel exports (GameActivity.exportReport) holds the point to reproduce from.

#include "warden_debug_mode.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "xenia/base/logging.h"

namespace warden::debug_mode {

namespace {

namespace fs = std::filesystem;

bool g_on = false;
std::string g_debug_dir;

std::mutex g_save_mutex;
std::string g_last_save;
std::chrono::steady_clock::time_point g_last_copy;

std::string Lower(std::string text) {
  for (char& c : text) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

}  // namespace

void Prepare(const std::string& files_dir) {
  bool on = false;
  if (FILE* file = std::fopen((files_dir + "/debug-mode").c_str(), "r")) {
    char text[16] = {};
    on = std::fscanf(file, "%15s", text) == 1 && std::strcmp(text, "on") == 0;
    std::fclose(file);
  }
  if (!on) {
    return;
  }
  g_on = true;
  g_debug_dir = files_dir + "/debug";
  std::error_code ec;
  fs::create_directories(g_debug_dir, ec);
  const fs::path current = fs::path(g_debug_dir) / "log-current.txt";
  if (fs::exists(current, ec)) {
    fs::rename(current, fs::path(g_debug_dir) / "log-previous.txt", ec);
  }
  setenv("WARDEN_LOG_FILE", current.c_str(), 1);
  setenv("WARDEN_DEBUG_MODE", "1", 1);
}

bool On() { return g_on; }

void LogHeader(const std::string& files_dir) {
  if (!g_on) {
    return;
  }
  XELOGI("Debugging Mode: this run's whole log, in {}/log-current.txt", g_debug_dir);
  std::ifstream header(files_dir + "/debug/run-header.txt");
  for (std::string line; std::getline(header, line);) {
    XELOGI("Debugging Mode: {}", line);
  }
}

void NoteSaveRead(const std::string& host_path) {
  if (!g_on) {
    return;
  }
  fs::path folder = fs::path(host_path).parent_path();
  if (Lower(folder.filename().string()) == "saves") {
    folder = folder.parent_path();
  }
  const std::string name = folder.filename().string();
  {
    // The game reads a save's file more than once as it loads it: copied once.
    std::lock_guard<std::mutex> lock(g_save_mutex);
    const auto now = std::chrono::steady_clock::now();
    if (name == g_last_save && now - g_last_copy < std::chrono::seconds(60)) {
      return;
    }
    g_last_save = name;
    g_last_copy = now;
  }
  const fs::path target_root = fs::path(g_debug_dir) / "loaded-save";
  std::error_code ec;
  fs::remove_all(target_root, ec);
  fs::create_directories(target_root, ec);
  fs::copy(folder, target_root / name, fs::copy_options::recursive, ec);
  const fs::path display_name = folder.parent_path() / (name + ".name");
  if (fs::exists(display_name)) {
    fs::copy_file(display_name, target_root / (name + ".name"), ec);
  }
  XELOGI("Debugging Mode: the save being loaded ({}) kept for the report{}", name,
         ec ? " - " + ec.message() : std::string());
}

}  // namespace warden::debug_mode
