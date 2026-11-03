// Kernel imports the title reaches during startup. Task 5.1.
//
// The order comes from the title itself, reproducibly
// (`tools/appbringup/boot_repeat.sh 3`), so these are the ones in the way rather than
// a guess at what might be needed.
//
// Each is a strong `__imp__` definition, which overrides the generated weak stub at
// link time. Arguments arrive in `ctx.r3` onwards, the result goes in `ctx.r3`, a
// pointer argument is `base + ctx.rN.u32`, and anything read from or written to guest
// memory is big-endian.
//
// `MmQueryStatistics` was left out at first, for want of a verified structure layout.
// It is here now: the layout is Xenia's, the title confirms its size, and the stub
// turned out to be the cause of the startup crash - see its comment.

#include <strings.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_guest_heap.h"
#include "warden_guest.h"
#include "warden_guest_registers.h"
#include "warden_lifecycle.h"
#include "xenia/memory.h"
#include "warden_threads.h"
#include "warden_trace.h"
#include "xenia/base/logging.h"

namespace {

// Defined below, with the other helpers; DbgPrint needs it first.
std::string_view GuestString(uint8_t* base, uint32_t address, size_t limit);

// ---------------------------------------------------------------- critical sections
//
// The guest passes a pointer to its own RTL_CRITICAL_SECTION in guest memory. The
// honest options were to implement the console's layout, which nothing in this
// checkout documents, or to keep the lock host-side and key it on that pointer. This
// does the latter.
//
// The trade is explicit: it is correct for a title that only ever passes the pointer
// back to these functions, and wrong for one that reads the structure's fields - a
// recursion count, say, or an owning thread id. If startup later misbehaves around
// locking, this is the first thing to suspect, and the fix is to find the real layout
// rather than to tune anything here.
struct CriticalSections {
  std::mutex table_mutex;
  // Pointer-keyed, and never erased: the guest may reuse a section after deleting it,
  // and a recursive_mutex is 40 bytes. Leaking a handful is cheaper than a
  // use-after-free in a lock.
  std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> locks;

  std::recursive_mutex& For(uint32_t guest_pointer) {
    std::lock_guard<std::mutex> lock(table_mutex);
    auto& slot = locks[guest_pointer];
    if (!slot) {
      slot = std::make_unique<std::recursive_mutex>();
    }
    return *slot;
  }
};

CriticalSections& Sections() {
  static CriticalSections sections;
  return sections;
}

// ---------------------------------------------------------------- thread-local slots
//
// KeTlsAlloc hands out a slot index from the executable's quota (64 here, from its
// XEX header); the values live in guest memory, in the calling thread's TLS block
// after its static part, exactly where the console keeps them - because the title
// also reads its TLS straight through r13 (warden_guest.cc, FillThreadBlock), and a
// value kept host-side was invisible there: the Update thread called a null method
// read from a TLS pointer the host-side KeTlsSetValue had never put in guest memory.
constexpr uint32_t kMaxTlsSlots = 256;
constexpr uint32_t kTlsInvalidSlot = 0xFFFFFFFF;

struct TlsSlots {
  std::mutex mutex;
  bool used[kMaxTlsSlots] = {};
};

TlsSlots& Tls() {
  static TlsSlots slots;
  return slots;
}

// The guest address of a slot's value on the calling thread, 0 for a bad slot or a
// thread with no TLS block.
uint32_t TlsSlotAddress(uint32_t slot) {
  const uint32_t tls = warden::CurrentGuestTls();
  if (!tls || slot >= warden::GuestTlsSlotCount() || slot >= kMaxTlsSlots) {
    return 0;
  }
  return tls + warden::GuestTlsDataSize() + slot * 4;
}

}  // namespace

// ---------------------------------------------------------------- DbgPrint

// The title's own diagnostics: Direct3D says why it thinks the GPU hung, the engine
// says what it could not load. A printf over guest memory, enough of the format
// language for what a console title prints: flags, width, precision, l/h, the
// conversions d i u x X o c s p and %%. Integer arguments come from r4..r10 and then
// the stack at r1 + 0x54 (the 9th argument onwards, as every import's do).
PPC_FUNC(__imp__DbgPrint) {
  static std::atomic<int> logged{0};
  const uint32_t format_address = ctx.r3.u32;
  if (!format_address || logged.fetch_add(1, std::memory_order_relaxed) >= 2000) {
    ctx.r3.u64 = 0;
    return;
  }
  const std::string_view format = GuestString(base, format_address, 2048);
  unsigned next_argument = 0;  // 0 = r4
  auto argument = [&]() -> uint64_t {
    const unsigned index = next_argument++;
    switch (index) {
      case 0: return ctx.r4.u64;
      case 1: return ctx.r5.u64;
      case 2: return ctx.r6.u64;
      case 3: return ctx.r7.u64;
      case 4: return ctx.r8.u64;
      case 5: return ctx.r9.u64;
      case 6: return ctx.r10.u64;
      default: return PPC_LOAD_U64(ctx.r1.u32 + 0x54 + 8 * (index - 7));
    }
  };
  std::string out;
  for (size_t i = 0; i < format.size(); ++i) {
    const char c = format[i];
    if (c != '%') {
      out += c;
      continue;
    }
    // Collect the specification up to its conversion character.
    std::string spec = "%";
    size_t j = i + 1;
    while (j < format.size() && std::strchr("-+ #0123456789.lhqI", format[j])) {
      spec += format[j];
      ++j;
    }
    if (j >= format.size()) {
      out += spec;
      break;
    }
    const char conversion = format[j];
    i = j;
    char buffer[512];
    // Length modifiers are dropped: every integer argument is a 64-bit register and
    // the console's long is 32 bits, so the value is taken as 32-bit.
    std::string plain;
    for (char ch : spec) {
      if (ch != 'l' && ch != 'h' && ch != 'q' && ch != 'I') {
        plain += ch;
      }
    }
    switch (conversion) {
      case '%':
        out += '%';
        break;
      case 'd':
      case 'i':
        std::snprintf(buffer, sizeof(buffer), (plain + "d").c_str(), int32_t(argument()));
        out += buffer;
        break;
      case 'u':
      case 'x':
      case 'X':
      case 'o':
        std::snprintf(buffer, sizeof(buffer), (plain + conversion).c_str(),
                      uint32_t(argument()));
        out += buffer;
        break;
      case 'p':
        std::snprintf(buffer, sizeof(buffer), "%08X", uint32_t(argument()));
        out += buffer;
        break;
      case 'c':
        out += char(argument());
        break;
      case 's': {
        const uint32_t address = uint32_t(argument());
        const std::string_view text = address ? GuestString(base, address, 1024)
                                              : std::string_view("(null)");
        std::snprintf(buffer, sizeof(buffer), (plain + "s").c_str(),
                      std::string(text).c_str());
        out += buffer;
        break;
      }
      default:
        out += spec;
        out += conversion;
        break;
    }
  }
  // One log line per line of output; a trailing newline is the title's, not ours.
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
    out.pop_back();
  }
  size_t start = 0;
  while (start <= out.size()) {
    const size_t end = out.find('\n', start);
    const std::string_view line(out.data() + start,
                                (end == std::string::npos ? out.size() : end) - start);
    XELOGI("[{}] DbgPrint: {}", warden::CurrentGuestThreadName(), line);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  ctx.r3.u64 = 0;
}

