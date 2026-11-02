// The app's end (PLAN 4.9): the title quitting, the player quitting, and the host GPU
// lost. Each is recorded here and acted on by Java, which polls (native never calls into
// Java): the activity finishes and the process ends, so the next launch starts a fresh
// runtime - the runtime cannot be started twice in one process, and its guest threads
// cannot be stopped from outside.
#ifndef WARDEN_LIFECYCLE_H_
#define WARDEN_LIFECYCLE_H_

#include <string>

namespace warden {

// The app should end: the title asked to (`error` false) or something it cannot run on
// without happened (`error` true, `why` shown to the player). The first request wins.
// Returns at once.
void RequestExit(const std::string& why, bool error);

// "" while the app should run; "quit:<why>" or "error:<what to tell the player>".
std::string ExitRequest();

// Blocks the calling guest thread for good: what the title's own ending calls do on the
// console, where they never return.
[[noreturn]] void ParkForever();

}  // namespace warden

// graphics_system.cc's host GPU loss (a recorded edit, in place of xe::FatalError, which
// aborts the process on Android with no word to the player).
extern "C" void warden_host_gpu_lost(bool is_responsible);

#endif  // WARDEN_LIFECYCLE_H_
