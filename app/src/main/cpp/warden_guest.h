// Starting guest code. See warden_guest.cc.
#ifndef WARDEN_GUEST_H_
#define WARDEN_GUEST_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PPCContext;

namespace xe {
class Memory;
}

namespace warden {

struct GuestBoot {
  size_t table_entries = 0;
  size_t table_skipped = 0;
  uint32_t entry_point = 0;
  bool entry_found = false;
  // Whether the entry point has actually been invoked. Deliberately separate from
  // entry_found: everything can be ready and the call still not have been made.
  bool called = false;
  std::string detail;

  std::string Describe() const;
};

// Fills the indirect-call table in guest memory from the recompiled function mapping.
// Nothing works before this: every `bctrl` the title executes reads a host function
// pointer out of that table.
bool BuildFunctionTable(xe::Memory& memory, GuestBoot& result);

// For xe::dao_host::AttachGuestThread, which the GPU calls from its own threads and
// which has no way to be handed a Memory. Uses the one the table was built against.
bool AttachGuestThreadForHost(const char* name);

// Every guest thread has a block of guest memory the console's kernel would have
// given it, because the title reads these through r13 without asking: the PCR at
// +0 (tls_ptr at +0, the current KTHREAD at +0x100, the current core at +0x10C), its
// KTHREAD at +kThreadBlockKThread (thread id at +0x14C), and its thread-local storage
// at +kThreadBlockTls - the executable's static TLS block, then the KeTlsAlloc slots.
// Layouts are Xenia's X_KPCR and X_KTHREAD (kernel/xthread.h).
constexpr uint32_t kThreadBlockSize = 0x2000;
constexpr uint32_t kThreadBlockKThread = 0x400;
constexpr uint32_t kThreadBlockTls = 0x1000;
// From the XEX header, before any thread attaches (Runtime::LoadTitle).
void SetGuestTlsInfo(uint32_t slot_count, uint32_t raw_data_address, uint32_t data_size,
                     uint32_t raw_data_size);
uint32_t GuestTlsSlotCount();
uint32_t GuestTlsDataSize();
// A zeroed block for a thread about to be created (ExCreateThread needs the KTHREAD's
// address before the thread runs); 0 if the heap is out of memory.
uint32_t AllocateGuestThreadBlock();
// The calling guest thread's structures; 0 on a thread that has not attached.
uint32_t CurrentGuestPcr();
uint32_t CurrentGuestKThread();
uint32_t CurrentGuestTls();

// Gives the calling thread a PPCContext and a guest stack of its own, carved from the
// guest heap. Idempotent.
bool AttachCurrentThreadToGuest(const char* name, xe::Memory& memory);
PPCContext* CurrentGuestContext();
// The guest memory base the table was built against, or null before that.
uint8_t* GuestMemoryBase();
// The guest memory itself, for address translation; null before the table is built.
xe::Memory* GuestMemory();

// Runs a guest thread's code on the calling host thread, as ExCreateThread asks for it
// (task 5.1h): attaches with a stack of `stack_size` bytes, then calls the XAPI
// trampoline with (start_address, start_context) if there is one, or start_address
// with start_context if not. Returns the guest's r3 when the code returns - the exit
// code for a raw thread. The thread may instead end inside through
// ExTerminateThread, which does not come back here.
uint32_t RunGuestThread(const char* name, uint32_t stack_size, uint32_t xapi_thread_startup,
                        uint32_t start_address, uint32_t start_context,
                        uint32_t guest_block = 0, uint32_t thread_id = 0);
// The top of the calling thread's guest stack, or 0 if it is not a guest thread. The
// fault handler's stack walk bounds every read by it.
uint32_t CurrentGuestStackTop();

// Calls the title's entry point on the calling thread. Does not return until the
// guest does - which it will not: the first unimplemented kernel import aborts the
// process. Run this on a thread of its own, and only after the report has been
// logged, or the run produces nothing to read.
void CallEntryPoint(uint32_t entry_point, xe::Memory& memory);

// Builds the table and checks the entry point is callable. Does not call it.
GuestBoot StartGuest(uint32_t entry_point, xe::Memory& memory);

}  // namespace warden

#endif  // WARDEN_GUEST_H_