// void RtlFillMemoryUlong(PVOID destination, SIZE_T length, ULONG pattern)
//
// length / 4 words of pattern, stored as the console stores a ULONG (big-endian).
PPC_FUNC(__imp__RtlFillMemoryUlong) {
  const uint32_t destination = ctx.r3.u32;
  const uint32_t count = ctx.r4.u32 / 4;
  const uint32_t pattern = ctx.r5.u32;
  for (uint32_t i = 0; i < count; ++i) {
    PPC_STORE_U32(destination + 4 * i, pattern);
  }
}

// The console's TIME_FIELDS is eight big-endian u16s: year, month, day, hour, minute,
// second, milliseconds, weekday (0 is Sunday). A time is a FILETIME - 100 ns units since
// 1601-01-01, the clock KeQuerySystemTime reads - and in UTC, the time zone
// ExGetXConfigSetting reports. The calendar arithmetic is Howard Hinnant's
// days_from_civil / civil_from_days, exact over the whole range, so no date library or
// host time zone is involved. Xenia's versions (xboxkrnl_rtl.cc) do the same through
// its date library.
namespace {

constexpr uint64_t kTicksPerDay = 864000000000ull;
constexpr int64_t kDaysFrom1601To1970 = 134774;

// Days since 1970-01-01 of a proleptic Gregorian date.
int64_t DaysFromCivil(int64_t year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = unsigned(year - era * 400);
  const unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + int64_t(day_of_era) - 719468;
}

// The date of a day counted from 1970-01-01.
void CivilFromDays(int64_t days, int64_t& year, unsigned& month, unsigned& day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned day_of_era = unsigned(days - era * 146097);
  const unsigned year_of_era =
      (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const unsigned day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned month_index = (5 * day_of_year + 2) / 153;
  day = day_of_year - (153 * month_index + 2) / 5 + 1;
  month = month_index < 10 ? month_index + 3 : month_index - 9;
  year = int64_t(year_of_era) + era * 400 + (month <= 2);
}

}  // namespace

// void RtlTimeToTimeFields(const LARGE_INTEGER* time, PTIME_FIELDS fields)
//
// Reached when a save is loaded or written (run 202). The stub was not neutral: the
// import is void, so it returned with the caller's TIME_FIELDS unwritten - whatever the
// stack held.
PPC_FUNC(__imp__RtlTimeToTimeFields) {
  const uint32_t time = ctx.r3.u32;
  const uint32_t fields = ctx.r4.u32;
  if (!time || !fields) {
    return;
  }
  const uint64_t ticks = PPC_LOAD_U64(time);
  const uint64_t days = ticks / kTicksPerDay;  // since 1601-01-01, a Monday
  const uint64_t ms = ticks % kTicksPerDay / 10000;
  int64_t year;
  unsigned month, day;
  CivilFromDays(int64_t(days) - kDaysFrom1601To1970, year, month, day);
  PPC_STORE_U16(fields + 0, uint16_t(year));
  PPC_STORE_U16(fields + 2, uint16_t(month));
  PPC_STORE_U16(fields + 4, uint16_t(day));
  PPC_STORE_U16(fields + 6, uint16_t(ms / 3600000));
  PPC_STORE_U16(fields + 8, uint16_t(ms / 60000 % 60));
  PPC_STORE_U16(fields + 10, uint16_t(ms / 1000 % 60));
  PPC_STORE_U16(fields + 12, uint16_t(ms % 1000));
  PPC_STORE_U16(fields + 14, uint16_t((days + 1) % 7));
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 8) {
    XELOGI("RtlTimeToTimeFields({:016X}) -> {:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}, weekday {}",
           ticks, year, month, day, ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000,
           (days + 1) % 7);
  }
}

// BOOLEAN RtlTimeFieldsToTime(PTIME_FIELDS fields, PLARGE_INTEGER time)
//
// The weekday is ignored, as Windows ignores it. An impossible date is FALSE with the
// time left alone - the stub's 0 said that of every date.
PPC_FUNC(__imp__RtlTimeFieldsToTime) {
  const uint32_t fields = ctx.r3.u32;
  const uint32_t time = ctx.r4.u32;
  ctx.r3.u64 = 0;
  if (!fields || !time) {
    return;
  }
  const unsigned year = PPC_LOAD_U16(fields + 0);
  const unsigned month = PPC_LOAD_U16(fields + 2);
  const unsigned day = PPC_LOAD_U16(fields + 4);
  const unsigned hour = PPC_LOAD_U16(fields + 6);
  const unsigned minute = PPC_LOAD_U16(fields + 8);
  const unsigned second = PPC_LOAD_U16(fields + 10);
  const unsigned ms = PPC_LOAD_U16(fields + 12);
  static constexpr unsigned kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  const bool valid = year >= 1601 && month >= 1 && month <= 12 && day >= 1 &&
                     day <= kDaysInMonth[month - 1] + (month == 2 && leap) && hour <= 23 &&
                     minute <= 59 && second <= 59 && ms <= 999;
  uint64_t ticks = 0;
  if (valid) {
    const uint64_t days = uint64_t(DaysFromCivil(year, month, day) + kDaysFrom1601To1970);
    ticks = days * kTicksPerDay +
            ((uint64_t(hour) * 3600 + minute * 60 + second) * 1000 + ms) * 10000;
    PPC_STORE_U64(time, ticks);
    ctx.r3.u64 = 1;
  }
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 8) {
    XELOGI("RtlTimeFieldsToTime({:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}) -> {}", year, month,
           day, hour, minute, second, ms,
           valid ? fmt::format("{:016X}", ticks) : std::string("FALSE, not a date"));
  }
}

