// Kernel import accounting, and the imports that are real. Task 5.1.
//
// Everything not defined here gets a weak stub from build/gen/guest_import_stubs.cc.
// To implement an import, write it here: a strong definition wins over the weak one at
// link time, so there is no registry to update and no way for the two to disagree.

#include "warden_imports.h"

#include "warden_trace.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_suspend.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

struct ImportCounters {
  // Call counts, indexed as kImportNames is. Atomic and relaxed: these are written
  // from guest threads thousands of times a frame, and an exact count is not worth a
  // lock on that path - the order imports are first seen in is what matters.
  std::vector<std::atomic<uint32_t>> counts;
  std::mutex order_mutex;
  // Indices in the order they were first called.
  std::vector<size_t> first_seen;

  ImportCounters() : counts(kImportCount) {
    for (auto& count : counts) {
      count.store(0, std::memory_order_relaxed);
    }
  }
};

ImportCounters& Counters() {
  static ImportCounters counters;
  return counters;
}

}  // namespace

void NoteImportCall(size_t index) {
  if (index >= kImportCount) {
    return;
  }
  // Into the thread's guest trace, so a thread spinning on a stub shows it by name in
  // the periodic `guest threads` report and in a fault report.
  warden_guest_trace(kImportNames[index]);
  ImportCounters& counters = Counters();
  const uint32_t previous =
      counters.counts[index].fetch_add(1, std::memory_order_relaxed);
  if (previous != 0) {
    return;
  }
  // First call only: everything below happens once per import, ever.
  {
    std::lock_guard<std::mutex> lock(counters.order_mutex);
    counters.first_seen.push_back(index);
  }
  // Said with what the stub hands back, because that decides the title's next step:
  // a failure status sends it down its error path, a 0 for a value is just a 0.
  XELOGW("unimplemented import reached: {} - returns {}", kImportNames[index],
         kImportFailures[index]);
  // Not flushed here. Xenia's logger buffers, and a line left in its buffer when the
  // process dies is lost - one run showed 14 imports and the next none, with no code
  // between them that could change that - but xe::FlushLog() sleeps 10 ms, and a
  // flush after every logged guest call made each thread's pace depend on how much it
  // logged (BUG-034). The fault handler drains the log once before its report instead.
}

std::string ImportReport() {
  ImportCounters& counters = Counters();
  std::vector<size_t> order;
  {
    std::lock_guard<std::mutex> lock(counters.order_mutex);
    order = counters.first_seen;
  }

  char line[256];
  std::string out = "KERNEL IMPORTS\n";
  std::snprintf(line, sizeof(line), "  %-30s %zu\n", "declared by the title",
                kImportCount);
  out += line;
  std::snprintf(line, sizeof(line), "  %-30s %zu\n", "reached so far",
                order.size());
  out += line;
  if (order.empty()) {
    out += "  nothing has called one yet - no guest code has run\n";
    return out;
  }
  out += "  in the order first called:\n";
  // Capped: once the game is running this list grows past anything worth reading off
  // a handheld's screen, and the full picture is in logcat anyway.
  size_t shown = 0;
  for (size_t index : order) {
    if (shown++ >= 40) {
      std::snprintf(line, sizeof(line), "    ... and %zu more\n",
                    order.size() - 40);
      out += line;
      break;
    }
    std::snprintf(line, sizeof(line), "    %-40s %u calls\n", kImportNames[index],
                  counters.counts[index].load(std::memory_order_relaxed));
    out += line;
  }
  return out;
}

}  // namespace warden

// ---------------------------------------------------------------------------
// Imports that are real.
//
// Each one here is implemented because it can be, correctly, without guessing at a
// convention - not because it was next in a list.
// ---------------------------------------------------------------------------

// Not an import but a XenonRecomp runtime hook: the guest reads the Xenon's timebase
// register through PPC_QUERY_TIMEBASE(). Xenia's Clock already provides guest ticks
// and is already compiled in, so using it means the guest and the GPU agree about what
// time it is - which they would not if this invented its own epoch.
//
// Phase 2's differential harness deliberately aborted here, because any function
// reaching the timebase was excluded from comparison and arriving meant a filter bug.
// At runtime it is simply needed. Less the time the guest spent suspended, so it stands
// still while the app is in the background (warden_suspend.cc, BUG-057).
// Not extern "C": ppc_context.h declares it with C++ linkage, and the recompiled code
// references the mangled name.
uint64_t PPCQueryTimebase() noexcept {
  return warden::GuestTimebase();
}
