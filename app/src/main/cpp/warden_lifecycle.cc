// The app's end (PLAN 4.9). See warden_lifecycle.h.
#include "warden_lifecycle.h"

#include <unistd.h>

#include <chrono>
#include <mutex>
#include <thread>

#include "xenia/base/logging.h"

namespace warden {

namespace {

std::mutex g_mutex;
std::string g_request;  // "" or "quit:..." / "error:..."

}  // namespace

void RequestExit(const std::string& why, bool error) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_request.empty()) {
      return;
    }
    g_request = (error ? "error:" : "quit:") + why;
  }
  if (error) {
    XELOGE("lifecycle: the app ends - {}", why);
  } else {
    XELOGI("lifecycle: the app ends - {}", why);
  }
}

std::string ExitRequest() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_request;
}

void ParkForever() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace warden

extern "C" void warden_host_gpu_lost(bool is_responsible) {
  warden::RequestExit(
      is_responsible
          ? "The graphics device stopped working while the game was drawing. The game has "
            "to close; progress since the last save is lost."
          : "The graphics device was lost. The game has to close; progress since the last "
            "save is lost.",
      true);
  // The thread that found the loss - the GPU command processor's, or the one painting -
  // stops here: whatever it would do next cannot reach the GPU. Not the main thread, which
  // has to show the message (gettid() is the process id only there).
  if (gettid() != getpid()) {
    warden::ParkForever();
  }
}
