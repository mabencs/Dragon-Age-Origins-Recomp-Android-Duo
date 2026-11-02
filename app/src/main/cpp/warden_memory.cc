// xe::Memory - guest memory for the GPU. Phase 5.
//
// `xenia/memory.h` in the grafted tree is not Xenia's: DAORecomp replaced Xenia's
// 2,444-line memory.cc with a 177-line interface and implements it in their own
// runtime. Its own comment is the specification, and this follows it:
//
//   one 4 GB host reservation where guest address A lives at membase + A, with the
//   three physical views (0xA0000000, 0xC0000000, 0xE0000000 at +0x1000) mapped onto
//   one 512 MB section
//
// That layout is also what the recompiled code needs. XenonRecomp emits
// `PPC_FUNC(x) void x(PPCContext&, uint8_t* base)` and every guest access is
// `base + address`, so the GPU and the guest must agree on one flat base - which they
// do, because this *is* that base.
//
// The aliasing is real, not simulated: the physical section is a memfd mapped three
// times with MAP_SHARED, so a guest write through 0xA0000000 is visible through
// 0xC0000000 and to the GPU, as on hardware. Copying between views instead would be
// wrong in a way that only shows up as intermittently stale geometry.
//
// Only four of this interface's methods are referenced by the renderer
// (GetPhysicalHeap, EnablePhysicalMemoryAccessCallbacks and the two invalidation
// callback registrations), but the rest are implemented where they are cheap, because
// a header with half its methods missing is a trap for whoever links the next scope.

#include <dlfcn.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_guest.h"
#include "warden_guest_registers.h"
#include "warden_threads.h"
#include "warden_trace.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace xe {

namespace {

// The guest address space. 4 GiB of reservation on a 64-bit process costs address
// space, not memory: pages are backed when first touched.
constexpr size_t kGuestAddressSpace = 0x100000000ull;
// Physical memory on the console. The 360 had 512 MB shared between CPU and GPU.
constexpr uint32_t kPhysicalSize = 0x20000000;
// Where the physical section is visible. The 0xE0000000 view is offset by 0x1000 on
// hardware, and the guest relies on that: an address there is physical + 0x1000.
constexpr uint32_t kViewA = 0xA0000000;
constexpr uint32_t kViewC = 0xC0000000;
constexpr uint32_t kViewE = 0xE0000000;
constexpr uint32_t kViewEOffset = 0x1000;

// Watches are tracked per 4 KiB page, the smallest granularity mprotect offers.
constexpr uint32_t kPageSize = 4096;
constexpr uint32_t kPageCount = kPhysicalSize / kPageSize;

struct InvalidationCallback {
  Memory::PhysicalMemoryInvalidationCallback callback;
  void* context;
};

// The signal handler needs to find the Memory instance, and a signal handler cannot
// be given a context argument. There is exactly one guest address space in a process,
// so a single pointer is not a limitation being accepted - it is the actual shape of
// the thing.
Memory* g_memory = nullptr;
struct sigaction g_previous_sigsegv = {};
struct sigaction g_previous_sigbus = {};
bool g_handler_installed = false;

}  // namespace

// The state mutex, with a note of whether this thread holds it: the fault handler
// takes it too, and must not block on itself - a fault raised while this thread holds
// the lock (there is no such path, but the cost of being wrong is a silent deadlock)
// is reported instead. Any other thread's hold is waited out: the GPU thread
// protecting a range of pages while a guest thread writes one of them is the normal
// case, not an error, and a try_lock there turned a served watch into a crash
// (BUG-038's third act: SEGV_ACCERR on the Update thread filling freed memory).
thread_local bool t_holds_state_mutex = false;
struct StateLock {
  explicit StateLock(std::mutex& mutex) : lock_(mutex) { t_holds_state_mutex = true; }
  ~StateLock() { t_holds_state_mutex = false; }
  std::lock_guard<std::mutex> lock_;
};

// Served write-watch faults, for the GPU status line.
std::atomic<uint32_t> g_watched_write_faults{0};
std::atomic<uint32_t> g_watched_write_races{0};  // BUG-044: faults retried, not served
// For warden_memory_host_write, which cannot name Memory::State: the pieces of it the
// notification needs, handed out when the memory is made.
struct HostWriteHooks {
  std::mutex* mutex = nullptr;
  std::vector<uint8_t>* page_watched = nullptr;
  std::vector<InvalidationCallback>* callbacks = nullptr;
};
HostWriteHooks g_host_write_hooks;
std::atomic<uint32_t> g_host_writes_served{0};

// Bring-up (BUG-039): write-watch faults landing in up to eight physical ranges - the
// memory probes in warden_gpu.cc - so a texture the title rewrites can be checked for
// whether its writes are seen. Signal-handler safe: atomics only.
struct ProbeRange {
  std::atomic<uint32_t> address{0};
  std::atomic<uint32_t> length{0};
  std::atomic<uint32_t> faults{0};
};
ProbeRange g_probe_ranges[8];

