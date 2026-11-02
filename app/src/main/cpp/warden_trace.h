// A trace of the guest functions each thread enters. See warden_trace.cc.
#ifndef WARDEN_TRACE_H_
#define WARDEN_TRACE_H_

#include <cstdint>
#include <string>

// Records that the calling thread entered `function` (a string literal; the pointer is
// kept, not the text). The translated functions' prologue calls this; the kernel
// imports that block call it too, so a parked thread's last entry names its wait.
extern "C" void warden_guest_trace(const char* function);

// A detail for the calling thread's latest entry - the handle or object a wait is on -
// shown in the thread report after the function's name, so "parked in
// NtWaitForSingleObjectEx" says on what.
extern "C" void warden_guest_trace_detail(uint32_t detail);

namespace warden {

// The name the periodic `guest threads` report uses for the calling thread.
void SetGuestTraceThreadName(const std::string& name);

// Prints the last guest functions the calling thread entered, oldest first. Reads only
// this thread's ring buffer, so it is safe from a signal handler on that thread.
void LogGuestTraceOnFault();

// The same, to the ordinary log with consecutive repeats collapsed, for a thread that
// is alive and doing something puzzling - say what from `why`.
void LogGuestTraceNow(const char* why);

// BUG-052: interrupts every live guest thread now; each logs its guest registers, call
// chain and the last 48 functions it entered (`[name] sampled here:`). For catching the
// thread that is recording something the GPU has just been given.
void SampleGuestThreadsNow(const char* why);

}  // namespace warden

#endif  // WARDEN_TRACE_H_
