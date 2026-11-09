// The guest held while the app is in the background. See warden_suspend.cc.
#ifndef WARDEN_SUSPEND_H_
#define WARDEN_SUSPEND_H_

#include <cstdint>

namespace warden {

// From the main thread, as the activity pauses and resumes. Neither blocks: the
// title's threads stop at their next suspension point, and go on when resumed.
void SuspendGuest();
void ResumeGuest();
bool GuestSuspended();

// BUG-058: the title about to start (Runtime::BootGuest). Until then, and while the
// guest is suspended, the host is idle: Xenia's timer queue and log writer, which
// otherwise spin-wait, nap instead (warden_host_idle, the edits in assemble_runtime.py).
void GuestStarting();

// BUG-119: from a title thread as it starts (RunGuestThread, the boot thread). A
// suspension that finds one of these still running a second in - spinning in the
// title's own code on a flag a held thread would set, so never reaching a suspension
// point - holds it by a signal instead, if the signal finds it in the title's code.
void RegisterTitleThread();

// A suspension point. Returns at once unless the guest is suspended; otherwise holds
// the calling thread until it resumes, and returns true. Only where the caller holds
// none of the runtime's own locks. `who` names a host thread for the log; the title's
// own threads go by their names.
bool GuestSuspendPoint(const char* who = nullptr);

// What the title reads from the timebase register (mftb): Xenia's guest clock less the
// time spent suspended - standing still while suspended, and never backwards on one
// thread.
uint64_t GuestTimebase();

}  // namespace warden

#endif  // WARDEN_SUSPEND_H_