struct Memory::State {
  // The whole reservation, so it can be released as one.
  void* reservation = nullptr;
  size_t reservation_size = 0;
  // memfd backing the physical section, mapped into each view.
  int physical_fd = -1;

  std::mutex mutex;
  std::vector<InvalidationCallback> invalidation_callbacks;
  // One byte per page rather than a bitset: this is read on the fault path, and 128 KiB
  // of plainly-indexed bytes beats bit arithmetic under a signal handler.
  std::vector<uint8_t> page_watched = std::vector<uint8_t>(kPageCount, 0);
  // Faults served on a page that was already unwatched - another thread's fault, or a
  // host write, got there first (BUG-044). Cleared when the page is watched again; a
  // page that keeps faulting while unwatched is a real violation.
  std::vector<uint8_t> page_retries = std::vector<uint8_t>(kPageCount, 0);

  class Heap final : public VirtualHeap {
   public:
    explicit Heap(State* state) : state_(state) {}

    bool QueryRegionInfo(uint32_t base_address,
                         HeapAllocationInfo* out_info) override {
      if (!out_info || base_address >= kPhysicalSize) {
        return false;
      }
      // One committed region covering physical memory. The guest allocator lives in
      // the DAO runtime, not here, so this reports the view rather than tracking
      // allocations it does not own.
      out_info->base_address = base_address & ~(kPageSize - 1);
      out_info->allocation_base = 0;
      out_info->allocation_protect = kMemoryProtectRead | kMemoryProtectWrite;
      out_info->allocation_size = kPhysicalSize;
      out_info->region_size = kPhysicalSize - out_info->base_address;
      out_info->state = kMemoryAllocationReserve | kMemoryAllocationCommit;
      out_info->protect = kMemoryProtectRead | kMemoryProtectWrite;
      return true;
    }

    xe::memory::PageAccess QueryRangeAccess(uint32_t low_address,
                                            uint32_t high_address) override {
      // The strictest access in the range, which is what the caller asked for: the
      // shared-memory uploader uses it to decide whether a range is safe to read
      // without taking a fault partway through.
      if (low_address > high_address || high_address >= kPhysicalSize) {
        return xe::memory::PageAccess::kNoAccess;
      }
      const uint32_t first = low_address / kPageSize;
      const uint32_t last = high_address / kPageSize;
      StateLock lock(state_->mutex);
      for (uint32_t page = first; page <= last && page < kPageCount; ++page) {
        if (state_->page_watched[page]) {
          // Watched means write-protected right now.
          return xe::memory::PageAccess::kReadOnly;
        }
      }
      return xe::memory::PageAccess::kReadWrite;
    }

   private:
    State* state_;
  };

  Heap heap{this};
};

// BUG-052: a write watch on a few guest pages, to find which guest code writes a value
// (warden::DaoFindAndWatch, below). An armed page is read-only; a write to it faults,
// and the fault path records the host pc and the faulting thread's guest lr and return
// addresses, makes the page writable and lets the write retry. The GPU thread logs the
// records and re-arms (warden::DaoWatchPoll). A fault on a watch page is always handled,
// armed or not, so a race with re-arming can never turn into a crash.
namespace {

constexpr int kDaoWatchPages = 4;
constexpr int kDaoWatchHits = 64;
struct DaoWatchHit {
  uint32_t guest_address;
  uint32_t guest_lr;
  uint32_t returns[6];
  uint64_t host_pc;
  int tid;
};
std::atomic<uint32_t> g_dao_watch_page[kDaoWatchPages];  // page address + 1; 0: none
std::atomic<bool> g_dao_watch_armed[kDaoWatchPages];
DaoWatchHit g_dao_watch_hits[kDaoWatchHits];
std::atomic<uint32_t> g_dao_watch_hit_count{0};

// From the fault handler, first: true if the fault was on a watch page. Only reads,
// stores, mprotect and gettid - safe in a signal handler.
bool DaoWatchFault(void* host_address, void* context) {
  Memory* const memory = g_memory;
  if (!memory || !g_dao_watch_page[0].load(std::memory_order_acquire)) {
    return false;
  }
  uint8_t* const base = memory->virtual_membase();
  const auto host = static_cast<uint8_t*>(host_address);
  if (host < base || host >= base + kGuestAddressSpace) {
    return false;
  }
  const uint32_t guest = uint32_t(host - base);
  const uint32_t page = guest & ~(kPageSize - 1);
  for (int i = 0; i < kDaoWatchPages; ++i) {
    if (g_dao_watch_page[i].load(std::memory_order_acquire) != page + 1) {
      continue;
    }
    if (g_dao_watch_armed[i].exchange(false, std::memory_order_acq_rel)) {
      const uint32_t n = g_dao_watch_hit_count.load(std::memory_order_relaxed);
      DaoWatchHit& hit = g_dao_watch_hits[n % kDaoWatchHits];
      hit.guest_address = guest;
      hit.host_pc = 0;
#if defined(__aarch64__)
      if (context) {
        hit.host_pc = uint64_t(static_cast<ucontext_t*>(context)->uc_mcontext.pc);
      }
#endif
      hit.tid = int(gettid());
      hit.guest_lr = 0;
      for (uint32_t& ret : hit.returns) {
        ret = 0;
      }
      if (PPCContext* ctx = ::warden::CurrentGuestContext()) {
        hit.guest_lr = uint32_t(ctx->lr);
        const uint32_t top = ::warden::CurrentGuestStackTop();
        uint32_t frame = ctx->r1.u32;
        for (int depth = 0; depth < 6 && top && frame < top; ++depth) {
          uint32_t caller;
          std::memcpy(&caller, base + frame, 4);
          caller = __builtin_bswap32(caller);
          if (caller <= frame || caller - frame < 8 || caller > top) {
            break;
          }
          uint32_t ret;
          std::memcpy(&ret, base + caller - 8, 4);
          hit.returns[depth] = __builtin_bswap32(ret);
          frame = caller;
        }
      }
      g_dao_watch_hit_count.store(n + 1, std::memory_order_release);
    }
    mprotect(base + page, kPageSize, PROT_READ | PROT_WRITE);
    return true;
  }
  return false;
}

}  // namespace

