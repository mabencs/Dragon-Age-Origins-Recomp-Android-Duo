// The kernel's exported variables, and the import records that point at them.
// PLAN 5.1d, done when BUG-038 made it unavoidable.
//
// An XEX imports two kinds of thing. Functions arrive as thunks, and XenonRecomp bound
// those to the __imp__ implementations at translation time. Variables arrive as a
// 4-byte record in the image that the console's loader overwrites with the variable's
// address; the title then reads the record and dereferences it. Nothing here had done
// that, so each record still held its raw ordinal word (0x00010059 for
// KeDebugMonitorData), which the title followed as a pointer into low memory - and
// whatever it found there, it used: the null call that ended the Render thread, and
// the D3D device pointer the title had been storing through VdGlobalDevice's record
// into page 0x10000.
//
// The values are Xenia's (xboxkrnl_module.cc, xboxkrnl_video.cc, kernel_state.cc):
// the monitors absent, six cores, a 2.0.17559 kernel, "default.xex" as the command
// line, a 500 MHz GPU clock, and KeTimeStampBundle kept current by a thread - the
// title reads its tick count for every timeout it measures.

#include "warden_kernel_variables.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "warden_guest_heap.h"
#include "warden_suspend.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/memory.h"

namespace warden {

namespace {

constexpr const char* kKernelLibrary = "xboxkrnl.exe";

// xboxkrnl.exe ordinals (Xenia's xboxkrnl_table.inc).
enum : uint32_t {
  kExThreadObjectType = 0x001B,
  kKeDebugMonitorData = 0x0059,
  kKeTimeStampBundle = 0x00AD,
  kXboxHardwareInfo = 0x0156,
  kXboxKrnlVersion = 0x0158,
  kXexExecutableModuleHandle = 0x0193,
  kExLoadedCommandLine = 0x01AE,
  kVdGlobalDevice = 0x01BE,
  kVdGlobalXamDevice = 0x01BF,
  kVdGpuClockInMHz = 0x01C0,
  kVdHSIOCalibrationLock = 0x01C1,
  kKeCertMonitorData = 0x0266,
};

struct Variable {
  const char* name;
  uint32_t size;
};

const std::unordered_map<uint32_t, Variable>& KnownVariables() {
  static const std::unordered_map<uint32_t, Variable> known = {
      {kExThreadObjectType, {"ExThreadObjectType", 0x40}},
      {kKeDebugMonitorData, {"KeDebugMonitorData", 4}},
      {kKeTimeStampBundle, {"KeTimeStampBundle", 24}},
      {kXboxHardwareInfo, {"XboxHardwareInfo", 16}},
      {kXboxKrnlVersion, {"XboxKrnlVersion", 8}},
      {kXexExecutableModuleHandle, {"XexExecutableModuleHandle", 4}},
      {kExLoadedCommandLine, {"ExLoadedCommandLine", 1024}},
      {kVdGlobalDevice, {"VdGlobalDevice", 4}},
      {kVdGlobalXamDevice, {"VdGlobalXamDevice", 4}},
      {kVdGpuClockInMHz, {"VdGpuClockInMHz", 4}},
      {kVdHSIOCalibrationLock, {"VdHSIOCalibrationLock", 28}},
      {kKeCertMonitorData, {"KeCertMonitorData", 4}},
  };
  return known;
}

std::mutex g_mutex;
std::unordered_map<uint32_t, uint32_t> g_addresses;  // ordinal -> guest address
uint32_t g_timestamp_bundle = 0;
xe::Memory* g_memory = nullptr;

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, sizeof(value));
}

void Store64(uint8_t* base, uint32_t address, uint64_t value) {
  value = __builtin_bswap64(value);
  std::memcpy(base + address, &value, sizeof(value));
}

void Store16(uint8_t* base, uint32_t address, uint16_t value) {
  value = __builtin_bswap16(value);
  std::memcpy(base + address, &value, sizeof(value));
}

// The console's clock interrupt updates the bundle every millisecond; the title's
// GetTickCount is its tick_count, and its timeouts are measured against it. The
// interrupt time and the tick count leave out the time the guest spent suspended
// (warden_suspend.cc, BUG-057), so they stand still in the background as the timebase
// does; the system time is the wall clock, and runs on.
void UpdateTimeStampBundle(uint8_t* base, uint32_t bundle, uint64_t interrupt_suspended,
                           uint32_t uptime_suspended) {
  Store64(base, bundle + 0, xe::Clock::QueryGuestInterruptTime() - interrupt_suspended);
  Store64(base, bundle + 8, xe::Clock::QueryGuestSystemTime());
  Store32(base, bundle + 16, xe::Clock::QueryGuestUptimeMillis() - uptime_suspended);
}