// void HalReturnToFirmware(FIRMWARE_REENTRY routine)
//
// A reboot or power-down: the end of this app (4.9). Never returns, as on the console.
PPC_FUNC(__imp__HalReturnToFirmware) {
  (void)base;
  warden::RequestExit("the title returned to the firmware (HalReturnToFirmware " +
                          std::to_string(ctx.r3.u32) + ")",
                      false);
  warden::ParkForever();
}

// ULONG KeGetCurrentProcessType()
//
// 0 idle, 1 user, 2 system - the title is a user process. Returning the wrong one
// would be a plausible-looking lie, which is why it is worth saying: a game running
// from disc is always a user process.
PPC_FUNC(__imp__KeGetCurrentProcessType) {
  (void)base;
  ctx.r3.u64 = 1;
}

// BOOLEAN XexCheckExecutablePrivilege(ULONG privilege)
//
// Whether the executable was granted a given privilege. Returning 0 - "not granted" -
// for everything, because this build is not signed for any of them and claiming
// otherwise invites the title down a path that then fails for a reason much harder to
// trace. If something turns out to need a privilege, the log says which was asked for.
PPC_FUNC(__imp__XexCheckExecutablePrivilege) {
  (void)base;
  XELOGI("XexCheckExecutablePrivilege({}) -> not granted", ctx.r3.u32);
  ctx.r3.u64 = 0;
}

// NTSTATUS XexGetModuleHandle(const char* name, HANDLE* handle)
//
// The title asks for its own module at the title screen (and then for a procedure in
// it, below). As DAORecomp: `default.xex`, or no name, is this executable and gets a
// handle; any other module does not exist. The handle is a stable non-zero token, not
// a loader entry - nothing has dereferenced it yet; if something does, the log of the
// fault names it.
constexpr uint32_t kThisModuleHandle = 0x0DEF0001;

PPC_FUNC(__imp__XexGetModuleHandle) {
  const uint32_t name = ctx.r3.u32;
  const uint32_t out = ctx.r4.u32;
  const char* text = name ? reinterpret_cast<const char*>(base + name) : "";
  if (!out) {
    ctx.r3.u64 = 0xC000000D;  // STATUS_INVALID_PARAMETER
    return;
  }
  const bool ours = !name || !strcasecmp(text, "default.xex");
  PPC_STORE_U32(out, ours ? kThisModuleHandle : 0);
  XELOGI("[{}] XexGetModuleHandle(\"{}\") -> {}", warden::CurrentGuestThreadName(), text,
         ours ? "this executable" : "no such module");
  ctx.r3.u64 = ours ? 0 : 0xC000000F;  // STATUS_NO_SUCH_FILE
}

// NTSTATUS XexGetProcedureAddress(HANDLE module, DWORD ordinal_or_name, PVOID* address)
//
// Unsupported, as DAORecomp: the address comes back 0 with STATUS_NOT_IMPLEMENTED,
// and the request is logged - an ordinal, or a name when the second argument points
// into the module image - so that a procedure the title needs can be given.
PPC_FUNC(__imp__XexGetProcedureAddress) {
  const uint32_t module = ctx.r3.u32;
  const uint32_t what = ctx.r4.u32;
  const uint32_t out = ctx.r5.u32;
  if (out) {
    PPC_STORE_U32(out, 0);
  }
  const bool is_name = what >= 0x82000000u && what < 0x83400000u;
  XELOGW("[{}] XexGetProcedureAddress(module {:08X}, {} {}) -> not implemented",
         warden::CurrentGuestThreadName(), module, is_name ? "name" : "ordinal",
         is_name ? std::string(reinterpret_cast<const char*>(base + what)) : std::to_string(what));
  ctx.r3.u64 = 0xC0000002;  // STATUS_NOT_IMPLEMENTED
}

// ---------------------------------------------------------------- interlocked
// singly-linked lists
//
// SLIST_HEADER is eight bytes, 8-aligned, big-endian: the first entry's address at
// +0, the depth at +4 and a sequence number at +6; an entry's first word is its next
// pointer. The title inlines most of these (lwarx/stwcx on the header) and calls the
// kernel's for the rest - the character stage's loader pops from one - so the kernel
// versions must be atomic against the inlined ones: a 64-bit compare-and-swap on the
// header's raw bytes, as Xenia does. A stub's null from a pop would hand the title an
// empty list that is not.
namespace {

uint64_t SListRaw(uint32_t next, uint16_t depth, uint16_t sequence) {
  const uint8_t bytes[8] = {uint8_t(next >> 24), uint8_t(next >> 16), uint8_t(next >> 8),
                            uint8_t(next),       uint8_t(depth >> 8), uint8_t(depth),
                            uint8_t(sequence >> 8), uint8_t(sequence)};
  uint64_t raw;
  std::memcpy(&raw, bytes, 8);
  return raw;
}

void SListFields(uint64_t raw, uint32_t& next, uint16_t& depth, uint16_t& sequence) {
  uint8_t b[8];
  std::memcpy(b, &raw, 8);
  next = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
  depth = uint16_t(b[4] << 8 | b[5]);
  sequence = uint16_t(b[6] << 8 | b[7]);
}

}  // namespace

