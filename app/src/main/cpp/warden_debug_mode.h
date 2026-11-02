// PLAN 11.1: Debugging Mode - the run's whole log to a file, and the save it loaded kept
// (warden_debug_mode.cc). Options -> Debugging Mode, files/debug-mode "on", from the next
// start.
#ifndef WARDEN_DEBUG_MODE_H_
#define WARDEN_DEBUG_MODE_H_

#include <string>

namespace warden::debug_mode {

// Before logging starts: with Debugging Mode on, this run's log goes to
// files/debug/log-current.txt (the last run's kept as log-previous.txt) as well as logcat.
void Prepare(const std::string& files_dir);

// Whether Debugging Mode is on for this run: the logs' budgets are lifted then.
bool On();

// After logging starts: the run's header (files/debug/run-header.txt, written by the app:
// device, Android, app version and commit, options and markers) into the log.
void LogHeader(const std::string& files_dir);

// A save's savegame.das opened to be read - the save being loaded: its folder copied to
// files/debug/loaded-save/ (replacing the last), so a report holds the point to reproduce
// from whatever the game has saved since.
void NoteSaveRead(const std::string& host_path);

}  // namespace warden::debug_mode

#endif  // WARDEN_DEBUG_MODE_H_
