// A trace of the guest functions each thread enters. Task 5.1h.
//
// The call chain says where a thread *is* when it faults; it cannot say how it got
// there, and at startup that is usually the question - which path a thread took through
// the title's own code between two kernel calls. So, when the build option
// WARDEN_GUEST_TRACE is on (app/src/main/cpp/CMakeLists.txt), every translated function's
// prologue records its name here, in a ring per thread, and the fault report prints the
// last few hundred. The names are the generated ones - __imp__tu6_sub_8269C130 - with
// the prefix dropped, so they match the recompiled source and the call chain.
//
// The rings are also read from outside their threads, by a reporter that prints where
// every guest thread is every few seconds (`guest threads:` lines): how many functions
// it entered since the last report and the last few, which is what says a thread is
// parked - zero calls, and the name of the wait it is in - once the budgeted wait logs
// have run out. The reads are racy by design: the counter is atomic, the names are
// pointers to string literals, and a torn view of a ring costs one wrong name in a
// status line, never a crash.
//
// The cost is one thread-local store per guest call. Fine while the title is being
// brought up; turn the option off once it runs.

#include "warden_trace.h"

#include <android/log.h>
#include <pthread.h>
#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "warden_guest.h"
#include "warden_guest_registers.h"
#include "warden_suspend.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"