// PSLIST_ENTRY InterlockedPushEntrySList(PSLIST_HEADER header, PSLIST_ENTRY entry)
PPC_FUNC(__imp__InterlockedPushEntrySList) {
  const uint32_t header = ctx.r3.u32;
  const uint32_t entry = ctx.r4.u32;
  if (!header || !entry) {
    ctx.r3.u64 = 0;
    return;
  }
  auto* word = reinterpret_cast<std::atomic<uint64_t>*>(base + header);
  uint64_t old_raw = word->load(std::memory_order_acquire);
  uint32_t next;
  uint16_t depth, sequence;
  do {
    SListFields(old_raw, next, depth, sequence);
    PPC_STORE_U32(entry, next);
  } while (!word->compare_exchange_weak(old_raw, SListRaw(entry, depth + 1, sequence + 1),
                                        std::memory_order_acq_rel));
  ctx.r3.u64 = next;
}

// PSLIST_ENTRY InterlockedPopEntrySList(PSLIST_HEADER header)
PPC_FUNC(__imp__InterlockedPopEntrySList) {
  const uint32_t header = ctx.r3.u32;
  if (!header) {
    ctx.r3.u64 = 0;
    return;
  }
  auto* word = reinterpret_cast<std::atomic<uint64_t>*>(base + header);
  uint64_t old_raw = word->load(std::memory_order_acquire);
  uint32_t next;
  uint16_t depth, sequence;
  uint32_t after;
  do {
    SListFields(old_raw, next, depth, sequence);
    if (!next) {
      ctx.r3.u64 = 0;
      return;
    }
    after = PPC_LOAD_U32(next);
  } while (!word->compare_exchange_weak(old_raw, SListRaw(after, depth - 1, sequence),
                                        std::memory_order_acq_rel));
  ctx.r3.u64 = next;
}

// PSLIST_ENTRY InterlockedFlushSList(PSLIST_HEADER header) - takes the whole list.
PPC_FUNC(__imp__InterlockedFlushSList) {
  const uint32_t header = ctx.r3.u32;
  if (!header) {
    ctx.r3.u64 = 0;
    return;
  }
  auto* word = reinterpret_cast<std::atomic<uint64_t>*>(base + header);
  uint64_t old_raw = word->load(std::memory_order_acquire);
  uint32_t next;
  uint16_t depth, sequence;
  do {
    SListFields(old_raw, next, depth, sequence);
  } while (!word->compare_exchange_weak(old_raw, SListRaw(0, 0, 0), std::memory_order_acq_rel));
  ctx.r3.u64 = next;
}

// void MmSetAddressProtect(PVOID address, SIZE_T size, ULONG protect)
//
// Page protection changes on the title's own memory - read-only data it wants to
// catch writes to. Accepted and not applied: this runtime's guest memory stays
// writable, and the GPU's write watch owns the protection bits of physical pages.
PPC_FUNC(__imp__MmSetAddressProtect) {
  (void)base;
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 4) {
    XELOGI("[{}] MmSetAddressProtect({:08X}, {:X} bytes, protect {:08X}) -> accepted, not applied",
           warden::CurrentGuestThreadName(), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  }
}

// void RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION cs)
PPC_FUNC(__imp__RtlInitializeCriticalSection) {
  (void)base;
  // Touching the map is what creates the lock; no state is written to the guest
  // structure. See the note on CriticalSections above.
  Sections().For(ctx.r3.u32);
}

// NTSTATUS RtlInitializeCriticalSectionAndSpinCount(PRTL_CRITICAL_SECTION cs,
//                                                   ULONG spin_count)
//
// The spin count is an optimisation hint on a console with in-order cores; a
// pthread mutex on this hardware does its own adaptive spinning, so it is accepted
// and ignored rather than approximated.
PPC_FUNC(__imp__RtlInitializeCriticalSectionAndSpinCount) {
  (void)base;
  Sections().For(ctx.r3.u32);
  ctx.r3.u64 = 0;  // STATUS_SUCCESS
}

PPC_FUNC(__imp__RtlEnterCriticalSection) {
  (void)base;
  Sections().For(ctx.r3.u32).lock();
}

PPC_FUNC(__imp__RtlLeaveCriticalSection) {
  (void)base;
  // unlock() on a mutex this thread does not hold is undefined, and the guest doing
  // that would be a bug in the guest - but one that would crash here rather than
  // there. Not guarded, because tracking ownership to guard it means reimplementing
  // the recursion count this deliberately avoids; if it ever fires, that is the
  // signal to go and find the real structure layout.
  Sections().For(ctx.r3.u32).unlock();
}

// BOOLEAN RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION cs)
PPC_FUNC(__imp__RtlTryEnterCriticalSection) {
  (void)base;
  ctx.r3.u64 = Sections().For(ctx.r3.u32).try_lock() ? 1 : 0;
}

// DWORD KeTlsAlloc()
PPC_FUNC(__imp__KeTlsAlloc) {
  TlsSlots& slots = Tls();
  std::lock_guard<std::mutex> lock(slots.mutex);
  const uint32_t count = std::min(warden::GuestTlsSlotCount(), kMaxTlsSlots);
  for (uint32_t slot = 0; slot < count; ++slot) {
    if (!slots.used[slot]) {
      slots.used[slot] = true;
      if (const uint32_t address = TlsSlotAddress(slot)) {
        PPC_STORE_U32(address, 0);
      }
      ctx.r3.u64 = slot;
      return;
    }
  }
  XELOGE("KeTlsAlloc: all {} slots in use", count);
  ctx.r3.u64 = kTlsInvalidSlot;
}

// BOOL KeTlsSetValue(DWORD slot, LPVOID value)
PPC_FUNC(__imp__KeTlsSetValue) {
  const uint32_t address = TlsSlotAddress(ctx.r3.u32);
  if (!address) {
    ctx.r3.u64 = 0;
    return;
  }
  PPC_STORE_U32(address, ctx.r4.u32);
  ctx.r3.u64 = 1;
}

