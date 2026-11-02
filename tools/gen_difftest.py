#!/usr/bin/env python3
"""
gen_difftest.py - generate a differential test for the recompiled game code.

Phase 2.3 asks a question compiling cannot answer: does the ARM64 build *behave*
the same as the x86-64 one? Compiling clean says nothing about `char` signedness,
float-to-int edge cases, or whether SIMD Everywhere's NEON lowering of a VMX
instruction matches the SSE original.

This selects recompiled functions that can be executed standalone, generates a
harness that runs each one from a deterministic starting state, and hashes the
resulting PPC context. Running the same harness on both architectures and diffing
the hashes shows exactly which functions disagree.

Selection: a candidate must not transfer control out of itself (no calls, no
longjmp, no indirect jumps), must not read anything non-deterministic (no
timebase), and must contain no backward branch, so it is guaranteed to terminate.
Memory access is allowed because guest memory is a fresh zero-filled mapping,
which is identical on both platforms.

Seeding is deliberately conservative:
  * GPR low 32 bits are bounded so computed addresses stay inside the mapping.
  * Floats and vector lanes get finite, normal values. NaN, infinity and denormal
    payload propagation legitimately differ between x86 and ARM, and seeding them
    would produce noise rather than findings. Those need their own targeted test.
  * fpscr is excluded from the hash: it mirrors the host FP control register, whose
    bit layout is architecture-specific by design (MXCSR vs FPCR).

Usage:
  python tools/gen_difftest.py                 # default 4000 functions
  python tools/gen_difftest.py --count 500
  python tools/gen_difftest.py --build ppc
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GENERATED = ROOT / "recomp" / "generated"
OUT = ROOT / "build" / "difftest"

FUNC = re.compile(r"^PPC_FUNC_IMPL\(__imp__(\w+)\) \{\n(.*?)^\}", re.M | re.S)
# Anything that leaves the function or reads non-deterministic state. Note these
# appear in the generated bodies as *macros*: matching PPCQueryTimebase alone misses
# PPC_QUERY_TIMEBASE(), and matching ctx.fn[ misses PPC_CALL_INDIRECT_FUNC. Both of
# those slipped through a first attempt. __builtin_unreachable is excluded as well:
# if it is ever reached the behaviour is undefined and could differ by platform for
# reasons that are not a port bug.
UNSAFE = re.compile(r"\bsub_[0-9A-Fa-f]{8}\b|tu6_sub_|__imp__|"
                    r"PPC_QUERY_TIMEBASE|PPCQueryTimebase|"
                    r"PPC_CALL_INDIRECT_FUNC|ctx\.fn\[|"
                    r"longjmp|setjmp|__builtin_debugtrap|__builtin_unreachable")
LABEL = re.compile(r"^loc_([0-9A-F]{8}):", re.M)
GOTO = re.compile(r"goto loc_([0-9A-F]{8})")

# Guest memory: the full 32-bit window, as the real runtime uses, so that *any*
# address the recompiled code computes is mapped. Bounding seeded registers is not
# enough on its own: effective addresses are uint32, so a small register plus a
# negative displacement (r1 + -8 with r1 near zero) wraps to just under 4 GB.
# Pages are only committed when touched, so this costs address space, not memory.
GUEST_SIZE = 1 << 32
# Seeded addresses sit in a low, mid-range band so the common case lands inside the
# region zeroed before each call, which keeps results deterministic and meaningful.
ADDR_BASE = 0x00200000
ADDR_SPAN = 0x00100000
# Zeroed before every call, covering that band plus room either side.
ZERO_SPAN = 8 << 20


def has_backward_goto(body):
    label_at = {m.group(1): m.start() for m in LABEL.finditer(body)}
    for m in GOTO.finditer(body):
        at = label_at.get(m.group(1))
        if at is not None and at < m.start():
            return True
    return False


def parse_context_fields(header):
    """Return the PPCContext members, in declaration order, as (type, name)."""
    text = header.read_text(encoding="utf-8", errors="replace")
    start = text.index("struct alignas(0x40) PPCContext")
    end = text.index("\n};", start)
    fields = []
    for line in text[start:end].splitlines()[1:]:
        m = re.match(r"\s*(\w+)\s+(\w+)(?:\s*=\s*[^;]+)?;", line)
        if m:
            fields.append((m.group(1), m.group(2)))
    return fields


def select(gen, count):
    chosen = []
    for f in sorted(gen.glob("ppc_recomp.*.cpp"), key=lambda p: int(p.stem.split(".")[1])):
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in FUNC.finditer(text):
            name, body = m.group(1), m.group(2)
            if UNSAFE.search(body) or has_backward_goto(body):
                continue
            simde = len(re.findall(r"simde_mm_\w+", body))
            chosen.append((name, simde, "base" in body, body.count("\n")))
    # Put the SIMD-using functions first: they are the ones most likely to diverge,
    # and they would otherwise be lost in the long tail of integer arithmetic.
    chosen.sort(key=lambda c: (-c[1], -c[2], -c[3], c[0]))
    return chosen[:count] if count else chosen


def emit(fields, funcs, build):
    gprs = [n for t, n in fields if t == "PPCRegister" and re.fullmatch(r"r\d+", n)]
    fprs = [n for t, n in fields if t == "PPCRegister" and re.fullmatch(r"f\d+", n)]
    other_regs = [n for t, n in fields if t == "PPCRegister" and n not in gprs and n not in fprs]
    vregs = [n for t, n in fields if t == "PPCVRegister"]
    crs = [n for t, n in fields if t == "PPCCRRegister"]

    L = []
    w = L.append
    w('// Generated by tools/gen_difftest.py - do not edit.')
    w('// Differential test: run recompiled functions from a deterministic state and')
    w('// hash the resulting context, so x86-64 and ARM64 results can be compared.')
    w('#include <cstdio>')
    w('#include <cstdint>')
    w('#include <cstring>')
    w('#include <cstddef>')
    w('#include "ppc_recomp_shared.h"')
    w('')
    w('#if defined(_WIN32)')
    w('#  include <windows.h>')
    w('#else')
    w('#  include <sys/mman.h>')
    w('#  include <csignal>')
    w('#  include <csetjmp>')
    w('#endif')
    w('')
    w(f'static constexpr size_t kGuestSize = {GUEST_SIZE}ull;')
    w(f'static constexpr uint32_t kAddrBase = {ADDR_BASE}u;')
    w(f'static constexpr uint32_t kAddrSpan = {ADDR_SPAN}u;')
    w(f'static constexpr size_t  kZeroSpan = {ZERO_SPAN}ull;')
    w('')
    w('// xorshift64* - same sequence on every platform.')
    w('static uint64_t g_state;')
    w('static inline uint64_t next64() {')
    w('    g_state ^= g_state >> 12;')
    w('    g_state ^= g_state << 25;')
    w('    g_state ^= g_state >> 27;')
    w('    return g_state * 0x2545F4914F6CDD1Dull;')
    w('}')
    w('')
    w('// Finite, normal values only - see the note in gen_difftest.py.')
    w('static inline double finiteDouble() {')
    w('    return double(int32_t(uint32_t(next64()))) / 4096.0;')
    w('}')
    w('static inline uint32_t finiteFloatBits() {')
    w('    return 0x3F800000u | uint32_t(next64() & 0x807FFFFFu);')
    w('}')
    w('')
    w('static uint64_t g_hash;')
    w('static inline void feed(const void* p, size_t n) {')
    w('    const uint8_t* b = (const uint8_t*)p;')
    w('    for (size_t i = 0; i < n; ++i) { g_hash ^= b[i]; g_hash *= 0x100000001B3ull; }')
    w('}')
    w('')
    w('static void seed(PPCContext& ctx) {')
    w('    std::memset(&ctx, 0, sizeof(ctx));')
    for n in gprs:
        w(f'    ctx.{n}.u64 = (next64() & 0xFFFFFFFF00000000ull) '
          f'| (kAddrBase + next64() % kAddrSpan);')
    for n in other_regs:
        w(f'    ctx.{n}.u64 = kAddrBase + next64() % kAddrSpan;')
    for n in fprs:
        w(f'    ctx.{n}.f64 = finiteDouble();')
    for n in vregs:
        w(f'    for (int i = 0; i < 4; ++i) ctx.{n}.u32[i] = finiteFloatBits();')
    for n in crs:
        w(f'    ctx.{n}.lt = next64() & 1; ctx.{n}.gt = next64() & 1;'
          f' ctx.{n}.eq = next64() & 1; ctx.{n}.so = next64() & 1;')
    w('    ctx.xer.so = next64() & 1; ctx.xer.ov = next64() & 1; ctx.xer.ca = next64() & 1;')
    w('    ctx.lr = 0;')
    w('    ctx.msr = 0x200A000;')
    w('    // Normalise the *whole* host FP mode, not just flush-to-zero. A previous')
    w('    // case may have executed mtfsf and left the host rounding mode changed;')
    w('    // without resetting it a later case reads different state through mffs,')
    w('    // which shows up as a false divergence.')
    w('    ctx.fpscr.csr = ctx.fpscr.getcsr();')
    w('    ctx.fpscr.disableFlushModeUnconditional();')
    w('    ctx.fpscr.storeFromGuest(PPC_ROUND_NEAREST);')
    w('}')
    w('')
    w('// fpscr is skipped: it mirrors the host FP control register (MXCSR vs FPCR),')
    w('// so its bits are architecture-specific by design.')
    w('static void digest(const PPCContext& ctx) {')
    for n in gprs + other_regs + fprs:
        w(f'    feed(&ctx.{n}.u64, 8);')
    for n in vregs:
        w(f'    feed(ctx.{n}.u32, 16);')
    for n in crs:
        w(f'    feed(&ctx.{n}, 4);')
    w('    feed(&ctx.xer.so, 1); feed(&ctx.xer.ov, 1); feed(&ctx.xer.ca, 1);')
    w('    feed(&ctx.lr, 8);')
    w('    feed(&ctx.msr, 4);')
    w('}')
    w('')
    w('// Some cases fault: seeded registers can produce an effective address that is')
    w('// legal but useless, or an aligned vector access on an address the seed made')
    w('// unaligned. Rather than exclude them by guesswork, catch the fault and record')
    w('// it as the outcome. A case that faults on both architectures still agrees.')
    w('#if defined(_WIN32)')
    w('static bool runCase(void (*fn)(PPCContext&, uint8_t*), PPCContext& ctx, uint8_t* base) {')
    w('    __try { fn(ctx, base); return true; }')
    w('    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }')
    w('}')
    w('#else')
    w('static sigjmp_buf g_jmp;')
    w('static void onFault(int) { siglongjmp(g_jmp, 1); }')
    w('static bool runCase(void (*fn)(PPCContext&, uint8_t*), PPCContext& ctx, uint8_t* base) {')
    w('    struct sigaction sa {}, oldSegv {}, oldBus {}, oldFpe {};')
    w('    sa.sa_handler = onFault;')
    w('    sigemptyset(&sa.sa_mask);')
    w('    sa.sa_flags = SA_NODEFER;')
    w('    sigaction(SIGSEGV, &sa, &oldSegv);')
    w('    sigaction(SIGBUS, &sa, &oldBus);')
    w('    sigaction(SIGFPE, &sa, &oldFpe);')
    w('    bool ok = true;')
    w('    if (sigsetjmp(g_jmp, 1) == 0) fn(ctx, base); else ok = false;')
    w('    sigaction(SIGSEGV, &oldSegv, nullptr);')
    w('    sigaction(SIGBUS, &oldBus, nullptr);')
    w('    sigaction(SIGFPE, &oldFpe, nullptr);')
    w('    return ok;')
    w('}')
    w('#endif')
    w('')
    w('static char g_hexbuf[24];')
    w('static const char* hex64(uint64_t v) {')
    w('    std::snprintf(g_hexbuf, sizeof(g_hexbuf), "%016llx", (unsigned long long)v);')
    w('    return g_hexbuf;')
    w('}')
    w('')
    w('struct Case { const char* name; PPCFunc* fn; };')
    w('static const Case kCases[] = {')
    for name, _s, _m, _l in funcs:
        w(f'    {{ "{name}", {name} }},')
    w('};')
    w('static constexpr size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);')
    w('')
    w('int main(int argc, char** argv) {')
    w('    bool verbose = (argc > 1 && std::strcmp(argv[1], "-v") == 0);')
    w('    // Unbuffered, and each case names itself before running: if one faults,')
    w('    // the last line printed identifies it instead of losing everything.')
    w('    std::setvbuf(stdout, nullptr, _IONBF, 0);')
    w('#if defined(_WIN32)')
    w('    // The whole window must be accessible: a negative displacement on a small')
    w('    // register produces an effective address just under 4 GB. Windows has no')
    w('    // commit-on-demand without an exception handler, so commit it all; pages')
    w('    // are zero-filled lazily and untouched ones cost no physical memory.')
    w('    uint8_t* base = (uint8_t*)VirtualAlloc(nullptr, kGuestSize,')
    w('                                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);')
    w('#else')
    w('    uint8_t* base = (uint8_t*)mmap(nullptr, kGuestSize, PROT_READ | PROT_WRITE,')
    w('                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);')
    w('    if (base == MAP_FAILED) base = nullptr;')
    w('#endif')
    w('    if (!base) { std::printf("FATAL: could not map %zu bytes of guest memory\\n", kGuestSize); return 1; }')
    w('')
    w('#if defined(__aarch64__) || defined(_M_ARM64)')
    w('    const char* arch = "aarch64";')
    w('#elif defined(__x86_64__) || defined(_M_X64)')
    w('    const char* arch = "x86_64";')
    w('#else')
    w('    const char* arch = "unknown";')
    w('#endif')
    w('    std::printf("# difftest arch=%s cases=%zu\\n", arch, kCaseCount);')
    w('    std::printf("# sizeof(PPCContext)=%zu alignof=%zu\\n", sizeof(PPCContext), alignof(PPCContext));')
    w('    std::printf("# sizeof(PPCRegister)=%zu sizeof(PPCVRegister)=%zu sizeof(PPCCRRegister)=%zu\\n",')
    w('                sizeof(PPCRegister), sizeof(PPCVRegister), sizeof(PPCCRRegister));')
    w('')
    w('    uint64_t total = 0xcbf29ce484222325ull;')
    w('    size_t faults = 0;')
    w('    PPCContext ctx;')
    w('    for (size_t i = 0; i < kCaseCount; ++i) {')
    w('        // Same starting state for case i on every platform.')
    w('        g_state = 0x9E3779B97F4A7C15ull ^ (uint64_t(i) * 0xBF58476D1CE4E5B9ull);')
    w('        if (!g_state) g_state = 1;')
    w('        seed(ctx);')
    w('        std::memset(base, 0, kZeroSpan);  // deterministic working region')
    w('        g_hash = 0xcbf29ce484222325ull;')
    w('        if (verbose) std::printf("%s ", kCases[i].name);')
    w('        bool ok = runCase(kCases[i].fn, ctx, base);')
    w('        if (ok) { digest(ctx); } else { ++faults; g_hash = 0xFA0100FA0100FA01ull; }')
    w('        total ^= g_hash; total *= 0x100000001B3ull;')
    w('        if (verbose) std::printf("%s\\n", ok ? hex64(g_hash) : "FAULT");')
    w('    }')
    w('    std::printf("# faults %zu of %zu\\n", faults, kCaseCount);')
    w('    std::printf("TOTAL %016llx\\n", (unsigned long long)total);')
    w('    return 0;')
    w('}')
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Generate the ARM64/x86-64 differential test")
    ap.add_argument("--count", type=int, default=4000, help="functions to include (0 = all)")
    ap.add_argument("--build", default="ppc_tu6", choices=["ppc", "ppc_tu6"])
    a = ap.parse_args()

    gen = GENERATED / a.build
    header = gen / "ppc_context.h"
    if not header.is_file():
        print(f"error: no generated code at {gen}", file=sys.stderr)
        print("       run: python tools/recompile.py tu", file=sys.stderr)
        return 1

    fields = parse_context_fields(header)
    funcs = select(gen, a.count)
    if not funcs:
        print("error: no suitable functions found", file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)
    src = OUT / "difftest.cpp"
    src.write_text(emit(fields, funcs, a.build), encoding="utf-8", newline="\n")

    # Every __imp__ the recompiled code references needs *a* definition to link,
    # even though none of our selected functions can reach one (they are call-free).
    # Same approach as DAORecomp's scripts/gen_stubs.py.
    shared = (gen / "ppc_recomp_shared.h").read_text(encoding="utf-8", errors="replace")
    imports = sorted(set(re.findall(r"PPC_EXTERN_FUNC\(__imp__(\w+)\);", shared)))
    stubs = ["// Generated by tools/gen_difftest.py - do not edit.",
             "// Definitions so the recompiled objects link. The differential test only",
             "// calls functions that cannot reach these, so aborting is the honest",
             "// behaviour: if one is ever reached the test is invalid, not merely wrong.",
             '#include <cstdio>',
             '#include <cstdlib>',
             '#include "ppc_recomp_shared.h"',
             "",
             "#define DIFFTEST_STUB(name) \\",
             "    PPC_FUNC(__imp__##name) { (void)ctx; (void)base; \\",
             '        std::printf("FATAL: difftest reached import %s\\n", #name); std::abort(); }',
             ""]
    stubs += [f"DIFFTEST_STUB({n})" for n in imports]
    stubs += ["",
              "uint64_t PPCQueryTimebase() noexcept {",
              '    std::printf("FATAL: difftest read the timebase\\n"); std::abort();',
              "}"]
    stub_src = OUT / "difftest_stubs.cpp"
    stub_src.write_text("\n".join(stubs) + "\n", encoding="utf-8", newline="\n")

    simd = sum(1 for f in funcs if f[1])
    mem = sum(1 for f in funcs if f[2])
    print(f"context members     {len(fields)}")
    print(f"functions selected  {len(funcs)}")
    print(f"  using SIMD        {simd}")
    print(f"  touching memory   {mem}")
    print(f"written             {src.relative_to(ROOT)}  ({src.stat().st_size:,} bytes)")
    print(f"written             {stub_src.relative_to(ROOT)}  "
          f"({len(imports)} import stubs, {stub_src.stat().st_size:,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