void TimeStampThread() {
  uint64_t interrupt_suspended = 0;  // 100 ns units
  uint32_t uptime_suspended = 0;     // milliseconds
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (GuestSuspended()) {
      // Held with the title's threads, the bundle left as it was.
      const uint64_t interrupt_before = xe::Clock::QueryGuestInterruptTime();
      const uint32_t uptime_before = xe::Clock::QueryGuestUptimeMillis();
      GuestSuspendPoint("kernel clock");
      interrupt_suspended += xe::Clock::QueryGuestInterruptTime() - interrupt_before;
      uptime_suspended += xe::Clock::QueryGuestUptimeMillis() - uptime_before;
    }
    uint8_t* base = g_memory ? g_memory->virtual_membase() : nullptr;
    if (base && g_timestamp_bundle) {
      UpdateTimeStampBundle(base, g_timestamp_bundle, interrupt_suspended, uptime_suspended);
    }
  }
}

// The initial value of each variable, in its freshly zeroed home.
void Initialise(uint8_t* base, uint32_t ordinal, uint32_t address) {
  switch (ordinal) {
    case kXboxHardwareInfo:
      Store32(base, address + 0, 0x20);  // flags, as Xenia reports them
      base[address + 4] = 6;             // cores
      break;
    case kXboxKrnlVersion:
      Store16(base, address + 0, 2);      // major
      Store16(base, address + 2, 0);      // minor
      Store16(base, address + 4, 17559);  // build
      Store16(base, address + 6, 0x8000);  // qfe
      break;
    case kExLoadedCommandLine: {
      static const char kCommandLine[] = "\"default.xex\"";
      std::memcpy(base + address, kCommandLine, sizeof(kCommandLine));
      break;
    }
    case kVdGpuClockInMHz:
      Store32(base, address, 500);
      break;
    case kKeTimeStampBundle:
      g_timestamp_bundle = address;
      UpdateTimeStampBundle(base, address, 0, 0);  // at the load: nothing suspended yet
      break;
    default:
      // Zero: no debug or certificate monitor, no global device yet, a thread object
      // type nothing inspects, a module handle nothing dereferences yet.
      break;
  }
}

}  // namespace

void PatchImportVariables(xe::Memory& memory, const std::vector<XexImport>& imports) {
  uint8_t* base = memory.virtual_membase();
  if (!base) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_memory = &memory;

  // A type-0 record is a value slot, and function imports have one too (the thunk
  // reads its target from it on the console; here the recompiler bound the thunk, so
  // the slot is never read). What tells a variable from a function is the export's
  // kind, which only the kernel's table knows: the variables this title imports are
  // all xboxkrnl's and all in KnownVariables. Everything else is left as it is.
  auto is_variable = [](const XexImport& import) {
    return import.variable && import.library == kKernelLibrary &&
           KnownVariables().count(import.ordinal) != 0;
  };

  // Size the block: every variable, 16-byte aligned.
  uint32_t total = 0;
  for (const XexImport& import : imports) {
    if (is_variable(import)) {
      total += (KnownVariables().at(import.ordinal).size + 15) & ~15u;
    }
  }
  if (!total) {
    XELOGI("import variables: none");
    return;
  }
  uint32_t block = 0;
  uint32_t got = 0;
  if (AllocateGuestMemory(0, total, 0x1000, block, got) != 0 || !block) {
    XELOGE("import variables: no guest memory for {} bytes", total);
    return;
  }
  std::memset(base + block, 0, got);

  uint32_t next = block;
  unsigned patched = 0;
  for (const XexImport& import : imports) {
    if (!is_variable(import)) {
      continue;
    }
    const Variable& known = KnownVariables().at(import.ordinal);
    const uint32_t address = next;
    next += (known.size + 15) & ~15u;
    Initialise(base, import.ordinal, address);
    g_addresses[import.ordinal] = address;
    XELOGI("import variable {} (ordinal {:#x}): record {:08X} -> {:08X}", known.name,
           import.ordinal, import.slot, address);
    Store32(base, import.slot, address);
    ++patched;
  }
  XELOGI("import variables: {} records patched, {} bytes at {:08X}", patched, total, block);
  if (g_timestamp_bundle) {
    static std::once_flag once;
    std::call_once(once, [] { std::thread(TimeStampThread).detach(); });
  }
}

uint32_t KernelVariableAddress(uint32_t ordinal) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto found = g_addresses.find(ordinal);
  return found == g_addresses.end() ? 0 : found->second;
}

}  // namespace warden