// LPVOID KeTlsGetValue(DWORD slot)
//
// Not in the reached list yet, because startup crashed before it - but a title that
// sets a TLS value reads it back, and leaving the weak stub here would return 0 for a
// value that had been set. Implemented alongside its pair rather than waiting to be
// surprised by it.
PPC_FUNC(__imp__KeTlsGetValue) {
  const uint32_t address = TlsSlotAddress(ctx.r3.u32);
  ctx.r3.u64 = address ? PPC_LOAD_U32(address) : 0;
}

// BOOL KeTlsFree(DWORD slot)
PPC_FUNC(__imp__KeTlsFree) {
  (void)base;
  const uint32_t slot = ctx.r3.u32;
  if (slot >= warden::GuestTlsSlotCount() || slot >= kMaxTlsSlots) {
    ctx.r3.u64 = 0;
    return;
  }
  TlsSlots& slots = Tls();
  std::lock_guard<std::mutex> lock(slots.mutex);
  slots.used[slot] = false;
  // The values are left in each thread's block. A freed slot's value is not readable
  // without allocating the slot again, which zeroes it on the allocating thread; the
  // console leaves the other threads' values too.
  ctx.r3.u64 = 1;
}

// files/heap-scan (a test marker holding a period in seconds): how full the title's big
// heaps get (PLAN 9.9: could memory move between them?). Every physical allocation of
// 1 MB or more is kept; every period a thread walks each in 64 KB blocks and logs how
// many hold anything but zeros, and the highest one that does - a rough high-water mark
// for a heap filled from the bottom (zeroed blocks in use read as empty, freed ones
// still holding data as full).
namespace {
struct BigAllocation {
  uint32_t base;  // physical
  uint32_t size;
};
std::mutex g_big_mutex;
std::vector<BigAllocation> g_big_allocations;

void ScanBigAllocations() {
  std::vector<BigAllocation> heaps;
  {
    std::lock_guard<std::mutex> lock(g_big_mutex);
    heaps = g_big_allocations;
  }
  const uint8_t* memory = warden::GuestMemoryBase() + 0xA0000000u;
  constexpr uint32_t kBlock = 0x10000;
  for (const BigAllocation& heap : heaps) {
    uint32_t written = 0;
    uint32_t highest = 0;
    for (uint32_t offset = 0; offset + kBlock <= heap.size; offset += kBlock) {
      const auto* words = reinterpret_cast<const uint64_t*>(memory + heap.base + offset);
      uint64_t any = 0;
      for (uint32_t i = 0; i < kBlock / 8 && !any; ++i) {
        any |= words[i];
      }
      if (any) {
        ++written;
        highest = offset + kBlock;
      }
    }
    XELOGI("heap-scan: {:08X} {:.1f} MB - {:.1f} MB of 64 KB blocks written, the highest "
           "ending at +{:.1f} MB", 0xA0000000u + heap.base, heap.size / 1048576.0,
           written * double(kBlock) / 1048576.0, highest / 1048576.0);
  }
}

void KeepBigAllocation(uint32_t physical, uint32_t size) {
  if (size < 0x100000) {
    return;
  }
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(g_big_mutex);
    first = g_big_allocations.empty();
    g_big_allocations.push_back({physical, size});
  }
  if (!first) {
    return;
  }
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  FILE* file = dir ? fopen((std::string(dir) + "/heap-scan").c_str(), "r") : nullptr;
  if (!file) {
    return;
  }
  int seconds = 0;
  if (fscanf(file, "%d", &seconds) != 1 || seconds <= 0) {
    seconds = 15;
  }
  fclose(file);
  XELOGI("heap-scan: the big physical allocations scanned every {} s", seconds);
  std::thread([seconds] {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
      ScanBigAllocations();
    }
  }).detach();
}
}  // namespace

// PVOID MmAllocatePhysicalMemoryEx(ULONG flags, SIZE_T size, ULONG protect,
//                                  ULONG min_address, ULONG max_address,
//                                  ULONG alignment)
//
// Physical memory, which the GPU reads through the views warden_memory.cc aliases.
// Returns a *virtual* address in the 0xA0000000 view, which is what makes
// Memory::TranslatePhysical agree with it: that view maps physical 0 at offset 0.
//
// 0 on failure, which here means "the physical heap is exhausted" rather than
// anything to do with the request.
PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx) {
  (void)base;
  const uint32_t size = ctx.r4.u32;
  const uint32_t alignment = ctx.r8.u32;
  uint32_t out_base = 0;
  uint32_t out_size = 0;
  // 4 KiB at minimum, and the requested alignment if it is larger and a power of two.
  uint32_t granularity = 0x1000;
  if (alignment > granularity && (alignment & (alignment - 1)) == 0) {
    granularity = alignment;
  }
  const uint32_t status =
      warden::AllocatePhysicalMemory(size, granularity, out_base, out_size);
  const uint32_t result = status == 0 ? (0xA0000000u + out_base) : 0u;
  XELOGI("MmAllocatePhysicalMemoryEx(size {:X}, align {:X}) -> {:08X}", size,
         alignment, result);
  if (status == 0) {
    KeepBigAllocation(out_base, out_size);
  }
  ctx.r3.u64 = result;
}

// ---------------------------------------------------------------- strings, exceptions

namespace {

// A guest C string, bounded so a missing terminator cannot run off into the
// reservation. Guest memory is mapped throughout, so the read itself cannot fault.
std::string_view GuestString(uint8_t* base, uint32_t address, size_t limit) {
  if (!address) {
    return {};
  }
  const char* text = reinterpret_cast<const char*>(base + address);
  return std::string_view(text, strnlen(text, limit));
}

}  // namespace

