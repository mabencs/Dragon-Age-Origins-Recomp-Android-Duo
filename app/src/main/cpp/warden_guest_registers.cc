// Dumping guest registers from a fault handler.
//
// A tombstone gives host registers, which have to be mapped back to guest ones by
// hand through generated code. The fault handler runs on the faulting thread, and for
// a guest fault that is a guest thread - so its PPCContext is right there in
// thread-local storage. Printing it turns "a bad pointer somewhere" into "r11 held
// this", which is the whole question.
//
// Kept in its own translation unit so warden_memory.cc does not have to include the
// generated PPC headers, which drag in simde and every x86 intrinsic shim.

#include "warden_guest_registers.h"

#include <android/log.h>

#include <cstring>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "warden_guest.h"

namespace warden {

namespace {

// Deeper than DAO's startup gets; a bound so a corrupt backchain cannot loop.
constexpr int kMaxFrames = 64;
// r1 further than this below the stack top is not this thread's stack. The XEX header
// asks for 256 KB; four times that is still plainly a stack.
constexpr uint32_t kMaxStackDepth = 0x100000;

// A big-endian guest word. Only ever called on addresses inside the 4 GiB guest
// reservation, which is mapped readable throughout, so it cannot fault.
uint32_t LoadGuest32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return __builtin_bswap32(value);
}

bool InCode(uint32_t address) {
  return address >= warden::game::CodeBase() && address - warden::game::CodeBase() < warden::game::CodeSize();
}

// The recompiled function an address is in: the highest entry point at or below it.
// A linear scan, deliberately - this runs in a signal handler, and the table's order
// is the generator's business. 53,108 compares per frame is nothing next to a crash.
uint32_t ContainingFunction(uint32_t address) {
  uint32_t best = 0;
  constexpr size_t kSanityLimit = 1u << 20;
  for (size_t i = 0; i < kSanityLimit; ++i) {
    const PPCFuncMapping& entry = warden::game::Mappings()[i];
    if (!entry.host && !entry.guest) {
      break;
    }
    const uint32_t start = uint32_t(entry.guest);
    if (start <= address && start > best) {
      best = start;
    }
  }
  return best;
}

enum class Call { kDirect, kIndirect, kUnknown };

// What the instruction before a return address was. A `bl` names its callee, which is
// how the walk names the innermost function and checks each frame against the next.
Call DecodeCallBefore(const uint8_t* base, uint32_t return_address, uint32_t& target) {
  const uint32_t site = return_address - 4;
  if (!InCode(site)) {
    return Call::kUnknown;
  }
  const uint32_t insn = LoadGuest32(base, site);
  const uint32_t opcode = insn >> 26;
  if (opcode == 18 && (insn & 1)) {  // bl, bla
    int32_t displacement = int32_t(insn & 0x03FFFFFC);
    if (displacement & 0x02000000) {
      displacement -= 0x04000000;  // LI is a signed 26-bit field
    }
    target = ((insn & 2) ? 0 : site) + uint32_t(displacement);
    return Call::kDirect;
  }
  if (opcode == 19 && ((insn >> 1) & 0x3FF) == 528 && (insn & 1)) {  // bcctrl
    return Call::kIndirect;
  }
  return Call::kUnknown;
}

}  // namespace

bool IsGuestCode(uint32_t guest_address) { return InCode(guest_address); }

uint32_t GuestFunctionContaining(uint32_t guest_address) {
  return InCode(guest_address) ? ContainingFunction(guest_address) : 0;
}