namespace {

// Re-protects one page in all three views. Called with State::mutex held, and from
// the fault path - so it does no allocation and no logging.
void ProtectPageInAllViews(uint8_t* membase, uint32_t page, int protection) {
  const uint32_t offset = page * kPageSize;
  mprotect(membase + kViewA + offset, kPageSize, protection);
  mprotect(membase + kViewC + offset, kPageSize, protection);
  mprotect(membase + kViewE + kViewEOffset + offset, kPageSize, protection);
}

void FaultHandler(int signal_number, siginfo_t* info, void* context) {
  // BUG-052: a write to a page under the investigation's write watch.
  if (info && DaoWatchFault(info->si_addr, context)) {
    return;
  }
  if (g_memory && info &&
      g_memory->HandleAccessViolation(info->si_addr, true)) {
    // Handled: the page is writable again and returning retries the instruction.
    return;
  }
  // Not ours. Chain to whatever was there before rather than swallowing a real
  // crash - an unexplained hang in a signal handler is far worse to debug than the
  // segfault it replaced.
  const struct sigaction& previous =
      signal_number == SIGBUS ? g_previous_sigbus : g_previous_sigsegv;
  if (previous.sa_flags & SA_SIGINFO) {
    if (previous.sa_sigaction) {
      previous.sa_sigaction(signal_number, info, context);
      return;
    }
  } else if (previous.sa_handler && previous.sa_handler != SIG_DFL &&
             previous.sa_handler != SIG_IGN) {
    previous.sa_handler(signal_number);
    return;
  }
  // Nothing to chain to. Restore the default and re-raise so the crash looks like a
  // crash to the system, with a usable tombstone.
  struct sigaction fallback = {};
  fallback.sa_handler = SIG_DFL;
  sigaction(signal_number, &fallback, nullptr);
  raise(signal_number);
}

}  // namespace

