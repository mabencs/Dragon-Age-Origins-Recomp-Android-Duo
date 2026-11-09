// The guest held while the app is in the background (BUG-057).
//
// A handheld game is put away at any moment - the power button, the home button,
// another app - and has to come back where it was. Until session 35 only the sound
// stopped: the title ran on unseen, a fight went on without the player, the GPU drew
// frames nobody saw, and the audio the title made was thrown away.
//
// So while the activity is paused (GameActivity's onPause to onResume) the title's own
// threads are held and its clocks stopped:
//
//   where threads hold  at the entry and the exit of every wait and sleep, and at each
//                       yield (warden_threads.cc); before each audio render callback
//                       (warden_audio.cc). Never inside the runtime's own locks, so the
//                       main thread cannot find one taken. A thread working between two
//                       waits runs on to its next one - a frame's work at most.
//   what runs on        the console's hardware: the GPU finishes what it was given, and
//                       the vblank and command processor interrupts still reach the
//                       title's interrupt handler, as on a console whose threads all
//                       wait. Holding those too would leave Direct3D's render thread
//                       spinning on a GPU that never gets there (sub_82D8E218 polls the
//                       read pointer, with no wait in the loop) - a core at full speed
//                       in a pocket.
//   the clocks          the timebase here, and the kernel's time stamp bundle
//                       (warden_kernel_variables.cc), stand still while suspended, so
//                       the title sees no time pass: a fight resumes mid-swing. The wall
//                       clock (KeQuerySystemTime) runs on, as a console's would.
//   threads that spin   (BUG-119) a title thread can wait with no call at all: the
//                       engine's render thread empties its job queue and then loops on
//                       a "batch done" byte the main thread sets (sub_82682BF0). Held
//                       mid-frame, the main thread never sets it, and the render thread
//                       spun a whole core until the game came back - 4 times in 7
//                       lid-closes in play (session 46). A watcher looks a second into
//                       each suspension: a registered title thread still using CPU gets
//                       a signal, and if it lands in the title's translated code
//                       (libdaogame.so, which takes none of the runtime's locks) the
//                       handler parks the thread on a futex until the resume; anywhere
//                       else it returns, and the thread holds at its next point.

#include "warden_suspend.h"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__) && defined(__aarch64__)
#define WARDEN_HOLD_BY_SIGNAL 1
#include <errno.h>
#include <link.h>
#include <linux/futex.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
#include <cstring>
#endif

#include "warden_threads.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

std::atomic<bool> g_suspended{false};
std::mutex g_mutex;
std::condition_variable g_resumed;
// Under g_mutex.
uint32_t g_held = 0;       // threads holding now
uint32_t g_held_most = 0;  // the most at once in this suspension
uint32_t g_suspensions = 0;
std::chrono::steady_clock::time_point g_since;

// The timebase in one word, so a reader takes one load: with the low bit set the guest
// is suspended and the rest is the reading it stopped at; clear, the rest is the ticks
// spent suspended so far, which come off Xenia's guest clock.
std::atomic<uint64_t> g_timebase{0};

// Each thread's hold is logged in the first suspensions, to see who stops where; after
// those, only the summary.
constexpr uint32_t kLoggedSuspensions = 2;

// BUG-119: the hold by signal (the file's head). Tells the watcher a suspension began.
std::condition_variable g_watch;

#if WARDEN_HOLD_BY_SIGNAL
// 1 while suspended: the futex word parked threads sleep on.
alignas(4) int g_hold_word = 0;
// The title's translated code (libdaogame.so's executable segments); set before the
// handler is installed, read-only after.
uintptr_t g_code_begin = 0;
uintptr_t g_code_end = 0;

// Taken neither by a translated function nor while the runtime holds a lock of its own:
// Xenia's threading has SIGRTMIN + 0-2, the trace's sampler + 5 (warden_trace.cc).
int HoldSignal() { return SIGRTMIN + 6; }

// The title's threads, by kernel thread id - not pthread_t: bionic aborts on one whose
// thread has ended (ExTerminateThread ends its thread with pthread_exit).
std::mutex g_titles_mutex;
std::vector<pid_t> g_titles;

// Unregisters the thread as it ends, by return or by pthread_exit.
struct TitleRegistration {
  pid_t tid = 0;
  ~TitleRegistration() {
    if (tid) {
      std::lock_guard<std::mutex> lock(g_titles_mutex);
      g_titles.erase(std::remove(g_titles.begin(), g_titles.end(), tid), g_titles.end());
    }
  }
};
thread_local TitleRegistration t_registration;

// On the spinning thread. Parks it only in the title's code: there it holds no lock of
// the runtime's, so nothing else can be left waiting on it. Only async-signal-safe
// calls: an atomic load and the futex.
void HoldSignalHandler(int, siginfo_t*, void* context) {
  const int saved_errno = errno;
  const uintptr_t pc = static_cast<ucontext_t*>(context)->uc_mcontext.pc;
  if (pc >= g_code_begin && pc < g_code_end) {
    while (__atomic_load_n(&g_hold_word, __ATOMIC_ACQUIRE) != 0) {
      syscall(SYS_futex, &g_hold_word, FUTEX_WAIT_PRIVATE, 1, nullptr, nullptr, 0);
    }
  }
  errno = saved_errno;
}

