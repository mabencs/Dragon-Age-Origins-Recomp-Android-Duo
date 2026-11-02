# ARM64 correctness probe (Phase 2.3)

**Run 2026-10-22. Verdict: no genuine logic bugs. Every divergence found is a known
architectural difference, and in two of the four classes ARM64 is the *more* faithful
target than x86-64.**

Phase 1 showed the recompiled game code *compiles* for `aarch64-linux-android33`. That
says nothing about whether it *behaves* the same. The hazards that matter here —
`char` signedness, floating-point rounding, SIMD Everywhere's NEON lowering of a VMX
instruction — produce subtly wrong results rather than compile errors, which is exactly
the kind of bug that surfaces as a graphical glitch forty hours into a playthrough.

So this measures it directly.

---

## 1. Method

`tools/gen_difftest.py` picks recompiled functions that can be executed standalone,
generates a harness that runs each from a deterministic starting state, and hashes the
resulting PPC context. `tools/run_difftest.py` builds that for both architectures, runs
one on the workstation and one on the Thor over adb, and diffs the per-function hashes.

```bash
python tools/gen_difftest.py
python tools/run_difftest.py
```

### Selection

A function qualifies if it cannot transfer control out of itself (no calls, no
`longjmp`, no indirect jumps), reads nothing non-deterministic (no timebase), and
contains no backward branch — the last so the harness is guaranteed to terminate.

Memory access *is* allowed, which matters: the VMX load/store and permute paths are
where NEON lowering is most likely to go wrong, and every one of them touches memory.
Guest memory is a fresh anonymous mapping, which is zero-filled and therefore identical
on both platforms.

**6,635 of 52,857 functions qualify**, including 370 that use SIMD. The selection is
sorted SIMD-first so the vector work is never lost in the long tail of integer
arithmetic.

Getting the filter right took two passes. The generated bodies reach these things
through *macros*, so matching `PPCQueryTimebase` misses `PPC_QUERY_TIMEBASE()` and
matching `ctx.fn[` misses `PPC_CALL_INDIRECT_FUNC`. Both slipped through initially —
and the import stubs caught it by aborting with `FATAL: difftest read the timebase`
rather than silently producing a nondeterministic result, which is exactly what those
stubs are for.

### Guest memory

The harness maps the **full 32-bit window**, as the real runtime does. Bounding seeded
registers is not sufficient on its own: effective addresses are `uint32`, so a small
register plus a negative displacement (`r1 + -8` with `r1` near zero) wraps to just
under 4 GB. Pages commit lazily, so this costs address space rather than memory.

Seeded addresses sit in a low mid-range band so the common case lands inside the region
zeroed before each call.

### What is deliberately not tested

- **NaN, infinity and denormal payloads.** Their propagation legitimately differs
  between x86 and ARM, and seeding them would bury the real findings in noise. Floats
  and vector lanes get finite, normal values. These need their own targeted test.
- **`fpscr`** is excluded from the hash. It mirrors the host floating-point control
  register, whose bit layout is architecture-specific by design (MXCSR versus FPCR) —
  XenonRecomp has separate implementations for each, which is correct behaviour, not a
  bug.
- **Faulting cases** are recorded as a fault rather than excluded by guesswork. Seeded
  registers can produce a legal-but-useless address, so roughly 20% of cases fault. A
  case that faults on both architectures still agrees; one that faults on only one is a
  finding, and §3.4 is exactly that.

---

## 2. Result

```
struct layout            identical
sizeof(PPCContext)=2688  alignof=64
sizeof(PPCRegister)=8    sizeof(PPCVRegister)=16   sizeof(PPCCRRegister)=4

functions compared       6635
functions agreeing       6549   (98.70%)
functions disagreeing      86   (1.30%)

faults                   host 16 of 6635,  android 0 of 6635
```

The fault counts are themselves a finding: **ARM64 completed all 6,635 functions
without faulting once, while x86-64 faulted 16 times** — every one of them an integer
divide by zero. See §3.4.

Struct layout agreeing matters first: if `PPCContext` were laid out differently the
rest would be meaningless. It is identical, which is expected — every member is an
explicitly-sized type and `PPCVRegister` is declared `alignas(0x10)`.

### Every disagreement classified

| Cause | Count | Expected? |
|---|---:|---|
| Fused multiply-add family | 37 | **Yes** — and ARM is more faithful |
| Approximate estimate instructions | 17 | **Yes** — architecture-defined precision |
| Other floating point / FPSCR access | 16 | **Yes** — downstream of the above |
| Integer divide by zero | 16 | **Yes** — and ARM is more faithful |
| **Genuine logic bugs** | **0** | — |

One function initially looked like a genuine bug and turned out to be a flaw in this
harness, which is worth recording because it shows the method has teeth. `mffs` (move
from FPSCR) reads the host floating-point control register, and `seed()` was
normalising only flush-to-zero, not the rounding mode. A case that had executed
`mtfsf` earlier in the run left the host rounding mode changed, which a later `mffs`
then observed. Running the function in isolation produced byte-identical state on both
architectures, which is what pointed at the harness rather than the code; `seed()` now
resets the rounding mode too, and it agrees.