namespace {

// 32,768 entries (256 KB a thread): enough to reach back from a kick to what the thread
// recorded before it (BUG-052). A fault report prints only the last kFaultEntries.
constexpr uint32_t kRing = 32768;
constexpr uint32_t kFaultEntries = 512;

struct Ring {
  const char* names[kRing] = {};
  std::atomic<uint32_t> next{0};
  // The detail the thread's last wait recorded (its handle), and the entry index it
  // belongs to, so a report shows it only while that wait is the newest entry.
  std::atomic<uint32_t> detail{0};
  std::atomic<uint32_t> detail_at{0};
  std::string thread_name;   // set by SetGuestTraceThreadName; read under g_rings_mutex
  pthread_t thread{};        // for the parked-thread sample (a signal)
  uint32_t reported = 0;     // `next` at the last status report
  bool alive = true;
  // Set by SampleGuestThreadsNow: the next sample also prints the ring's newest entries.
  std::atomic<bool> sample_with_ring{false};
};

std::mutex g_rings_mutex;
std::vector<Ring*> g_rings;
std::once_flag g_reporter_once;

void ReportLoop();

// Each thread owns a ring, registered on first use and marked dead when the thread
// goes - the ring itself stays, so a report can still say what a finished thread did
// last. The ring lives on the heap and is never freed: a thread_local's storage goes
// with the thread, and the registry's pointer into it was what the report crashed on
// (BUG-041). Threads here are few; the leak is a few kilobytes each. The reporter
// starts with the first ring.
struct RingHolder {
  Ring* ring = new Ring;
  RingHolder() {
    ring->thread = pthread_self();
    std::lock_guard<std::mutex> lock(g_rings_mutex);
    g_rings.push_back(ring);
    std::call_once(g_reporter_once, [] { std::thread(ReportLoop).detach(); });
  }
  ~RingHolder() {
    std::lock_guard<std::mutex> lock(g_rings_mutex);
    ring->alive = false;
  }
};

Ring& ThisRing() {
  // The holder is constructed on the thread's first guest call, so its name arrives
  // afterwards - SetGuestTraceThreadName handles both orders.
  thread_local RingHolder* holder = nullptr;
  if (!holder) {
    thread_local RingHolder storage;
    holder = &storage;
  }
  return *holder->ring;
}

const char* StripPrefix(const char* name) {
  static constexpr const char* kPrefixes[] = {"__imp__tu6_", "__imp__", "tu6_"};
  for (const char* prefix : kPrefixes) {
    const size_t length = std::strlen(prefix);
    if (std::strncmp(name, prefix, length) == 0) {
      return name + length;
    }
  }
  return name;
}

// A thread that made no call in a report period and is not in a known wait is in a
// loop the trace cannot see - a guest-side poll of memory, with no calls. The report
// sends it this signal; the handler, running on that thread, logs its guest registers
// and call chain the way the fault report does. The chain is the loop's caller.
const int kSampleSignal = SIGRTMIN + 5;  // not a constant: bionic computes SIGRTMIN

// One sample logs at a time: two threads' chains would interleave in the log.
std::atomic_flag g_sample_lock = ATOMIC_FLAG_INIT;

void SampleSignalHandler(int, siginfo_t*, void*) {
  while (g_sample_lock.test_and_set(std::memory_order_acquire)) {
  }
  Ring& ring = ThisRing();
  __android_log_print(ANDROID_LOG_WARN, "Warden", "[%s] sampled here:",
                      ring.thread_name.empty() ? "?" : ring.thread_name.c_str());
  warden::LogGuestRegistersOnFault();
  warden::LogGuestCallChainOnFault(warden::GuestMemoryBase());
  // BUG-052: a sample SampleGuestThreadsNow asked for also shows the functions the
  // thread entered last, oldest first - what it was doing, not only where it is.
  if (ring.sample_with_ring.exchange(false, std::memory_order_acq_rel)) {
    const uint32_t total = ring.next.load(std::memory_order_relaxed);
    const uint32_t count = total < 48 ? total : 48;
    for (uint32_t i = 0; i < count; ++i) {
      const char* name = ring.names[(total - count + i) % kRing];
      __android_log_print(ANDROID_LOG_WARN, "Warden", "    entered %s",
                          name ? StripPrefix(name) : "?");
    }
  }
  g_sample_lock.clear(std::memory_order_release);
}

void InstallSampleSignal() {
  struct sigaction action = {};
  action.sa_sigaction = SampleSignalHandler;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  sigaction(kSampleSignal, &action, nullptr);
}

constexpr int kReportSeconds = 5;
constexpr size_t kReportLast = 6;      // collapsed entries shown per thread
constexpr uint32_t kReportWindow = 128;  // entries the collapsing looks back over
constexpr size_t kReportPiece = 16 * 1024;  // the most of a report in one log line

// Logs `body` (lines starting with '\n') after `head`, in pieces of at most
// kReportPiece characters split at a line break; a piece after the first says so.
void LogInPieces(const char* head, const std::string& body) {
  size_t start = 0;
  int piece = 0;
  while (start < body.size()) {
    size_t end = body.size();
    if (end - start > kReportPiece) {
      end = body.rfind('\n', start + kReportPiece);
      if (end == std::string::npos || end <= start) {
        end = start + kReportPiece;
      }
    }
    if (piece == 0) {
      XELOGI("{}{}", head, std::string_view(body).substr(start, end - start));
    } else {
      XELOGI("{} (continued, piece {}){}", head, piece + 1,
             std::string_view(body).substr(start, end - start));
    }
    ++piece;
    start = end;
  }
}

void ReportLoop() {
  InstallSampleSignal();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(kReportSeconds));
    // Held while the guest is suspended (warden_suspend.cc): every thread would read as
    // parked, and sampling each with a signal every few seconds would wake a handheld
    // that was put away.
    warden::GuestSuspendPoint("thread report");
    std::string report;
    std::vector<pthread_t> to_sample;
    {
      std::lock_guard<std::mutex> lock(g_rings_mutex);
      for (Ring* ring : g_rings) {
        const uint32_t total = ring->next.load(std::memory_order_relaxed);
        const uint32_t since = total - ring->reported;
        ring->reported = total;
        if (!ring->alive && since == 0) {
          continue;
        }
        // Named in WARDEN_SAMPLE_THREADS (files/sample-threads, a substring): sampled
        // every report, busy or not - for a spin whose caller the ring hides. Else a
        // thread parked without calls and not in a wait that recorded its handle: a
        // loop with no calls.
        const char* wanted = getenv("WARDEN_SAMPLE_THREADS");
        if (ring->alive && !ring->thread_name.empty()) {
          if (wanted && *wanted) {
            if (ring->thread_name.find(wanted) != std::string::npos) {
              to_sample.push_back(ring->thread);
            }
          } else if (since == 0 && total &&
                     ring->detail_at.load(std::memory_order_relaxed) != total) {
            to_sample.push_back(ring->thread);
          }
        }
        report += fmt::format("\n  [{}] {}{} calls{}; last:",
                              ring->thread_name.empty() ? "?" : ring->thread_name,
                              ring->alive ? "" : "finished, ", since,
                              since == 0 && ring->alive ? " - parked" : "");
        // The last entries with consecutive repeats collapsed, so a spin shows as one
        // name with a count and the function it spins in stays visible before it.
        const uint32_t window = total < kReportWindow ? total : kReportWindow;
        std::vector<std::pair<const char*, uint32_t>> runs;
        for (uint32_t i = 0; i < window; ++i) {
          const char* name = ring->names[(total - window + i) % kRing];
          if (!runs.empty() && runs.back().first == name) {
            ++runs.back().second;
          } else {
            runs.push_back({name, 1});
          }
        }
        const size_t shown = runs.size() < kReportLast ? runs.size() : kReportLast;
        for (size_t i = runs.size() - shown; i < runs.size(); ++i) {
          report += ' ';
          report += runs[i].first ? StripPrefix(runs[i].first) : "?";
          if (runs[i].second > 1) {
            report += fmt::format("*{}", runs[i].second);
          }
        }
        // A thread this busy is in a loop; its whole recent window, collapsed, shows
        // the loop's shape (what it calls, and in what order).
        if (since > 1000000 && runs.size() > shown) {
          report += "\n      window:";
          for (const auto& run : runs) {
            report += ' ';
            report += run.first ? StripPrefix(run.first) : "?";
            if (run.second > 1) {
              report += fmt::format("*{}", run.second);
            }
          }
        }
        // The newest entry's detail - the wait's handle - while that entry is newest.
        if (total && ring->detail_at.load(std::memory_order_relaxed) == total) {
          report += fmt::format(" (on {:08X})", ring->detail.load(std::memory_order_relaxed));
        }
      }
    }
    // BUG-084: a log line is formatted into a 64 KiB thread buffer, and a report longer
    // than that killed the process during area loads (the logger copied the untruncated
    // length past the buffer's end; the logger now clamps it, tools/assemble_runtime.py).
    // Either report is logged in pieces of at most kReportPiece characters, split at a
    // line, so none is cut short.
    if (!report.empty()) {
      LogInPieces("guest threads, the last 5 s:", report);
    }
    std::string sync;
    warden::RecentSyncReport(sync);
    if (!sync.empty()) {
      LogInPieces("signals and blocking waits since the last report:", sync);
    }
    for (pthread_t thread : to_sample) {
      pthread_kill(thread, kSampleSignal);
    }
  }
}

}  // namespace