// void RtlInitAnsiString(PANSI_STRING destination, PCSZ source)
//
// ANSI_STRING is { u16 Length; u16 MaximumLength; u32 Buffer }, big-endian - the
// buffer is the caller's own string, not a copy, as Xenia and the console do it.
//
// Logged, for now: this is how the title builds the paths it is about to open, so the
// strings it passes are the clearest view of what startup is looking for. Capped, since
// a title calls this constantly once it runs.
PPC_FUNC(__imp__RtlInitAnsiString) {
  const uint32_t destination = ctx.r3.u32;
  const uint32_t source = ctx.r4.u32;
  if (!destination) {
    return;
  }
  const std::string_view text = GuestString(base, source, 0xFFFE);
  const auto length = uint16_t(text.size());
  PPC_STORE_U16(destination + 0, source ? length : 0);
  PPC_STORE_U16(destination + 2, source ? uint16_t(length + 1) : 0);
  PPC_STORE_U32(destination + 4, source);

  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 64) {
    XELOGI("RtlInitAnsiString(\"{}\")", text);
  }
}

// void RtlRaiseException(PEXCEPTION_RECORD record)
//
// The real one does not return: it dispatches the exception to a handler and unwinds
// to it. This runtime cannot unwind guest frames yet, and returning instead - which the
// weak stub did - runs the title on past a `throw` down a path its compiler never meant
// to be taken (BUG-032). So:
//
//   0x406D1388  the MSVC "set thread name" convention. A title raises it on purpose and
//               its own handler simply continues, so it is logged and returned from -
//               as Xenia does.
//   0xE06D7363  a C++ `throw`. The thrown type's name is decoded from the ThrowInfo
//               (layouts from Xenia's xboxkrnl_debug.cc), because that says what went
//               wrong in the title's own words.
//   anything else, and every C++ throw: logged with the guest call chain, and the guest
//               thread stops there. A raise is the end of the run until exceptions are
//               dispatched, and the log says why it ended.
//
// EXCEPTION_RECORD: code, flags, nested record, address, number_parameters, then up to
// 15 parameter words - 0x50 bytes.
PPC_FUNC(__imp__RtlRaiseException) {
  const uint32_t record = ctx.r3.u32;
  if (!record) {
    return;
  }
  const uint32_t code = PPC_LOAD_U32(record + 0);
  const uint32_t flags = PPC_LOAD_U32(record + 4);
  const uint32_t address = PPC_LOAD_U32(record + 12);
  const uint32_t parameters = PPC_LOAD_U32(record + 16);
  auto parameter = [&](uint32_t i) { return PPC_LOAD_U32(record + 20 + 4 * i); };

  if (code == 0x406D1388) {
    // THREADNAME_INFO { u32 type = 0x1000; u32 name; u32 thread_id; u32 flags }
    const std::string_view thread_name = GuestString(base, parameter(1), 64);
    XELOGI("RtlRaiseException: thread named \"{}\" (thread id {:08X})", thread_name,
           parameter(2));
    // -1 is the calling thread, the only case seen; a title naming another thread by
    // id would need the id looked up, and would be logged above either way.
    if (parameter(2) == 0xFFFFFFFF) {
      warden::NameCurrentGuestThread(thread_name);
    }
    return;
  }

  XELOGE("RtlRaiseException: code {:08X} flags {:08X} at {:08X}, {} parameters "
         "[{:08X} {:08X} {:08X}]", code, flags, address, parameters, parameter(0),
         parameter(1), parameter(2));
  if (code == 0xE06D7363 && parameters >= 3) {
    // parameter 1 is the thrown object, 2 its ThrowInfo:
    //   ThrowInfo           +12 CatchableTypeArray*
    //   CatchableTypeArray  +0 count, +4 CatchableType*[]
    //   CatchableType       +4 TypeDescriptor*
    //   TypeDescriptor      +8 the decorated name, ".?AV<type>@@"
    // The first catchable type is the thrown type itself.
    const uint32_t throw_info = parameter(2);
    const uint32_t types = throw_info ? PPC_LOAD_U32(throw_info + 12) : 0;
    const uint32_t count = types ? PPC_LOAD_U32(types) : 0;
    const uint32_t first = count ? PPC_LOAD_U32(types + 4) : 0;
    const uint32_t descriptor = first ? PPC_LOAD_U32(first + 4) : 0;
    XELOGE("  a C++ throw of {} (object at {:08X}, {} catchable types)",
           descriptor ? GuestString(base, descriptor + 8, 128) : std::string_view("?"),
           parameter(1), count);
  }
  xe::FlushLog();
  warden::LogGuestCallChainOnFault(base);
  warden::LogGuestTraceOnFault();
  XELOGE("guest thread stopped: this runtime cannot unwind to the title's handler "
         "yet (BUG-032)");
  xe::FlushLog();
  for (;;) {
    std::this_thread::sleep_for(std::chrono::hours(1));
  }
}

