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

#include "warden_suspend.h"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

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

}  // namespace

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
    g_suspended.store(true, std::memory_order_release);
  }
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