extern "C" void warden_guest_trace(const char* function) {
  Ring& ring = ThisRing();
  const uint32_t next = ring.next.load(std::memory_order_relaxed);
  ring.names[next % kRing] = function;
  ring.next.store(next + 1, std::memory_order_relaxed);
}

extern "C" void warden_guest_trace_detail(uint32_t detail) {
  Ring& ring = ThisRing();
  ring.detail.store(detail, std::memory_order_relaxed);
  ring.detail_at.store(ring.next.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

namespace warden {

void SetGuestTraceThreadName(const std::string& name) {
  Ring& ring = ThisRing();
  std::lock_guard<std::mutex> lock(g_rings_mutex);
  ring.thread_name = name;
}

void SampleGuestThreadsNow(const char* why) {
  std::vector<pthread_t> threads;
  {
    std::lock_guard<std::mutex> lock(g_rings_mutex);
    for (Ring* ring : g_rings) {
      if (ring->alive && !ring->thread_name.empty()) {
        ring->sample_with_ring.store(true, std::memory_order_release);
        threads.push_back(ring->thread);
      }
    }
  }
  XELOGI("sampling {} guest threads: {}", threads.size(), why);
  for (pthread_t thread : threads) {
    pthread_kill(thread, kSampleSignal);
  }
}

void LogGuestTraceNow(const char* why) {
  const Ring& ring = ThisRing();
  const uint32_t total = ring.next.load(std::memory_order_relaxed);
  const uint32_t count = total < kRing ? total : kRing;
  std::vector<std::pair<const char*, uint32_t>> runs;
  for (uint32_t i = 0; i < count; ++i) {
    const char* name = ring.names[(total - count + i) % kRing];
    if (!runs.empty() && runs.back().first == name) {
      ++runs.back().second;
    } else {
      runs.push_back({name, 1});
    }
  }
  std::string text;
  for (const auto& run : runs) {
    text += ' ';
    text += run.first ? StripPrefix(run.first) : "?";
    if (run.second > 1) {
      text += fmt::format("*{}", run.second);
    }
    if (text.size() > 900) {
      XELOGI("guest trace ({}), {} of {} entries, oldest first:{}", why, count, total, text);
      text.clear();
    }
  }
  XELOGI("guest trace ({}), {} of {} entries, oldest first:{}", why, count, total, text);
}

void LogGuestTraceOnFault() {
  const Ring& ring = ThisRing();
  const uint32_t total = ring.next.load(std::memory_order_relaxed);
  const uint32_t count = total < kFaultEntries ? total : kFaultEntries;
  if (!count) {
    __android_log_print(ANDROID_LOG_FATAL, "Warden",
                        "  no guest trace on this thread (WARDEN_GUEST_TRACE off, or no "
                        "guest code ran here)");
    return;
  }
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "  guest functions entered on this thread, the last %u of %u, "
                      "oldest first:", count, total);
  char line[400];
  size_t used = 0;
  int on_line = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const char* name = ring.names[(total - count + i) % kRing];
    const int written = std::snprintf(line + used, sizeof(line) - used, " %s",
                                      name ? StripPrefix(name) : "?");
    if (written < 0) {
      break;
    }
    used += size_t(written);
    if (++on_line == 8 || used > sizeof(line) - 48) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden", "   %s", line);
      used = 0;
      on_line = 0;
      line[0] = 0;
    }
  }
  if (on_line) {
    __android_log_print(ANDROID_LOG_FATAL, "Warden", "   %s", line);
  }
}

}  // namespace warden
