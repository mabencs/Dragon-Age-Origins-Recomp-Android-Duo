// Dumping guest registers from a fault handler. See warden_guest_registers.cc.
#ifndef WARDEN_GUEST_REGISTERS_H_
#define WARDEN_GUEST_REGISTERS_H_

#include <cstdint>

namespace warden {

// Whether `host_address` is a read of the indirect-call table, and if so which guest
// address's slot it was. A null indirect call faults far below the guest base and
// looks like a wild pointer; the offset is invertible, so the address is recoverable
// without a trap stub or any extra memory. See the comment in the .cc.
bool DescribeNullIndirectCall(const void* host_address, const void* membase,
                              uint32_t& guest_address_out);

// Whether a guest address is in the title's code - the range ProtectGuestCode makes
// read-only, so a fault there is a write into the code.
bool IsGuestCode(uint32_t guest_address);

// The entry point of the recompiled function containing a guest code address - the
// `sub_XXXXXXXX` a log reader looks up - or 0 when none does. A linear scan of the
// function table; for logs that happen a few times, not per call.
uint32_t GuestFunctionContaining(uint32_t guest_address);

// Prints the current thread's guest registers with __android_log_print, or says there
// is no guest context. Safe to call from a signal handler: no locks, no allocation,
// and nothing that can fault if there is no context.
void LogGuestRegistersOnFault();

// Walks the current guest thread's stack and prints its call chain: each return
// address, the recompiled function it is in, and what that function called. Neither
// the host backtrace nor ctx.lr identifies the faulting frame (RESUMING.md §2); this
// does. Signal-safe on the same terms as above - it reads guest memory only between
// r1 and the thread's stack top, all inside the reservation where every page is
// readable.
void LogGuestCallChainOnFault(const void* membase);

}  // namespace warden

#endif  // WARDEN_GUEST_REGISTERS_H_
