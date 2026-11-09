// Starting guest code. Task 5.1.
//
// Three things have to be true before the title's entry point can be called, and only
// the first was obvious:
//
//   1. the image is in guest memory (task 5.0e/f, done and verified);
//   2. **the indirect-call table is built**. XenonRecomp does not resolve indirect
//      calls at translation time - it emits
//        PPC_LOOKUP_FUNC(base, y) = *(PPCFunc**)(base + IMAGE_BASE + IMAGE_SIZE +
//                                                (y - CODE_BASE) * 2)
//      so every `bctrl` reads a host function pointer *out of guest memory*. That
//      table does not exist until the runtime writes it, and nothing says so: the
//      first indirect call would simply jump through whatever happened to be there.
//      It is 33 MB at 0x83400000, two bytes of table per byte of guest code.
//   3. a thread with a PPCContext and a stack.
//
// Expect the first call to abort on an unimplemented kernel import. That is the point:
// the 251 weak stubs count their calls and log the first, so the import report names
// what the title actually reaches, in order.

#include "warden_guest.h"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "warden_affinity.h"
#include "warden_guest_heap.h"
#include "warden_guest_info.h"
#include "warden_suspend.h"
#include "warden_trace.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

namespace warden {

namespace {

// Where the indirect-call table lives, per PPC_LOOKUP_FUNC in ppc_context.h: right after
// the image. From the loaded game's code (PLAN 12.2) rather than this build's
// ppc_config.h - the title update decides the image's size (Title Update 6's is 64 KB
// smaller than 7's), and the game's code computes the table's place from its own.
uint64_t FunctionTableBase() { return warden::game::ImageBase() + warden::game::ImageSize(); }
uint64_t FunctionTableSize() { return warden::game::CodeSize() * 2; }

// The guest stack. Below the image, in the part of the address space the title's own
// allocator does not own, and well clear of both the image and the table above it.
// The XEX header asks for 256 KB; honoured rather than guessed.
constexpr uint32_t kStackTop = 0x70100000;

// One context per guest thread. thread_local because a guest thread's registers are
// its own, and because AttachGuestThread is documented as idempotent.
thread_local PPCContext* t_context = nullptr;
// Where that thread's stack starts. Kept beside the context rather than read from
// kStackTop by the fault handler, so the stack walk stays right once threads stop
// sharing one stack top.
thread_local uint32_t t_stack_top = 0;

// Remembered when the table is built, so xe::dao_host::AttachGuestThread can attach a
// thread without being handed the Memory - the GPU calls it from its own threads and
// has no way to pass one. There is exactly one guest address space in the process.
xe::Memory* g_memory = nullptr;

// Makes the title's code read-only, as the console's hypervisor keeps it: a title has
// no business writing its own code, so a write there is corruption, and faulting on it
// names the writer at the moment it happens rather than leaving the damage to be found
// when the code next runs. The fault handler reports such a write with the guest
// registers and call chain (warden_memory.cc).
//
// Whole pages only, rounded inward, so a page the code shares with data at either end
// stays writable. Called just before guest code starts, after the image is loaded, the
// title update applied and the call table built - nothing host-side writes there after.
void ProtectGuestCode(xe::Memory& memory) {
  const auto page = uint32_t(sysconf(_SC_PAGESIZE));
  const uint32_t first = uint32_t((warden::game::CodeBase() + page - 1) & ~uint64_t(page - 1));
  const uint32_t end = uint32_t((warden::game::CodeBase() + warden::game::CodeSize()) & ~uint64_t(page - 1));
  if (end <= first) {
    return;
  }
  if (mprotect(memory.virtual_membase() + first, end - first, PROT_READ) != 0) {
    XELOGE("could not make the title's code read-only: {}", strerror(errno));
    return;
  }
  XELOGI("title code {:08X}-{:08X} is read-only from here, as on the console", first,
         end);
}

}  // namespace

bool BuildFunctionTable(xe::Memory& memory, GuestBoot& result) {
  uint8_t* base = memory.virtual_membase();
  if (!base) {
    result.detail = "no guest memory";
    return false;
  }
  g_memory = &memory;
  uint8_t* table = base + FunctionTableBase();
  // Zeroed first: a slot left holding stale bytes would be jumped through on an
  // indirect call to an address that was never translated, and a null is at least a
  // diagnosable crash.
  std::memset(table, 0, size_t(FunctionTableSize()));

  const GuestImage image = DescribeGuestImage();
  size_t written = 0;
  size_t skipped = 0;
  for (size_t i = 0; i < image.function_count; ++i) {
    const PPCFuncMapping& entry = warden::game::Mappings()[i];
    const uint32_t address = uint32_t(entry.guest);
    if (address < warden::game::CodeBase() ||
        uint64_t(address - warden::game::CodeBase()) * 2 + sizeof(PPCFunc*) > FunctionTableSize()) {
      ++skipped;
      continue;
    }
    PPCFunc** slot =
        reinterpret_cast<PPCFunc**>(table + (uint64_t(address - warden::game::CodeBase()) * 2));
    *slot = entry.host;
    ++written;
  }
  result.table_entries = written;
  result.table_skipped = skipped;
  XELOGI("indirect-call table: {} entries at {:08X} ({} MB), {} outside the code range",
         written, uint32_t(FunctionTableBase()), FunctionTableSize() >> 20, skipped);
  return written > 0;
}

PPCContext* CurrentGuestContext() { return t_context; }

uint32_t CurrentGuestStackTop() { return t_stack_top; }

namespace {

// The default when a thread asks for no particular size: what the XEX header asks for
// the main thread, 256 KB. Stacks come from the heap's 64 KiB-page region, so that is
// also the granularity and the minimum.
constexpr uint32_t kDefaultStackSize = 0x40000;
constexpr uint32_t kStackGranularity = 0x10000;

// The thread's kernel structures (warden_guest.h). thread_local like the context.
thread_local uint32_t t_pcr = 0;
thread_local uint32_t t_kthread = 0;
thread_local uint32_t t_tls = 0;

// The executable's thread-local storage parameters, from its XEX header.
uint32_t g_tls_slot_count = 64;
uint32_t g_tls_raw_data_address = 0;
uint32_t g_tls_data_size = 0;
uint32_t g_tls_raw_data_size = 0;

// Thread ids for threads the title did not create through ExCreateThread (the boot
// thread, the GPU's), above the range those get.
std::atomic<uint32_t> g_next_host_thread_id{0xF000};

void StoreBe32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, sizeof(value));
}