// NTSTATUS MmQueryStatistics(PMM_STATISTICS stats)
//
// **The crash 5.1 chased was this stub.** The title's GlobalMemoryStatus wrapper
// (sub_82AB4A00) calls it while its allocator initialises itself (sub_826613C0). The
// weak stub wrote nothing and left r3 = 0 - STATUS_SUCCESS - so the allocator sized
// itself against a console reporting no memory at all, and every allocation returned
// null. The first object built from one was a singleton (constructed by
// sub_8251C748, cached by sub_8251C810), and sub_8251B240 called its vtable through
// that null: the "indirect call to guest 00000000". Found with the guest stack walk.
//
// The layout is Xenia's X_MM_QUERY_STATISTICS_RESULT
// (third_party/xenia-canary/src/xenia/kernel/xboxkrnl/xboxkrnl_memory.h): 104 bytes,
// which is the length the title writes into `size` before calling - the check that
// this is the structure it expects. All big-endian:
//
//   0 size  4 total_physical_pages  8 kernel_pages
//   12 title section, 11 words:  available_pages, total_virtual_memory_bytes,
//      reserved_virtual_memory_bytes, physical/pool/stack/image/heap/virtual/
//      page_table/cache pages
//   56 system section, the same 11 words   100 highest_physical_page
//
// The title reads four of these: total_physical_pages, title.available_pages,
// title.total_virtual_memory_bytes and title.reserved_virtual_memory_bytes.
//
// **One value deliberately differs from Xenia.** Xenia reports available pages as
// 512 MB less what has been allocated. This port's physical heap hands out only the
// upper half (warden_guest_heap.cc), so that would promise memory AllocatePhysicalMemory
// cannot give, and a title that sized a later allocation from it would get null back
// again. Available is therefore what the heap can still actually hand out. The rest
// follow Xenia's xeMmQueryStatistics, which titles have been run against; the page
// counts in the title section beyond the four read here are its guesses, kept as such.
PPC_FUNC(__imp__MmQueryStatistics) {
  constexpr uint32_t kSize = 104;
  constexpr uint32_t kPage = 0x1000;
  const uint32_t stats = ctx.r3.u32;
  if (!stats) {
    ctx.r3.u64 = 0xC000000D;  // STATUS_INVALID_PARAMETER
    return;
  }
  const uint32_t asked_size = PPC_LOAD_U32(stats);
  if (asked_size != kSize) {
    XELOGE("MmQueryStatistics: size {} is not the {} this layout has", asked_size, kSize);
    xe::FlushLog();
    ctx.r3.u64 = 0xC0000023;  // STATUS_BUFFER_TOO_SMALL, as Xenia and the kernel do
    return;
  }

  uint32_t physical_in_use = 0;
  uint32_t physical_available = 0;
  warden::PhysicalMemoryUsage(physical_in_use, physical_available);

  std::memset(base + stats, 0, kSize);
  PPC_STORE_U32(stats + 0, kSize);
  PPC_STORE_U32(stats + 4, 0x00020000);  // total physical pages: 512 MB
  PPC_STORE_U32(stats + 8, 0x00000100);  // kernel pages
  // The title section.
  PPC_STORE_U32(stats + 12, physical_available / kPage);  // available pages
  PPC_STORE_U32(stats + 16, 0x2FFE0000);                  // total virtual bytes
  PPC_STORE_U32(stats + 20, physical_in_use);             // reserved virtual bytes
  PPC_STORE_U32(stats + 24, 0x00001000);                  // physical pages
  PPC_STORE_U32(stats + 28, 0x00000010);                  // pool pages
  for (uint32_t offset = 32; offset <= 52; offset += 4) {
    // stack, image, heap, virtual, page table and cache pages
    PPC_STORE_U32(stats + offset, 0x00000100);
  }
  // The system section (56..99) stays zero, as Xenia leaves it.
  PPC_STORE_U32(stats + 100, 0x0001FFFF);  // highest physical page

  XELOGI("MmQueryStatistics -> {} pages available, {:X} bytes physical in use",
         physical_available / kPage, physical_in_use);
  xe::FlushLog();
  ctx.r3.u64 = 0;  // STATUS_SUCCESS
}

// ---------------------------------------------------------------- console settings

// NTSTATUS ExGetXConfigSetting(USHORT category, USHORT setting, PVOID buffer,
//                              USHORT buffer_size, PUSHORT required_size)
//
// The console's stored settings - region, language, video and parental flags, the
// time zone. **The stub's refusal here is what cancelled Direct3D device creation:**
// the title's device init (sub_82DA4800) asks for the user video flags and treats a
// failing status as "no display", tearing the device down it had just built, and the
// renderer then called through the device it no longer had (5.1h).
//
// Values are Xenia's defaults (kernel/xconfig.cc SetDefaults) for an NTSC-M, English,
// United States console with parental controls off, except that the time zone is UTC
// rather than Xenia's chosen one - the device has a clock of its own. Sizes are what
// the title passes for each id, which is what the real kernel's table holds. An id
// not listed is refused and logged: it is the next value to find out.
namespace warden {
bool VideoFourByThree();  // warden_video.cc: the 4:3 test
}  // namespace warden

PPC_FUNC(__imp__ExGetXConfigSetting) {
  const uint32_t category = ctx.r3.u32 & 0xFFFF;
  const uint32_t setting = ctx.r4.u32 & 0xFFFF;
  const uint32_t buffer = ctx.r5.u32;
  const uint32_t buffer_size = ctx.r6.u32 & 0xFFFF;
  const uint32_t required_size_pointer = ctx.r7.u32;

  struct Setting {
    uint32_t category, id, size;
    uint32_t value;        // for sizes 1 and 4
    const char* text;      // for 4-byte names
  };
  // Secured category 2; user category 3. Flags per Xenia's xconfig.h.
  static constexpr Setting kSettings[] = {
      {2, 0x02, 4, 0x00400100, nullptr},  // AV region: NTSC-M
      {3, 0x01, 4, 0, nullptr},           // time zone bias, minutes: UTC
      {3, 0x02, 4, 0, "UTC"},             // standard-time name
      {3, 0x03, 4, 0, "UTC"},             // daylight-time name
      {3, 0x04, 4, 0, nullptr},           // standard date: none
      {3, 0x05, 4, 0, nullptr},           // daylight date: none
      {3, 0x06, 4, 0, nullptr},           // standard bias
      {3, 0x07, 4, 0, nullptr},           // daylight bias
      {3, 0x09, 4, 1, nullptr},           // language: English
      {3, 0x0A, 4, 0x00010000, nullptr},  // video flags: widescreen
      {3, 0x0B, 4, 0x00010001, nullptr},  // audio flags: Dolby Digital + Pro Logic
      {3, 0x0C, 4, 0x00000040, nullptr},  // retail flags: dashboard initialised
      {3, 0x0E, 1, 103, nullptr},         // country: United States
      {3, 0x0F, 1, 0x03, nullptr},        // parental control flags: Live allowed
      {3, 0x16, 4, 0x000000FF, nullptr},  // parental control, games: no restrictions
      {3, 0x19, 4, 0, nullptr},           // parental control, game rating
  };

  const Setting* found = nullptr;
  for (const Setting& candidate : kSettings) {
    if (candidate.category == category && candidate.id == setting) {
      found = &candidate;
      break;
    }
  }
  if (!found) {
    XELOGW("ExGetXConfigSetting(category {}, setting {}, {} bytes): not a setting this "
           "runtime has", category, setting, buffer_size);
    xe::FlushLog();
    ctx.r3.u64 = 0xC000000Du;  // STATUS_INVALID_PARAMETER
    return;
  }
  if (required_size_pointer) {
    PPC_STORE_U16(required_size_pointer, uint16_t(found->size));
  }
  if (buffer && buffer_size < found->size) {
    ctx.r3.u64 = 0xC0000023u;  // STATUS_BUFFER_TOO_SMALL
    return;
  }
  if (buffer) {
    if (found->text) {
      std::memset(base + buffer, 0, 4);
      std::memcpy(base + buffer, found->text, std::strlen(found->text));
    } else if (found->size == 1) {
      base[buffer] = uint8_t(found->value);
    } else if (category == 3 && setting == 0x0A && warden::VideoFourByThree()) {
      // The 4:3 test: the console's video flags without widescreen.
      PPC_STORE_U32(buffer, 0);
    } else if (category == 3 && setting == 0x09) {
      // PLAN 10.16: the Language option (files/language) - the disc carries English,
      // Spanish and Italian text (its voices are English): the console's 1, 5 and 6.
      static const uint32_t language = [] {
        char word[16] = {};
        const char* dir = std::getenv("WARDEN_FILES_DIR");
        if (FILE* file =
                dir ? std::fopen((std::string(dir) + "/language").c_str(), "r") : nullptr) {
          if (std::fscanf(file, "%15s", word) != 1) {
            word[0] = 0;
          }
          std::fclose(file);
        }
        const uint32_t code = !std::strcmp(word, "es") ? 5u : !std::strcmp(word, "it") ? 6u : 1u;
        XELOGI("language: the console's language {} ({})", code, word[0] ? word : "en");
        return code;
      }();
      PPC_STORE_U32(buffer, language);
    } else {
      PPC_STORE_U32(buffer, found->value);
    }
  }
  ctx.r3.u64 = 0;  // STATUS_SUCCESS
}

