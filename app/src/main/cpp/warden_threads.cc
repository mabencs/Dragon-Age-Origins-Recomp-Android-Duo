// Guest threads, and the kernel objects threads wait on. Task 5.1h.
//
// What stopped startup once the filesystem answered was ExCreateThread: the title
// names Eclipse::StartupThread, creates it, and waits for it. So:
//
//   ExCreateThread      a host thread of its own, pinned to the performance cores,
//                       running the title's code on a guest stack of its own
//                       (warden_guest.cc, RunGuestThread)
//   ExTerminateThread   ends it - and does not return, which the stub did (BUG-032's
//                       shape again)
//   events, semaphores  in the object table, with one wait mutex and one condition
//                       variable between them (warden_objects.h says why)
//   the waits           NtWaitForSingleObjectEx, NtWaitForMultipleObjectsEx,
//                       KeWaitForSingleObject
//   KeDelayExecutionThread  actually sleeps, so a wait loop stops spinning
//
// Signatures and status codes follow Xenia's xboxkrnl_threading.cc and
// xboxkrnl_ob.cc; register usage follows the title's own call sites, which were read
// first (STATUS.md §4, 5.1h) - BUG-033 is what trusting a signature cost.
//
// Timeouts and intervals are the console's: a 64-bit count of 100 ns units, negative
// for relative, positive for absolute since 1601, zero to poll; a null pointer waits
// forever.

#include "warden_threads.h"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_affinity.h"
#include "warden_guest.h"
#include "warden_guest_registers.h"
#include "warden_net.h"
#include "warden_thermal.h"
#include "warden_objects.h"
#include "warden_suspend.h"
#include "warden_trace.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

// While the app is in the background the title's threads hold at the entry and the exit
// of every wait and sleep (warden_suspend.cc, BUG-057). Declared before a wait takes
// the wait mutex, so its destructor - the exit's hold - runs after the mutex is let go:
// a thread never holds with a lock of the runtime's taken.
struct SuspendPoints {
  SuspendPoints() { GuestSuspendPoint(); }
  ~SuspendPoints() { GuestSuspendPoint(); }
  SuspendPoints(const SuspendPoints&) = delete;
  SuspendPoints& operator=(const SuspendPoints&) = delete;
};

constexpr uint32_t kStatusSuccess = 0x00000000;
constexpr uint32_t kStatusTimeout = 0x00000102;
constexpr uint32_t kStatusInvalidHandle = 0xC0000008;
constexpr uint32_t kStatusInvalidParameter = 0xC000000D;
constexpr uint32_t kStatusSemaphoreLimitExceeded = 0xC0000047;
constexpr uint32_t kStatusObjectTypeMismatch = 0xC0000024;

// ExCreateThread's creation flags, the ones that change what happens here.
constexpr uint32_t kThreadInitiallySuspended = 0x00000001;
constexpr uint32_t kReturnKThreadPtr = 0x00000080;

// 100 ns units since 1601-01-01, the console's FILETIME.
constexpr int64_t kUnixEpochAsFiletime = 116444736000000000ll;

// Thread ids are multiples of 4 from a base well away from handles.
std::atomic<uint32_t> g_next_thread_id{0x1000};

// The guest thread this host thread is running, if ExCreateThread made it. The boot
// thread has none.
thread_local std::shared_ptr<GuestThread> t_thread;

using Clock = std::chrono::steady_clock;

int64_t NowAsFiletime() {
  const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
  return kUnixEpochAsFiletime +
         std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count() / 100;
}

// A guest timeout as a deadline on the steady clock. nullopt is "never".
struct Deadline {
  bool forever = false;
  Clock::time_point at;
};

Deadline DeadlineFrom(uint8_t* base, uint32_t timeout_pointer) {
  Deadline deadline;
  if (!timeout_pointer) {
    deadline.forever = true;
    return deadline;
  }
  const int64_t ticks = int64_t(PPC_LOAD_U64(timeout_pointer));
  int64_t relative_ticks;
  if (ticks < 0) {
    relative_ticks = -ticks;
  } else if (ticks == 0) {
    relative_ticks = 0;
  } else {
    relative_ticks = ticks - NowAsFiletime();
    if (relative_ticks < 0) {
      relative_ticks = 0;
    }
  }
  // Clamped: INT64_MIN - the title's Sleep(INFINITE) - would overflow the
  // conversion, and anything over a year is forever in practice.
  constexpr int64_t kForeverTicks = 315360000000000ll;  // a year
  if (relative_ticks >= kForeverTicks) {
    deadline.forever = true;
    return deadline;
  }
  deadline.at = Clock::now() + std::chrono::nanoseconds(relative_ticks * 100);
  return deadline;
}

// Waits, under the wait mutex, until `ready` holds or the deadline passes. True if
// it held.
template <typename Predicate>
bool WaitUntil(std::unique_lock<std::mutex>& lock, const Deadline& deadline,
               Predicate ready) {
  if (deadline.forever) {
    WaitCondition().wait(lock, ready);
    return true;
  }
  return WaitCondition().wait_until(lock, deadline.at, ready);
}

// What a thread created by ExCreateThread does on its host thread.
void* GuestThreadMain(void* argument) {
  std::shared_ptr<GuestThread> thread(static_cast<std::shared_ptr<GuestThread>*>(argument)[0]);
  delete static_cast<std::shared_ptr<GuestThread>*>(argument);
  t_thread = thread;

  // The game's worker threads belong on the performance cluster, with the prime core
  // kept for the main thread and the efficiency cores for the OS (ARCHITECTURE.md §6).
  Affinity::Get().PinCurrentThreadToPerformance();
  pthread_setname_np(pthread_self(), thread->name.c_str());

  {
    std::unique_lock<std::mutex> lock(WaitMutex());
    WaitCondition().wait(lock, [&] { return thread->suspend_count == 0; });
  }
  const uint32_t exit_code =
      RunGuestThread(thread->name.c_str(), thread->stack_size, thread->xapi_thread_startup,
                     thread->start_address, thread->start_context, thread->guest_block,
                     thread->thread_id);
  // Returned rather than terminating - a raw thread's exit code is its return value.
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    thread->finished = true;
    thread->exit_code = exit_code;
    NoteSignalStateChanged(thread);
  }
  WaitCondition().notify_all();
  XELOGI("guest thread {} returned with {:08X}", thread->name, exit_code);
  xe::FlushLog();
  t_thread.reset();
  return nullptr;
}