// Fills a thread's block - PCR, KTHREAD, TLS - as the console's kernel would have.
// Offsets are Xenia's X_KPCR / X_KTHREAD (kernel/xthread.h); only the fields the
// title has been seen to read, plus what names the thread, are set.
void FillThreadBlock(uint8_t* base, uint32_t block, uint32_t stack_top, uint32_t stack_size,
                     uint32_t thread_id, uint32_t start_address) {
  const uint32_t pcr = block;
  const uint32_t kthread = block + kThreadBlockKThread;
  const uint32_t tls = block + kThreadBlockTls;
  std::memset(base + block, 0, kThreadBlockSize);
  // PCR
  StoreBe32(base, pcr + 0x000, tls);                       // tls_ptr
  StoreBe32(base, pcr + 0x030, 0);                         // pcr_ptr, 64-bit
  StoreBe32(base, pcr + 0x034, pcr);
  StoreBe32(base, pcr + 0x070, stack_top);                 // stack_base_ptr
  StoreBe32(base, pcr + 0x074, stack_top - stack_size);    // stack_end_ptr
  StoreBe32(base, pcr + 0x100, kthread);                   // prcb_data.current_thread
  base[pcr + 0x10C] = 0;                                   // current_cpu
  StoreBe32(base, pcr + 0x2A8, pcr + 0x100);               // prcb
  // KTHREAD: a dispatcher object of type 6 (thread), signalled when it finishes.
  base[kthread + 0] = 6;
  StoreBe32(base, kthread + 0x05C, stack_top);             // stack_base
  StoreBe32(base, kthread + 0x060, stack_top - stack_size);  // stack_limit
  base[kthread + 0x070] = 8;                               // priority: normal
  base[kthread + 0x072] = 1;                               // process_type: title
  base[kthread + 0x073] = 1;
  StoreBe32(base, kthread + 0x14C, thread_id);
  StoreBe32(base, kthread + 0x150, start_address);
  // TLS: the static block the executable's .tls section initialises, then the
  // KeTlsAlloc slots, zero. The copy comes from the loaded image.
  if (g_tls_raw_data_address && g_tls_raw_data_size) {
    const uint32_t copy = std::min(g_tls_raw_data_size, g_tls_data_size);
    std::memcpy(base + tls, base + g_tls_raw_data_address, copy);
  }
}