// PHYSICAL_ADDRESS MmGetPhysicalAddress(PVOID address)
//
// The physical address behind a virtual one in the 0xA0000000 / 0xC0000000 /
// 0xE0000000 views - what the title hands the GPU for its ring buffer. 0 for an
// address that is not physical, as Xenia answers; the stub's 0 for everything is what
// the title was building its command buffer on before this existed.
PPC_FUNC(__imp__MmGetPhysicalAddress) {
  (void)base;
  xe::Memory* memory = warden::GuestMemory();
  const uint32_t physical = memory ? memory->GetPhysicalAddress(ctx.r3.u32) : UINT32_MAX;
  ctx.r3.u64 = physical == UINT32_MAX ? 0u : physical;
}

// ULONG RtlNtStatusToDosError(NTSTATUS status)
//
// The title's GetLastError. The stub answered 0 for everything - ERROR_SUCCESS - so
// every failure it converted read as success. The common statuses this runtime hands
// back, mapped as Windows maps them; anything else that failed becomes
// ERROR_MR_MID_NOT_FOUND (317), which is also what Windows says for a status it has no
// message for, and is logged so the table can grow.
PPC_FUNC(__imp__RtlNtStatusToDosError) {
  (void)base;
  const uint32_t status = ctx.r3.u32;
  struct Entry {
    uint32_t status, error;
  };
  static constexpr Entry kTable[] = {
      {0x00000000, 0},     // SUCCESS
      {0x00000102, 1460},  // TIMEOUT -> ERROR_TIMEOUT
      {0x80000005, 234},   // BUFFER_OVERFLOW -> ERROR_MORE_DATA
      {0x80000006, 18},    // NO_MORE_FILES
      {0xC0000002, 120},   // NOT_IMPLEMENTED -> ERROR_CALL_NOT_IMPLEMENTED
      {0xC0000003, 87},    // INVALID_INFO_CLASS -> ERROR_INVALID_PARAMETER
      {0xC0000004, 24},    // INFO_LENGTH_MISMATCH -> ERROR_BAD_LENGTH
      {0xC0000008, 6},     // INVALID_HANDLE
      {0xC000000D, 87},    // INVALID_PARAMETER
      {0xC000000F, 2},     // NO_SUCH_FILE -> ERROR_FILE_NOT_FOUND
      {0xC0000011, 38},    // END_OF_FILE -> ERROR_HANDLE_EOF
      {0xC0000017, 8},     // NO_MEMORY -> ERROR_NOT_ENOUGH_MEMORY
      {0xC0000022, 5},     // ACCESS_DENIED
      {0xC0000023, 122},   // BUFFER_TOO_SMALL -> ERROR_INSUFFICIENT_BUFFER
      {0xC0000024, 6},     // OBJECT_TYPE_MISMATCH -> ERROR_INVALID_HANDLE
      {0xC0000033, 123},   // OBJECT_NAME_INVALID -> ERROR_INVALID_NAME
      {0xC0000034, 2},     // OBJECT_NAME_NOT_FOUND -> ERROR_FILE_NOT_FOUND
      {0xC000003A, 3},     // OBJECT_PATH_NOT_FOUND -> ERROR_PATH_NOT_FOUND
      {0xC0000047, 298},   // SEMAPHORE_LIMIT_EXCEEDED -> ERROR_TOO_MANY_POSTS
      {0xC000009A, 1450},  // INSUFFICIENT_RESOURCES -> ERROR_NO_SYSTEM_RESOURCES
      {0xC00000A2, 19},    // MEDIA_WRITE_PROTECTED -> ERROR_WRITE_PROTECT
      {0xC00000BA, 5},     // FILE_IS_A_DIRECTORY -> ERROR_ACCESS_DENIED
      {0xC00000BB, 50},    // NOT_SUPPORTED
      {0xC0000103, 267},   // NOT_A_DIRECTORY -> ERROR_DIRECTORY
  };
  for (const Entry& entry : kTable) {
    if (entry.status == status) {
      ctx.r3.u64 = entry.error;
      return;
    }
  }
  if (!(status & 0x80000000)) {
    ctx.r3.u64 = 0;  // a success or informational status
    return;
  }
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 20) {
    XELOGW("RtlNtStatusToDosError({:08X}): not in the table", status);
  }
  ctx.r3.u64 = 317;
}