int FindGameCode(dl_phdr_info* info, size_t, void*) {
  if (!info->dlpi_name || !std::strstr(info->dlpi_name, "libdaogame")) {
    return 0;
  }
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr)& header = info->dlpi_phdr[i];
    if (header.p_type == PT_LOAD && (header.p_flags & PF_X)) {
      const uintptr_t begin = info->dlpi_addr + header.p_vaddr;
      const uintptr_t end = begin + header.p_memsz;
      g_code_begin = g_code_begin ? std::min(g_code_begin, begin) : begin;
      g_code_end = std::max(g_code_end, end);
    }
  }
  return 1;
}

// A thread's CPU time so far, in clock ticks (utime + stime), or ULLONG_MAX if gone.
unsigned long long ThreadTicks(pid_t tid, std::string* name) {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", int(tid));
  FILE* file = std::fopen(path, "re");
  if (!file) {
    return ULLONG_MAX;
  }
  char text[512];
  const size_t length = std::fread(text, 1, sizeof(text) - 1, file);
  std::fclose(file);
  text[length] = '\0';
  const char* open = std::strchr(text, '(');
  const char* close = std::strrchr(text, ')');
  if (!open || !close || close < open) {
    return ULLONG_MAX;
  }
  if (name) {
    name->assign(open + 1, close);
  }
  // After the name: state, ppid, pgrp, session, tty, tpgid, flags, four fault counts,
  // then utime and stime (proc(5) fields 14 and 15).
  unsigned long long user = 0;
  unsigned long long system = 0;
  if (std::sscanf(close + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
                  &user, &system) != 2) {
    return ULLONG_MAX;
  }
  return user + system;
}

// Whether suspension `number` is still on. Takes g_mutex.
bool StillSuspension(uint32_t number) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_suspended.load(std::memory_order_relaxed) && g_suspensions == number;
}

// Sleeps up to `duration`, ending early when suspension `number` ends. Takes g_mutex.
bool SleepInSuspension(uint32_t number, std::chrono::milliseconds duration) {
  std::unique_lock<std::mutex> lock(g_mutex);
  return !g_resumed.wait_for(lock, duration, [number] {
    return !g_suspended.load(std::memory_order_relaxed) || g_suspensions != number;
  });
}

// The watcher: idle on g_watch between suspensions; in one, after a second (a frame's
// work to reach a wait), every 2 s, signals each title thread that used most of a core
// over half a second.
void WatchSuspensions() {
  pthread_setname_np(pthread_self(), "warden-hold");
  const unsigned long long ticks_per_second =
      sysconf(_SC_CLK_TCK) > 0 ? static_cast<unsigned long long>(sysconf(_SC_CLK_TCK)) : 100;
  uint32_t watched = 0;
  for (;;) {
    uint32_t number;
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_watch.wait(lock, [watched] {
        return g_suspended.load(std::memory_order_relaxed) && g_suspensions != watched;
      });
      number = watched = g_suspensions;
    }
    if (!SleepInSuspension(number, std::chrono::milliseconds(1000))) {
      continue;
    }
    std::vector<pid_t> signalled;
    while (StillSuspension(number)) {
      std::vector<pid_t> tids;
      {
        std::lock_guard<std::mutex> lock(g_titles_mutex);
        tids = g_titles;
      }
      std::vector<unsigned long long> before(tids.size());
      for (size_t i = 0; i < tids.size(); ++i) {
        before[i] = ThreadTicks(tids[i], nullptr);
      }
      if (!SleepInSuspension(number, std::chrono::milliseconds(500))) {
        break;
      }
      for (size_t i = 0; i < tids.size(); ++i) {
        std::string name;
        const unsigned long long after = ThreadTicks(tids[i], &name);
        if (before[i] == ULLONG_MAX || after == ULLONG_MAX) {
          continue;
        }
        // Over 60% of a core in the half second: running, not held.
        const unsigned long long used = after - before[i];
        if (used * 10 < ticks_per_second * 3) {
          continue;
        }
        if (std::find(signalled.begin(), signalled.end(), tids[i]) == signalled.end()) {
          signalled.push_back(tids[i]);
          XELOGI("suspension #{}: [{}] still running ({}% of a core) - held by signal",
                 number, name, used * 100 * 2 / ticks_per_second);
        }
        syscall(SYS_tgkill, getpid(), tids[i], HoldSignal());
      }
      if (!SleepInSuspension(number, std::chrono::milliseconds(1500))) {
        break;
      }
    }
  }
}

// Once, as the title starts (GuestStarting): the code range, the handler, the watcher.
void StartHoldBySignal() {
  static std::once_flag once;
  std::call_once(once, [] {
    dl_iterate_phdr(FindGameCode, nullptr);
    if (!g_code_end) {
      XELOGW("suspension: the title's code was not found; a spinning thread is not held");
      return;
    }
    struct sigaction action = {};
    action.sa_sigaction = HoldSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(HoldSignal(), &action, nullptr) != 0) {
      XELOGW("suspension: no handler for the hold signal; a spinning thread is not held");
      return;
    }
    std::thread(WatchSuspensions).detach();
  });
}
#endif  // WARDEN_HOLD_BY_SIGNAL

}  // namespace

