// The guest heap, and the first kernel import the title actually needs. Task 5.1.
//
// `NtAllocateVirtualMemory` is the first import Dragon Age calls, and the weak stub
// returning 0 is what killed it: a stub cannot know whether its caller reads 0 as
// success, false or null, so it leaves r3 zero and logs - and here 0 happens to *be*
// STATUS_SUCCESS while the base address it left behind is null. The title then reached
// `HalReturnToFirmware`, which is "reboot the console", four calls later. That is a
// program giving up.
//
// Allocation is cheap here in a way it is not on a console. Guest memory is one flat
// 4 GiB mapping (warden_memory.cc) that is already readable and writable everywhere,
// so "allocating" is handing out an address and recording it - there are no pages to
// commit. What has to be right is the *address*: the title stores guest pointers in
// its own structures and in guest memory, so they have to be plausible 32-bit guest
// addresses in the region the console would have used.
//
// The console's virtual layout, which this follows:
//   0x00000000-0x3FFFFFFF   4 KiB pages
//   0x40000000-0x7FFFFFFF   64 KiB pages
// The guest stack sits at 0x70100000 (warden_guest.cc), so the heap stops below it.

#include <atomic>
#include <cstring>
#include <mutex>

#include "ppc_recomp_shared.h"
#include "warden_guest_heap.h"
#include "xenia/base/logging.h"

namespace warden {

namespace {

// 4 KiB-page region, and the 64 KiB-page region above it. Allocations default to the
// 64 KiB region, which is what a console allocator hands out for anything sizeable.
constexpr uint32_t kSmallPageBase = 0x10000000;
constexpr uint32_t kSmallPageEnd = 0x3FFFFFFF;
constexpr uint32_t kLargePageBase = 0x40000000;
// Stops well below the guest stack at 0x70100000 so the two cannot meet.
constexpr uint32_t kLargePageEnd = 0x6FF00000;

constexpr uint32_t kSmallPageSize = 0x1000;
constexpr uint32_t kLargePageSize = 0x10000;

// NTSTATUS values the title checks.
constexpr uint32_t kStatusSuccess = 0x00000000;
constexpr uint32_t kStatusNoMemory = 0xC0000017;
constexpr uint32_t kStatusInvalidParameter = 0xC000000D;

struct Heap {
  std::mutex mutex;
  uint32_t small_next = kSmallPageBase;
  uint32_t large_next = kLargePageBase;
  uint32_t physical_next = 0;
  uint64_t allocated = 0;
  uint32_t allocations = 0;
};

Heap& TheHeap() {
  static Heap heap;
  return heap;
}

uint32_t RoundUp(uint32_t value, uint32_t granularity) {
  return (value + granularity - 1) & ~(granularity - 1);
}

}  // namespace

uint32_t AllocateGuestMemory(uint32_t requested_base, uint32_t size,
                             uint32_t granularity, uint32_t& out_base,
                             uint32_t& out_size) {
  if (!size) {
    return kStatusInvalidParameter;
  }
  Heap& heap = TheHeap();
  const uint32_t page = granularity ? granularity : kLargePageSize;
  const uint32_t rounded = RoundUp(size, page);
  if (rounded < size) {  // overflowed
    return kStatusNoMemory;
  }

  std::lock_guard<std::mutex> lock(heap.mutex);

  // A requested base is honoured rather than refused. The whole address space is
  // already mapped, so there is nothing to fail - and a title that asks for a
  // specific address usually has a reason, like a fixed-address pool it will later
  // compute offsets into.
  if (requested_base) {
    out_base = requested_base & ~(page - 1);
    out_size = rounded;
    heap.allocated += rounded;
    ++heap.allocations;
    return kStatusSuccess;
  }

  const bool small = page < kLargePageSize;
  uint32_t& cursor = small ? heap.small_next : heap.large_next;
  const uint32_t limit = small ? kSmallPageEnd : kLargePageEnd;
  const uint32_t base = RoundUp(cursor, page);
  if (base > limit || rounded > limit - base) {
    XELOGE("guest heap exhausted: wanted {} bytes at {:08X}, limit {:08X}", rounded,
           base, limit);
    return kStatusNoMemory;
  }
  cursor = base + rounded;
  out_base = base;
  out_size = rounded;
  heap.allocated += rounded;
  ++heap.allocations;
  return kStatusSuccess;
}

// The console had 512 MB shared between CPU and GPU, and the title sizes its last heap
// from what MmQueryStatistics says is left: with only the top half handed out here,
// its first 250 MB of heaps left 21 MB "available", the heap it then made was 15 MB,
// and FMOD - which lives in it - ran out of memory at the title screen (BUG-043).
// Nothing maps physical memory below the title's own allocations (the GPU is only ever
// given addresses from here), so the whole range is handed out, less the first
// megabyte: physical page 0 stays unmapped by this allocator so that an address of 0 -
// the stubbed MmGetPhysicalAddress once produced one - is caught, not served.
constexpr uint32_t kPhysicalBase = 0x00100000;
constexpr uint32_t kPhysicalEnd = 0x1FF00000;

uint32_t AllocatePhysicalMemory(uint32_t size, uint32_t granularity,
                               uint32_t& out_base, uint32_t& out_size) {
  if (!size) {
    return kStatusInvalidParameter;
  }
  Heap& heap = TheHeap();
  const uint32_t page = granularity ? granularity : kSmallPageSize;
  const uint32_t rounded = RoundUp(size, page);
  if (rounded < size) {
    return kStatusNoMemory;
  }
  std::lock_guard<std::mutex> lock(heap.mutex);
  if (!heap.physical_next) {
    heap.physical_next = kPhysicalBase;
  }
  const uint32_t at = RoundUp(heap.physical_next, page);
  if (at > kPhysicalEnd || rounded > kPhysicalEnd - at) {
    XELOGE("physical heap exhausted: wanted {} bytes at {:08X}", rounded, at);
    return kStatusNoMemory;
  }
  heap.physical_next = at + rounded;
  out_base = at;
  out_size = rounded;
  heap.allocated += rounded;
  ++heap.allocations;
  return kStatusSuccess;
}

void PhysicalMemoryUsage(uint32_t& in_use, uint32_t& available) {
  Heap& heap = TheHeap();
  std::lock_guard<std::mutex> lock(heap.mutex);
  const uint32_t next = heap.physical_next ? heap.physical_next : kPhysicalBase;
  in_use = next - kPhysicalBase;
  available = next < kPhysicalEnd ? kPhysicalEnd - next : 0;
}

std::string GuestHeapReport() {
  Heap& heap = TheHeap();
  std::lock_guard<std::mutex> lock(heap.mutex);
  char line[256];
  std::string out = "GUEST HEAP\n";
  std::snprintf(line, sizeof(line), "  %-30s %u\n", "allocations", heap.allocations);
  out += line;
  std::snprintf(line, sizeof(line), "  %-30s %.2f MiB\n", "handed out",
                double(heap.allocated) / double(1u << 20));
  out += line;
  std::snprintf(line, sizeof(line), "  %-30s %08X (4 KiB pages)\n", "next small",
                heap.small_next);
  out += line;
  std::snprintf(line, sizeof(line), "  %-30s %08X (64 KiB pages)\n", "next large",
                heap.large_next);
  out += line;
  return out;
}

}  // namespace warden