// Who is calling, for the wait and signal log: the thread's name, or "boot" for the
// title's main thread, which ExCreateThread did not make.
std::string Who() { return t_thread ? t_thread->name : std::string("boot"); }

// The last signals and blocking waits, for the 5 s report: who sets or releases which
// object, who blocks on which (BUG-043: an acknowledgement the Update thread waits for
// never comes). Polls - waits with a zero timeout - are not recorded.
std::mutex g_sync_mutex;
std::map<std::string, uint32_t> g_sync_counts;  // "kind handle body by who" -> count

void RecordSync(const char* kind, uint32_t handle, uint32_t body) {
  const std::string key = fmt::format("{:8} handle {:08X} body {:08X} by {}", kind, handle, body, Who());
  std::lock_guard<std::mutex> lock(g_sync_mutex);
  ++g_sync_counts[key];
}

// Waits and signals are logged while startup is being understood - who parks on what
// is the question when a thread runs ahead of another - and capped per site, since a
// running title does thousands a second.
#define WARDEN_SYNC_LOG(limit)   ([] { static std::atomic<int> n{0}; return n.fetch_add(1, std::memory_order_relaxed) < (limit); }())

std::string DefaultThreadName(uint32_t thread_id, uint32_t start_address) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), "guest-%X@%08X", thread_id, start_address);
  return buffer;
}

}  // namespace

std::string CurrentGuestThreadName() { return Who(); }

void NameCurrentGuestThread(std::string_view name) {
  // Bionic caps thread names at 16 bytes including the terminator.
  std::string host_name(name.substr(0, 15));
  pthread_setname_np(pthread_self(), host_name.c_str());
  if (t_thread) {
    std::lock_guard<std::mutex> lock(WaitMutex());
    t_thread->name.assign(name);
  }
  SetGuestTraceThreadName(std::string(name));
}

void RecentSyncReport(std::string& out) {
  std::map<std::string, uint32_t> counts;
  {
    std::lock_guard<std::mutex> lock(g_sync_mutex);
    counts.swap(g_sync_counts);
  }
  for (const auto& [key, count] : counts) {
    out += fmt::format("\n  {:6} x {}", count, key);
  }
}

}  // namespace warden

using namespace warden;

// ---------------------------------------------------------------------- threads

// NTSTATUS ExCreateThread(PHANDLE handle, DWORD stack_size, PDWORD thread_id,
//                         PVOID xapi_thread_startup, PVOID start_address,
//                         PVOID start_context, DWORD creation_flags)
//
// The title's wrapper (sub_82AB34C0) passes its XAPI trampoline (sub_82AB8B30) as
// xapi_thread_startup, which calls start_address(start_context) and ends in
// ExTerminateThread; the engine's own thread pool (sub_83001C60) passes none and a
// raw start. Both forms are run as the console runs them. Suspended creation waits
// for NtResumeThread.
PPC_FUNC(__imp__ExCreateThread) {
  const uint32_t handle_pointer = ctx.r3.u32;
  const uint32_t stack_size = ctx.r4.u32;
  const uint32_t thread_id_pointer = ctx.r5.u32;
  const uint32_t flags = ctx.r9.u32;

  auto thread = std::make_shared<GuestThread>();
  thread->thread_id = g_next_thread_id.fetch_add(4, std::memory_order_relaxed);
  thread->xapi_thread_startup = ctx.r6.u32;
  thread->start_address = ctx.r7.u32;
  thread->start_context = ctx.r8.u32;
  thread->stack_size = stack_size;
  thread->suspend_count = (flags & kThreadInitiallySuspended) ? 1 : 0;
  thread->name = DefaultThreadName(thread->thread_id, thread->start_address);
  // Its kernel structures now, not when it runs: the KTHREAD's address can be asked
  // for here (flag 0x80), and KeWaitForSingleObject on that address must find it.
  thread->guest_block = AllocateGuestThreadBlock();
  if (!thread->guest_block) {
    XELOGE("ExCreateThread: no guest memory for the thread's kernel structures");
    ctx.r3.u64 = 0xC000009Au;  // STATUS_INSUFFICIENT_RESOURCES
    return;
  }
  AdoptGuestBody(thread, thread->guest_block + kThreadBlockKThread);
  const uint32_t handle = AddObject(thread);

  // Detached: nothing joins a guest thread; its object says when it has finished.
  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  pthread_t host_thread;
  auto* argument = new std::shared_ptr<GuestThread>(thread);
  const int error = pthread_create(&host_thread, &attributes, GuestThreadMain, argument);
  pthread_attr_destroy(&attributes);
  if (error != 0) {
    delete argument;
    CloseObjectHandle(handle);
    XELOGE("ExCreateThread: pthread_create failed ({})", error);
    ctx.r3.u64 = 0xC000009Au;  // STATUS_INSUFFICIENT_RESOURCES
    return;
  }

  if (handle_pointer) {
    PPC_STORE_U32(handle_pointer,
                  (flags & kReturnKThreadPtr) ? GuestBodyOf(thread) : handle);
  }
  if (thread_id_pointer) {
    PPC_STORE_U32(thread_id_pointer, thread->thread_id);
  }
  XELOGI("ExCreateThread(start {:08X}, context {:08X}, stack {:X}, flags {:08X}) -> "
         "handle {:08X}, thread {:X}{}",
         thread->start_address, thread->start_context, stack_size, flags, handle,
         thread->thread_id, thread->suspend_count ? ", suspended" : "");
  ctx.r3.u64 = kStatusSuccess;
}

// void ExTerminateThread(DWORD exit_code) - does not return.
//
// The thread's object is signalled so anything waiting on its handle wakes, and the
// host thread exits from inside the guest frames. Nothing on them needs unwinding:
// translated functions hold no destructors. The boot thread has no object, so for it
// this just ends the thread.
PPC_FUNC(__imp__ExTerminateThread) {
  (void)base;
  const uint32_t exit_code = ctx.r3.u32;
  if (t_thread) {
    {
      std::lock_guard<std::mutex> lock(WaitMutex());
      t_thread->finished = true;
      t_thread->exit_code = exit_code;
      NoteSignalStateChanged(t_thread);
    }
    WaitCondition().notify_all();
    XELOGI("ExTerminateThread: {} exits with {:08X}", t_thread->name, exit_code);
    t_thread.reset();
  } else {
    XELOGW("ExTerminateThread({:08X}) on the boot thread", exit_code);
  }
  xe::FlushLog();
  pthread_exit(nullptr);
}

