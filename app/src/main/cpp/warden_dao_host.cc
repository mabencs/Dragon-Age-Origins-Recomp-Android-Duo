// xe::dao_host - the services Xenia's GPU code needs from the host runtime. Phase 5.
//
// Four functions, declared in xenia/dao_host.h. In the emulator these came from the
// kernel and CPU modules; DAORecomp replaced those three includes with this interface,
// and it is the whole of what the GPU needs from outside itself.
//
// Two of them can be real now and two cannot, and the difference is worth stating
// plainly rather than stubbing all four alike:
//
//   TitleId      real. The value is known from the disc (0x454108C0, measured in
//                warden_title.h), and nothing about returning it needs guest code.
//   ShortSleep   real. A high-resolution sleep is a POSIX primitive.
//   AttachGuestThread    needs the recompiled game: it gives a host thread a guest
//   DispatchInterrupt    identity so it can run guest callbacks, and there is no
//                        guest code in this build to run. Both abort.
//
// The two that abort do so loudly, as the Phase 3.2 stubs did and for the same
// reason: the GPU calls DispatchInterrupt to deliver vblank and command-processor
// interrupts to the title, and a silent no-op there would look like a game that
// renders but never advances - a far harder thing to diagnose than a crash naming the
// function.

#include <errno.h>
#include <time.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#include "ppc_recomp_shared.h"
#include "warden_guest.h"
#include "warden_guest_heap.h"
#include "warden_title.h"
#include "xenia/base/logging.h"
#include "xenia/dao_host.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace {

[[noreturn]] void NotImplemented(const char* what) {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "%s was called, but no guest code is loaded.", what);
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "See app/src/main/cpp/warden_dao_host.cc - this needs the "
                      "recompiled title, which Phase 5 has not linked yet.");
#endif
  std::abort();
}

}  // namespace

namespace xe::dao_host {

void AttachGuestThread(const char* name) {
  // Real as of task 5.1. Allocates a PPCContext and a guest stack for this thread so
  // it can execute recompiled functions; idempotent, as the header promises.
  if (!warden::AttachGuestThreadForHost(name)) {
    NotImplemented("xe::dao_host::AttachGuestThread (no guest memory)");
  }
}

void DispatchInterrupt(uint32_t callback, uint32_t user_data, uint32_t source,
                       uint32_t cpu) {
  // Real as of task 5 (the GPU). The graphics system's vsync worker raises source 0
  // at every vblank and its command processor source 1 for the title's own PM4
  // INTERRUPT packets; both call from their own threads, which attach to the guest
  // here if they have not. The console delivers interrupts one at a time, so they are
  // serialised.
  //
  // The cpu matters: an INTERRUPT packet names the cores to interrupt, the title's
  // callback reads the current core from the PCR (r13 + 0x10C) and clears that
  // core's bit in its pending mask - so the callback must run "on" the core the
  // packet named, or the bit stays set and the title's Direct3D waits for it forever.
  // Every attached thread has a PCR (warden_guest.cc); the core number goes into it
  // before each call, as Xenia's EmulateCPInterruptDPC and DAORecomp's
  // CallInterruptCallback do.
  if (!callback) {
    return;
  }
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  if (!warden::AttachGuestThreadForHost("gpu-interrupt")) {
    NotImplemented("xe::dao_host::DispatchInterrupt (no guest memory)");
  }
  uint8_t* base = warden::GuestMemoryBase();
  PPCContext* ctx = warden::CurrentGuestContext();
  const uint32_t pcr = warden::CurrentGuestPcr();
  if (!pcr) {
    NotImplemented("xe::dao_host::DispatchInterrupt (the thread has no PCR)");
  }
  constexpr uint32_t kPcrCurrentCpu = 0x10C;
  base[pcr + kPcrCurrentCpu] = uint8_t(cpu > 5 ? 2 : cpu);
  PPCFunc* function = PPC_LOOKUP_FUNC(base, callback);
  if (!function) {
    static uint32_t said = 0;
    if (said != callback) {
      said = callback;
      XELOGE("graphics interrupt callback {:08X} is not a translated function", callback);
    }
    return;
  }
  ctx->r3.u64 = source;
  ctx->r4.u64 = user_data;
  function(*ctx, base);
}

uint32_t TitleId() {
  // Both Dragon Age discs carry this; the media id is what separates Origins from
  // Awakening, and nothing here needs to know which disc is mounted.
  return warden::kDragonAgeTitleId;
}

void ShortSleep(uint32_t microseconds) {
  // clock_nanosleep against the monotonic clock, not usleep: usleep is obsolescent,
  // and nanosleep without TIMER_ABSTIME restarts its interval from scratch after a
  // signal - which this process delivers deliberately, on every watched guest write
  // (warden_memory.cc). A relative sleep interrupted by those would run long.
  struct timespec deadline;
  if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
    return;
  }
  deadline.tv_nsec += long(microseconds % 1000000u) * 1000L;
  deadline.tv_sec += time_t(microseconds / 1000000u);
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_nsec -= 1000000000L;
    ++deadline.tv_sec;
  }
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) ==
         EINTR) {
    // Absolute deadline, so resuming after a signal cannot overshoot.
  }
}

}  // namespace xe::dao_host