// ---------------------------------------------------------------------------
// The imports.
//
// Arguments arrive in ctx.r3 onwards and the result goes in ctx.r3, per the PowerPC
// ABI. Guest pointers are `base + ctx.rN.u32`, and everything the guest reads or
// writes is big-endian.
// ---------------------------------------------------------------------------

namespace {

uint32_t LoadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

void StoreBe32(uint8_t* p, uint32_t value) {
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

// MEM_LARGE_PAGES. The title passes allocation flags through; the only one that
// changes the answer here is the page size it implies.
constexpr uint32_t kMemLargePages = 0x20000000;

}  // namespace

// NTSTATUS NtAllocateVirtualMemory(PVOID* BaseAddress, SIZE_T* RegionSize,
//                                  ULONG AllocationType, ULONG Protect, ULONG unknown)
//
// Both of the first two are in/out: the caller may ask for a base and a size, and the
// kernel writes back what it actually gave. Writing those back is the part that
// matters - the stub left them untouched, so the title read back the null it passed in.
PPC_FUNC(__imp__NtAllocateVirtualMemory) {
  const uint32_t base_pointer = ctx.r3.u32;
  const uint32_t size_pointer = ctx.r4.u32;
  const uint32_t allocation_type = ctx.r5.u32;

  if (!base_pointer || !size_pointer) {
    ctx.r3.u64 = 0xC000000D;  // STATUS_INVALID_PARAMETER
    return;
  }

  uint8_t* base_slot = base + base_pointer;
  uint8_t* size_slot = base + size_pointer;
  const uint32_t requested_base = LoadBe32(base_slot);
  const uint32_t requested_size = LoadBe32(size_slot);
  const uint32_t granularity =
      (allocation_type & kMemLargePages) ? 0x10000u : 0x1000u;

  uint32_t out_base = 0;
  uint32_t out_size = 0;
  const uint32_t status = warden::AllocateGuestMemory(
      requested_base, requested_size, granularity, out_base, out_size);
  if (status == 0) {
    StoreBe32(base_slot, out_base);
    StoreBe32(size_slot, out_size);
  }
  // Logged for now. The next thing anyone debugging guest startup wants to know is
  // what the title asked for and what it got, and inferring that from a crash
  // afterwards is guesswork. Drop this once startup is past the allocator.
  XELOGI("NtAllocateVirtualMemory({:08X}, {:X}, type {:08X}) -> {:08X} {:X} status {:08X}",
         requested_base, requested_size, allocation_type, out_base, out_size, status);
  // Not flushed: the fault handler drains the log before its report (warden_memory.cc),
  // which is what used to need a 10 ms flush after every line here.
  ctx.r3.u64 = status;
}

// NTSTATUS NtFreeVirtualMemory(PVOID* base, PSIZE_T size, ULONG free_type, ULONG debug)
//
// Accepted, and the range kept: this heap does not reuse address space yet, so a
// free is a leak of address space (not of host memory - untouched pages cost nothing,
// and the reservation is 4 GiB). Success rather than the stub's failure, which the
// title's allocator treats as corruption. Counted, so the leak is visible if it ever
// matters.
PPC_FUNC(__imp__NtFreeVirtualMemory) {
  const uint32_t base_slot = ctx.r3.u32;
  const uint32_t size_slot = ctx.r4.u32;
  const uint32_t address = base_slot ? PPC_LOAD_U32(base_slot) : 0;
  if (!address) {
    ctx.r3.u64 = 0xC00000A0u;  // STATUS_MEMORY_NOT_ALLOCATED
    return;
  }
  static std::atomic<uint64_t> freed_bytes{0};
  static std::atomic<uint32_t> frees{0};
  const uint32_t size = size_slot ? PPC_LOAD_U32(size_slot) : 0;
  const uint64_t total = freed_bytes.fetch_add(size) + size;
  const uint32_t count = frees.fetch_add(1) + 1;
  if (count <= 4 || (count & (count - 1)) == 0) {
    XELOGI("NtFreeVirtualMemory({:08X}, {:X}, type {:08X}): kept ({} frees, {} bytes so "
           "far)", address, size, ctx.r5.u32, count, total);
  }
  ctx.r3.u64 = 0;
}
