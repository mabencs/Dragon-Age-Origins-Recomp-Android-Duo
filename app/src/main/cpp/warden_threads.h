// Guest threads and the dispatcher imports. See warden_threads.cc.
#ifndef WARDEN_THREADS_H_
#define WARDEN_THREADS_H_

#include <string>
#include <string_view>

namespace warden {

// Records the name the title gave the calling guest thread (through the MSVC
// thread-naming exception), for the log and the host thread's own name.
void NameCurrentGuestThread(std::string_view name);

// The calling guest thread's name - the title's, once it has named itself - or "boot"
// for the title's main thread. For logs: Xenia's logger writes from its own thread, so
// logcat's tid never says who called.
std::string CurrentGuestThreadName();

// The signals and blocking waits since the last call, one line each, for the report.
void RecentSyncReport(std::string& out);

}  // namespace warden

#endif  // WARDEN_THREADS_H_