// NTSTATUS NtResumeThread(HANDLE thread, PULONG previous_suspend_count)
PPC_FUNC(__imp__NtResumeThread) {
  auto thread = LookupObject<GuestThread>(ctx.r3.u32, KernelObject::Type::kThread);
  if (!thread) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  uint32_t previous;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    previous = thread->suspend_count;
    if (thread->suspend_count) {
      --thread->suspend_count;
    }
  }
  WaitCondition().notify_all();
  if (ctx.r4.u32) {
    PPC_STORE_U32(ctx.r4.u32, previous);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// NTSTATUS NtSuspendThread(HANDLE thread, PULONG previous_suspend_count)
//
// Honoured only for a thread that has not started: a guest thread that is running
// cannot be stopped from outside without a way to interrupt it, and pretending would
// be a quiet wrong answer.
PPC_FUNC(__imp__NtSuspendThread) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  auto thread = LookupObject<GuestThread>(ctx.r3.u32, KernelObject::Type::kThread);
  if (!thread) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  uint32_t previous;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    previous = thread->suspend_count;
    ++thread->suspend_count;
  }
  if (previous == 0) {
    XELOGW("NtSuspendThread: {} is running and will not stop until it next waits",
           thread->name);
  }
  if (ctx.r4.u32) {
    PPC_STORE_U32(ctx.r4.u32, previous);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// BUG-117 (the owner and issue #2, 2026-11-03: the Thor's big cores at 95 C): the title
// waits for its other threads with Sleep(0) in a loop - the main thread's hand-off to the
// render thread (sub_825596E0) polls an event with no timeout and Sleep(0)s until the
// render thread answers. On the Xenon a zero sleep gave the core to its sibling hardware
// thread; here sched_yield returns at once when nothing else wants the core, so the loop
// spun a big core flat out (run 697: Eclipse::Update 99% of a core, 71% of it in
// sched_yield). With Heat guard's Rest the CPU in the game's waits (files/heat-cpu-rest),
// a zero-length sleep that follows another
// within kSpinGap is a spin: from the kSpinStreak-th on it naps kNap instead, the core idle
// meanwhile. Only then: the answer is seen up to a nap later, and that costs a little
// smoothness - 41.8 game frames over 50 ms a Korcari Wilds run against 37.4 (runs 704-717)
// - so it is the guard's, a trade for heat. A lone Sleep(0) between pieces of work only
// yields either way.
namespace {
constexpr auto kSpinGap = std::chrono::microseconds(200);
constexpr uint32_t kSpinStreak = 8;
constexpr auto kNap = std::chrono::microseconds(30);

}  // namespace

namespace warden {
// BUG-117's test switch: files/hack-spin restores the spinning wait (Sleep(0) as a yield
// whatever the streak), to measure against.
bool SpinWaits() {
  static const bool spin = [] {
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    FILE* file = dir ? std::fopen((std::string(dir) + "/hack-spin").c_str(), "r") : nullptr;
    if (file) {
      std::fclose(file);
      XELOGW("waits: spinning as before BUG-117 (files/hack-spin)");
    }
    return file != nullptr;
  }();
  return spin;
}
}  // namespace warden

namespace {
void YieldOrNap() {
  if (warden::SpinWaits() || !warden::thermal::CpuRest()) {
    sched_yield();
    return;
  }
  thread_local Clock::time_point t_last;
  thread_local uint32_t t_streak = 0;
  const Clock::time_point now = Clock::now();
  t_streak = now - t_last < kSpinGap ? t_streak + 1 : 0;
  t_last = now;
  if (t_streak >= kSpinStreak) {
    std::this_thread::sleep_for(kNap);
    t_last = Clock::now();
  } else {
    sched_yield();
  }
}
}  // namespace

// NTSTATUS KeDelayExecutionThread(KPROCESSOR_MODE mode, BOOLEAN alertable,
//                                 PLARGE_INTEGER interval)
//
// The title's Sleep(ms) wrapper (sub_82AB83F0) arrives here with a relative interval,
// and Sleep(INFINITE) as INT64_MIN. No APCs exist to alert a sleeper, so an alertable
// infinite sleep is simply a sleep - the thread parks, which is what the title's main
// thread does once its startup thread is away.
PPC_FUNC(__imp__KeDelayExecutionThread) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  const SuspendPoints suspend_points;  // before the deadline: a sleep counts from a resume
  const Deadline deadline = DeadlineFrom(base, ctx.r5.u32);
  if (deadline.forever) {
    XELOGI("KeDelayExecutionThread: {} sleeps indefinitely",
           t_thread ? t_thread->name : std::string("the boot thread"));
    xe::FlushLog();
    for (;;) {
      std::this_thread::sleep_for(std::chrono::hours(1));
    }
  }
  if (WARDEN_SYNC_LOG(300)) {
    XELOGI("[{}] KeDelayExecutionThread({} ticks)", Who(),
           ctx.r5.u32 ? int64_t(PPC_LOAD_U64(ctx.r5.u32)) : 0);
  }
  // A title that spins here - a zero-tick delay in a loop, polling a flag - is waiting
  // for something this runtime has not delivered. The return addresses name the loop,
  // once per distinct chain, so the polled condition can be read off the recompiled
  // code. Three levels, because the first is always the title's Sleep() wrapper
  // (sub_82AB83F0): each caller's return address sits 8 below its caller's frame,
  // and the frames chain through [r1] (RESUMING.md §7).
  {
    static std::mutex sites_mutex;
    static std::vector<uint64_t> sites;
    uint32_t chain[3] = {uint32_t(ctx.lr), 0, 0};
    uint32_t frame = ctx.r1.u32;
    for (int level = 1; level < 3 && frame; ++level) {
      const uint32_t caller_frame = PPC_LOAD_U32(frame);
      if (caller_frame <= frame || caller_frame - frame > 0x100000) {
        break;
      }
      chain[level] = PPC_LOAD_U32(caller_frame - 8);
      frame = caller_frame;
    }
    const uint64_t key = (uint64_t(chain[0]) << 32) | chain[1];
    std::lock_guard<std::mutex> lock(sites_mutex);
    if (sites.size() < 48 && std::find(sites.begin(), sites.end(), key) == sites.end()) {
      sites.push_back(key);
      XELOGI("[{}] KeDelayExecutionThread({} ticks) from {:08X} in sub_{:08X} < {:08X} in "
             "sub_{:08X} < {:08X} in sub_{:08X}",
             Who(), ctx.r5.u32 ? int64_t(PPC_LOAD_U64(ctx.r5.u32)) : 0, chain[0],
             GuestFunctionContaining(chain[0]), chain[1], GuestFunctionContaining(chain[1]),
             chain[2], GuestFunctionContaining(chain[2]));
    }
  }
  if (deadline.at <= Clock::now()) {
    YieldOrNap();
  } else {
    std::this_thread::sleep_until(deadline.at);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// NTSTATUS NtYieldExecution()
PPC_FUNC(__imp__NtYieldExecution) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  (void)base;
  GuestSuspendPoint();  // a thread yielding in a loop is waiting too
  YieldOrNap();  // BUG-117
  ctx.r3.u64 = kStatusSuccess;
}

// ULONGLONG KeQueryPerformanceFrequency()
//
// The Xenon's timebase, which Xenia's clock already models for the GPU side; the
// stub's 0 would have been a divisor somewhere.
PPC_FUNC(__imp__KeQueryPerformanceFrequency) {
  (void)base;
  ctx.r3.u64 = xe::Clock::guest_tick_frequency();
}

// ------------------------------------------------------------- events, semaphores

// NTSTATUS NtCreateEvent(PHANDLE handle, POBJECT_ATTRIBUTES attributes,
//                        EVENT_TYPE type, BOOLEAN initial_state)
//
// Type 0 is a notification event (manual reset), 1 a synchronization event (auto
// reset) - Xenia's `Initialize(!event_type, ...)`. Names in the attributes are
// ignored: nothing here is shared between processes.
PPC_FUNC(__imp__NtCreateEvent) {
  const uint32_t handle_pointer = ctx.r3.u32;
  const bool manual_reset = ctx.r5.u32 == 0;
  const bool initial = ctx.r6.u32 != 0;
  if (!handle_pointer) {
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  auto event = std::make_shared<GuestEvent>(manual_reset, initial);
  const uint32_t handle = AddObject(event);
  PPC_STORE_U32(handle_pointer, handle);
  if (WARDEN_SYNC_LOG(200)) {
    XELOGI("[{}] NtCreateEvent({}, initially {}) -> {:08X}", Who(),
           manual_reset ? "manual-reset" : "auto-reset", initial ? "set" : "clear", handle);
  }
  ctx.r3.u64 = kStatusSuccess;
}

namespace {

// NtSetEvent and NtPulseEvent share everything but whether the signal stays.
void SetEvent(PPCContext& ctx, uint8_t* base, bool pulse) {
  auto event = LookupObject<GuestEvent>(ctx.r3.u32, KernelObject::Type::kEvent);
  if (!event) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  RecordSync(pulse ? "pulse" : "set", ctx.r3.u32, event->guest_body);
  uint32_t previous;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    previous = event->signalled ? 1 : 0;
    event->signalled = true;
    NoteSignalStateChanged(event);
  }
  WaitCondition().notify_all();
  if (pulse) {
    // Everyone waiting has been woken and will see it set; nobody arriving later
    // should. Waiters re-check under the mutex, so this ordering is what makes a pulse
    // reach those who were there.
    std::lock_guard<std::mutex> lock(WaitMutex());
    event->signalled = false;
    NoteSignalStateChanged(event);
  }
  if (ctx.r4.u32) {
    PPC_STORE_U32(ctx.r4.u32, previous);
  }
  if (WARDEN_SYNC_LOG(300)) {
    XELOGI("[{}] {}({:08X})", Who(), pulse ? "NtPulseEvent" : "NtSetEvent", ctx.r3.u32);
  }
  ctx.r3.u64 = kStatusSuccess;
}

}  // namespace

// NTSTATUS NtSetEvent(HANDLE event, PLONG previous_state)
PPC_FUNC(__imp__NtSetEvent) { SetEvent(ctx, base, false); }

// NTSTATUS NtPulseEvent(HANDLE event, PLONG previous_state)
PPC_FUNC(__imp__NtPulseEvent) { SetEvent(ctx, base, true); }

// NTSTATUS NtClearEvent(HANDLE event)
PPC_FUNC(__imp__NtClearEvent) {
  (void)base;
  auto event = LookupObject<GuestEvent>(ctx.r3.u32, KernelObject::Type::kEvent);
  if (!event) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  std::lock_guard<std::mutex> lock(WaitMutex());
  event->signalled = false;
  NoteSignalStateChanged(event);
  ctx.r3.u64 = kStatusSuccess;
}

// LONG KeSetEvent(PKEVENT event, KPRIORITY increment, BOOLEAN wait)
//
// By guest pointer rather than handle: the one ObReferenceObjectByHandle gave out.
PPC_FUNC(__imp__KeSetEvent) {
  (void)base;
  auto object = ObjectAtGuestBody(ctx.r3.u32);
  if (!object || object->type() != KernelObject::Type::kEvent) {
    XELOGW("KeSetEvent: {:08X} is not an event this runtime knows", ctx.r3.u32);
    ctx.r3.u64 = 0;
    return;
  }
  RecordSync("KeSet", 0, ctx.r3.u32);
  auto event = std::static_pointer_cast<GuestEvent>(object);
  uint32_t previous;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    previous = event->signalled ? 1 : 0;
    event->signalled = true;
    NoteSignalStateChanged(event);
  }
  WaitCondition().notify_all();
  if (WARDEN_SYNC_LOG(300)) {
    XELOGI("[{}] KeSetEvent({:08X})", Who(), ctx.r3.u32);
  }
  ctx.r3.u64 = previous;
}