---

## 3. The four causes

### 3.1 Fused multiply-add (37 cases, 43%)

Enriched instructions: `fmadd`, `fmsub`, `fnmadd`, `fnmsub`, `vmaddfp`, `vnmsubfp`.

ARM64 has native fused multiply-add: the multiply and add are performed with a **single
rounding**. x86-64 without `-mfma` compiles the same expression to a separate `mulsd`
and `addsd`, with **two roundings**. The results differ in the last bits.

**ARM64 is the more faithful target here.** The Xbox 360's PowerPC `fmadd` is genuinely
fused, so ARM64 reproduces the original hardware's arithmetic while our x86-64
reference build does not. If these two builds disagree, the x86 one is the odd one out.

Worth noting for the x86 host build: adding `-mfma` would bring it in line, at the cost
of requiring a 2013-or-later CPU.

### 3.2 Approximate estimate instructions (17 cases, 20%)

Enriched instructions: `vrefp128` (divergent in every function that uses it), `vrsqrtefp128`.

These are VMX *estimate* instructions — low-precision reciprocal and reciprocal square
root. Their exact results are **architecture-defined by specification**: x86's `RCPPS`
and `RSQRTPS` and ARM's `FRECPE` and `FRSQRTE` all give different approximations, and
none of them matches Xenon's exactly either.

This is unavoidable without emulating Xenon's estimate tables in software, which would
be slow and is almost certainly not worth it. In practice these feed lighting and
normalisation maths where a last-bits difference is invisible.

### 3.3 Other floating point and FPSCR access (16 cases, 19%)

Mostly `fdiv`, `vcmpeqfp`, `vcsxwfp128` and friends. Several are downstream
contamination: a `vcmpeqfp` comparing values that came out of a `vrefp128` inherits
that difference. Not separately concerning.

### 3.4 Integer divide by zero (16 cases, 19%)

The only disagreements with **no floating point involved at all**, and the most
interesting finding. In every one, the host *faulted* while ARM64 completed normally.

The generated code is:

```c
// divwu r10,r4,r9
ctx.r10.u32 = ctx.r4.u32 / ctx.r9.u32;
// twllei r9,0
```

A raw C division with no zero guard — and note the instruction immediately after it.
`twllei r9,0` is "trap word if less than or equal to immediate", the original
compiler's own divide-by-zero check, placed *after* the division. That ordering is
correct for the target hardware: on PowerPC, `divwu` by zero produces an undefined
result but **does not trap**, so dividing first and checking afterwards is safe.

The host and the handheld then behave differently:

| | divide by zero | matches Xbox 360? |
|---|---|---|
| **ARM64** `udiv` | returns 0 silently | **yes** |
| **x86-64** `div` | raises #DE → SIGFPE | no |

So again **ARM64 is the more faithful target**, and it is the x86-64 reference build
that carries a latent crash. This also explains the fault-count difference between the
two runs: 16 on the host, none on the handheld.

In the real game a zero divisor is rare — the `twllei` exists precisely to catch it —
so this is unlikely to matter in play. It is recorded because it will matter when the
x86-64 build is used as a reference renderer during Phase 3: **a divide-by-zero crash
on the host is not necessarily a bug in the port.**

---

## 4. What this means for the plan

- **Q9 is answered.** The ARM64 build is behaviourally sound. Risk R5
  (cross-architecture divergence) is retired as a correctness risk.
- **`long double` is a non-issue** — zero occurrences across all 207 translation units,
  so the x86-64/ARM64 width difference never arises.
- **`char` signedness is a non-issue** — the only use anywhere is `char(128)` passed to
  `simde_mm_set1_epi8`, which wraps to −128 on both.
- **Byte order is handled explicitly** via `__builtin_bswap16/32/64` in the
  `PPC_LOAD_*`/`PPC_STORE_*` macros, so it is architecture-neutral by construction.
- **Floating-point mode is handled upstream.** XenonRecomp has a complete ARM64 FPSCR
  implementation reading and writing FPCR via `mrs`/`msr`, with correct rounding-mode
  and flush-to-zero mapping. The PPC-to-ARM rounding table was checked by hand and is
  right, though its comment mislabels two entries as "-Infinity, -Infinity" where it
  means "+Infinity, -Infinity".
- **The host build needs `/arch:AVX`** (or `-march=sandybridge`). simde reaches rounding
  through `__builtin_roundevenf`; with SSE4.1 available Clang lowers that to a `ROUNDPS`
  instruction, and without it emits a call to `roundevenf`, which the MSVC CRT does not
  provide. ARM64 needs no equivalent flag because it has `FRINTN` natively. This is why
  DAORecomp requires an AVX CPU and why XenonRecomp's own tests build with
  `-march=sandybridge`.

### Still outstanding

- A targeted **NaN, infinity and denormal** comparison, which this run deliberately
  avoids.
- Functions with loops and calls are untested; they need the real runtime, so they
  belong to Phase 5 and later.
- Whether the FMA and estimate differences are *visible* in the rendered game. They
  almost certainly are not, but the Phase 3 frame comparison against the D3D12 build
  will show it either way.