// Attaches with a given stack top. Every thread gets its own: two guest threads on
// one stack would push frames over each other (BUG-031). The block is the thread's
// kernel structures, allocated here unless the creator already did (ExCreateThread,
// which hands the KTHREAD's address out before the thread runs).
bool AttachWithStack(const char* name, xe::Memory& memory, uint32_t stack_top,
                     uint32_t stack_size, uint32_t block, uint32_t thread_id,
                     uint32_t start_address) {
  if (t_context) {
    return true;  // idempotent, as dao_host.h says
  }
  uint8_t* base = memory.virtual_membase();
  if (!base) {
    return false;
  }
  if (!block) {
    block = AllocateGuestThreadBlock();
    if (!block) {
      XELOGE("no guest memory for thread {}'s kernel structures", name ? name : "?");
      return false;
    }
  }
  if (!thread_id) {
    thread_id = g_next_host_thread_id.fetch_add(4, std::memory_order_relaxed);
  }
  FillThreadBlock(base, block, stack_top, stack_size, thread_id, start_address);
  t_pcr = block;
  t_kthread = block + kThreadBlockKThread;
  t_tls = block + kThreadBlockTls;
  t_context = new PPCContext{};
  // The stack is guest memory, not a host allocation: the title stores guest
  // addresses into it and hands them to its own code.
  t_context->r1.u64 = stack_top;
  // r13 is the PCR on the console, and the title reads through it: its TLS, its
  // KTHREAD (thread id), the core it is on.
  t_context->r13.u64 = t_pcr;
  t_stack_top = stack_top;
  // msr has a default in the struct; everything else starts zero, which is what a
  // thread entered from the loader sees.
  XELOGI("guest thread attached: {} (stack top {:08X}, pcr {:08X}, thread id {:X})",
         name ? name : "?", stack_top, t_pcr, thread_id);
  SetGuestTraceThreadName(name ? name : "?");
  return true;
}

// A fresh stack from the guest heap, for a thread that is not the boot thread.
uint32_t AllocateStack(uint32_t size) {
  if (!size) {
    size = kDefaultStackSize;
  }
  size = (size + kStackGranularity - 1) & ~(kStackGranularity - 1);
  uint32_t base = 0;
  uint32_t got = 0;
  if (AllocateGuestMemory(0, size, kStackGranularity, base, got) != 0) {
    return 0;
  }
  return base + got;
}

}  // namespace

bool AttachCurrentThreadToGuest(const char* name, xe::Memory& memory) {
  if (t_context) {
    return true;
  }
  const uint32_t top = AllocateStack(kDefaultStackSize);
  if (!top) {
    XELOGE("no guest memory for a stack for thread {}", name ? name : "?");
    return false;
  }
  return AttachWithStack(name, memory, top, kDefaultStackSize, 0, 0, 0);
}

uint8_t* GuestMemoryBase() { return g_memory ? g_memory->virtual_membase() : nullptr; }

xe::Memory* GuestMemory() { return g_memory; }

void SetGuestTlsInfo(uint32_t slot_count, uint32_t raw_data_address, uint32_t data_size,
                     uint32_t raw_data_size) {
  if (slot_count) {
    g_tls_slot_count = slot_count;
  }
  g_tls_raw_data_address = raw_data_address;
  g_tls_data_size = data_size;
  g_tls_raw_data_size = raw_data_size;
  XELOGI("guest TLS: {} slots, static block {} bytes from {:08X} ({} initialised)",
         g_tls_slot_count, data_size, raw_data_address, raw_data_size);
}

uint32_t GuestTlsSlotCount() { return g_tls_slot_count; }
uint32_t GuestTlsDataSize() { return g_tls_data_size; }

uint32_t AllocateGuestThreadBlock() {
  uint32_t block = 0;
  uint32_t got = 0;
  if (AllocateGuestMemory(0, kThreadBlockSize, 0x1000, block, got) != 0 || !block) {
    return 0;
  }
  if (uint8_t* base = GuestMemoryBase()) {
    std::memset(base + block, 0, kThreadBlockSize);
  }
  return block;
}

uint32_t CurrentGuestPcr() { return t_pcr; }
uint32_t CurrentGuestKThread() { return t_kthread; }
uint32_t CurrentGuestTls() { return t_tls; }

uint32_t RunGuestThread(const char* name, uint32_t stack_size, uint32_t xapi_thread_startup,
                        uint32_t start_address, uint32_t start_context,
                        uint32_t guest_block, uint32_t thread_id) {
  if (!g_memory) {
    XELOGE("guest thread {}: no guest memory", name);
    return 0;
  }
  const uint32_t rounded_size =
      ((stack_size ? stack_size : kDefaultStackSize) + kStackGranularity - 1) &
      ~(kStackGranularity - 1);
  const uint32_t top = AllocateStack(stack_size);
  if (!top || !AttachWithStack(name, *g_memory, top, rounded_size, guest_block, thread_id,
                               start_address)) {
    XELOGE("guest thread {}: could not attach", name);
    return 0;
  }
  RegisterTitleThread();  // BUG-119: a suspension can hold it if it spins
  uint8_t* base = g_memory->virtual_membase();
  PPCContext* ctx = CurrentGuestContext();

  // The entry is whichever the title gave: the XAPI trampoline, which takes the real
  // start as its arguments and ends in ExTerminateThread, or the start itself.
  const uint32_t entry = xapi_thread_startup ? xapi_thread_startup : start_address;
  PPCFunc* function = entry ? PPC_LOOKUP_FUNC(base, entry) : nullptr;
  if (!function) {
    XELOGE("guest thread {}: entry {:08X} is not a translated function", name, entry);
    return 0;
  }
  if (xapi_thread_startup) {
    ctx->r3.u64 = start_address;
    ctx->r4.u64 = start_context;
  } else {
    ctx->r3.u64 = start_context;
  }
  XELOGI("guest thread {} starts at {:08X} (stack top {:08X})", name, entry, top);
  xe::FlushLog();
  function(*ctx, base);
  return ctx->r3.u32;
}