// NTSTATUS NtCreateSemaphore(PHANDLE handle, POBJECT_ATTRIBUTES attributes,
//                            LONG initial_count, LONG maximum_count)
PPC_FUNC(__imp__NtCreateSemaphore) {
  const uint32_t handle_pointer = ctx.r3.u32;
  const auto count = int32_t(ctx.r5.u32);
  const auto limit = int32_t(ctx.r6.u32);
  if (!handle_pointer || count < 0 || limit <= 0 || count > limit) {
    if (handle_pointer) {
      PPC_STORE_U32(handle_pointer, 0);
    }
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  const uint32_t handle = AddObject(std::make_shared<GuestSemaphore>(count, limit));
  PPC_STORE_U32(handle_pointer, handle);
  if (WARDEN_SYNC_LOG(200)) {
    XELOGI("[{}] NtCreateSemaphore({} of {}) -> {:08X}", Who(), count, limit, handle);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// NTSTATUS NtCreateMutant(PHANDLE handle, POBJECT_ATTRIBUTES attributes,
//                        BOOLEAN initial_owner)
//
// The title's CreateMutex. Named mutants are not shared between opens yet (Xenia looks
// the name up); nothing seen so far opens one by name twice.
PPC_FUNC(__imp__NtCreateMutant) {
  const uint32_t handle_pointer = ctx.r3.u32;
  const bool initially_owned = ctx.r5.u32 != 0;
  if (!handle_pointer) {
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  const uint32_t handle = AddObject(std::make_shared<GuestMutant>(initially_owned));
  PPC_STORE_U32(handle_pointer, handle);
  if (WARDEN_SYNC_LOG(64)) {
    XELOGI("[{}] NtCreateMutant({}) -> {:08X}", Who(),
           initially_owned ? "owned" : "free", handle);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// NTSTATUS NtReleaseMutant(HANDLE mutant, PLONG previous_count)
//
// STATUS_MUTANT_NOT_OWNED from a thread that does not hold it, as on Windows.
PPC_FUNC(__imp__NtReleaseMutant) {
  auto mutant = LookupObject<GuestMutant>(ctx.r3.u32, KernelObject::Type::kMutant);
  if (!mutant) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  RecordSync("unmutant", ctx.r3.u32, mutant->guest_body);
  uint32_t previous = 0;
  bool owned = false;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    owned = mutant->count != 0 && mutant->owner == CurrentWaiterIdentity();
    if (owned) {
      previous = mutant->count;
      if (--mutant->count == 0) {
        mutant->owner = nullptr;
      }
      NoteSignalStateChanged(mutant);
    }
  }
  if (!owned) {
    ctx.r3.u64 = 0xC0000046u;  // STATUS_MUTANT_NOT_OWNED
    return;
  }
  WaitCondition().notify_all();
  if (ctx.r4.u32) {
    PPC_STORE_U32(ctx.r4.u32, previous);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// NTSTATUS NtReleaseSemaphore(HANDLE semaphore, LONG release_count, PLONG previous)
PPC_FUNC(__imp__NtReleaseSemaphore) {
  auto semaphore =
      LookupObject<GuestSemaphore>(ctx.r3.u32, KernelObject::Type::kSemaphore);
  const auto release = int32_t(ctx.r4.u32);
  if (!semaphore) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  RecordSync("release", ctx.r3.u32, semaphore->guest_body);
  uint32_t status = kStatusSuccess;
  int32_t previous;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    previous = semaphore->count;
    if (release <= 0 || semaphore->count > semaphore->limit - release) {
      status = release <= 0 ? kStatusInvalidParameter : kStatusSemaphoreLimitExceeded;
    } else {
      semaphore->count += release;
      NoteSignalStateChanged(semaphore);
    }
  }
  if (status == kStatusSuccess) {
    WaitCondition().notify_all();
  }
  if (ctx.r5.u32) {
    PPC_STORE_U32(ctx.r5.u32, uint32_t(previous));
  }
  if (WARDEN_SYNC_LOG(300)) {
    XELOGI("[{}] NtReleaseSemaphore({:08X}, +{}) -> {:08X}, was {}", Who(), ctx.r3.u32,
           release, status, previous);
  }
  ctx.r3.u64 = status;
}

// ----------------------------------------------------------------------- waits

namespace {

// Waits on one object. Consumes it on success.
uint32_t WaitOne(const std::shared_ptr<KernelObject>& object, const Deadline& deadline) {
  if (deadline.forever ||
      deadline.at > Clock::now() + std::chrono::milliseconds(1)) {
    RecordSync("wait", 0, object->guest_body);
  }
  std::unique_lock<std::mutex> lock(WaitMutex());
  const bool ready = WaitUntil(lock, deadline, [&] { return object->IsSignalled(); });
  if (!ready) {
    return kStatusTimeout;
  }
  object->Consume();
  NoteSignalStateChanged(object);
  return kStatusSuccess;
}

}  // namespace

// NTSTATUS NtWaitForSingleObjectEx(HANDLE object, KPROCESSOR_MODE mode,
//                                  BOOLEAN alertable, PLARGE_INTEGER timeout)
PPC_FUNC(__imp__NtWaitForSingleObjectEx) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  warden_guest_trace_detail(ctx.r3.u32);
  const SuspendPoints suspend_points;
  auto object = LookupObject(ctx.r3.u32);
  if (!object) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  const bool log = WARDEN_SYNC_LOG(300);
  if (log) {
    XELOGI("[{}] NtWaitForSingleObjectEx({:08X}{}) ...", Who(), ctx.r3.u32,
           ctx.r6.u32 ? fmt::format(", timeout {}", int64_t(PPC_LOAD_U64(ctx.r6.u32))) : "");
  }
  ctx.r3.u64 = WaitOne(object, DeadlineFrom(base, ctx.r6.u32));
  if (log) {
    XELOGI("[{}] NtWaitForSingleObjectEx({:08X}) -> {:08X}", Who(), ctx.r3.u32, ctx.r3.u32);
  }
}

// NTSTATUS KeWaitForSingleObject(PVOID object, KWAIT_REASON reason,
//                                KPROCESSOR_MODE mode, BOOLEAN alertable,
//                                PLARGE_INTEGER timeout)
PPC_FUNC(__imp__KeWaitForSingleObject) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  warden_guest_trace_detail(ctx.r3.u32);
  const SuspendPoints suspend_points;
  auto object = ObjectAtGuestBody(ctx.r3.u32);
  if (!object) {
    XELOGW("KeWaitForSingleObject: {:08X} is not an object this runtime knows",
           ctx.r3.u32);
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  const uint32_t address = ctx.r3.u32;
  const bool log = WARDEN_SYNC_LOG(300);
  if (log) {
    XELOGI("[{}] KeWaitForSingleObject({:08X}{}) ...", Who(), address,
           ctx.r7.u32 ? fmt::format(", timeout {}", int64_t(PPC_LOAD_U64(ctx.r7.u32))) : "");
  }
  ctx.r3.u64 = WaitOne(object, DeadlineFrom(base, ctx.r7.u32));
  if (log) {
    XELOGI("[{}] KeWaitForSingleObject({:08X}) -> {:08X}", Who(), address, ctx.r3.u32);
  }
}

// NTSTATUS NtWaitForMultipleObjectsEx(ULONG count, PHANDLE handles, WAIT_TYPE type,
//                                     KPROCESSOR_MODE mode, BOOLEAN alertable,
//                                     PLARGE_INTEGER timeout)
//
// Type 0 waits for all, 1 for any; the result for "any" is STATUS_WAIT_0 plus the
// index of the one that was ready.
PPC_FUNC(__imp__NtWaitForMultipleObjectsEx) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  warden_guest_trace_detail(ctx.r4.u32 ? PPC_LOAD_U32(ctx.r4.u32) : 0);
  const SuspendPoints suspend_points;
  const uint32_t count = ctx.r3.u32;
  const uint32_t handles = ctx.r4.u32;
  const bool wait_all = ctx.r5.u32 == 0;
  if (!count || count > 64 || !handles) {
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  std::vector<std::shared_ptr<KernelObject>> objects;
  for (uint32_t i = 0; i < count; ++i) {
    auto object = LookupObject(PPC_LOAD_U32(handles + 4 * i));
    if (!object) {
      ctx.r3.u64 = kStatusInvalidHandle;
      return;
    }
    objects.push_back(std::move(object));
  }
  const Deadline deadline = DeadlineFrom(base, ctx.r8.u32);
  std::unique_lock<std::mutex> lock(WaitMutex());
  size_t which = 0;
  const bool ready = WaitUntil(lock, deadline, [&] {
    if (wait_all) {
      for (const auto& object : objects) {
        if (!object->IsSignalled()) {
          return false;
        }
      }
      return true;
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      if (objects[i]->IsSignalled()) {
        which = i;
        return true;
      }
    }
    return false;
  });
  if (!ready) {
    ctx.r3.u64 = kStatusTimeout;
    return;
  }
  if (wait_all) {
    for (const auto& object : objects) {
      object->Consume();
      NoteSignalStateChanged(object);
    }
    ctx.r3.u64 = kStatusSuccess;
  } else {
    objects[which]->Consume();
    NoteSignalStateChanged(objects[which]);
    ctx.r3.u64 = uint32_t(which);  // STATUS_WAIT_0 + index
  }
}

// ------------------------------------------------------------------ spinlocks

// A KSPIN_LOCK is one guest word: 0 free, otherwise the owner. The console takes it at
// raised IRQL with the other cores spinning; here the holders are the graphics
// interrupt thread and Direct3D's worker, around a few stores, so a thread spins
// briefly and then yields. The IRQL handed back and taken again is nominal - this
// runtime has no interrupt levels - and the owner word is a big-endian 1, so a title
// reading it sees "held" rather than a host pointer.
namespace {

uint32_t* LockWord(uint8_t* base, uint32_t address) {
  return reinterpret_cast<uint32_t*>(base + address);
}

void AcquireGuestSpinLock(uint8_t* base, uint32_t address) {
  uint32_t* word = LockWord(base, address);
  for (int spins = 0;; ++spins) {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(word, &expected, 0x01000000u, false, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED)) {
      return;
    }
    if (spins >= 64) {
      sched_yield();
    }
  }
}

void ReleaseGuestSpinLock(uint8_t* base, uint32_t address) {
  __atomic_store_n(LockWord(base, address), 0u, __ATOMIC_RELEASE);
}

}  // namespace

// KIRQL KfAcquireSpinLock(PKSPIN_LOCK lock)  - returns the IRQL to restore.
PPC_FUNC(__imp__KfAcquireSpinLock) {
  if (ctx.r3.u32) {
    AcquireGuestSpinLock(base, ctx.r3.u32);
  }
  ctx.r3.u64 = 0;
}

// void KfReleaseSpinLock(PKSPIN_LOCK lock, KIRQL old_irql)
PPC_FUNC(__imp__KfReleaseSpinLock) {
  if (ctx.r3.u32) {
    ReleaseGuestSpinLock(base, ctx.r3.u32);
  }
}

// void KeAcquireSpinLockAtRaisedIrql(PKSPIN_LOCK lock)
PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql) {
  if (ctx.r3.u32) {
    AcquireGuestSpinLock(base, ctx.r3.u32);
  }
}

// void KeReleaseSpinLockFromRaisedIrql(PKSPIN_LOCK lock)
PPC_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql) {
  if (ctx.r3.u32) {
    ReleaseGuestSpinLock(base, ctx.r3.u32);
  }
}

// KIRQL KeRaiseIrqlToDpcLevel(void)  /  void KfLowerIrql(KIRQL)
PPC_FUNC(__imp__KeRaiseIrqlToDpcLevel) {
  (void)base;
  ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__KfLowerIrql) {
  (void)ctx;
  (void)base;
}

// -------------------------------------------------------------- Winsock events

// Winsock's events are dispatcher events under another name: WSACreateEvent makes a
// manual-reset event and hands back its handle, as Xenia does, so NtClose and
// WSACloseEvent both close it. The title's network thread gathers its sockets' events
// behind a wake-up event of its own and waits on them all with no timeout; with real
// events it blocks here as on the console, where a stub answering "event 0 fired" had
// it spinning through two stubs at five million calls a second (BUG-035). The
// BOOL-returning calls say FALSE and set the Winsock last error when they fail.

namespace {

bool SetWinsockEvent(uint32_t handle, bool signalled) {
  auto event = LookupObject<GuestEvent>(handle, KernelObject::Type::kEvent);
  if (!event) {
    SetWsaLastError(kWsaInvalidHandle);
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    event->signalled = signalled;
    NoteSignalStateChanged(event);
  }
  if (signalled) {
    WaitCondition().notify_all();
  }
  return true;
}

}  // namespace

// WSAEVENT NetDll_WSACreateEvent(void)
PPC_FUNC(__imp__NetDll_WSACreateEvent) {
  (void)base;
  const uint32_t handle = AddObject(std::make_shared<GuestEvent>(true, false));
  if (WARDEN_SYNC_LOG(64)) {
    XELOGI("[{}] WSACreateEvent -> {:08X}", Who(), handle);
  }
  ctx.r3.u64 = handle;
}

// BOOL NetDll_WSACloseEvent(WSAEVENT event)
PPC_FUNC(__imp__NetDll_WSACloseEvent) {
  (void)base;
  const bool closed = CloseObjectHandle(ctx.r3.u32);
  if (!closed) {
    SetWsaLastError(kWsaInvalidHandle);
  }
  ctx.r3.u64 = closed ? 1 : 0;
}

// BOOL NetDll_WSASetEvent(WSAEVENT event)
PPC_FUNC(__imp__NetDll_WSASetEvent) {
  (void)base;
  RecordSync("wsa-set", ctx.r3.u32, 0);
  ctx.r3.u64 = SetWinsockEvent(ctx.r3.u32, true) ? 1 : 0;
}

// BOOL NetDll_WSAResetEvent(WSAEVENT event)
PPC_FUNC(__imp__NetDll_WSAResetEvent) {
  (void)base;
  ctx.r3.u64 = SetWinsockEvent(ctx.r3.u32, false) ? 1 : 0;
}

// DWORD NetDll_WSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events,
//                                       BOOL wait_all, DWORD timeout_ms, BOOL alertable)
//
// WSA_WAIT_EVENT_0 (0) plus the index of the event that fired, WSA_WAIT_TIMEOUT, or
// WSA_WAIT_FAILED with the last error set. The timeout is in milliseconds here, not
// 100 ns ticks; WSA_INFINITE is forever.
PPC_FUNC(__imp__NetDll_WSAWaitForMultipleEvents) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  warden_guest_trace_detail(ctx.r4.u32 ? PPC_LOAD_U32(ctx.r4.u32) : 0);
  const SuspendPoints suspend_points;
  const uint32_t count = ctx.r3.u32;
  const uint32_t events = ctx.r4.u32;
  const bool wait_all = ctx.r5.u32 != 0;
  const uint32_t timeout = ctx.r6.u32;
  if (!count || count > 64 || !events) {
    SetWsaLastError(kWsaInvalidParameter);
    ctx.r3.u64 = kWsaWaitFailed;
    return;
  }
  std::vector<std::shared_ptr<KernelObject>> objects;
  for (uint32_t i = 0; i < count; ++i) {
    auto object = LookupObject(PPC_LOAD_U32(events + 4 * i));
    if (!object) {
      SetWsaLastError(kWsaInvalidHandle);
      ctx.r3.u64 = kWsaWaitFailed;
      return;
    }
    objects.push_back(std::move(object));
  }
  Deadline deadline;
  if (timeout == kWsaInfinite) {
    deadline.forever = true;
  } else {
    deadline.at = Clock::now() + std::chrono::milliseconds(timeout);
  }
  const bool log = WARDEN_SYNC_LOG(64);
  if (log) {
    XELOGI("[{}] WSAWaitForMultipleEvents({} events{}, timeout {}) ...", Who(), count,
           wait_all ? ", all" : "", timeout == kWsaInfinite ? std::string("infinite")
                                                             : fmt::format("{} ms", timeout));
  }
  std::unique_lock<std::mutex> lock(WaitMutex());
  size_t which = 0;
  const bool ready = WaitUntil(lock, deadline, [&] {
    if (wait_all) {
      for (const auto& object : objects) {
        if (!object->IsSignalled()) {
          return false;
        }
      }
      return true;
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      if (objects[i]->IsSignalled()) {
        which = i;
        return true;
      }
    }
    return false;
  });
  if (!ready) {
    ctx.r3.u64 = kWsaWaitTimeout;
  } else if (wait_all) {
    for (const auto& object : objects) {
      object->Consume();
      NoteSignalStateChanged(object);
    }
    ctx.r3.u64 = 0;
  } else {
    objects[which]->Consume();
    NoteSignalStateChanged(objects[which]);
    ctx.r3.u64 = uint32_t(which);
  }
  if (log) {
    XELOGI("[{}] WSAWaitForMultipleEvents -> {:08X}", Who(), ctx.r3.u32);
  }
}

// -------------------------------------------------------------- object manager

// NTSTATUS ObReferenceObjectByHandle(HANDLE handle, POBJECT_TYPE type, PVOID* object)
//
// Hands out the object's guest body. The type the title passes is one of the kernel's
// exported variables - PsThreadObjectType and friends - which the unpatched import
// table (5.1d) leaves holding whatever the image has there, so it cannot be checked
// yet and is not.
PPC_FUNC(__imp__ObReferenceObjectByHandle) {
  const uint32_t out_pointer = ctx.r5.u32;
  if (out_pointer) {
    PPC_STORE_U32(out_pointer, 0);
  }
  auto object = LookupObject(ctx.r3.u32);
  if (!object) {
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  const uint32_t body = GuestBodyOf(object);
  if (!body) {
    ctx.r3.u64 = 0xC000009Au;  // STATUS_INSUFFICIENT_RESOURCES
    return;
  }
  if (out_pointer) {
    PPC_STORE_U32(out_pointer, body);
  }
  ctx.r3.u64 = kStatusSuccess;
}

// void ObDereferenceObject(PVOID object)
//
// Bodies are not reference-counted here: an object lives while its handle or the
// table's body record does, and both outlive any reference the title holds.
PPC_FUNC(__imp__ObDereferenceObject) {
  (void)ctx;
  (void)base;
}

// LONG KeResetEvent(PKEVENT event)  - by guest pointer; returns the previous state.
PPC_FUNC(__imp__KeResetEvent) {
  (void)base;
  auto object = ObjectAtGuestBody(ctx.r3.u32);
  if (!object || object->type() != KernelObject::Type::kEvent) {
    XELOGW("KeResetEvent: {:08X} is not an event this runtime knows", ctx.r3.u32);
    ctx.r3.u64 = 0;
    return;
  }
  auto event = std::static_pointer_cast<GuestEvent>(object);
  std::lock_guard<std::mutex> lock(WaitMutex());
  const uint32_t previous = event->signalled ? 1 : 0;
  event->signalled = false;
  NoteSignalStateChanged(event);
  ctx.r3.u64 = previous;
}

// NTSTATUS KeSetAffinityThread(PKTHREAD thread, DWORD affinity, PDWORD previous)
//
// The console stores the previous affinity through the pointer and returns a status,
// unlike Windows. Threads here are placed by cluster (warden_affinity.cc), not by the
// console's six hardware threads, so the mask is accepted and recorded only.
PPC_FUNC(__imp__KeSetAffinityThread) {
  const uint32_t affinity = ctx.r4.u32;
  if (!affinity) {
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  auto object = ObjectAtGuestBody(ctx.r3.u32);
  if (!object || object->type() != KernelObject::Type::kThread) {
    XELOGW("KeSetAffinityThread: {:08X} is not a thread this runtime knows", ctx.r3.u32);
    ctx.r3.u64 = kStatusInvalidHandle;
    return;
  }
  if (ctx.r5.u32) {
    PPC_STORE_U32(ctx.r5.u32, 1);  // "was on hardware thread 0"
  }
  ctx.r3.u64 = kStatusSuccess;
}

// void KeInitializeSemaphore(PKSEMAPHORE semaphore, LONG count, LONG limit)
//
// Writes the dispatcher header the console would - type 5, signal state, limit at
// +16 - and adopts the object, so a later wait on the address finds it.
PPC_FUNC(__imp__KeInitializeSemaphore) {
  const uint32_t address = ctx.r3.u32;
  if (!address) {
    return;
  }
  std::memset(base + address, 0, 20);
  base[address] = 5;
  PPC_STORE_U32(address + 4, ctx.r4.u32);
  PPC_STORE_U32(address + 16, ctx.r5.u32);
  ObjectAtGuestBody(address);
}

// LONG KeReleaseSemaphore(PKSEMAPHORE semaphore, KPRIORITY increment, LONG adjustment,
//                         BOOLEAN wait)  - returns the previous count.
PPC_FUNC(__imp__KeReleaseSemaphore) {
  (void)base;
  auto object = ObjectAtGuestBody(ctx.r3.u32);
  if (!object || object->type() != KernelObject::Type::kSemaphore) {
    XELOGW("KeReleaseSemaphore: {:08X} is not a semaphore this runtime knows",
           ctx.r3.u32);
    ctx.r3.u64 = 0;
    return;
  }
  RecordSync("KeRelSem", 0, ctx.r3.u32);
  auto semaphore = std::static_pointer_cast<GuestSemaphore>(object);
  const auto adjustment = int32_t(ctx.r5.u32);
  int32_t previous;
  {
    std::lock_guard<std::mutex> lock(WaitMutex());
    previous = semaphore->count;
    if (adjustment > 0 && semaphore->count <= semaphore->limit - adjustment) {
      semaphore->count += adjustment;
      NoteSignalStateChanged(semaphore);
    }
  }
  WaitCondition().notify_all();
  ctx.r3.u64 = uint32_t(previous);
}

// NTSTATUS KeWaitForMultipleObjects(ULONG count, PVOID objects[], WAIT_TYPE type,
//     KWAIT_REASON reason, KPROCESSOR_MODE mode, BOOLEAN alertable,
//     PLARGE_INTEGER timeout, PKWAIT_BLOCK wait_blocks)
//
// By guest pointers. Type 0 waits for all, 1 for any. Eight arguments, so all eight are
// in registers (r3-r10) and the timeout pointer is the seventh, r9. Until session 32
// it was read from the stack at r1 + 0x54 - the ninth argument's slot - which in the
// title's XAudio render callback holds the second object's address: the event's own
// header then served as an absolute time in the past, every infinite wait polled, and
// the callback skipped its frame whenever the mixer was not already done (BUG-050).
PPC_FUNC(__imp__KeWaitForMultipleObjects) {
  warden_guest_trace(__func__);  // so a parked thread's trace ends in its wait
  warden_guest_trace_detail(ctx.r4.u32 ? PPC_LOAD_U32(ctx.r4.u32) : 0);
  const SuspendPoints suspend_points;
  const uint32_t count = ctx.r3.u32;
  const uint32_t pointers = ctx.r4.u32;
  const bool wait_all = ctx.r5.u32 == 0;
  const uint32_t timeout_pointer = ctx.r9.u32;
  if (!count || count > 64 || !pointers) {
    ctx.r3.u64 = kStatusInvalidParameter;
    return;
  }
  std::vector<std::shared_ptr<KernelObject>> objects;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t address = PPC_LOAD_U32(pointers + 4 * i);
    auto object = ObjectAtGuestBody(address);
    if (!object) {
      XELOGW("KeWaitForMultipleObjects: {:08X} is not an object this runtime knows",
             address);
      ctx.r3.u64 = kStatusInvalidParameter;
      return;
    }
    objects.push_back(std::move(object));
  }
  const Deadline deadline = DeadlineFrom(base, timeout_pointer);
  std::unique_lock<std::mutex> lock(WaitMutex());
  size_t which = 0;
  const bool ready = WaitUntil(lock, deadline, [&] {
    if (wait_all) {
      for (const auto& object : objects) {
        if (!object->IsSignalled()) {
          return false;
        }
      }
      return true;
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      if (objects[i]->IsSignalled()) {
        which = i;
        return true;
      }
    }
    return false;
  });
  if (!ready) {
    ctx.r3.u64 = kStatusTimeout;
    return;
  }
  if (wait_all) {
    for (const auto& object : objects) {
      object->Consume();
      NoteSignalStateChanged(object);
    }
    ctx.r3.u64 = kStatusSuccess;
  } else {
    objects[which]->Consume();
    NoteSignalStateChanged(objects[which]);
    ctx.r3.u64 = uint32_t(which);
  }
}

// void KeQuerySystemTime(PLARGE_INTEGER time)  - 100 ns units since 1601.
PPC_FUNC(__imp__KeQuerySystemTime) {
  if (ctx.r3.u32) {
    PPC_STORE_U64(ctx.r3.u32, uint64_t(NowAsFiletime()));
  }
}
