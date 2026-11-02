// Self-test for the guest memory model. Phase 5.
//
// Two things here are riskier than anything else in the port, and both can be checked
// right now, without any guest code:
//
//   1. Do the physical views genuinely alias? A guest write through 0xA0000000 must be
//      visible through 0xC0000000, 0xE0001000, and to the GPU. If the mapping is
//      private rather than shared, everything still works until the game writes a
//      texture through a different view than the GPU reads it - and then it is stale
//      geometry nobody can reproduce.
//
//   2. Does the fault-based write watch work on Android? The renderer caches guest
//      data and relies on a write-protect fault to learn that it changed. The
//      technique - mprotect the page, catch SIGSEGV, fire the invalidation callbacks,
//      unprotect, return and let the instruction retry - is well understood on
//      desktop, and this is a device where a vendor could have installed its own
//      handler, or where a write to a read-only shared mapping might raise SIGBUS
//      instead.
//
// Both are checked by doing them, not by reading the code. The results go in the
// bring-up report.

#include "warden_memory_check.h"

#include <cstdio>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/memory.h"

namespace warden {

namespace {

// Somewhere harmless and page-aligned. Physical page 1 rather than 0: an address of
// zero is the one value a bug is most likely to produce by accident, so a test that
// passes at zero is the least convincing place to pass.
constexpr uint32_t kProbePhysical = 0x1000;
constexpr uint32_t kViewA = 0xA0000000;
constexpr uint32_t kViewC = 0xC0000000;
constexpr uint32_t kViewE = 0xE0001000;

// Set by the invalidation callback so the test can tell whether it ran.
struct CallbackRecord {
  int calls = 0;
  uint32_t last_address = 0;
  uint32_t last_length = 0;
};

std::pair<uint32_t, uint32_t> OnInvalidate(void* context, uint32_t address,
                                           uint32_t length, bool exact_range) {
  (void)exact_range;
  auto* record = static_cast<CallbackRecord*>(context);
  ++record->calls;
  record->last_address = address;
  record->last_length = length;
  // "If it doesn't matter, return (0, UINT32_MAX)" - it does not, here.
  return {0u, UINT32_MAX};
}

void Line(std::string& out, const char* label, const char* value) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), "  %-32s %s\n", label, value);
  out += buffer;
}

}  // namespace

std::string CheckMemory(xe::Memory& memory) {
  std::string out = "GUEST MEMORY\n";

  uint8_t* base = memory.virtual_membase();
  if (!base) {
    Line(out, "reservation", "FAILED - no guest address space");
    return out;
  }
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "%p", static_cast<void*>(base));
  Line(out, "virtual membase", buffer);
  std::snprintf(buffer, sizeof(buffer), "%p (0xA0000000 view)",
                static_cast<void*>(memory.physical_membase()));
  Line(out, "physical membase", buffer);

  // ---- 1. do the views alias? ----
  volatile uint32_t* through_a =
      reinterpret_cast<volatile uint32_t*>(base + kViewA + kProbePhysical);
  volatile uint32_t* through_c =
      reinterpret_cast<volatile uint32_t*>(base + kViewC + kProbePhysical);
  volatile uint32_t* through_e =
      reinterpret_cast<volatile uint32_t*>(base + kViewE + kProbePhysical);

  constexpr uint32_t kPattern = 0xD4A60A6E;
  *through_a = kPattern;
  const bool c_sees_it = *through_c == kPattern;
  const bool e_sees_it = *through_e == kPattern;
  // And the other direction, so a test that happens to read a stale copy of the right
  // value cannot pass: write a different pattern through C and read it back through A.
  *through_c = ~kPattern;
  const bool a_sees_it = *through_a == ~kPattern;
  *through_a = 0;

  Line(out, "0xA0000000 -> 0xC0000000", c_sees_it ? "aliased" : "NOT ALIASED");
  Line(out, "0xA0000000 -> 0xE0001000", e_sees_it ? "aliased" : "NOT ALIASED");
  Line(out, "0xC0000000 -> 0xA0000000", a_sees_it ? "aliased" : "NOT ALIASED");

  // ---- 2. does the write watch fire? ----
  CallbackRecord record;
  void* handle =
      memory.RegisterPhysicalMemoryInvalidationCallback(OnInvalidate, &record);
  memory.EnablePhysicalMemoryAccessCallbacks(kProbePhysical, 4096, true, false);

  // The page should now be read-only, which the heap's own query should agree with -
  // the renderer asks exactly this before reading a range.
  xe::VirtualHeap* heap = memory.GetPhysicalHeap();
  const bool reported_readonly =
      heap && heap->QueryRangeAccess(kProbePhysical, kProbePhysical + 4095) ==
                  xe::memory::PageAccess::kReadOnly;
  Line(out, "page reported read-only", reported_readonly ? "yes" : "NO");

  // The write that must fault, be handled, and then succeed. If the handler does not
  // work this is where the process dies - which is a clear failure rather than a
  // wrong answer, and is why this runs at startup and not in the middle of a frame.
  *through_a = 0x5A5A5A5A;
  const bool write_succeeded = *through_a == 0x5A5A5A5A;

  Line(out, "watched write completed", write_succeeded ? "yes" : "NO");
  std::snprintf(buffer, sizeof(buffer), "%d", record.calls);
  Line(out, "invalidation callbacks fired", buffer);
  if (record.calls) {
    std::snprintf(buffer, sizeof(buffer), "%08X + %X", record.last_address,
                  record.last_length);
    Line(out, "callback reported", buffer);
  }

  const bool reported_writable =
      heap && heap->QueryRangeAccess(kProbePhysical, kProbePhysical + 4095) ==
                  xe::memory::PageAccess::kReadWrite;
  Line(out, "page writable again", reported_writable ? "yes" : "NO");

  memory.UnregisterPhysicalMemoryInvalidationCallback(handle);
  *through_a = 0;

  const bool all_good = c_sees_it && e_sees_it && a_sees_it && reported_readonly &&
                        write_succeeded && record.calls > 0 && reported_writable;
  Line(out, "RESULT", all_good ? "aliasing and write watch both work"
                               : "SOMETHING IS WRONG - see above");
  if (all_good) {
    XELOGI("guest memory self-test passed");
  } else {
    XELOGE("guest memory self-test FAILED");
  }
  return out;
}

}  // namespace warden