bool AttachGuestThreadForHost(const char* name) {
  if (!g_memory) {
    return false;
  }
  return AttachCurrentThreadToGuest(name, *g_memory);
}

GuestBoot StartGuest(uint32_t entry_point, xe::Memory& memory) {
  GuestBoot result;
  result.entry_point = entry_point;

  if (!BuildFunctionTable(memory, result)) {
    if (result.detail.empty()) {
      result.detail = "could not build the indirect-call table";
    }
    return result;
  }

  // The entry point must itself have been translated, or there is nothing to call.
  const GuestImage image = DescribeGuestImage();
  PPCFunc* entry = nullptr;
  for (size_t i = 0; i < image.function_count; ++i) {
    if (uint32_t(warden::game::Mappings()[i].guest) == entry_point) {
      entry = warden::game::Mappings()[i].host;
      break;
    }
  }
  if (!entry) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer),
                  "entry point %08X is not in the recompiled function table",
                  entry_point);
    result.detail = buffer;
    XELOGE("{}", result.detail);
    return result;
  }
  result.entry_found = true;
  result.detail = "ready to call";
  return result;
}

void CallEntryPoint(uint32_t entry_point, xe::Memory& memory) {
  // The prime core, as the device profile asks: this is the thread that runs guest
  // code, and sched_setaffinity is accepted on this device (verified in 4.8).
  Affinity::Get().PinCurrentThreadToPrime();

  // The boot thread keeps the stack the XEX header asks for, at its fixed address;
  // every other thread gets one from the heap.
  if (!AttachWithStack("guest-main", memory, kStackTop, kDefaultStackSize, 0, 0xFFC,
                       entry_point)) {
    XELOGE("could not attach the boot thread to the guest");
    return;
  }
  RegisterTitleThread();  // BUG-119: a suspension can hold it if it spins
  PPCContext* ctx = CurrentGuestContext();
  if (!ctx) {
    XELOGE("no guest context after attaching");
    return;
  }

  PPCFunc* entry = nullptr;
  const GuestImage image = DescribeGuestImage();
  for (size_t i = 0; i < image.function_count; ++i) {
    if (uint32_t(warden::game::Mappings()[i].guest) == entry_point) {
      entry = warden::game::Mappings()[i].host;
      break;
    }
  }
  if (!entry) {
    XELOGE("entry point {:08X} is not in the function table", entry_point);
    return;
  }

  ProtectGuestCode(memory);

  // Everything after this line is the title's own code. The first unimplemented
  // kernel import aborts the process, by design and loudly - its name is what this
  // whole exercise is for, and warden_imports.cc has already logged it by then.
  XELOGI("calling the title entry point {:08X} - guest code starts here", entry_point);
  // Everything logged up to here must survive what follows.
  xe::FlushLog();
  entry(*ctx, memory.virtual_membase());
  // Reaching this would mean the entry point returned, which the title's does not do
  // on real hardware: it runs the game and exits through the kernel.
  XELOGW("the entry point returned, which is unexpected");
}

std::string GuestBoot::Describe() const {
  char line[256];
  char value[160];
  std::string out = "GUEST BOOT\n";
  auto row = [&](const char* label, const char* text) {
    std::snprintf(line, sizeof(line), "  %-30s %s\n", label, text);
    out += line;
  };

  std::snprintf(value, sizeof(value), "%zu entries at %08X (%llu MB)", table_entries,
                uint32_t(warden::game::ImageBase() + warden::game::ImageSize()),
                (unsigned long long)(warden::game::CodeSize() * 2) >> 20);
  row("indirect-call table", table_entries ? value : "NOT BUILT");
  if (table_skipped) {
    std::snprintf(value, sizeof(value), "%zu outside the code range", table_skipped);
    row("skipped", value);
  }
  std::snprintf(value, sizeof(value), "%08X %s", entry_point,
                entry_found ? "found in the function table" : "NOT FOUND");
  row("entry point", value);
  row("state", detail.empty() ? "?" : detail.c_str());
  if (!called) {
    out += "  Not called yet. The first unimplemented kernel import will abort the\n"
           "  process by design - its name is the next thing to implement, and the\n"
           "  import report lists everything reached in order.\n";
  }
  return out;
}

}  // namespace warden