void RegisterTitleThread() {
#if WARDEN_HOLD_BY_SIGNAL
  const pid_t tid = gettid();
  t_registration.tid = tid;
  std::lock_guard<std::mutex> lock(g_titles_mutex);
  g_titles.push_back(tid);
#endif
}

void SuspendGuest() {
  uint32_t suspension;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_suspended.load(std::memory_order_relaxed)) {
      return;
    }
    const uint64_t word = g_timebase.load(std::memory_order_relaxed);
    const uint64_t stopped = xe::Clock::QueryGuestTickCount() - (word >> 1);
    g_timebase.store((stopped << 1) | 1, std::memory_order_release);
    g_held_most = g_held;
    g_since = std::chrono::steady_clock::now();
    suspension = ++g_suspensions;
#if WARDEN_HOLD_BY_SIGNAL
    __atomic_store_n(&g_hold_word, 1, __ATOMIC_RELEASE);
#endif
    g_suspended.store(true, std::memory_order_release);
  }
  g_watch.notify_all();
  XELOGI("guest suspended (#{}): its threads hold at their next wait", suspension);
}

void ResumeGuest() {
  double seconds;
  uint32_t held_most;
  uint32_t suspension;
  uint64_t stopped;
  uint64_t spent;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_suspended.load(std::memory_order_relaxed)) {
      return;
    }
    // From here the guest clock goes on from the reading it stopped at.
    stopped = g_timebase.load(std::memory_order_relaxed) >> 1;
    spent = xe::Clock::QueryGuestTickCount() - stopped;
    g_timebase.store(spent << 1, std::memory_order_release);
    g_suspended.store(false, std::memory_order_release);
#if WARDEN_HOLD_BY_SIGNAL
    // BUG-119: the threads parked by signal go on too.
    __atomic_store_n(&g_hold_word, 0, __ATOMIC_RELEASE);
    syscall(SYS_futex, &g_hold_word, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
#endif
    seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_since).count();
    held_most = g_held_most;
    suspension = g_suspensions;
  }
  g_resumed.notify_all();
  const double tick_seconds = 1.0 / double(xe::Clock::guest_tick_frequency());
  XELOGI("guest resumed (#{}) after {:.1f} s; {} threads were held; its clock stopped at "
         "{:.3f} s and goes on from there, {:.1f} s suspended in all",
         suspension, seconds, held_most, double(stopped) * tick_seconds,
         double(spent) * tick_seconds);
}

bool GuestSuspended() { return g_suspended.load(std::memory_order_acquire); }

namespace {
std::atomic<bool> g_guest_starting{false};
}  // namespace

void GuestStarting() {
#if WARDEN_HOLD_BY_SIGNAL
  StartHoldBySignal();  // BUG-119: the title's code is loaded by now
#endif
  g_guest_starting.store(true, std::memory_order_release);
}

}  // namespace warden

// BUG-058: for the edits to Xenia's timer queue and log writer (assemble_runtime.py):
// nonzero while nothing needs them to answer at once - no title started yet (the
// launcher), or the title suspended (put away, the lid, the lock; the options panel).
extern "C" int warden_host_idle() {
  return !warden::g_guest_starting.load(std::memory_order_acquire) ||
         warden::GuestSuspended();
}

namespace warden {

bool GuestSuspendPoint(const char* who) {
  if (!g_suspended.load(std::memory_order_acquire)) {
    return false;
  }
  std::unique_lock<std::mutex> lock(g_mutex);
  if (!g_suspended.load(std::memory_order_relaxed)) {
    return false;
  }
  g_held_most = std::max(g_held_most, ++g_held);
  if (g_suspensions <= kLoggedSuspensions) {
    lock.unlock();
    // The host thread's name too: the threads attached to the guest from the host side
    // all answer "boot" to the guest's name.
    char host_name[16] = {};
    pthread_getname_np(pthread_self(), host_name, sizeof(host_name));
    XELOGI("[{}] held for the suspension (host thread \"{}\")",
           who ? std::string(who) : CurrentGuestThreadName(), host_name);
    lock.lock();
  }
  g_resumed.wait(lock, [] { return !g_suspended.load(std::memory_order_relaxed); });
  --g_held;
  return true;
}

uint64_t GuestTimebase() {
  const uint64_t ticks = xe::Clock::QueryGuestTickCount();
  const uint64_t word = g_timebase.load(std::memory_order_acquire);
  uint64_t value = (word & 1) ? word >> 1 : ticks - (word >> 1);
  // A reading taken across a suspension's start or end can land on either side of it,
  // and a title subtracting two readings would take a step back for a wrap-around.
  thread_local uint64_t t_last = 0;
  if (value < t_last) {
    value = t_last;
  }
  t_last = value;
  return value;
}

}  // namespace warden