Memory::Memory() {
  state_ = std::make_unique<State>();
  g_host_write_hooks = {&state_->mutex, &state_->page_watched,
                        &state_->invalidation_callbacks};

  // One reservation for the whole guest address space, so that membase + A is valid
  // for every A without per-region bookkeeping. MAP_NORESERVE because almost none of
  // it is ever touched, and committing 4 GiB up front on a handheld would be absurd.
  void* reservation =
      mmap(nullptr, kGuestAddressSpace, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (reservation == MAP_FAILED) {
    XELOGE("could not reserve {} bytes of guest address space: {}",
           kGuestAddressSpace, strerror(errno));
    return;
  }
  state_->reservation = reservation;
  state_->reservation_size = kGuestAddressSpace;
  virtual_membase_ = static_cast<uint8_t*>(reservation);
  physical_membase_ = virtual_membase_ + kViewA;

  // The physical section, mapped three times so the views genuinely alias.
  // bionic declares memfd_create in <sys/mman.h> from API 30; this builds against 33,
  // so no raw syscall is needed.
  int fd = memfd_create("dao-physical", 0);
  if (fd < 0) {
    XELOGE("memfd_create failed ({}); physical views will not alias",
           strerror(errno));
  } else if (ftruncate(fd, kPhysicalSize) != 0) {
    XELOGE("could not size the physical section: {}", strerror(errno));
    close(fd);
    fd = -1;
  }
  state_->physical_fd = fd;

  if (fd >= 0) {
    const uint32_t views[3] = {kViewA, kViewC, kViewE + kViewEOffset};
    for (uint32_t view : views) {
      void* mapped = mmap(virtual_membase_ + view, kPhysicalSize,
                          PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
      if (mapped == MAP_FAILED) {
        XELOGE("could not map the physical view at {:08X}: {}", view,
               strerror(errno));
      }
    }
    XELOGI("guest memory: 4 GiB reserved at {}, 512 MiB physical aliased at "
           "A0000000 / C0000000 / E0001000",
           static_cast<void*>(virtual_membase_));
  }

  // The write watch needs the fault, and the fault needs a handler. Installed here
  // rather than lazily so that the first watched write cannot race with it.
  if (!g_handler_installed) {
    g_memory = this;
    struct sigaction action = {};
    action.sa_sigaction = FaultHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    // SIGBUS as well as SIGSEGV: a write to a read-only *shared* mapping can raise
    // either, and which one depends on the kernel rather than on anything we control.
    if (sigaction(SIGSEGV, &action, &g_previous_sigsegv) == 0 &&
        sigaction(SIGBUS, &action, &g_previous_sigbus) == 0) {
      g_handler_installed = true;
    } else {
      XELOGE("could not install the fault handler: {}", strerror(errno));
      g_memory = nullptr;
    }
  }
}

Memory::~Memory() {
  if (g_memory == this) {
    // Put the previous handlers back before the address space goes, or a later fault
    // runs a handler pointing at freed state.
    if (g_handler_installed) {
      sigaction(SIGSEGV, &g_previous_sigsegv, nullptr);
      sigaction(SIGBUS, &g_previous_sigbus, nullptr);
      g_handler_installed = false;
    }
    g_memory = nullptr;
  }
  if (state_) {
    if (state_->physical_fd >= 0) {
      close(state_->physical_fd);
    }
    if (state_->reservation) {
      munmap(state_->reservation, state_->reservation_size);
    }
  }
}

uint32_t Memory::HostToGuestVirtual(const void* host_address) const {
  const auto host = static_cast<const uint8_t*>(host_address);
  if (host < virtual_membase_ ||
      host >= virtual_membase_ + kGuestAddressSpace) {
    return UINT32_MAX;
  }
  return uint32_t(host - virtual_membase_);
}

uint32_t Memory::GetPhysicalAddress(uint32_t address) const {
  // Only the physical views have a physical address; everything else does not.
  if (address >= kViewA && address < kViewA + kPhysicalSize) {
    return address - kViewA;
  }
  if (address >= kViewC && address < kViewC + kPhysicalSize) {
    return address - kViewC;
  }
  if (address >= kViewE + kViewEOffset &&
      address < kViewE + kViewEOffset + kPhysicalSize) {
    return address - kViewE - kViewEOffset;
  }
  return UINT32_MAX;
}

void* Memory::RegisterPhysicalMemoryInvalidationCallback(
    PhysicalMemoryInvalidationCallback callback, void* callback_context) {
  StateLock lock(state_->mutex);
  state_->invalidation_callbacks.push_back(
      InvalidationCallback{callback, callback_context});
  // The context doubles as the handle, which is what the callers already use to
  // unregister - see shared_memory.cc and primitive_processor.cc.
  return callback_context;
}

void Memory::UnregisterPhysicalMemoryInvalidationCallback(
    void* callback_handle) {
  StateLock lock(state_->mutex);
  auto& list = state_->invalidation_callbacks;
  list.erase(std::remove_if(list.begin(), list.end(),
                            [callback_handle](const InvalidationCallback& entry) {
                              return entry.context == callback_handle;
                            }),
             list.end());
}

void* Memory::RegisterPhysicalMemoryReadCallback(
    PhysicalMemoryReadCallback callback, void* callback_context) {
  // "Data providers are unsupported", per the header. Nothing in the Vulkan path
  // registers one, so this says so rather than pretending to accept it.
  (void)callback;
  XELOGW("physical memory read callbacks are not implemented");
  return callback_context;
}

void Memory::UnregisterPhysicalMemoryReadCallback(void* callback_handle) {
  (void)callback_handle;
}

void Memory::EnablePhysicalMemoryAccessCallbacks(
    uint32_t physical_address, uint32_t length,
    bool enable_invalidation_notifications, bool enable_data_providers) {
  if (enable_data_providers) {
    XELOGW("data providers requested for {:08X}+{:X}; unsupported",
           physical_address, length);
  }
  if (!enable_invalidation_notifications || !length || !state_->reservation) {
    return;
  }
  const uint32_t start = physical_address & ~(kPageSize - 1);
  const uint64_t end = std::min<uint64_t>(
      uint64_t(physical_address) + length, kPhysicalSize);
  if (start >= kPhysicalSize) {
    return;
  }

  StateLock lock(state_->mutex);
  for (uint32_t offset = start; offset < end; offset += kPageSize) {
    const uint32_t page = offset / kPageSize;
    if (state_->page_watched[page]) {
      continue;
    }
    state_->page_watched[page] = 1;
    state_->page_retries[page] = 0;
    // Read-only, so the next guest write faults and the GPU learns its copy is
    // stale. This is the whole mechanism: without it the renderer caches guest data
    // and never finds out it changed.
    ProtectPageInAllViews(virtual_membase_, page, PROT_READ);
  }
}

bool Memory::TriggerPhysicalMemoryCallbacks(
    global_unique_lock_type global_lock_locked_once, uint32_t virtual_address,
    uint32_t length, bool is_write, bool unwatch_exact_range, bool unprotect) {
  (void)global_lock_locked_once;
  (void)unwatch_exact_range;
  if (!is_write) {
    return false;
  }
  const uint32_t physical_address = GetPhysicalAddress(virtual_address);
  if (physical_address == UINT32_MAX) {
    return false;
  }

  std::vector<InvalidationCallback> callbacks;
  std::vector<uint32_t> pages;
  {
    StateLock lock(state_->mutex);
    const uint64_t end =
        std::min<uint64_t>(uint64_t(physical_address) + std::max(length, 1u),
                           kPhysicalSize);
    for (uint64_t offset = physical_address & ~uint64_t(kPageSize - 1);
         offset < end; offset += kPageSize) {
      const uint32_t page = uint32_t(offset / kPageSize);
      if (page < kPageCount && state_->page_watched[page]) {
        state_->page_watched[page] = 0;
        pages.push_back(page);
      }
    }
    if (pages.empty()) {
      return false;
    }
    // Copied out, so the callbacks run without this lock held: they call back into
    // the GPU, which takes locks of its own, and holding both orders would be a
    // deadlock waiting for the right frame.
    callbacks = state_->invalidation_callbacks;
  }

  if (unprotect) {
    for (uint32_t page : pages) {
      ProtectPageInAllViews(virtual_membase_, page, PROT_READ | PROT_WRITE);
    }
  }
  for (const InvalidationCallback& entry : callbacks) {
    if (entry.callback) {
      entry.callback(entry.context, physical_address,
                     std::max(length, kPageSize), false);
    }
  }
  return true;
}

namespace {

// The first lines of every fatal report. Xenia's logger buffers and writes from its
// own thread, which idles 50 ms at a time; the lines that say what the title did just
// before the fault are the ones most likely to be lost when the process dies, so
// everything logged so far is given time to reach logcat here - once, instead of a
// 10 ms flush after every logged guest call, which made each thread's speed depend on
// how much it logged (BUG-034's timing). nanosleep is signal-safe; the rest of the
// report is not, and never was. Then who faulted, which logcat's tid does not say for
// the lines the logger wrote.
void BeginFatalReport() {
  usleep(150 * 1000);
  xe::FlushLog();
  __android_log_print(ANDROID_LOG_FATAL, "Warden", "fault on guest thread [%s]",
                      warden::CurrentGuestThreadName().c_str());
}

}  // namespace

bool Memory::HandleAccessViolation(void* host_address, bool is_write) {
  if (!state_ || !virtual_membase_ || !is_write) {
    return false;
  }
  const auto host = static_cast<uint8_t*>(host_address);
  if (host < virtual_membase_ ||
      host >= virtual_membase_ + kGuestAddressSpace) {
    // A thread with no guest context is not running the title: Android's runtime
    // faults on purpose (its garbage collector protects pages it is moving) and
    // handles it in the next handler. Chained silently - a report here was a false
    // alarm that looked fatal while the process carried on.
    if (!warden::CurrentGuestContext()) {
      return false;
    }
    // Outside the guest space entirely, so the faulting value was never a guest
    // address plus the base. Reported with the signed distance from the base, because
    // that distance is what identifies the arithmetic that produced it - a sign
    // extension, a 64-bit register used where 32 bits were meant, a stale host
    // pointer.
    const int64_t distance =
        int64_t(static_cast<uint8_t*>(host_address) - virtual_membase_);
    BeginFatalReport();
    __android_log_print(ANDROID_LOG_FATAL, "Warden",
                        "non-guest fault: host %p, base %p, base%+lld",
                        host_address, static_cast<void*>(virtual_membase_),
                        (long long)distance);
    // The handler runs on the faulting thread, and for a guest fault that is a guest
    // thread - so its PPCContext is reachable from here. Printing it is the
    // difference between "a bad pointer" and knowing which register held it.
    // A null indirect call lands here and looks like a wild pointer. It is not: the
    // table offset is invertible, so say which guest address the title was trying to
    // call rather than leaving the reader to work it out from the arithmetic.
    uint32_t called = 0;
    if (warden::DescribeNullIndirectCall(host_address, virtual_membase_, called)) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "  this is an INDIRECT CALL to guest %08X whose table slot "
                          "is empty", called);
    }
    warden::LogGuestRegistersOnFault();
    // And who called whom to get here, which neither the host backtrace nor ctx.lr
    // can say reliably (RESUMING.md §7).
    warden::LogGuestCallChainOnFault(virtual_membase_);
    warden::LogGuestTraceOnFault();
    return false;
  }
  const uint32_t virtual_address = uint32_t(host - virtual_membase_);
  const uint32_t physical_address = GetPhysicalAddress(virtual_address);
  if (physical_address == UINT32_MAX) {
    // Inside the guest address space but not in a physical view, so not a write
    // watch. Said out loud because this is the shape of a guest access to an address
    // the title did not mean, and the guest address is the only useful thing about
    // it - a tombstone gives host registers, which have to be mapped back by hand.
    //
    // __android_log_print rather than XELOG*: this is a signal handler, and Xenia's
    // logger takes a lock and buffers.
    BeginFatalReport();
    __android_log_print(ANDROID_LOG_FATAL, "Warden",
                        "guest fault: guest address %08X (host %p, base %p)",
                        virtual_address, host_address,
                        static_cast<void*>(virtual_membase_));
    // The title's code is read-only from boot (ProtectGuestCode), and every other
    // guest page is mapped read-write, so a fault there is a write into the code -
    // corruption, caught by whoever is doing it.
    if (warden::IsGuestCode(virtual_address)) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "  this is a WRITE INTO THE TITLE'S CODE, which is read-only "
                          "as on the console - the chain below is the writer");
    }
    warden::LogGuestRegistersOnFault();
    warden::LogGuestCallChainOnFault(virtual_membase_);
    warden::LogGuestTraceOnFault();
    return false;
  }
  const uint32_t page = physical_address / kPageSize;

  std::vector<InvalidationCallback> callbacks;
  {
    // This runs in a signal handler. Waiting for another thread's hold is right
    // (see StateLock); waiting for our own would deadlock, so that is reported.
    if (t_holds_state_mutex) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "guest write to %08X faulted while this thread holds the "
                          "memory state lock", virtual_address);
      return false;
    }
    StateLock lock(state_->mutex);
    if (page >= kPageCount) {
      return false;
    }
    if (!state_->page_watched[page]) {
      // Not watched now - but it was when this thread's store faulted: another guest
      // thread faulted on the same page and unprotected it, or a file read did
      // (warden_memory_host_write), between the fault and this lock. The store is
      // simply retried (BUG-044: with the title's heaps in the GPU-watched half of
      // memory this happened within seconds and was reported as a real violation,
      // killing the process). The protection is set again in case it is stale, and a
      // page that keeps faulting while unwatched is treated as the real violation it
      // then is.
      if (++state_->page_retries[page] > 16) {
        return false;
      }
      ProtectPageInAllViews(virtual_membase_, page, PROT_READ | PROT_WRITE);
      g_watched_write_races.fetch_add(1, std::memory_order_relaxed);
      return true;
    }
    state_->page_watched[page] = 0;
    ProtectPageInAllViews(virtual_membase_, page, PROT_READ | PROT_WRITE);
    callbacks = state_->invalidation_callbacks;
  }

  for (const InvalidationCallback& entry : callbacks) {
    if (entry.callback) {
      entry.callback(entry.context, page * kPageSize, kPageSize, false);
    }
  }
  g_watched_write_faults.fetch_add(1, std::memory_order_relaxed);
  for (ProbeRange& range : g_probe_ranges) {
    const uint32_t length = range.length.load(std::memory_order_relaxed);
    const uint32_t start = range.address.load(std::memory_order_relaxed);
    if (length && physical_address - start < length) {
      range.faults.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return true;
}

extern "C" void warden_probe_range_set(unsigned index, uint32_t address, uint32_t length) {
  if (index >= 8) {
    return;
  }
  ProbeRange& range = g_probe_ranges[index];
  if (range.address.load() != address || range.length.load() != length) {
    range.faults.store(0);
  }
  range.address.store(address);
  range.length.store(length);
}

extern "C" uint32_t warden_probe_range_faults(unsigned index) {
  return index < 8 ? g_probe_ranges[index].faults.load(std::memory_order_relaxed) : 0;
}

// A host write is about to land in guest memory (a file read through pread(), which
// the kernel refuses with EFAULT on a write-protected page rather than faulting the
// process): the watched pages of the range are unprotected and the GPU is told, as the
// fault handler would have done for a guest store. Virtual addresses outside the
// physical views have no watch and nothing to do.
extern "C" void warden_memory_host_write(uint32_t guest_address, uint32_t length) {
  const HostWriteHooks hooks = g_host_write_hooks;
  xe::Memory* memory = warden::GuestMemory();
  uint8_t* membase = warden::GuestMemoryBase();
  if (!hooks.mutex || !memory || !membase || !length) {
    return;
  }
  const uint32_t physical = memory->GetPhysicalAddress(guest_address);
  if (physical == UINT32_MAX) {
    return;
  }
  const uint32_t first = physical / kPageSize;
  const uint64_t end = std::min<uint64_t>(uint64_t(physical) + length, kPhysicalSize);
  const uint32_t last = uint32_t((end - 1) / kPageSize);
  std::vector<InvalidationCallback> callbacks;
  std::vector<uint32_t> pages;
  {
    StateLock lock(*hooks.mutex);
    for (uint32_t page = first; page <= last && page < kPageCount; ++page) {
      if (!(*hooks.page_watched)[page]) {
        continue;
      }
      (*hooks.page_watched)[page] = 0;
      ProtectPageInAllViews(membase, page, PROT_READ | PROT_WRITE);
      pages.push_back(page);
    }
    if (!pages.empty()) {
      callbacks = *hooks.callbacks;
    }
  }
  for (uint32_t page : pages) {
    for (const InvalidationCallback& entry : callbacks) {
      if (entry.callback) {
        entry.callback(entry.context, page * kPageSize, kPageSize, false);
      }
    }
  }
  if (!pages.empty()) {
    const uint32_t served = g_host_writes_served.fetch_add(1) + 1;
    if (served <= 8 || (served & (served - 1)) == 0) {
      XELOGI("memory: host write into {:08X}+{:X} unwatched {} GPU-watched pages ({} such "
             "writes so far)", guest_address, length, pages.size(), served);
    }
  }
}

extern "C" uint32_t warden_watched_write_races() {
  return g_watched_write_races.load(std::memory_order_relaxed);
}

extern "C" uint32_t warden_watched_write_faults() {
  return g_watched_write_faults.load(std::memory_order_relaxed);
}

VirtualHeap* Memory::GetPhysicalHeap() { return &state_->heap; }

BaseHeap* Memory::LookupHeapByType(bool physical, uint32_t page_size) {
  (void)page_size;
  // Only the physical heap exists here. The guest's virtual heaps belong to the DAO
  // runtime's allocator, which the GPU never queries.
  return physical ? static_cast<BaseHeap*>(&state_->heap) : nullptr;
}

}  // namespace xe