bool DescribeNullIndirectCall(const void* host_address, const void* membase,
                              uint32_t& guest_address_out) {
  // A null indirect call does not fault at a null address. XenonRecomp resolves
  // indirect calls through a table in guest memory:
  //
  //   PPC_LOOKUP_FUNC(base, y) = *(PPCFunc**)(base + PPC_IMAGE_BASE + PPC_IMAGE_SIZE +
  //                                           (uint64_t(uint32_t(y) - PPC_CODE_BASE) * 2))
  //
  // and PPC_CODE_BASE is `ull`, so the subtraction happens in 64-bit unsigned and does
  // *not* wrap at 32 bits. For y = 0 the offset is 0xFFFFFFFEFBBE0000, which lands far
  // below the base and looks like a wild pointer.
  //
  // But it is invertible, which is better than a trap stub: no memory, no per-address
  // thunks, and it recovers the guest address the title was trying to call. The table
  // stays zeroed, and a fault in it decodes to the address whose slot was empty.
  const auto host = reinterpret_cast<uintptr_t>(host_address);
  const auto base = reinterpret_cast<uintptr_t>(membase);
  const uint64_t offset = uint64_t(host) - uint64_t(base) - warden::game::ImageBase() -
                          warden::game::ImageSize();
  if (offset & 1) {
    return false;  // every slot is at an even offset
  }
  const uint64_t scaled = offset / 2;
  const uint32_t guest = uint32_t(scaled + warden::game::CodeBase());
  // Only believe it if re-deriving the address from the recovered guest value gives
  // back exactly what faulted. That makes a false positive essentially impossible,
  // which matters because this runs on an unrelated crash too.
  const uint64_t check = uint64_t(base) + warden::game::ImageBase() + warden::game::ImageSize() +
                         ((uint64_t(guest) - warden::game::CodeBase()) * 2);
  if (check != uint64_t(host)) {
    return false;
  }
  guest_address_out = guest;
  return true;
}

void LogGuestRegistersOnFault() {
  PPCContext* ctx = CurrentGuestContext();
  if (!ctx) {
    __android_log_print(ANDROID_LOG_FATAL, "Warden",
                        "  (no guest context on this thread - not a guest fault)");
    return;
  }
  // __android_log_print only; this is a signal handler. Printed in a few lines rather
  // than one so a truncated log still carries the useful registers: r1 is the stack,
  // r3 the first argument, and lr says who called.
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "  guest lr=%08X r1=%08X r3=%08X r4=%08X",
                      uint32_t(ctx->lr), ctx->r1.u32, ctx->r3.u32, ctx->r4.u32);
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "  guest r11=%08X r12=%08X r29=%08X r30=%08X r31=%08X",
                      ctx->r11.u32, ctx->r12.u32, ctx->r29.u32, ctx->r30.u32,
                      ctx->r31.u32);
  // The 64-bit halves of the registers most likely to be holding a value that was
  // never truncated to 32 bits - which is what a fault outside the guest space looks
  // like.
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "  guest r3.u64=%016llX r11.u64=%016llX r31.u64=%016llX",
                      (unsigned long long)ctx->r3.u64,
                      (unsigned long long)ctx->r11.u64,
                      (unsigned long long)ctx->r31.u64);
}

