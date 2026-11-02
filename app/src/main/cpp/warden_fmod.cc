// A hook on FMOD's allocator, for BUG-043: the title's FMOD could not allocate the
// non-blocking loader's request pool (8192 x 44 bytes), set its once-flag anyway, and the
// first non-blocking load has waited for a free request node ever since. This wraps the
// recompiled allocator - `sub_82AF22A8(memory system, size, file, line, type mask, zero)`
// returns the block or 0 - to say what FMOD's memory looks like (a fixed pool of blocks
// with a bitmap, or a user allocator, or the default heap), which allocations fail, with
// the FMOD source file and line that asked, and which large ones fill the pool.
//
// The hook works because the recompiler emits every function as a weak `tu6_sub_X`
// alias of `__imp__tu6_sub_X`: a strong `tu6_sub_X` here takes both the direct calls and
// the function table's indirect ones, and calls the original by its `__imp__` name.

#include <atomic>
#include <cstring>
#include <mutex>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"

WARDEN_GAME_FUNC(__imp__tu6_sub_82AF22A8);

namespace {

// The FMOD memory system object, as sub_82AF22A8 reads it.
constexpr uint32_t kBitmap = 0;        // used-block bitmap of the fixed pool
constexpr uint32_t kPoolBase = 4;      // the fixed pool's memory
constexpr uint32_t kFixedPool = 8;     // byte: allocate from the fixed pool
constexpr uint32_t kBlockCount = 16;   // blocks in the fixed pool
constexpr uint32_t kPeakUsage = 292;   // bytes, high-water mark
constexpr uint32_t kCurrentUsage = 296;
constexpr uint32_t kUserAlloc = 312;   // the title's allocator callback, if any
constexpr uint32_t kBlockSize = 332;

std::mutex g_mutex;
bool g_described = false;
uint32_t g_calls = 0;
uint32_t g_failures = 0;
uint64_t g_bytes = 0;
uint32_t g_large_logged = 0;

const char* GuestString(uint8_t* base, uint32_t address) {
  // FMOD's source file names live in the module image.
  if (address < 0x82000000u || address >= 0x83400000u) {
    return "?";
  }
  const char* text = reinterpret_cast<const char*>(base + address);
  return strnlen(text, 128) < 128 ? text : "?";
}

const char* BaseName(const char* path) {
  const char* name = path;
  for (const char* p = path; *p; ++p) {
    if (*p == '\\' || *p == '/') {
      name = p + 1;
    }
  }
  return name;
}

}  // namespace

WARDEN_OVERRIDE(tu6_sub_82AF22A8) {
  const uint32_t memory = ctx.r3.u32;
  const uint32_t size = ctx.r4.u32;
  const uint32_t file = ctx.r5.u32;
  const uint32_t line = ctx.r6.u32;
  const uint32_t type = ctx.r7.u32;
  __imp__tu6_sub_82AF22A8(ctx, base);
  const uint32_t result = ctx.r3.u32;

  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_calls;
  if (result) {
    g_bytes += size;
  }
  const uint32_t current = memory ? PPC_LOAD_U32(memory + kCurrentUsage) : 0;
  const uint32_t peak = memory ? PPC_LOAD_U32(memory + kPeakUsage) : 0;
  if (!g_described && memory) {
    g_described = true;
    const uint32_t count = PPC_LOAD_U32(memory + kBlockCount);
    const uint32_t block = PPC_LOAD_U32(memory + kBlockSize);
    XELOGI("FMOD memory: system {:08X}: {} (bitmap {:08X}, pool {:08X}, {} blocks of {} = "
           "{} bytes), user allocator {:08X}, usage {} now / {} peak; first request {} "
           "bytes from {}:{}",
           memory, PPC_LOAD_U8(memory + kFixedPool) ? "fixed pool" : "default heap",
           PPC_LOAD_U32(memory + kBitmap), PPC_LOAD_U32(memory + kPoolBase), count, block,
           uint64_t(count) * block, PPC_LOAD_U32(memory + kUserAlloc), current, peak, size,
           BaseName(GuestString(base, file)), line);
  }
  if (!result) {
    if (++g_failures <= 16) {
      XELOGE("[{}] FMOD memory: {} bytes (type {:08X}) from {}:{} FAILED; usage {} now / {} "
             "peak after {} allocations ({} bytes asked in all), failure {}",
             warden::CurrentGuestThreadName(), size, type, BaseName(GuestString(base, file)),
             line, current, peak, g_calls, g_bytes, g_failures);
    }
    return;
  }
  if (size >= 32768 && g_large_logged++ < 48) {
    XELOGI("[{}] FMOD memory: {} bytes (type {:08X}) from {}:{} -> {:08X}; usage {} now / "
           "{} peak",
           warden::CurrentGuestThreadName(), size, type, BaseName(GuestString(base, file)),
           line, result, current, peak);
  }
}