namespace warden {

// BUG-052: searches the guest's resident pages for `count` dwords (as the GPU's
// register file holds them; guest memory holds them big-endian) at 4-byte alignment,
// logs where they are, and arms the write watch on up to four of the pages holding
// them below the physical views - the title's own heap rather than GPU memory. Once:
// it takes a second or two.
void DaoFindAndWatch(const uint32_t* values, uint32_t count) {
  xe::Memory* const memory = xe::g_memory;
  if (!memory || !values || count < 4) {
    return;
  }
  count = std::min<uint32_t>(count, 16);
  uint32_t pattern[16];
  for (uint32_t i = 0; i < count; ++i) {
    pattern[i] = __builtin_bswap32(values[i]);
  }
  uint8_t* const base = memory->virtual_membase();
  constexpr size_t kChunk = size_t(256) << 20;
  std::vector<unsigned char> resident(kChunk / xe::kPageSize);
  uint32_t found[32];
  bool whole[32];
  uint32_t found_count = 0;
  size_t resident_pages = 0;
  for (size_t chunk = 0; chunk < xe::kGuestAddressSpace && found_count < 32;
       chunk += kChunk) {
    if (mincore(base + chunk, kChunk, resident.data()) != 0) {
      continue;
    }
    for (size_t p = 0; p < resident.size() && found_count < 32; ++p) {
      if (!(resident[p] & 1)) {
        continue;
      }
      ++resident_pages;
      const uint8_t* page = base + chunk + p * xe::kPageSize;
      for (uint32_t offset = 0; offset + 16 <= xe::kPageSize && found_count < 32;
           offset += 4) {
        uint32_t first;
        std::memcpy(&first, page + offset, 4);
        if (first != pattern[0] || std::memcmp(page + offset, pattern, 16) != 0) {
          continue;
        }
        const size_t span = std::min<size_t>(count * 4, xe::kPageSize - offset);
        whole[found_count] = std::memcmp(page + offset, pattern, span) == 0;
        found[found_count++] = uint32_t(chunk + p * xe::kPageSize + offset);
      }
    }
  }
  Dl_info library = {};
  dladdr(reinterpret_cast<void*>(&DaoFindAndWatch), &library);
  XELOGI("BUG-052 watch: {} matches of the face's first constant in {} resident pages; "
         "libwarden.so at {}", found_count, resident_pages, library.dli_fbase);
  int armed = 0;
  for (uint32_t i = 0; i < found_count; ++i) {
    const uint32_t address = found[i];
    const uint32_t page = address & ~(xe::kPageSize - 1);
    bool arm = address < xe::kViewA && whole[i] && armed < xe::kDaoWatchPages;
    for (int k = 0; arm && k < armed; ++k) {
      arm = xe::g_dao_watch_page[k].load() != page + 1;
    }
    XELOGI("BUG-052 watch: match at {:08X} ({} of {} dwords){}", address,
           whole[i] ? count : 4, count, arm ? ", watched" : "");
    if (arm) {
      xe::g_dao_watch_page[armed].store(page + 1, std::memory_order_release);
      xe::g_dao_watch_armed[armed].store(true, std::memory_order_release);
      mprotect(base + page, xe::kPageSize, PROT_READ);
      ++armed;
    }
  }
}

// BUG-052: watches the page holding [guest_address, guest_address + length) - in any
// view, the title's heap included (it lives in the 0xA0000000 view) - and logs only the
// writes that land in that range. One target at a time; the first call wins.
std::atomic<uint32_t> g_dao_watch_target_low{0};
std::atomic<uint32_t> g_dao_watch_target_high{0};
void DaoWatchAddress(uint32_t guest_address, uint32_t length) {
  xe::Memory* const memory = xe::g_memory;
  if (!memory || xe::g_dao_watch_page[0].load()) {
    return;
  }
  const uint32_t page = guest_address & ~(xe::kPageSize - 1);
  g_dao_watch_target_low.store(guest_address);
  g_dao_watch_target_high.store(guest_address + length);
  xe::g_dao_watch_page[0].store(page + 1, std::memory_order_release);
  xe::g_dao_watch_armed[0].store(true, std::memory_order_release);
  mprotect(memory->virtual_membase() + page, xe::kPageSize, PROT_READ);
  XELOGI("BUG-052 watch: writes to {:08X}..{:08X} watched (page {:08X})", guest_address,
         guest_address + length, page);
}

// BUG-052: from the GPU thread, cheaply on every draw - logs the watch's new records,
// one line per distinct writer (host pc), and re-arms, three hundred times in all.
void DaoWatchPoll() {
  static uint32_t logged = 0;
  static uint32_t rearms = 0;
  static uint64_t writers[64];
  static uint32_t writer_count = 0;
  if (!xe::g_dao_watch_page[0].load(std::memory_order_acquire)) {
    return;
  }
  const uint32_t count = xe::g_dao_watch_hit_count.load(std::memory_order_acquire);
  for (uint32_t i = logged; i < count; ++i) {
    if (count - i > uint32_t(xe::kDaoWatchHits)) {
      continue;
    }
    const xe::DaoWatchHit& hit = xe::g_dao_watch_hits[i % xe::kDaoWatchHits];
    // An address target (DaoWatchAddress): only writes inside it.
    const uint32_t target_high = g_dao_watch_target_high.load();
    if (target_high && (hit.guest_address < g_dao_watch_target_low.load() ||
                        hit.guest_address >= target_high)) {
      continue;
    }
    bool known = false;
    for (uint32_t w = 0; w < writer_count; ++w) {
      known = known || writers[w] == hit.host_pc;
    }
    if (known) {
      continue;
    }
    if (writer_count < 64) {
      writers[writer_count++] = hit.host_pc;
    }
    std::string chain;
    for (uint32_t ret : hit.returns) {
      if (ret) {
        chain += fmt::format(" {:08X}(sub_{:08X})", ret, GuestFunctionContaining(ret));
      }
    }
    XELOGI("BUG-052 watch: write to {:08X} by host pc {:X}, tid {}, guest lr {:08X} "
           "(in sub_{:08X}); returns{}", hit.guest_address, hit.host_pc, hit.tid,
           hit.guest_lr, GuestFunctionContaining(hit.guest_lr), chain);
  }
  logged = count;
  uint8_t* const base = xe::g_memory ? xe::g_memory->virtual_membase() : nullptr;
  // An address target's page also takes the writes around it, so it gets more re-arms.
  const uint32_t rearm_budget = g_dao_watch_target_high.load() ? 3000 : 300;
  for (int i = 0; base && i < xe::kDaoWatchPages && rearms < rearm_budget; ++i) {
    const uint32_t page = xe::g_dao_watch_page[i].load(std::memory_order_acquire);
    if (page && !xe::g_dao_watch_armed[i].load(std::memory_order_acquire)) {
      ++rearms;
      xe::g_dao_watch_armed[i].store(true, std::memory_order_release);
      mprotect(base + page - 1, xe::kPageSize, PROT_READ);
    }
  }
}

}  // namespace warden