void LogGuestCallChainOnFault(const void* membase) {
  PPCContext* ctx = CurrentGuestContext();
  const uint32_t top = CurrentGuestStackTop();
  if (!ctx || !top || !membase) {
    return;  // LogGuestRegistersOnFault has already said there is no guest context
  }
  const auto* base = static_cast<const uint8_t*>(membase);
  const uint32_t r1 = ctx->r1.u32;
  if (r1 >= top || top - r1 > kMaxStackDepth) {
    __android_log_print(ANDROID_LOG_FATAL, "Warden",
                        "  guest call chain: r1=%08X is not in this thread's stack "
                        "(top %08X)", r1, top);
    return;
  }

  // The frame layout, read off the generated prologues rather than assumed:
  //
  //   mflr r12 ; bl __savegprlr_N   the helper does  stw r12,-8(r1)
  //   stwu r1,-F(r1)                stores the entry r1 *at* the new r1
  //
  // (or `stw r12,-8(r1)` inline, to the same slot). So from a frame, [frame] is the
  // caller's frame and [caller - 8] is the return address into the caller.
  //
  // ctx.r1 can lag one frame behind the code: every translated function takes
  // `PPCContext& __restrict__`, so the compiler may sink a register store past the
  // table load that faults. The stack itself cannot lag - stores to guest memory are
  // not reordered past a load from it - so the chain is right from wherever it starts.
  __android_log_print(ANDROID_LOG_FATAL, "Warden",
                      "  guest call chain from r1=%08X (newest first: return address, "
                      "the function it is in, what that function called)", r1);
  uint32_t frame = r1;
  uint32_t newer_function = 0;  // the function the previous line's return address is in
  for (int depth = 0; depth < kMaxFrames; ++depth) {
    const uint32_t caller = LoadGuest32(base, frame);
    // Stacks grow down, so a real backchain points strictly upward, by at least the
    // 8 bytes the saved lr needs, and never past the top.
    if (caller <= frame || caller - frame < 8 || caller > top) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "    (stopped: the backchain at %08X holds %08X)", frame,
                          caller);
      return;
    }
    const uint32_t ret = LoadGuest32(base, caller - 8);
    if (ret == 0 && caller == top) {
      // The entry point's own frame: it saved the lr it was entered with, which a
      // thread started by the runtime leaves at zero.
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "    #%-2d (the thread's entry - returns nowhere)", depth);
      return;
    }
    const uint32_t function = InCode(ret) ? ContainingFunction(ret) : 0;
    uint32_t target = 0;
    const Call call = DecodeCallBefore(base, ret, target);
    // A frame's call should land on the function the newer frame is in. Where it does
    // not, one of the two is junk - a leaf function that set up a frame without saving
    // lr leaves stale bytes in the slot - so say so rather than present it as fact.
    const bool consistent =
        depth == 0 || call != Call::kDirect || target == newer_function;
    if (call == Call::kDirect && consistent) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "    #%-2d %08X in sub_%08X  called sub_%08X", depth, ret,
                          function, target);
    } else if (call == Call::kDirect) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "    #%-2d %08X in sub_%08X  called sub_%08X  [!] but #%d is "
                          "in sub_%08X", depth, ret, function, target, depth - 1,
                          newer_function);
    } else if (call == Call::kIndirect) {
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "    #%-2d %08X in sub_%08X  called indirectly (bctrl)", depth,
                          ret, function);
    } else {
      // Print the word that is there. In the image it is whatever the title was
      // compiled to; if it is not a call, either this frame is junk or the code has
      // been overwritten since it was loaded - and the word says which.
      __android_log_print(ANDROID_LOG_FATAL, "Warden",
                          "    #%-2d %08X in sub_%08X  no call before it: [%08X] = %08X",
                          depth, ret, function, ret - 4,
                          InCode(ret - 4) ? LoadGuest32(base, ret - 4) : 0u);
    }
    newer_function = function;
    if (depth == 0) {
      // The newest frame's prologue saved the caller's r26-r31 just below the caller's
      // frame (__savegprlr_N: rN at caller - 8 * (32 - N), 64-bit), so the values the
      // faulting function was handed are still on the stack. Printed with the first
      // words each one points at: a null vtable call usually means an object whose
      // header was never what the code expected, and this shows the header.
      for (uint32_t n = 26; n <= 31; ++n) {
        const uint32_t slot = caller - 8 * (32 - n) + 4;  // low word of the 64-bit save
        if (slot < frame || slot + 4 > top) {
          continue;
        }
        const uint32_t value = LoadGuest32(base, slot);
        if (value >= 0x1000 && value <= 0xFFFFFFF0) {
          __android_log_print(ANDROID_LOG_FATAL, "Warden",
                              "      saved r%u=%08X -> [%08X %08X %08X %08X]", n, value,
                              LoadGuest32(base, value), LoadGuest32(base, value + 4),
                              LoadGuest32(base, value + 8), LoadGuest32(base, value + 12));
        } else {
          __android_log_print(ANDROID_LOG_FATAL, "Warden", "      saved r%u=%08X", n, value);
        }
      }
    }
    if (caller == top) {
      return;
    }
    frame = caller;
  }
  __android_log_print(ANDROID_LOG_FATAL, "Warden", "    (stopped after %d frames)",
                      kMaxFrames);
}

}  // namespace warden
