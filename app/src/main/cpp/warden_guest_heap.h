// The guest heap. See warden_guest_heap.cc.
#ifndef WARDEN_GUEST_HEAP_H_
#define WARDEN_GUEST_HEAP_H_

#include <cstdint>
#include <string>

namespace warden {

// Hands out a guest address range. `requested_base` of 0 means "anywhere".
// Returns an NTSTATUS: 0 on success.
uint32_t AllocateGuestMemory(uint32_t requested_base, uint32_t size,
                             uint32_t granularity, uint32_t& out_base,
                             uint32_t& out_size);

// Physical memory, which the GPU reads through the aliased views. Returns a
// *physical* address (0 .. 0x1FFFFFFF); the caller adds the view base it wants.
uint32_t AllocatePhysicalMemory(uint32_t size, uint32_t granularity,
                                uint32_t& out_base, uint32_t& out_size);

// What the physical heap has handed out and what it can still hand out, in bytes.
// For MmQueryStatistics, which has to agree with what AllocatePhysicalMemory will
// actually do - the title sizes its allocations from the answer.
void PhysicalMemoryUsage(uint32_t& in_use, uint32_t& available);

std::string GuestHeapReport();

}  // namespace warden

#endif  // WARDEN_GUEST_HEAP_H_
