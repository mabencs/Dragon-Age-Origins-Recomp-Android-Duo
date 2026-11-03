# Master plan

**Dragon Age Origins Recomp Android Duo** - Dragon Age: Origins → native Android ARM64, targeting the AYN Thor
and its two screens.

This is the authoritative task breakdown. [`../STATUS.md`](../STATUS.md) tracks where we
actually are; this file describes where we are going.

---

## 0. Honest framing

This is a large project. The Windows DAORecomp effort it builds on is itself a
substantial achievement, and we are changing two of its load-bearing assumptions at
once: the CPU architecture (x86-64 → ARM64) and the graphics API (D3D12 → Vulkan).
On top of that we are adding three features the upstream project does not have.

Rough shape of the effort, assuming solo part-time work:

| Band | Phases | Character of the work |
|---|---|---|
| Weeks | 0–2 | Mostly mechanical; well-trodden paths |
| **Months** | **3** | **The Vulkan renderer. This is the project.** |
| Weeks | 4–6 | Android platform work; conventional app engineering |
| Weeks | 7–8 | The headline features; depends on a running game |
| Weeks | 9–10 | Feature port and tuning |
| Days | 11–12 | The release, and the game's code made on the player's device |

Phase 3 dominates. Everything before it is preparation and everything after it is
comparatively ordinary engineering. The single most valuable thing to do early is
**reduce uncertainty about Phase 3** — hence the probe in Phase 0.4.

Two milestones matter more than any others, because they are where the project either
becomes real or reveals that it cannot:

- **Phase 5 — First light.** The game boots to its main menu on the Thor.
- **Phase 6 — Playable.** Character creation through to Ostagar without a crash.

Everything the project is *for* (glyphs, second screen, HDR, framerate) sits behind
Phase 6. Do not start those early; they will be built on sand.

---

## 1. Architecture in one picture

As built (Phase 12). The planning-era picture had a shader recompiler (XenosRecomp) beside
XenonRecomp; Xenia translates the shaders at run time instead (1.4).

```
  YOUR DISC (186A1115) + a title update (disc 2's TU6, or TU7)
            │
            │  extracted on the device by the app, once
            ▼
      default.xex + default.xexp
            │
            │  XenonRecomp (PPC → C++)       ── on the device, once (Phase 12)
            │  clang + lld (LLVM 19.1.7), arm64-v8a
            ▼
        ┌──────────────────────────────────────┐
        │  libdaogame.so  the game's code      │  never in the APK
        └──────────────────┬───────────────────┘
                           │ loaded at boot, its ABI checked
        ┌──────────────────▼───────────────────┐
        │  libwarden.so  (native, no emulation)│
        │                                      │
        │  kernel / XAM shims                  │
        │  Xenos GPU frontend ─► VULKAN backend│  ◄── Phase 3
        │  glyph redirection layer             │  ◄── Phase 7
        │  state mirror (read-only)            │  ◄── Phase 8
        └──────────────────────────────────────┘
                       │ JNI
                       ▼
        ┌──────────────────────────────────────┐
        │  Android app (API 33, arm64-v8a)     │
        │                                      │
        │  GameActivity   → display 0, 1920×1080
        │  DeckActivity   → display 4, 1240×1080  ◄── Phase 8
        │  GameCodeService (makes the game's code) ◄── Phase 12
        │  AAudio · input · SAF · Swappy       │
        └──────────────────────────────────────┘
```

Full detail in [`ARCHITECTURE.md`](ARCHITECTURE.md).

---

## Phase 0 — Groundwork

**Goal:** a reproducible build environment and zero unknowns about inputs.
**Exit criteria:** `verify_assets.py` passes; toolchain installed; upstream vendored;
first push to the git remote done.

### 0.1 Asset verification — **done**
- [x] Parse XDVDFS and XEX2 headers from both ISOs
- [x] Parse STFS headers from both title updates
- [x] Confirm base disc is media `186A1115` (the targeted press)
- [x] Confirm TU7 present and matching
- [x] Establish Awakening is a different disc (media `2E6F7560`)

### 0.2 Repo and tooling — **done**
- [x] `.gitignore` covering all game content; proven with `git check-ignore`
- [x] `tools/dao_iso.py` — XDVDFS reader/extractor
- [x] `tools/dao_erf.py` — ERF V2.1 reader + atlas dumper
- [x] `tools/verify_assets.py` — edition checker
- [x] `device/thor-profile.md` — measured hardware profile

### 0.3 Host toolchain — **done**
- [x] LLVM/Clang 23.1.4 host (upstream uses 22 — close enough; noted)
- [x] CMake 4.4.4, Ninja 1.13.2
- [x] Android NDK **28.2.13676358** (Clang 19.0.1), `arm64-v8a` + API 33 sysroot verified
- [x] JDK 17 (Temurin) + Android SDK platform 33 + build-tools 35.0.1
- [x] Environment variables and PATH set at user scope
- [x] **Proved the chain on hardware** — cross-compiled a native arm64-v8a binary,
      pushed it to the Thor and ran it, rather than assuming the toolchain works

Installed versions and the platform gotchas are in [`TOOLCHAIN.md`](TOOLCHAIN.md).

### 0.4 Vendor upstream, and probe the Phase 3 risk — **done**
- [x] Vendored `96dot/DAORecomp` — its `config/` (function database, patch set) and
      `DAORecompLib` are the irreplaceable part
- [x] Vendored `hedge-dev/XenonRecomp` and `hedge-dev/XenosRecomp`
- [x] Vendored `xenia-canary` — the source of the Vulkan backend, SPIR-V translator
      and `ui/vulkan/` presentation layer
- [x] Pinned all four commits in [`../THIRD_PARTY.md`](../THIRD_PARTY.md)
- [x] **Q1 risk probe — ANSWERED GREEN.** Full writeup in
      [`PROBE-Q1.md`](PROBE-Q1.md). Vulkan backend is at 1:1 parity with D3D12;
      DAORecomp diverged from the Xenos frontend by only 2.4%; Adreno 740 has every
      critical feature. One caveat found (`VK_EXT_fragment_shader_interlock` absent →
      the less accurate EDRAM path, now **Q8**) and one underestimated cost (7,178
      lines of DAO-specific rendering code inside the D3D12 backend need
      reimplementing, not porting).

### 0.5 Version control — **done**
- [x] `origin` wired to the project's git server
- [x] Commits pushed, remote tree verified to contain no game content
- [x] Clone-and-verify path documented in `STATUS.md` §8

### 0.6 Device loop — **done**
- [x] `tools/deploy.sh` — `check / info / vulkan / shot / logs / crash / perf / run`
- [x] `tools/capture_device.sh` — re-capture the hardware profile
- [x] `tools/vkprobe/` — on-device Vulkan capability probe
- [x] Dual-display screenshots confirmed working at 1920×1080 and 1240×1080

---

## Phase 1 — Host-side recompilation

**Goal:** generate the C++ translation of the game's PowerPC code on the workstation.
**Exit criteria:** generated code populated and compiling for **x86-64 host** first,
proving the generation is sound before adding ARM64 as a variable.

**COMPLETE — 2026-10-22.**

- [x] 1.1 Extract `default.xex` from the disc with `tools/dao_iso.py`.
      **SHA1 `532c0ec1…` — exact match** to DAORecomp's `GAME_XEX_SHA1`, so this is
      byte-for-byte the binary its function database was built against.
- [x] 1.2 Extract **TU7**'s `default.xexp` from the LIVE package using DAORecomp's own
      `scripts/stfs.py`. **SHA1 `dd5b493f…` — exact match** to their `TU7_XEXP_SHA1`.
      Resolved the TU6/TU7 worry: upstream states TU7's code differs from TU6's only
      by a version number, so `config/DAO_tu6.toml` serves both, and the generated
      directory keeps upstream's `ppc_tu6` name deliberately.
- [x] 1.3 Build XenonRecomp (after applying DAORecomp's `patch_recompiler.py`, which
      adds the PPC/VMX128 instructions DAO needs — +228 lines) and run it for both
      builds. **Clean: zero warnings or errors in either log.**

      | build | config | translation units | generated |
      |---|---|---:|---:|
      | disc | `DAO.toml` | 202 | 264 MB |
      | tu | `DAO_tu6.toml` | 207 | 276 MB |

      **52,857 functions**, symbols prefixed `tu6_` on the TU build so both builds can
      link into one executable. Driver: `tools/recompile.py`.
- [x] ~~1.4 Run XenosRecomp over the game's shaders~~ — **DROPPED, not applicable.**
      DAORecomp does not use XenosRecomp at all; it translates shaders through
      **Xenia's runtime translator** (`dxbc_shader_translator`). There is no
      ahead-of-time shader step in this project, and the SPIR-V work is simply part of
      the Phase 3 backend swap (`dxbc_shader_translator` → `spirv_shader_translator`,
      both of which Xenia ships). See [`../THIRD_PARTY.md`](../THIRD_PARTY.md).
- [x] 1.5 Compile the generated code for **x86-64 host** — `tools/compile_check.py`.
- [x] 1.6 Tool versions and input hashes recorded in `STATUS.md` §4 and
      [`../THIRD_PARTY.md`](../THIRD_PARTY.md).

**The Phase 1.4 risk did not materialise.** There was no DXIL-versus-SPIR-V divergence
to budget for here, because the shader path is not part of this phase at all.

### XenonRecomp operational notes

Three behaviours cost real time to diagnose; `tools/recompile.py` now handles all three:

- **`out_directory_path` must be relative to the config file's directory.** Every TOML
  path resolves that way (`recompiler.cpp` builds
  `"{configDir}{outDirectoryPath}/{file}"`), so an absolute path silently becomes
  `config/C:/…` and every write fails.
- **The output directory must already exist.** XenonRecomp does not create it, and the
  failure is an uncaught C++ exception that aborts with `0xC0000409` and **no output at
  all** — not even a log line. Both original upstream configs reproduce this if their
  `ppc/` and `ppc_tu6/` directories are absent.
- **A jump table missing from the switch tables becomes a call to nothing.** XenonRecomp
  ends the function at the table's `bctr` and emits it as an indirect call through the
  function table; a case with no function behind it calls host address 0. DAORecomp's
  tables come from its `find_switches.py`, which needs a `cmplwi` bound check, so a
  table bounded otherwise is missed - one was, in code that runs when the Korcari Wilds
  load (BUG-051). `tools/recompile.py`'s `ADDITIONS` adds such a function's size and its
  `[[switch]]` to the config copy it hands XenonRecomp, asserted, and `python
  tools/find_missed_switches.py` scans the generated code for any other.

### Host toolchain on Windows

No MSVC is installed. Following DAORecomp's documented path, `tools/setup_winsysroot.py`
fetches just the MSVC CRT and Windows SDK with **xwin** (about 85 MB, versions pinned to
upstream's) and we build with the LLVM already present — upstream accepts any system
LLVM 18+ that has `clang-cl`, `lld-link`, `llvm-lib` and `llvm-rc`. Ours is 23.1.4.
XenonRecomp builds against that with upstream's own `cmake/clang-cl-xwin.cmake`.

---

## Phase 2 — ARM64 bring-up

**Goal:** the recompiled game code compiles, links and *behaves correctly* for
`arm64-v8a`.
**Exit criteria:** all translation units build for ARM64, and the ARM64 build is shown
to produce the same results as the x86-64 reference.

**COMPLETE — 2026-10-22.** Full write-up in [`PROBE-ARM64.md`](PROBE-ARM64.md).

- [x] 2.1 Switch the compile target to the NDK's ARM64 toolchain. All **208 translation
      units** compile for `aarch64-linux-android33` (`tools/compile_check.py`).
- [x] 2.2 Resolve the SIMD layer. The generated code reaches SIMD through **SIMD
      Everywhere** — every translation unit includes `<x86/sse.h>` and `<x86/avx.h>`,
      which simde maps onto NEON. Verified it produces real NEON rather than scalar
      fallbacks: the most vector-heavy unit disassembles to 822 `tbl` (VMX permute),
      1,083 `fmul`, 489 `scvtf` and 19 `rev32` (the big-endian byte swaps).
- [x] 2.3 Cross-architecture hazard audit — **done by measurement, not inspection.**
      `tools/gen_difftest.py` and `tools/run_difftest.py` run **6,635** recompiled
      functions from an identical deterministic state on both the workstation and the
      handheld, then diff the resulting PPC context.

      ```
      struct layout       identical
      compared            6635
      agreeing            6549  (98.70%)
      disagreeing           86  (1.30%)
      faults              host 16,  android 0
      genuine logic bugs     0
      ```

      All 86 divergences fall into four understood classes, and in two of them **ARM64
      is the more faithful target than x86-64**:

  - [x] **Byte order** — non-issue. Handled explicitly by `__builtin_bswap16/32/64`
        inside the `PPC_LOAD_*`/`PPC_STORE_*` macros, so it is architecture-neutral by
        construction.
  - [x] **`long double`** — non-issue. **Zero occurrences** across all 207 translation
        units, so the 128-bit/64-bit width difference never arises.
  - [x] **Unaligned access** — non-issue in practice; no faults attributable to it on
        either architecture across 6,635 functions.
  - [x] **`char` signedness** — non-issue. The only use anywhere is `char(128)` passed
        to `simde_mm_set1_epi8`, which wraps to −128 on both.
  - [x] **Denormals and rounding** — handled upstream. XenonRecomp has a complete
        ARM64 FPSCR implementation reading and writing FPCR via `mrs`/`msr`. The
        PPC-to-ARM rounding table was checked by hand and is correct (its comment
        mislabels two entries, but the mapping is right).
  - [x] **Fused multiply-add (37)** — ARM64 fuses with a single rounding; x86-64
        without `-mfma` does two. **The Xbox 360's `fmadd` is genuinely fused, so
        ARM64 matches the original hardware and our x86 reference does not.**
  - [x] **Approximate estimates (17)** — `vrefp128`/`vrsqrtefp128` are *estimate*
        instructions whose precision is architecture-defined. Unavoidable without
        emulating Xenon's estimate tables, which is not worth it.
  - [x] **Integer divide by zero (16)** — the generated code emits a raw C division
        with no zero guard, because on PowerPC `divwu` by zero does not trap (the
        original compiler's `twllei` check comes *after* the divide). **ARM64's `udiv`
        returns 0 silently, matching the Xbox 360; x86-64's `div` raises #DE.** This is
        why the host faults 16 times and the handheld never does.
- [x] 2.4 Build the unimplemented-mnemonic stubs into loud runtime traps, so an
      unimplemented instruction produces a clear diagnostic rather than silent
      corruption. *(Deferred into Phase 5: it needs the runtime to have somewhere to
      report to. Note the recompilation itself produced zero unimplemented-mnemonic
      warnings, so there may be nothing to stub.)* **Done further than asked (session
      40, 2026-11-02, the owner chose it): the instructions translated, not trapped.**
      XenonRecomp warns only of mnemonics it does not know; a known one in a form it
      cannot translate becomes `__builtin_debugtrap()` silently - six in this title
      (BUG-093): `blrl` in 82AADBE0, `vpkd3d128` type 3 in 82D9F030, `vupkd3d128` type 3
      in 82DB4D58 and 82DB4E60, type 5 in 82EC2F60 and 82EC3080. Running one killed
      the game with signal 5 and nothing in the log. `tools/patch_xenonrecomp.py`
      teaches XenonRecomp all three as Xenia's CPU has them (the Xenos half floats -
      no infinity or NaN, truncation, denormals flushed - and vpkd3d128's pack and
      shift by Xenia's permute table), after DAORecomp's patch; the bootstrap applies
      it, builds the recompiler again and recompiles when the generated code is older.
      The emitted conversions checked against Xenia's `float_to_xenos_half` and
      `xenos_half_to_float` over 465,547 values (every half, the edge cases, random
      floats): no difference. The generated code now holds no trap at all.
- [x] 2.5 **Overtaken (2026-10-25): the whole game runs on the device.** Was: stand up
      a bare native harness that loads the runtime on device and runs CPU-only
      self-tests. *(Partly delivered: the differential test already executes
      6,635 real game functions natively on the handheld. What remains needs the
      kernel and filesystem shims, so it belongs with Phase 5.)*

### What this does not prove

- **NaN, infinity and denormal payload propagation** is deliberately untested — those
  legitimately differ between x86 and ARM, and seeding them would bury real findings in
  noise. Needs its own targeted comparison.
- Functions with **loops and calls** are untested; they need the real runtime, so they
  belong to Phase 5 and later.
- Whether the FMA and estimate differences are *visible* in the rendered game. Almost
  certainly not, but the Phase 3 frame comparison against the D3D12 build will settle it.

### Carried into Phase 3

**A divide-by-zero crash on the x86-64 host is not necessarily a bug in the port.** The
x86-64 build is the reference renderer for Phase 3, and it carries a latent fault class
that the ARM64 target does not.

---

## Phase 3 — Vulkan renderer

**The core of the project.** Budget months, not weeks.

**Goal:** a Vulkan backend that correctly services the Xenos GPU frontend.
**Exit criteria:** the game renders a correct frame on the Thor.

**Q1 is answered** ([`PROBE-Q1.md`](PROBE-Q1.md)), so this phase is no longer
open-ended. Keep Xenia's Xenos **frontend** (PM4 command processor, EDRAM model,
register state) and swap only the **backend** beneath it — measured at 2.4% frontend
divergence, so the seam is clean.

- [x] 3.1 **Graft done — full write-up in [`GRAFT.md`](GRAFT.md).** Canary's
      `gpu/vulkan/` + `spirv_shader_translator*` + `ui/vulkan/` onto DAORecomp's
      frontend via `tools/assemble_runtime.py`: 665 base files + 72 grafted + **17
      asserted edits**. **30/30 Vulkan translation units compile for
      `aarch64-linux-android33`.** The frontend deltas that actually mattered were the
      two `Presenter` signature changes, not the ~1,900 lines estimated — most of that
      divergence is in files the Vulkan backend never touches.
- [x] 3.1b **SPIR-V shader bytecode generated — 121/121** via
      `tools/build_xenia_shaders.py`. No Vulkan SDK needed: the NDK ships `glslc`,
      `spirv-opt` and `spirv-dis`, so this is Xenia's own pipeline with glslc
      substituted for glslangValidator.
- [x] 3.1c **Per-platform source selection — done. 114/114 compile for ARM64.**
      `tools/compile_runtime.py --scope android` now selects sources, excluding 65
      files: 56 that are wrong for this target (Windows, macOS, x86-64 codegen, both
      Direct3D backends, and the GNU/Linux desktop windowing that leaks in because
      Xenia sets `XE_PLATFORM_LINUX` on Android too) and 9 deferred (the ImGui
      overlay, GPU trace capture, demos).

      Five real portability bugs were found and fixed on the way, all logged in
      [`BUGS.md`](BUGS.md): `mutex.cc`'s `reinterpret_cast` between integer types
      (BUG-003), two unguarded Win32 uses in `graphics_system.cc` (BUG-016), and three
      in Xenia's own Android path — a wrong argument count, glibc robust mutexes
      bionic does not have, and a missing `const` (BUG-017/018/019). The last three
      share a cause: **nobody builds Xenia's Android target, so it had rotted.**
- [x] 3.2a **Link the runtime.** Done. Linking is what turned "DAORecomp implements
      some things outside the Xenia subset it vendors" into an exact list of **eight
      symbols** — its Windows frame pacer and Xenia's window layer. Both are Phase 4's
      job (Swappy, and a SurfaceView); they are stubbed in
      `tools/vkdevice/host_stubs.cc`, each aborting rather than returning a plausible
      value. Four other symbols were resolved by supplying the real thing: `fmt`,
      snappy (so GPU traces work for 3.3), an stb unit, and snappy's generated header
      via `tools/prepare_deps.py`.
- [x] 3.2b **Vulkan device bring-up on Adreno 740 — done, and Xenia accepts it.**
      `tools/vkdevice/run.sh` runs Xenia's own `VulkanProvider::Create(gpu_emulation =
      true)` on the handheld — the path that refuses a device lacking what Xenos
      emulation needs. Result in
      [`../device/vulkan-device-bringup.txt`](../device/vulkan-device-bringup.txt):
      Adreno 740, Vulkan 1.3.128, ten of thirteen optional extensions enabled, and
      **sparse binding resolved to a real queue family** (family 0, shared with
      graphics+compute), which the Phase 0 probe could not confirm. Absent and not
      needed: `VK_EXT_conditional_rendering`, `VK_EXT_memory_budget`.
- [x] 3.2c Swapchain and presentation. **Done in Phase 4 (4.2a).** Deferred to Phase 4: there is no surface until
      the app shell provides one from a SurfaceView, and `with_presentation = false`
      is what let the device come up now.
- [x] 3.3a **EDRAM hardware capability measured — see [`EDRAM.md`](EDRAM.md).** The
      risk was that `kHostRenderTargets` would be forced into degraded behaviour. It
      is not: gamma targets blend as **linear UNORM16** (not 8-bit gamma), depth is
      **`D24_UNORM_S8_UINT`** matching Xenos exactly (no `D32_SFLOAT` fallback),
      float24 depth converts **in the pixel shader** (`sampleRateShading` present),
      and **all nine** render target formats support sampled + colour attachment +
      blend. Zero format substitutions required.

      Also found: Adreno 740 meets *every other* requirement of the accurate interlock
      path — only `VK_EXT_fragment_shader_interlock` itself is missing. A future driver
      exposing it would enable the accurate path by cvar alone.
- [x] 3.3b **Frame comparison against the D3D12 reference.** Still the real test, and
      unchanged. What capability probing cannot answer is the *structural*
      approximation: tile ownership transfer between aliasing render targets, resolve
      semantics, and MSAA sample layout. Those need rendered frames.

      **Blocked on Phase 4**: command submission needs a `VulkanCommandProcessor`,
      which needs the host services the app shell provides (`dao_host`, `xe::Memory`,
      a real surface). The eight stubbed symbols in `tools/vkdevice/host_stubs.cc` are
      exactly that boundary. GPU trace capture is already built and working, which is
      why snappy was worth resolving rather than excluding.

      **Moved to Phase 9's start, with DAORecomp's Windows build (the owner,
      2026-10-27).** Phase 4 unblocked it long ago; the reference is the open question.
      DAORecomp built for Windows from the pinned checkout (`1a6c668`, the port's own
      base) with Title Update 7, as the port runs: the same recompiled code, so a
      difference is the GPU backend's - and Phase 9's reference for 60 fps, HDR and FSR
      too. Until then the owner's hand playthrough is the visual check.

      **Can DAORecomp run headless? Unattended, yes; without a window, no** (its source,
      checked 2026-10-27):
      - *Building* needs no window: from the pinned checkout, `python install.py --iso
        <disc image> --title-update <TU7 package> --dest <install folder>
        --accept-msvc-license` is the command line (the installer's window opens only
        without arguments). The flag accepts the license of the Microsoft build tools it
        downloads - the owner's to accept. `DAO_WORK_DIR` outside this repository holds
        the tools and the build (the installer copies the source there). About 25 GB and
        30-60 minutes, and the title update's second translation about as long again. A
        build: the game check first (CLAUDE.md).
      - *Running* needs no one at the controller: `--play` (or `DAO_SKIP_LAUNCHER=1`)
        skips the launcher; `DAO_BACKGROUND=1` opens the window behind the active one,
        focus kept; `DAO_INPUT_SCRIPT` plays a scripted gamepad (`<time s> <BUTTONS>
        [hold s]`, `every <period s> ...`); `DAO_CAPTURE_EVERY=<s>` saves the guest
        output to `userdata/capture_N.png` - the presenter's `CaptureGuestOutput`, the
        image our own `guest-output-N.png` captures save; `DAO_AUDIO=null` keeps it
        silent. Its `scripts/test/run_game.sh` wraps a run: a test copy of `config.toml`
        (the launcher hidden, the intro logos skipped, `section.key=value` overrides such
        as `video.resolution_scale=1`) put back byte for byte afterwards, the saves the
        run made removed, the captures and logs collected; `contact_sheet.py` makes one
        image of a run.
      - *Not windowless*: frames need a window on the logged-in desktop and the D3D12
        GPU (`DAO_GPU=null` runs without rendering - nothing to compare). A run uses the
        owner's GPU and puts a window behind theirs: the same game check as a build. A
        hidden or minimized window keeps running (the program opts out of Windows 11's
        timer throttling for it); not yet tried on this PC.

      The comparison then: the same scenes on both - the title, character creation, the
      King's Camp - with DAORecomp at the port's settings (resolution scale 1, no
      temporal upscaler, HDR off, the game's own 30 fps), the guest output captured on
      both sides, and a difference image where a scene holds still. It answers Q8
      (`kHostRenderTargets`' structural approximations).
      **Done (session 35, the first of Phase 9).** The reference is DAORecomp's Windows
      build at `C:\Games\DAORecomp`, built by `python tools/reference_build.py` from a
      clone of the pinned checkout with the port's own recompiler corrections - without
      them it crashed entering the Korcari Wilds exactly as the port once did (BUG-051,
      in DAORecomp's build too) - in about 30 minutes with this PC's LLVM 23.1.4. Every
      start goes through `tools/dao_reference.py` (the owner's rule: the game check, the
      window behind the owner's - checked every second of every run - muted, windowed,
      the owner's settings put back). The scenes: the test character's (Jaden's) autosave in the
      Korcari Wilds, with the game's profile, copied from the Thor
      (`tools/reference_save.sh`), resumed on both and walked out to the King's Camp - the
      travel map, the arrival fly-by, the King's cutscene - and the title and the main
      menu (`tools/scene_capture.sh` on the Thor, `tools/compare_frames.py` to set them
      side by side). **Nothing renders wrongly on the Thor:** the still scenes match to a
      mean difference of 2.5-3.2 of 255 (the clouds' drift, foliage edges, idle
      animation), and the cutscene's frames - faces, armour, lighting, depth of field -
      match by eye (captured a moment apart, the camera moving). One difference, the
      reference's own: its party portraits are coloured orbs, not faces - the port's
      BUG-052 (a placeholder pixel shader at a draw made once), in DAORecomp's Direct3D 12
      build. Q8: no structural approximation shows in these scenes. Not compared:
      character creation, combat's effects.
- [x] ~~3.4 Shader pipeline — consume XenosRecomp's SPIR-V, build pipeline layouts,
      descriptor sets, and specialisation constants~~ — **moot**: Xenia's SPIR-V
      translator and pipeline cache do this (XenosRecomp was dropped at 1.4).
- [x] 3.5 **Working through Xenia's backend (2026-10-25):** menus, character
      creation and the first area render. Texture path — Xenos formats and tiling →
      Vulkan formats. Adreno's supported
      format set differs from desktop; some formats will need conversion rather than
      reinterpretation.
- [x] 3.6 Render-target and resolve paths, including MSAA interaction with EDRAM.
      **Working (2026-10-25)**, the title's predicated tiling and EDRAM restore
      included, after BUG-039 and BUG-045 (both Adreno driver behaviour).
- [x] 3.7 Memory management via VMA, tuned for a unified-memory mobile GPU rather than
      a discrete one — the allocation strategy that suits a desktop GPU is wrong here.
      **Measured 2026-10-27 (session 35, runs 333-337).** At the King's Camp: 731 MB PSS
      (164 MB native heap, 292 MB of the guest's memory touched) and 1.8 GB of GPU memory
      by the driver's count, of 15.6 GB with 8.6 GB available - no pressure. Every
      `vkAllocateMemory` is now counted with its caller (`warden_gpu_memory.cc`, two
      recorded edits; a `GPU memory:` line when the total moves by 16 MB): 614 MB - the
      texture cache through VMA 224, the shared memory's sparse 4 MB chunks 160 (the plain
      buffer would be 512: sparse binding saves 350 MB, run 336), render targets 154,
      upload pages 52, two dedicated buffers 26. The rest, 1.18 GB, is the driver's memory
      for pipelines - about 10 MB each, all 106 of the storage's created at startup
      (BUG-060). The unified-memory way to drop the shared memory's copy of the guest's
      memory, importing host memory (`VK_EXT_external_memory_host`), is not in this
      driver. **Done (runs 338-345): BUG-060.** The driver's own statistics per pipeline
      (`VK_KHR_pipeline_executable_properties`, now enabled; `files/hack-pipeline-stats`
      logs them) show a pipeline whose shaders spill registers holds about 24 MB - 44 of
      181, 1.04 GB together - and the storage created all of them at startup. Now each
      stored pipeline is created when first drawn with, a hit in the driver's cache: the
      King's Camp 1,027 MB of GPU memory against 1,799, the title 386 against 1,511, the
      frames around the camp's load unchanged. Left, for later: the translated shaders
      spill (up to 6,645 instructions and 48 registers), and a long session keeps every
      pipeline it used.
- [ ] 3.8 Adreno-specific correctness and performance work: tiler-friendly render-pass
      structure, avoiding needless load/store of attachments, respecting the GPU's
      preference for fewer, larger render passes. **2026-10-25:** at Ostagar the GPU
      is saturated with about 2,100 draws a frame in 34 render passes; render passes
      and attachment traffic are not the limit (6.7). Next: the translated shaders
      themselves - vertex fetch from the shared-memory storage buffer with endian
      swaps, the runtime swizzle switches in every texture fetch - measured per
      shader on the device, and the depth-only prepass
      (343 draws) and shadow maps.
      **2026-10-26 (session 33), where the GPU's time goes, and the first fix
      (runs 240-248):** timestamp queries around each render pass of a traced frame
      (`files/gpu-timing.txt`; `files/hack-split-pass` cuts passes into pieces of n draws,
      or one draw each over a range) - the King's Camp frame is about 70 ms of GPU time
      in 73 passes, 47 ms of it the main scene pass (896 draws), one terrain draw alone
      7 ms; the depth prepass (722 draws) is 4 ms. An 8x8 scissor on 160 draws of the
      main pass leaves about 4 µs a draw, so per-draw vertex and setup work is small
      (some 8 ms a frame) and per-pixel shading is the cost. A third of the main pass was
      the translator's texture-fetch emulation: the per-component sign handling -
      switches and signed samples, about 50 switches and 100 branches in the terrain
      shader, all unused (every texture in the frame is unsigned) - about 12 ms;
      explicit-gradient sampling about 5 ms; texel snapping about 3 ms (measured by
      experiments that strip them: `hack-simple-fetch`, `hack-implicit-lod`,
      `hack-no-snap`). **The fix:** two shader modification bits per stage, from the
      fetch constants of the textures a shader fetches - every sign field unsigned, and
      equal per-axis gradient exponent adjustments - select a translation without the
      sign handling and with implicit LOD plus bias for 2D fetches, exact under those
      conditions; other draws keep the general translation. The traced frame 70.6 ->
      57.1 ms, the main pass 46.7 -> 35.4 ms (the terrain draw's piece 10.8 -> 4.9 ms),
      **the King's Camp 20.6 -> 24.9 fps**, the picture unchanged. **Runs 249-251:** each
      texture size's reciprocal once per fetch, so normalization, offsets and texel
      snapping multiply instead of dividing (exact for power-of-two sizes, within an ulp
      otherwise), and a third modification bit, `textures_integer_scale_round_only` - no
      texture of the shader has integer-scale lane bits, so the post-fetch conversion is
      only the rounding of normalized results to 16 fractional bits, emitted as a select
      without its branches (exact): the traced frame 57.1 -> 55.9 ms, **the camp 24.9 ->
      25.7 fps**. Skipping the rounding as well (`hack-no-integer-scale`, inexact) gives
      26.2 - it emulates the Xenos returning normalized results with 16 fractional bits,
      and stays. The terrain draw (4.9 ms) and the foliage (3.1 ms) did not move: once
      the driver folds the constant sign switches, little fetch work is left in them.
      **Where the rest goes** (run 251's traced frame): 20 render-target transfer passes
      with no guest draws, 4.1 ms (the game reuses EDRAM at other pitches and sample
      counts within the frame - the shadow maps, a 4x MSAA depth at base 1024); 6.9 ms
      between passes; and every pass's render area is the whole EDRAM-sized framebuffer
      (1280x2048 for the 720p targets, upstream's TODO), which a tiler loads, stores and
      bins in full. Next: each pass's render area narrowed to what it touched - draw
      scissors, transfer and clear rectangles; outside the area Vulkan leaves the
      attachments as they were - then the transfers and the time between passes.
      **Runs 252-255:** narrowing each pass's render area to what it touched (the union
      of draw scissors, transfer and clear rectangles, patched into the recorded begin
      command when the pass ends) changed nothing - 55.9 -> 55.3 ms traced; the driver
      does not pay for the untouched part of the framebuffer; reverted. The transfer
      passes are single draws, per sample for the 4x MSAA depth, exporting depth and
      stencil (`VK_EXT_shader_stencil_export`): their cost is the copy itself. **The
      multiplication rule was a third of the frame:** the translator keeps Xenos's `0 *
      anything = +0` - even for inf and NaN - at every multiplication, mad and dot
      product, testing `NMin(|a|, |b|) == 0`, and the Adreno compiler expands NMin's NaN
      handling. With IEEE multiplication instead (`hack-no-mul-zero`, inexact) the camp
      ran at 30.0 fps - the game's own cap - and the main pass halved (34.4 -> 18.3 ms
      traced; run 253). Two forms without NMin, measured in one run by ucode hash parity
      (run 254): `(|a| == 0) || (|b| == 0)`, exact, and `FMin(|a|, |b|) == 0`, exact
      unless a NaN operand meets a hardware min that returns it - both 30.0 fps,
      recovering about half and two thirds of IEEE's gain. **The fix:** the exact form by
      default (run 255): **30.0 fps**, the traced draws 52.4 -> 41.5 ms (IEEE 32.6), the
      GPU 98% busy at 680 MHz - at the cap, with no headroom. The captures match run
      251's as closely as two runs of one build do (the HUD portrait's disc pulses red
      and grey in every run - not a NaN). Next, for headroom: most multiplications have
      operands that cannot be infinite or NaN (only rcp, rsq, log, exp and some textures
      make those), where the test reduces to the sign of a zero product (`+0.0`); then
      the transfer passes and the time between passes. A flow-sensitive estimate over
      the dumped microcode puts 40-93% of the big pixel shaders' product terms there -
      but only if textures, interpolators and constants are finite, which only textures
      could be checked per draw; not pursued. **Run 256:** the `morrigan` route's fight
      in the Korcari Wilds runs at 29.9 fps for its 63 s; its traced frame is 40.5 ms
      against the camp's 51.9 (1,008 draws against 2,106). The resolves between passes
      dump each render target to the EDRAM buffer and resolve from there;
      DAORecomp's own `dao_resolve_direct.xesli` resolves single-sampled 8888 targets
      straight from the render target, but only its D3D12 backend uses it. The King's Camp is the heaviest scene measured, 98% busy at the cap; combat in the Korcari Wilds runs at the cap with about three quarters of its load (run 256). Left for headroom: exactly, the resolve straight from a single-sampled 8888 render target that DAORecomp already has (`dao_resolve_direct.xesli`, not wired into the Vulkan backend; one full-image copy less per resolve, about 1 ms); trading exactness - the owner's call - the FMin form of the multiplication rule (about 1.5 ms more; differs only for a NaN times zero) or IEEE multiplication (about 6 ms; NaN where Xenos gives 0 * inf = 0). **Runs 295-298: the resolve straight from the render target, done.** DAORecomp's `dao_resolve_direct.xesli` wired into the Vulkan backend (eight recorded edits in `vulkan_render_target_cache`): a plain 32bpp copy of one single-sampled k_8_8_8_8 render target that owns the whole resolved area, unscaled, is done from the render target itself, without its dump to the EDRAM buffer - 71% of the King's Camp's resolves. Run 296: 29.9 fps with the GPU 91-93% busy (98% before); the traced frame 42.1 ms in 68 render passes (run 255: 36-56 ms in 121-130); and the title's own frames even - 1.5% off two vblanks against 21% (run 285), its 1% low 14.9 -> 19.5 fps. The picture is unchanged (run 298's captures against run 248's). `files/hack-no-resolve-direct` turns it off. Left for headroom, both the owner's call: the FMin form of the multiplication rule (about 1.5 ms) or IEEE multiplication (about 6 ms). **Deferred to Phase 9 (the owner, 2026-10-27).** The game holds its own 30 fps cap, so headroom is wanted only when Phase 9 asks for 60 fps and FSR; then: those two forms of the multiplication rule (inexact, the owner's call), or smaller translated shaders - the biggest are 6,645 instructions with 48 registers, keep 12-25% of the GPU's shader capacity busy, and spill (BUG-060's pipelines, the driver's statistics per pipeline: `files/hack-pipeline-stats`). **BUG-062 (session 35, run 397):** the presenter made its last pass's pipeline again at every paint, after a wait for the GPU - fixed, the camp's frames now evenly 4 refreshes apart (p95 37.1 -> 33.2 ms, the 1% low 26.2 -> 29.7 fps). **Session 35 (runs 391-392), with Phase 9's settings in:** a traced frame at the King's Camp (slower than an untraced one) - the main scene pass 18.2 ms (723 draws, the terrain's shader first), the foliage 6.1 ms, a depth-only pass 3.8 ms (721 draws), the post-processing chain 49 passes in 10.1 ms with 3.9 ms of copies between them, the render-target transfers 18 passes in 3.5 ms. The transfers cannot simply go: without them (`files/hack-no-transfers`) the scene renders white - they carry it into the post-processing chain - so that run's 46.5 fps at 60 means nothing. An exact analysis that skips the multiplication rule's test would prove little: constants, interpolants and overflow can all bring an inf in. What is left: the rule's inexact forms (the owner's call), smaller translated shaders, or the post-processing chain's copies; and the switches in Settings (9.8) already give 60 fps mode 16-31% at the camp.
- [x] 3.9 Validation-layer clean under `VK_LAYER_KHRONOS_validation`. **Done 2026-10-27
      (session 35, runs 323-327):** the Khronos layer (Vulkan SDK 1.4.363.0) with core and
      synchronization validation, its messages in Xenia's log - **no error and no
      synchronization hazard** through the boot, the intro movie, the title, the main
      menu, a save loaded into the Fade, the pause menu, the prelude debug menu to the
      King's Camp (about 2,100 draws a frame), combat with Morrigan's spells in the
      Korcari Wilds, the intro cutscene and character creation. One warning, at each
      swapchain's creation: the swapchain is not pre-rotated for the portrait panel
      (`WARNING-Swapchain-PreTransform`) - harmless here, the compositor puts the layer
      on the display hardware with its 90-degree transform (`dumpsys SurfaceFlinger`:
      Device composition, transform 90), no GPU pass. The best-practice checks (run 324,
      the Arm and IMG tile-based ones too) gave only advisories that follow from
      emulating the Xenos faithfully - render passes that load their attachments (the
      EDRAM), the title's own depth pre-pass, its small draws, sampler LOD clamps and
      wrap modes from its fetch constants - or from choices made on purpose: the SPIR-V
      workgroup size the Adreno needs (`docs/TESTING.md`), mailbox presentation for
      frame pacing (4.7), `VK_EXT_custom_border_color` for the Xenos border colours.
      The layer costs a third of the frame rate (about 20 fps in the Fade).
- [x] 3.10 Capture-based debugging set up with RenderDoc or GFXReconstruct. **Done
      2026-10-27 (session 35, runs 328-330): GFXReconstruct** (the Vulkan SDK 1.4.363.0
      Android release), chosen over RenderDoc because it is driven by properties and
      files, not a window. `tools/gfxr.sh` sets it up and drives it: the layer, stripped
      to 10 MB, in the app's own data directory (Android 11's package visibility hides
      the replay app's copy from ours - BUG-059 was found that way); a capture of chosen
      frames, or of one frame on a trigger; the replay app on the Thor with a screenshot
      of every frame. A King's Camp frame is about 160 MB and a second to write, and its
      replay is the game's frame exactly (run 330); the movie's end replays as captured
      (run 329). The default page-guard memory tracking coexists with the runtime's own
      write-watch handler, which passes on the faults it does not own. Replays are for
      looking, not timing: a looped camp frame takes about 90 ms (the replay's CPU, a
      wait for the GPU each time, forced FIFO) against the game's 30 - per-pass GPU time
      stays with `files/gpu-timing.txt`. Not set up: the desktop tools (`gfxrecon-info`,
      `-convert`), which this release does not ship for Windows; the replay's
      `--pbi-all` prints the calls on the device instead.

**Mitigations if 3.3 proves intractable:** fall back to a less accurate but simpler
EDRAM approximation that is correct enough for this one game. We only need DAO to look
right — not every 360 title. That is a meaningful simplification available to us that
Xenia itself cannot take.

---

## Phase 4 — Android shell

**Goal:** an APK that loads the native library, finds game data, and runs a frame loop.
**Exit criteria:** app installs, locates game data, presents a surface, plays audio.

Reproduce the whole of what is done so far with `./tools/appbringup/run.sh`, which
builds, installs, launches, captures the report and samples the screen.

- [x] 4.1 Gradle project, `minSdk 33`, `arm64-v8a` only, NDK CMake integration.
      Gradle 9.8.2 (fetched and verified by `tools/setup_gradle.py` - winget has no
      Gradle package), AGP 9.4.1, SDK CMake 3.31.6 rather than the 4.x on PATH. The
      native source list is generated by `tools/gen_cmake_sources.py` from
      `compile_runtime.py`, so the exclusion rules have one home.
      `targetSdk` is **33**, matching the device, not the newest available.
- [x] 4.2 `GameActivity` on display 0 with a Vulkan-backed `SurfaceView`.
      Plain `SurfaceView`, deliberately not `androidx.games:games-activity` - see
      4.6 for why a Java-side input path is needed regardless. **Since session 35 every
      launch starts the game**, from the icon too, unless its intent says `--ez boot
      false` (the bring-up report alone); it was opt-in while a boot still crashed. The
      report is hidden while the game starts and comes back with the reason if it cannot
      (runs 346-349).
- [x] 4.2a **Xenia's `Presenter` presenting through that SurfaceView.** Not in the
      original plan, and expected to be blocked: the presenter requires an
      `xe::ui::Window`, and `window.cc` includes ImGui, which was Phase 10. The
      includes turned out to be dead - `window.cc` never names ImGui in 758 lines -
      so one graft edit removes them. What was actually needed was a
      `WindowedAppContext` (two pure virtuals, over the main thread's `ALooper`) and
      a `Window` (four pure virtuals, over our `ANativeWindow`).
      Verified: `Created 1920x1080 swapchain with format 37, color space 0,
      presentation mode 1`, and the frame confirmed **on the panel** by pixel
      sampling, not inferred from logs - see BUG-026 for why that distinction is now
      part of the capture.
- [x] 4.3 Storage via **Storage Access Framework**. Two routes, direct path first
      because a plain descriptor has no grant to lose and nothing to re-ask for after
      a reboot: `getExternalFilesDir()` (app-owned, no permission, reachable over adb
      or MTP), then the user picks the disc image and the title update directly
      (`ACTION_OPEN_DOCUMENT`, multiple selection), with the image's grant persisted
      and the title update copied into app storage.
      **Corrected 2026-10-23:** this was a persisted `ACTION_OPEN_DOCUMENT_TREE`
      folder grant, which Android 11+ refuses for the root of `Download` - the one
      folder the files were in (BUG-029).
      The common warning that SAF is too slow for large files is half the story — it
      is slow only if every read goes through a `ContentResolver`.
      `openFileDescriptor` returns a real seekable fd and native code then uses
      `pread` like any other file, so the framework is in the way for one call.
- [x] 4.4 First-run extraction, **done and verified on the device**:
      `9450 files, 6,859,144,771 bytes` — every file present at exactly the size on
      the disc (`python tools/verify_extraction.py`). 7m44s, about 14 MB/s through
      FUSE.
      Not ~20 GB as this plan said: **6.39 GiB**, measured. `$SystemUpdate/` is
      skipped, being the console's own update payload.
      The XDVDFS reader is a port of `tools/dao_iso.py` and is checked against it
      rather than trusted — `python tools/check_xdvdfs.py --all` compares the full
      recursive listing of both discs, 9,668 and 12,610 lines identical.
      Resume is tested both ways: stamp present skips entirely, stamp removed
      re-scans and finds all 9,450 present in 57 ms.
      **Remaining:** TU7's `packages/core/patch/` overlay (25 files, 3.3 MB) takes
      precedence over the base ERFs, so the device needs it too. `dao_iso.py` already
      extracts it host-side; reading the STFS container natively is a bigger job than
      XDVDFS and is not done.
- [x] 4.5 Audio via **AAudio** low-latency path, with Oboe as fallback. **Done
      2026-10-25 (runs 207-209): the title's sound plays, music included** - details
      below. Oboe was not needed; the Thor's AAudio is shared and normal-latency (20 ms
      bursts), which a lower latency would need an MMAP path for. **2026-10-24:**
      the title's side is in - the XAudio render driver client that is its audio
      clock (`warden_audio.cc`) and the XMA decoder contexts with a silent decoder
      (`warden_xma.cc`, DAORecomp's design); the frames it submits are measured and
      discarded. **The output is done (2026-10-25, runs 207-208):** the frames are
      mixed down to stereo and played through an AAudio stream, which paces the
      title's audio clock through a queue two of its bursts deep (the Thor gives a
      shared, normal-latency stream with 960-frame bursts); no silence for want of a
      frame. The movies' sound plays. On the way, every render callback started
      submitting its frame: `KeWaitForMultipleObjects` had been reading its timeout from
      the stack (BUG-050). **The XMA decoder is done too (run 209):** DAORecomp's
      real decoder (a port of Xenia's `XmaContextNew`) over FFmpeg's `xmaframes`, the
      FFmpeg subset built for AArch64 with NEON from DAORecomp's own CMake file. The
      title screen's music now reaches the speakers (peak 0.02-0.07 where it was 0).
      Voice-over (6.4) and effects in the game are the same path, not yet heard. **Later that day:** the title creates
      three XMA contexts at the title screen and never enables one before it stalls
      in FMOD's memory (BUG-043) - which may itself be about what FMOD does with our
      XMA answers; the `XMA:` 5 s report counts every call.
- [x] 4.6 Input: Android `InputDevice` → Xbox 360 controller semantics, including
      analog triggers and correct deadzones. Produces `X_INPUT_GAMEPAD` exactly as
      `xenia/hid/input.h` defines it, host-endian - the big-endian conversion belongs
      at the Phase 5 kernel shim, not here. Mapping self-test: **20/20**.
      The handheld's own pad is `vendor 0x2020 / product 0x0111 "Odin Controller"`,
      with analog triggers and hat axes ([`../device/thor-profile.md`](../device/thor-profile.md)).
      **It names itself an Odin, not a Thor** - so Phase 7 must key on vendor/product.
- [x] 4.7 **Frame pacing** (planned as Swappy) — still mandatory, but *not* the replacement for
      `dao_frame_pacer.cc` as this plan previously implied. Those are different jobs:
      Swappy paces presents against the display's refresh cycle, while
      `dao_frame_pacer.cc` paces a *generated* frame against the real frame that
      follows it. Frame generation does not exist on this backend at all (see R1c),
      so the pacer's correct behaviour here is to do nothing, which
      `app/src/main/cpp/dao_frame_pacer_android.cc` implements and justifies case by
      case. (Since session 37 it presents 9.14's frame generation in pairs.) Swappy itself replaces the `vkQueuePresentKHR` call inside
      `vulkan_presenter.cc` and wants sustained frame production to be worth
      measuring, so it follows Phase 5.
      Already in hand: `MAILBOX` is available, and `AChoreographer` paint coalescing
      is in `warden_window.cc`.
      **Measured (6.7, runs 277-278):** at the King's Camp the presents come 4
      refreshes (120 Hz) apart for 88.8% of frames and 3 or 5 for 11% - judder at an
      even 29.9 fps. The thread that presents is the GPU command processor's (paint
      mode `kGuestOutputThreadImmediately`) and the GPU is 97% busy, so nothing may
      make it wait - Swappy's way, sleeping until the target refresh, is out. The
      Thor's driver has **`VK_GOOGLE_display_timing`** (`cmd gpu vkjson`): each
      present can carry a time before which it must not reach the panel, and the
      engine reports when each frame did, how early it could have, and how long the
      finished frame waited - pacing by holding early frames, without blocking.
      **Runs 279-283 (session 33):** `VK_GOOGLE_display_timing` enabled (recorded edits
      in `vulkan_device.cc/.h`), and `app/src/main/cpp/warden_frame_pacing.cc`: every
      present carries an id and, paced, a time before which the compositor must not show
      it; `vkGetPastPresentationTimingGOOGLE` reads back when each frame reached the panel.
      A `pacing:` line every 5 s per mode (the panel's intervals, present-to-panel and the
      title's swap-to-panel latency, the presenter's wait for an image, frames held, late
      or never shown); `files/frame-pacing` holding `off`, `paced` or `alternate` (a minute
      of each, to compare them in one run). Found:
      - **The panel refreshes every 8.445 ms - 118.4 Hz, not 120** (SurfaceFlinger's vsync
        model and the panel's own times; `docs/BUGS.md`, constraints). Four refreshes are
        29.6 fps, so the title's 30 needs a three-refresh frame about every twentieth:
        **4.2% of frames off the four-refresh cadence is the floor** for its 33.42 ms.
      - **Unpaced, on the panel** (runs 279-282): 11.2-12.1% of frames 3, 5 or 6 refreshes
        apart - the presents had said the same. The title's swap reaches the panel 77 ms
        later (p50; p95 82), the present 31 ms. The presenter never waits for a swapchain
        image (p95 0.1 ms), paced or not: holding frames does not stall the GPU's thread.
      - **Paced:** each frame held for the refresh nearest its place on a timeline that
        advances by the title's frame period. At the nominal 33.33 ms nothing improved (run
        280: the title makes 29.9 fps, 33.42 ms, and the timeline ran ahead of it); at the
        measured rate (the mean of 64 frames) 9.3% against 12.1% (run 281), with the
        regulation retuned 7.6% against 11.9% at 5 ms more latency (run 282), and with a
        drift correction, paced throughout, **7.4% at 16 ms more** (run 283: 0.6% of
        frames late for their slots, but a third held three refreshes or more).
      - What is left is mostly the title's own: 9% of its frames take three vblanks, with
        the GPU 97% busy. Bigger levers than the pacer, both the owner's call: the title's
        vblank at half the panel's rate (59.2 Hz - every frame four refreshes, but what the
        title counts in vblanks runs 1.4% slow), or GPU headroom (3.8).
      **Runs 284-285: done, on by default.** The offset now tracks a late-frame rate -
      half a refresh later for a frame late for its slot (once a burst), a 200th of that
      earlier for each frame on time, so about one frame in two hundred is late; a frame
      held two refreshes or more pulls it in by a sixteenth of the excess at once; and the
      period's correction learns only once the offset has found its edge. Run 284 (paced
      throughout): 7.7% at 4 ms more latency. **Run 285 (a minute of each):** paced
      **6.8%** against 11.1% off the four-refresh cadence, the title's swap to the panel
      85 ms against 78 (p50); the slots themselves five refreshes apart for 0.3% of
      frames - what is left is frames late for theirs (1.5%), the title's own slow frames.
      Without `files/frame-pacing`, paced; `off` turns it off, `alternate` compares.
      Swappy itself was not used: it paces by making the presenting thread sleep, and
      that thread is the GPU command processor's.
      **Runs 296-301: three faults found and fixed (BUG-054).** With the title's frames
      even after the direct resolve (3.8), the panel showed more judder, not less: 9.9%
      off the cadence (run 296). Every frame had been shown a refresh before its slot by
      the panel's times (run 297), so a frame a refresh late went uncounted; the period's
      correction wound up; and the catch-up frames after a stall moved the timeline a
      period each (run 299: 72 ms from present to panel). The desired time is now a
      quarter of a refresh after the slot - run 298 compared four (`files/frame-pacing`
      holding `phases`): 6.2-6.9% off, no frame early, against a floor of 4.1% for the
      title's 33.43 ms - and a timeline a period ahead of the frames starts again. Run
      301, the default over eleven minutes: 6.4% off the cadence, 38 ms from present to panel (p50), no frame early, the period's correction within 0.025 ms a frame either way, two new timelines once at the camp.
- [x] 4.8 Thread affinity — clusters detected from `cpuinfo_max_freq` rather than
      hardcoded indices. `sched_setaffinity` is **accepted** on this device (checked
      by pinning and reading the mask back): prime `7`, performance `3-6`,
      efficiency `0-2`. Applied to the guest thread when Phase 5 creates one; the UI
      thread is deliberately left alone.
- [x] 4.9 Lifecycle: pause/resume, surface loss, and Vulkan device-loss recovery.
      `configChanges` keeps the activity alive across controller and theme changes,
      pause/resume reach the runtime, and `surfaceDestroyed` blocks until native has
      released the window. **The app's end (session 33, runs 286-291,
      `warden_lifecycle.cc`):** once guest code has run, the app ends by ending the
      process - the title's threads cannot be stopped from outside, and the runtime
      cannot start twice in one process. Every way out is recorded natively and polled
      by Java (`exitRequest`, with the keyboard's poll), which finishes the task and
      ends the process in `onDestroy`: the title's own end (`XamLoaderTerminateTitle`,
      `XamLoaderLaunchTitle`, `HalReturnToFirmware` - implemented; never returning, as
      on the console; no menu of this title calls them in the paths run so far); the
      runtime's quit (`PlatformQuitFromUIThread`); and **Back**, which asks first
      ("Quit Dragon Age: Origins? Progress since the last save will be lost.") - run
      289, after BUG-053 had kept Back from the activity. **A lost host GPU** no longer
      aborts the process (`xe::FatalError`, a recorded edit in `graphics_system.cc`):
      the player is told the game has to close, and OK ends it (run 286, with
      `files/hack-gpu-loss` reporting a loss at 60 s). Recovery - a new device with the
      game going on - is out of reach: Xenia's own TODO there needs the provider, the
      command processor and the presenter rebuilt under the running title. A launch
      after a quit starts clean (run 290). Not yet tried: the Thor's pad on these
      dialogs (Android turns a pad's A into a confirm and B into Back). **Suspend
      (session 35, runs 319-322):** coming back to the app could hang its main thread
      for good - the wake pipe was blocking and `OnWake` read it until a read failed
      (BUG-056: the frozen picture, "not responding", the black screen after the power
      button); and put away, only the sound stopped (BUG-057). Now the guest is
      suspended from `onPause` to `onResume` (`warden_suspend.cc`): the title's threads
      hold at their waits, sleeps and yields, the audio worker and the kernel's clock
      with them, and the title's clocks stop, so it sees no time pass; the GPU and the
      vblank go on. `tools/suspend_check.sh` checks it beside a capture. Not covered:
      Android ending the process in the background when memory runs short (STATUS Q12).
- [x] 4.10 JNI bridge with a narrow, explicit surface area. Twelve methods at first,
      twenty-four now (the extraction, the image identity, the boot and why it did not, the name
      keyboard's `keyboardRequest` / `keyboardAnswer`, and the app's end:
      `exitRequest` / `requestQuit`), all in `NativeBridge.java`,
      registered through `JNI_OnLoad` rather than by mangled name so a typo is a load
      error instead of a crash at first call. Java calls native, never the reverse:
      the keyboard is polled, not called back.

---

## Phase 5 — First light  ⭐ MILESTONE

**Goal:** the game boots to its main menu on the Thor.
**Exit criteria:** main menu renders correctly, audio plays, controller navigates it.

Prerequisites, done ahead of 5.1 because the link boundary had to be known before
anything could be planned against it:

- [x] 5.0a **The whole renderer links into the APK.** 118 translation units, an 85 MB
      native library, 51,249 symbols. The boundary was enumerated rather than guessed:
      lld reports only 20 errors by default, so with `--error-limit=0` the real list is
      **65 symbols** - 57 of them glslang's `spv::Builder`, which is a missing library
      rather than porting work, and 8 real ones.
- [x] 5.0b **`xe::dao_host`** - four functions. `TitleId` and `ShortSleep` are real;
      `AttachGuestThread` and `DispatchInterrupt` abort, because they give a host
      thread a guest identity and there is no guest code linked yet.
- [x] 5.0c **`xe::Memory`, and it works on the device.** `xenia/memory.h` in the
      grafted tree is DAORecomp's 177-line replacement for Xenia's 2,444-line one, and
      its own comment is the specification: one 4 GB reservation where guest address A
      is at `membase + A`, with the physical views at `0xA0000000`, `0xC0000000` and
      `0xE0000000+0x1000` mapped onto one 512 MB section. That is the same flat base
      the recompiled code wants, so the GPU and the guest genuinely share one.
      Verified on hardware (`./tools/appbringup/run.sh`):

      ```
      0xA0000000 -> 0xC0000000         aliased
      0xA0000000 -> 0xE0001000         aliased
      0xC0000000 -> 0xA0000000         aliased
      page reported read-only          yes
      watched write completed          yes
      invalidation callbacks fired     1
      page writable again              yes
      RESULT    aliasing and write watch both work
      ```

      The aliasing is a memfd mapped three times with `MAP_SHARED`, not a copy. The
      write watch - mprotect, catch the fault, invalidate, unprotect, retry - is how
      the renderer learns that cached guest data changed, and it was the single
      riskiest mechanism in the port.
- [x] 5.0d **The recompiled title is linked.** 208 generated units, 265 MB of source,
      compiled with Phase 2's exact settings (C++17, `-O2`, `-g0`) rather than the build
      type's, because that is what 6,635 functions were differentially tested at.
      Result: **157,061 defined symbols, 52,620 recompiled guest functions**, a 160 MB
      library in a 199 MB APK, still presenting, 133 ms to start.
- [x] 5.0e **The XEX image loads into guest memory, and is checked.**
      `app/src/main/cpp/warden_xex.cc`, verified against the recompiled source by
      `python tools/check_xex.py` — **839 instruction groups, all 839 agreeing**, with
      `mflr r12` encoding to `7D8802A6` at all 36,403 sites. No disassembler needed:
      grouping entry points by the instruction XenonRecomp quotes above each one and
      requiring agreement tests decryption, key derivation, header offsets and block
      assembly at once.
      Entry point `0x829E8698`, base `0x82000000`, 19,759,104 bytes, 256 KB stack.
- [x] 5.0f **The Title Update 7 patch applies, on the device.**
      `mflr r12 at entry  70.5% of 53108 entry points` → *matches the linked
      recompilation*, against 1.8% unpatched. The generation mismatch that blocked
      first light is cleared.
      The app reads the update straight out of the LIVE package with its own STFS
      reader (`warden_stfs.cc`, verified byte-for-byte against DAORecomp's
      `scripts/stfs.py` by `tools/check_stfs.py`), so the user only has to drop the
      file in. It checks for a loose `default.xexp` first, then any LIVE/PIRS/CON
      package in the game data directory, its parent, or app storage — and requires
      `default.xexp` at exactly 2,758,656 bytes, because Title Update 2 is also in
      circulation and is not usable.
      XenonRecomp's `XexPatcher` is used rather than reimplemented since it is what
      produced `ppc_tu6`; `tools/gen_xex_patcher.py` trims its one Win32-only overload
      with asserted edits. Verified on the host first:
      `tools/check_xex.py --build ppc_tu6 --patch` → **843 of 843 groups agree**.
- [x] 5.0g **The indirect-call table, and a callable entry point.** Not in the
      original plan and easy to miss: XenonRecomp resolves indirect calls *at runtime*
      through a table in guest memory — `PPC_LOOKUP_FUNC(base, y)` reads a host
      function pointer from `base + IMAGE_BASE + IMAGE_SIZE + (y - CODE_BASE) * 2` — so
      every `bctrl` depends on a 31 MB table the runtime has to write first. Nothing
      announces that; the first indirect call would just jump through stale bytes.
      Built: **53,108 entries at `0x83400000`**, zeroed first so an untranslated
      address gives a diagnosable null. Entry point `0x82AB2650` confirmed present in
      the function table. `AttachGuestThread` is real (a `PPCContext` and a 256 KB
      guest stack at `0x70100000`).
      Built only when the image matches the linked build — from a mismatched image it
      would hold pointers to functions whose addresses mean something else.
- [x] 5.0h **The entry point runs.** First execution of the recompiled game on the
      device: `_tu6_xstart` and five frames of translated PowerPC below it, before a
      SIGSEGV in `tu6_sub_8251B240`. Opt-in via `am start ... --ez boot true`, 1500 ms
      after the report is logged, so the report survives.
      **The import log named the work**: 14 imports reached, in order — see 5.1.
- [x] 5.1 Kernel and XAM import shims sufficient to reach the menu. **Reached
      (main menu, run 96) and well past it.**
      **The list is no longer a guess.** These 14, in the order the title reached them
      — a recognisable C runtime startup:

   1. `NtAllocateVirtualMemory`      8. `RtlInitializeCriticalSectionAndSpinCount`
   2. `KeGetCurrentProcessType`      9. `KeTlsAlloc`
   3. `RtlInitializeCriticalSection` 10. `KeTlsSetValue`
   4. `HalReturnToFirmware`          11. `KeTlsFree`
   5. `RtlEnterCriticalSection`      12. `RtlTryEnterCriticalSection`
   6. `RtlLeaveCriticalSection`      13. `MmAllocatePhysicalMemoryEx`
   7. `XexCheckExecutablePrivilege`  14. `MmQueryStatistics`

      **Eleven of twelve done** (`warden_imports_kernel.cc`, `warden_guest_heap.cc`):
      stub hits 12 → 1. Only `MmQueryStatistics` is left, deliberately — its structure
      layout is not documented in this checkout.
      **But the crash did not move**, so the remaining blocker is not an import: see
      5.1a. Also still open: move `LoadTitle` off the main thread (10+ seconds).
      **Corrected 2026-10-23: it was an import after all - the twelfth.** The
      `MmQueryStatistics` stub's zeroed answer made the title's allocator size itself
      to nothing (5.1e). The layout *was* in this checkout, in
      `third_party/xenia-canary`'s kernel; implemented, twelve of twelve. Startup now
      reaches a second list, all still stubs: `NtCreateEvent`, `NtCreateSemaphore`,
      `KeQueryPerformanceFrequency`, `XamNotifyCreateListener`,
      `XamUserGetSigninInfo`, `ExGetXConfigSetting`, `XGetGameRegion`,
      `RtlInitAnsiString`, `NtOpenFile`, `NtQueryDirectoryFile`.
- [x] 5.1a **Why does the guest fault? Answered.** `bctrl` through a function pointer
      read as **zero**, inside `tu6_sub_8251C810`. Confirmed arithmetically: the fault
      address is exactly `PPC_LOOKUP_FUNC(base, 0)`, once you notice `PPC_CODE_BASE` is
      `ull` so the subtraction is 64-bit and does not wrap. The fault handler reporting
      guest registers is what made it findable, and it ruled out the stack and
      register-truncation guesses on the way.
- [x] 5.1b **A null indirect call names itself.** Done with a decoder rather than the
      planned trap stub: the table offset is invertible, so the fault handler recovers
      the guest address with no memory and no per-address thunks, and only believes
      itself when re-deriving the fault address matches exactly.
- [x] 5.1c **Find where the zero comes from — and retract the answer.** The whole
      `0x832F13CC` line of reasoning is **wrong and withdrawn**. Reading the translated
      `tu6_sub_8251C810` shows it is a lazily-initialised singleton getter: it tests
      that load for zero, constructs the object, and caches it back
      (`PPC_STORE_U32(ctx.r30.u32 + 5068, …)`). Zero is the correct "not created yet"
      state. The function also contains **no indirect branch at all**, so the null call
      is not in it. Guest `lr` points at the `bl 0x8251c810` call site, so the fault is
      at or below that call, among 32,377 indirect call sites.
      Two identifiers that looked usable are not: the host backtrace shifts between
      builds (`-O2` inlines translated functions — six frames one build, three the
      next, identical guest state), and `ctx.lr` is a store the compiler may elide.
- [x] 5.1e **Walk the guest stack for a real call chain — done 2026-10-23.**
      `LogGuestCallChainOnFault` in `warden_guest_registers.cc`: `[frame]` is the
      caller's frame (`stwu r1,-F(r1)`), `[caller - 8]` its return address
      (`stw r12,-8(r1)`, inline or in `__savegprlr_N`). Each frame is named by the
      function holding its return address and checked against the `bl` before the
      next frame up, so a junk frame is marked rather than believed. Reads stay
      between `r1` and the thread's stack top.
      **It found the crash at once:** `sub_8251B240` calls a method on a null
      singleton, null because `operator new(24)` failed, because the allocator sized
      itself from the `MmQueryStatistics` stub (see 5.1).
- [x] 5.1f **Find what overwrites the title's code — done 2026-10-23.** With
      `MmQueryStatistics` done, startup ran through ten more imports and crashed with
      `[82AB21EC]` holding a guest stack address where `bl 0x82ab82f8` was. The code
      range is now read-only from just before the entry point, as on the console, and
      the guest-address fault path prints registers and the chain. The first write was
      caught at once: the title's `FindFirstFile` (`sub_82AB2228`) converting a
      directory entry copies `FileNameLength` bytes from a buffer the
      `NtQueryDirectoryFile` stub returned `STATUS_SUCCESS` for and never wrote - a
      stack smear climbing ~300 MB into the image. BUG-030, as suspected.
- [x] 5.1g **Make status-returning stubs fail, not succeed — done 2026-10-23**
      (BUG-030 fixed). `tools/gen_import_stubs.py` reads each import's family from
      Xenia's kernel - `STATUS_NOT_IMPLEMENTED`, `ERROR_FUNCTION_FAILED`, `E_NOTIMPL`,
      or 0 for a pointer or value - and `--list` shows the derivation. Startup no
      longer crashes: the title takes its error paths through sixteen imports and
      then spins. The first failure that matters is `NtOpenFile`, which the title
      answers with `RtlRaiseException` - so 5.2 is next. The `RtlRaiseException` stub
      returning instead of unwinding is BUG-032.
      **Corrected the same day:** the raise was the title naming its thread
      (`0x406D1388`), not a reaction to `NtOpenFile`; what stops startup is
      `ExCreateThread` failing - see 5.1h.
- [x] 5.1h **Guest threads and kernel objects — done 2026-10-23.** `ExCreateThread`
      runs a host thread per guest thread on a guest stack of its own (BUG-031 fixed),
      through the title's XAPI trampoline; `ExTerminateThread` exits it. Events,
      semaphores and threads in one object table (`warden_objects.cc`) with a shared
      wait mutex; the `Nt*`/`Ke*` waits; `KeDelayExecutionThread` sleeps;
      `ObReferenceObjectByHandle` hands out guest bodies, and `Ke*` calls on the
      title's own in-structure events adopt them from the header. The engine's eight
      threads, Direct3D's two and the network's two all start and name themselves.
      With them came the video driver's answers (`warden_video.cc`),
      `ExGetXConfigSetting` (whose refusal had been cancelling device creation),
      `MmGetPhysicalAddress` and `RtlNtStatusToDosError` - every one a stub whose
      answer the title had been acting on. `STATUS.md` §4 has the sequence.
- [x] 5.1i **The package mount that was outrun (BUG-034).** **Closed (session 38):** not
      seen again in about 450 boots since 2026-10-24 (runs to 479); BUG-034 watched. One build crashed every
      time on the renderer's first shader with no package opened; the next, slower
      by the guest trace, mounted everything. Thread-named file logs (2026-10-24) show
      the mount is one synchronous pass on the Update thread after the splash frame,
      and that the 10 ms `xe::FlushLog()` after every logged call had stretched it to
      two seconds - so the likely shape is a shader request on another thread
      outrunning it. The per-line flushes are gone (mount: 70 ms) and the fault report
      names the faulting thread; not recurred in seven boots since. Waiting for a
      recurrence, not being chased.
- [x] 5.1d **Patch the XEX import table.** **Done 2026-10-24** (`warden_kernel_variables.cc`):
      the loader now reads `XEX_HEADER_IMPORT_LIBRARIES` (`warden_xex.cc`), and the
      twelve variable records the title has - all `xboxkrnl` - get a home with Xenia's
      values and their record patched to its address. It was the null call of BUG-038:
      `KeDebugMonitorData`'s record still held its ordinal word. Function records stay
      as they are; the recompiler bound those.
      Found while testing the above. Briefly a
      suspect for the crash; **cleared by 5.1e** - the null came from a failed
      allocation, not an import slot. Still a real gap: the executable has **514 import slots** (200 `xam.xex`, 314
      `xboxkrnl.exe`) that the console's loader patches and nothing here does.
      XenonRecomp handles imported *functions* through its `__imp__` calls, but
      imported **variables** are data — a title reading one reads whatever the image
      happens to hold.
      **Surface measured: 251 imports** — see [`IMPORTS.md`](IMPORTS.md).
      `Net 47 · Xam 42 · Nt 30 · Ke 28 · Vd 20 · Rtl 20 · XMA 16 · other 13 · Mm 8 ·
      XAudio 6 · Ex 4 · XGet 4 · Ob 4 · Xe 3 · Dbg 2 · Interlocked 2 · XNotify 1 ·
      Hal 1`.
      All 251 have weak stubs, so the library links now; a strong definition in
      `warden_imports.cc` overrides one with no list to maintain. Each stub counts its
      calls and logs the first, so the next step is to **run the title and read the
      log** — the order imports are reached in is the order they need implementing, and
      it is far shorter than 251.
      Done: `PPCQueryTimebase`.
- [x] 5.2 Filesystem shim mapping the game's expected layout onto app storage. **Done
      for what the game uses (closed session 38):** it never asks for `cache:` in play
      (run 476's log), and the title update is applied by the loader (`LoadTitle`).
      **Started 2026-10-23.** `RtlInitAnsiString` is done and logs the paths: the
      first two things the title asks of the filesystem are directory listings,
      `D:\packages\core\audio\vo\*` and `D:\NS\*` - `D:\` being the game's root - and
      it tolerates both failing.
      **Listings done** (`warden_fs.cc`, `warden_objects.cc`): `NtOpenFile`,
      `NtQueryDirectoryFile` and `NtClose`, with `D:\` / `game:\` / `\Device\Cdrom0\`
      mapped onto the extracted disc, case-insensitive, read-only. The voice-over
      listing finds `en-us`; `D:\NS` is correctly not found. `NtOpenFile` has six
      arguments, not Xenia's five (BUG-033).
      **Reads done, 2026-10-23:** `NtCreateFile` (nine arguments, the ninth on the
      stack), `NtReadFile`, `NtReadFileScatter`, `NtQueryInformationFile`,
      `NtSetInformationFile` (position - a seek), `NtQueryFullAttributesFile`,
      `NtQueryVolumeInformationFile`; writes refused. The title mounts its packages
      with them - `shaders.erf` in 529 reads, `guiexport.erf` 2.5 MB, `models.erf`,
      the talk table, 20 archives - and opens the intro movie. Remaining: anything a
      save path needs (5.3), and the `cache:` / `UPDATE:` / `WRITE_PATCH:` devices the
      title probes, which can stay "not found" until something needs them.
      **Writes done 2026-10-25 (run 199)**, for content roots only (the disc stays
      read-only): every NT disposition for files and folders, `NtWriteFile`,
      `NtFlushBuffersFile` (`fsync`), and `NtSetInformationFile` for the end of file,
      delete-on-close and rename (`docs/IMPORTS.md`). Left: the `cache:` and update
      devices, as above.
- [x] 5.3 Save-data shim pointing at app-private storage. **Done 2026-10-25 (run 199).**
      `XamContentCreateEx` mounts a save's folder,
      `files/content/<type>/<the title's file name>` in the app's internal storage, as
      the title's `save:` root until `XamContentClose`; `XamContentCreateEnumerator`
      lists those folders. The dispositions are the flags' low four bits; replacing a
      save moves the old one to `.backup/` first. On the `ostagar` route the title
      saved "Save (1)" - recorded here at first as the autosave on entering the King's
      Camp; the save is at the default start area (session 32) - open-existing (not
      found), then create-always, `Saves\savegame.das` (76 KB), `savegame.met`, `scrnshot.bin`
      (458 KB) and their `.x` checksums, close - with no error. Loading: 6.5, done (runs 202-203).
- [x] 5.4 Boot sequence through to the main menu. **Main menu 2026-10-25 (run 96).** **Reached 2026-10-23:** the
      intro movie is opened (`movies\edge_logo_withaudio.bik`), input and audio are
      queried, the user's sign-in state asked for. **2026-10-24:** the network answers
      as a console with no cable (`warden_net.cc`; no online functionality, by
      decision) and its thread parks in a real Winsock event wait (BUG-035). Every
      thread is now accounted for (the `guest threads` report): all parked except
      `Eclipse::Update`, spinning in `Sleep(0)` on a flag after its sign-in screen
      (BUG-036). **Later that day:** the GPU - Xenia's Vulkan graphics system driven
      by the title's Direct3D through the ring buffer, the write-pointer store hook,
      the swap packet and the interrupts (`warden_gpu.cc`), plus a PCR, KTHREAD and
      TLS per guest thread - and the main loop runs: Update in game code, Render
      presenting. **Session 28:** BUG-038 fixed (an unpatched import variable, a
      link-training stub answering 0, a fault handler that would not wait for a lock):
      the title runs at 60 frames a second with ~52 draws each, resolves and presents
      through Xenia's Vulkan presenter, and loads the single-player module 30 s in. The
      frames are black (BUG-039) - the traced frame is a Bink movie's luma plane drawn
      to the screen. `XamInputGetState` is real (`warden_xam_input.cc`); the XAudio
      pair is still stubbed, and is a suspect. **Session 29:** the
      movie and the audio clock are cleared as causes of the black frames; the GPU
      samples zeros where guest memory holds data (BUG-039). **Later:** the shader
      build's SPIR-V 1.6 workgroup sizes, ignored by the Adreno driver, fixed (Vulkan
      1.1 target); the quad draw itself produces no fragments on this driver, with
      every host-side state valid by inspection; the validation layer found nothing;
      the cause was `gl_SampleMask` declared but unwritten in the translated pixel
      shaders, which this driver takes as no coverage. **FIRST LIGHT 2026-10-24:**
      the main menu's key art renders and is presented, yellow-green (BUG-040).
      **Session 30:** user 0 signed in (`warden_xam.cc`, with the `XMsg*` and
      notification answers): the title screen renders in true colours; the
      yellow-green frame was the EA logo phase drawn through the Bink shader with
      the key art bound (BUG-040, WATCH). About a second into the title screen the
      Update thread blocks in FMOD's non-blocking loader, whose 8192-slot request
      pool was never allocated because a 360 KB FMOD allocation failed (BUG-043) -
      the next task. Thread sampler, signal report and virtual-memory probe added
      to find it (`docs/TESTING.md`). **Session 31 (2026-10-25):** BUG-043 was our
      physical allocator handing out half the console's memory - the title sizes its
      last heap from what is left, and FMOD lived in 15 MB; with the whole range it
      is 270 MB and the title runs on: title screen, then on START its own Scaleform
      prompt "You have not selected a storage device" with A/B glyphs, read off the
      pad on the Thor. The XAM storage, achievement and system-UI calls answered as
      DAORecomp does; `files/inject-pad` presses buttons from a file for unattended
      runs. **MAIN MENU 2026-10-25 (run 96):** New Game / Load Game / Options /
      Extras, drawn by the title's Scaleform UI and navigated by the pad; the hard
      drive chosen, saves and DLC enumerated (none), the profile settings read. Left
      for this phase's exit criteria: audio (4.5), and a new game started.
      **Later the same day:** "New Game" went to a test area with a HUD and no intro
      - the user caught it; BUG-046, the title update applied wrongly on ARM64 (an
      overlapping memcpy in XenonRecomp's patcher). Fixed: New Game plays the intro
      cutscene and movie; character creation is next. **End of the day:**
      character creation renders with the character - BUG-045, the Adreno driver
      reading `gl_FragCoord` as zero under `DenormFlushToZero`, fixed in the
      translator. Run 168, played by hand: through character creation into the
      Mage origin's Fade, rendered with the HUD, no fault; the name entry takes
      the default (no keyboard yet).
- [x] 5.5 Scaleform UI rendering correctly — this exercises a large slice of the
      renderer and is a good early correctness signal. **Done:** title screen,
      storage prompt, main menu, character creation, HUD and dialogue (run 168).
- [x] 5.6 Capture a screenshot of the menu on device and commit it. This is the
      project's first real proof of life. **Open question first:** a screenshot is
      game imagery, and the repo carries no game material - the owner decides.
      **Done (session 41, the owner's call, 2026-11-02):** both screens as they were -
      the Dwarf Noble and Gorim in Orzammar's Royal Palace, the deck's Party tab with their faces
      - taken with `screencap -d` per display, laid out as on the handheld (the main
      screen over the second, two thirds of their size) in
      `docs/images/thor-both-screens.jpg` (270 KB; the hero's name pixelated on both screens, 2026-11-02), at the top of the README. A picture
      of the game in play, not its files: the pre-commit rule's types stay out.

---

## Phase 6 — Playable  ⭐ MILESTONE

**Goal:** a real play session.
**Exit criteria:** character creation through to Ostagar, no crashes, stable framerate.

**Build order (2026-10-25, the owner's call): as much as possible is built and tested
before anyone plays through to Ostagar by hand.** 1. the automated route (6.0) - done;
2. the frame rate at Ostagar (6.7: 8 fps, then 20, now 30 - the game's cap - after 3.8's
shader work; the baseline measured, runs 277-278) - done; 3. the name keyboard (6.1) - done;
4. saves (5.3, 6.5) - done (runs 199-203, 215-216; the thumbnails since session 34, BUG-049); 5. audio (4.5) - done; 6. tests at Ostagar - area loading (6.2) - done
(runs 218-224), combat (6.3) - done (runs 219-223); 7. the hand playthrough as the final check of the exit
criteria.

- [x] 6.0 **Ostagar without a hand playthrough - done 2026-10-25 (run 175).**
      `bash tools/route.sh ostagar` then a boot: New Game skips the module's start
      script, so the engine falls back to its test area with a default player; the
      runtime calls `zz_pre_debug`'s native main through the engine's script registry
      (the dispatcher refuses that name); scripted presses pick "Jump to an area", "Start
      in the King's camp", then "I'm all done" - explore mode at Ostagar about two
      minutes after the first input poll, no hands. The menu also offers the Korcari
      Wilds (`bash tools/route.sh wilds`, run 218), the night camp and the signal
      tower, and under (MORE) Flemeth's hut, the end of the Prelude, its very end with
      Morrigan (`route.sh morrigan`, run 223) and the Tower of Ishal's third level. What was found first: the 360 build
      compiles its scripts to native code and registers each by name at startup
      (`sub_82306F10`), the developer debug scripts included: `zz_pre_debug` (starts the
      prelude debug dialogue `zz_pre_debugger.dlg`), `zz_prept_debug` (its plot script:
      spawns Alistair, Morrigan and the dog, sets the prelude's plot state and jumps to
      the King's Camp `pre100ar_kings_camp`, the Korcari Wilds `pre200ar_korcari_wilds`
      or the tower levels), `zz_jump_to_map`, `zz_goto`, and the same per origin and
      area. The engine's script dispatcher (`sub_824B1EE8`, hooked in `warden_game.cc`)
      runs a script by name for an event. The route: once a game is running, dispatch
      `zz_pre_debug` from the runtime and pick its options with `inject-pad`, or set the
      debug plot's flags directly; then scripted input for combat in the Korcari Wilds,
      with screen captures and the performance log. Risks: plot state a jump leaves
      inconsistent, and the dialogue's option order (the `.dlg` is in
      `designerdialogs.erf`).
- [x] 6.1 Character creation (exercises `chargen.rim`, face rendering, the tint system).
      **Works by hand (run 168):** creation through to the Mage origin. **The name
      entry works (run 193):** `XamShowKeyboardUI` files the prompt and returns
      pending; the app shows an Android text dialog with the title's own prompt and
      default name and the system keyboard, and the answer completes the title's
      call - "Aldric Cousland" on the Appearance screen. **Checked screen by screen
      (run 292, session 33)**, driven a press at a time and read from screenshots:
      the Character Generation screen - male and female, then human, elf and dwarf
      (each model right; Mage greyed for a dwarf), Warrior, Dwarf Commoner; the name
      dialog with the background's default ("Natia"); **Appearance and Voice** - the
      close-up and its portrait both right, a preset changing the face's shape, skin,
      hair and eyes at once, the Skin Tone slider (dark to pale), Hair Color (black to
      auburn), Eye Size and Eye Color (to grey-blue), the portrait following each;
      Attributes, Skills, Talents and the difficulty prompt. OK loaded the Dwarf
      Commoner origin - Dust Town (`orz201d`), never loaded before - into its opening
      conversation with the customised Natia, at 30 fps, paced at the ideal cadence. No
      fault. (The portrait on this screen has its face; the HUD's, drawn at an area
      load, have theirs since session 34 - BUG-052.)
- [x] 6.2 Area loading and streaming. **Done 2026-10-25 (runs 218-224).** **The first
      origin area loads (run 168);**
      **the Korcari Wilds load (run 218)** from the prelude debug menu (`route.sh
      wilds`), after BUG-051 - a jump table missing from the recompiler's config had
      crashed it (run 217): explore mode, 27.7 fps at the start position. **Streaming:** an
      area's environment streams in grid cells as the party walks - in run 220
      `ost100d_5_6_0.rim` and its `.gpu.rim` opened mid-walk, and the five-second frame
      rate held at 26-30 fps around it. **An exit on foot (run 224):** walking back
      from the Wilds' start reaches "Gather your party and venture forth?"; A loads the
      King's Camp in 3.6 s, no fault. Single long frames inside a five-second average
      are not measured.
- [x] 6.3 Combat — particles, spell effects, post-processing. **Done 2026-10-25
      (runs 219-223).** `inject-pad` presses the
      triggers and sticks too since run 210 (movement and the radial menu need them).
      The King's Camp's forced tutorials, which held every other button until the
      one they named was pressed, are off since run 214 (Options -> Game -> Interface ->
      Show tutorials, the owner's choice). **The first fight works (runs
      219-220):** the `wilds` route walks up the path and attacks the wolves there with
      no hands; the party's AI fights (Alistair's Shield Bash), damage numbers float,
      blood stays on faces afterwards, and corpses can be looted - about 52 s of combat
      at 24-27 fps, no fault. **Spells (runs 222-223):** the
      `morrigan` route (the debug menu's "Start at the very end of the Prelude (with
      Morrigan)") fights the same wolves with Morrigan in the party - her Winter's Grasp
      (a frost burst, cold damage in blue), a spell cast by hand with X on her quickbar
      (frost particles; the icon dims for its cooldown), the level-up light and ring,
      the codex notice, XP; 18.6-20.9 fps in those fights at about 1,000-1,100 draws a
      frame. **Darkspawn and death (run 221):** in the Tower of Ishal's third level the
      player, alone and unarmoured, fell to genlocks in seven seconds; the death screen
      (Load Last Save / Load Game / Change Difficulty / Quit, with its music) and Load
      Last Save, which reloaded the level in two seconds. Not compared with the
      reference renderer (3.3b).
- [x] 6.4 Conversations — Scaleform dialogue UI, VO, talktables. **The dialogue UI and
      talk tables work (Greagoir, run 168);** **the voice-over plays, as far as the output
      can tell (run 219):** Jory's conversation in the Wilds reads his voice bank
      (`pre100_jory.fsb`) and the lip sync, and the output peaks at 0.36-0.43 while he
      speaks, against 0.02-0.09 between his lines. **Done 2026-10-27: the owner heard the
      voice-over, playing by hand.**
- [x] 6.5 Save and load round-trip. **Done 2026-10-25 (runs 202-203).** Resume, Load
      Game from the main menu and from the pause menu; a manual save, a save over an
      existing one (the old folder moved to `.backup/`) and the autosave, each loaded
      back where it was made, with no fault (`docs/TESTING.md`, Saves). Found on the
      way: the title writes the save time into each save's display name and sorts the
      lists by it, and both time conversions were stubs (BUG-048, fixed). A save made at
      the King's Camp loads back there through Resume (runs 215-216). The
      thumbnails are whole since session 34 (BUG-049: the pipeline cache's placeholder
      pixel shader).
- [x] ~~6.6 Two-hour stability soak with thermal observation~~ — **DROPPED
      (2026-10-25), the owner's call:** not wanted.
- [x] 6.7 Establish the performance baseline: fps, frametime distribution, 1% lows,
      temperature and battery drain. Everything in Phase 9 is measured against this.
      **First numbers (runs 175-188, `bash tools/measure_ostagar.sh`):** the King's
      Camp at Ostagar ran at 8 fps with about 3,275 draws a frame. The runtime and the
      app code were built at -O0 in the debug APK; at -O2 (CMakeLists.txt), 13.5 fps.
      Then the Adreno is the limit: 99% busy at its 680 MHz maximum, the command
      processor waiting on its fences in the swap, the title's threads spinning.
      One tile without MSAA, as DAORecomp does for its upscaler, is 20 fps and now
      the default (30 - the game's cap - since session 33's shader work, 3.8) (`files/scene-msaa` for the console's two 2x MSAA tiles). An 8x8
      scissor on every draw gives 25 fps at 90% busy, so pixels and vertices/draws
      cost about half each. Not the cause (no change): the sparse shared memory, the
      sample-mask output and its discard, the render area (2048 rows for 720 - tried
      and reverted). Where the rest goes is the translated shaders - 3.8.
      **The baseline (runs 277-278, session 33):** eleven minutes each at the King's
      Camp on the `ostagar` route, measured as `docs/TESTING.md` §4 describes
      (`tools/sample_power.sh`, `tools/frame_report.py`, the `frames:` status line from
      `warden_frame_times.cc`). The two runs agree to a tenth of a percent.
      - **29.9 fps.** The presents - when the presenter queues a frame - are 33.4 ms
        apart on average; in the panel's 120 Hz refreshes 88.8% of them 4 apart, 7.5%
        3, 3.2% 5, 0.4% 6: **11.2% off the even 4-refresh cadence** - judder at the
        right average, which 4.7 is for. 1% low 21.7-22.0 fps (the slowest 1% average
        45-46 ms); worst 79-97 ms. The title's own swaps, in 60 Hz vblanks: 2 apart
        77-78%, 1 apart 11-12%, 3 apart 9.5%, 4 apart 1%; 1% low 15 fps.
      - **No throttling.** The GPU holds its top clock, 680 MHz, at 96-98% busy for the
        whole ten minutes, at 79-83 C; the big CPU cores 84-93 C, the board 55-62 C,
        the battery 28-30 C.
      - **About 10 W: 2.2 hours from a full battery** (5,864 mAh, 22.5 Wh), with the
        panel at its minimum brightness. Measured plugged in: the computer's USB port
        gives 3.9 W and the battery still discharged at about 1.8 A. In run 278 the
        battery's charge counter lagged its current (8.4 W against 10.2); in runs
        279-281 they agree, 10.2-10.8 W. The owner's usual brightness adds to it.

---

## Phase 7 — Glyph system

Depends on Phase 5. Full design in [`GLYPHS.md`](GLYPHS.md).

**Goal:** on-screen button prompts match the controller in your hands.
**Exit criteria:** Xbox default; switching to Nintendo or PlayStation changes every
prompt in the game, with no change to what the buttons do.

**Done in session 35 (2026-10-27, runs 346-352):** Xbox, Nintendo and PlayStation
prompts, chosen in Back -> Settings -> Button prompts, from the next start. Two gaps,
both the owner's to weigh: a change needs a restart (live switching crashed, BUG-061),
and the controller-layout loading screen's picture of an Xbox pad stays as it is. The
detail is [`GLYPHS.md`](GLYPHS.md).

- [x] 7.1 Locate the four face-button glyph cells across all 37 atlas groups.
      **Done:** every prompt is an image named by position - `XBoxGamepad_BottomAction`,
      `_RightAction`, `_LeftBumper`... in the shared atlas - and the game's PS3 release
      left its own set beside it, `PS3Gamepad_*`, in the same art. Eleven screens keep
      copies of their own (the dialogue pop-up's A/B/X/Y, character creation's A, the
      tutorials' A...), found by eye over all 436 small UI cells. `tools/dao_xds.py`
      decodes the atlas pages (tiled DXT, Xenia's Tiled2D), `tools/dao_gfx.py` reads the
      Scaleform movies (tags, images, exports, ActionScript).
- [x] 7.2 Decode `X360_ControllerInteraction.GDA` — 19 columns, 76-byte rows.
      **Done:** 10 rows of an object ID and 18 flags - which interactions an object type
      allows. Nothing about prompts; no override needed.
- [x] 7.3 Build the asset override pipeline (shared with the Phase 9 texture-pack work).
      **Done:** a loose-file tree, `files/override/`, laid over D:\ (`warden_fs.cc`,
      `SetGuestOverrideRoot`): its files replace the disc's of the same name or join its
      listings. Through it the title's own patch folder, `packages/core/patch/`, which
      replaces any archived file by name (the title update's 25 are there).
- [x] 7.4 Author Nintendo and PlayStation glyph sets matching DAO's art style.
      **None needed:** the PlayStation set ships in the game (its PS3 coins: cross,
      circle, square, triangle, L1-R3, Select/Start); Nintendo is the Xbox letters on
      other positions.
- [x] 7.5 Implement redirection at the texture/UV level so one switch covers every
      prompt at once. **Done at the UV level:** a set is a list of redirects - this image
      drawn from that cell - applied to the atlas descriptions (`atl_*_dat.xml`) and
      written into the override tree's patch folder as the title loads
      (`warden_glyphs.cc`, `warden_glyphs_apply.cc`); no pixel changes.
      `tools/check_glyphs.py` checks a change on the computer.
- [x] 7.6 Handle the **text** path too. **Done by 7.5:** the talk table's tokens (`<A/>`,
      `<LB/>`...) are expanded by the engine to inline images of the same names, and
      `GetButtonText` / `ButtonIdToString` handle position names, never letters.
- [x] 7.7 Settings integration. Back -> Settings -> Button prompts (`files/glyphs`),
      **from the next start**: the dialog offers to quit now. **Live switching is not
      met** - the title reads the descriptions again at every load, but rewriting them
      under it crashed the next load (BUG-061).
- [x] 7.8 Audit every prompt surface. **Done as far as a script reaches** (GLYPHS.md
      section 5): the main menu, the HUD's quickbar, the pause menu and the screens behind
      BACK, in both sets. The rest - tutorials, conversations, level-up, crafting - in
      play. **Stays Xbox:** the controller-layout loading screen, one picture of an Xbox
      360 pad.

---

## Phase 8 — Second-screen deck

Depends on Phase 6. Full design in [`SECOND-SCREEN.md`](SECOND-SCREEN.md).

**Goal:** a Dragon Age-styled companion deck on display 4.
**Exit criteria:** six tabs showing live game state, read-only, DAO-authentic styling.

- [x] 8.1 Resolve Q6 — `Presentation` versus a second Activity with
      `setLaunchDisplayId` on API 33. Test both on the real device. **Done (session 37,
      the owner asked for Phase 8, 2026-10-30; runs 454-457): both work on the Thor's
      Android 13** - `DeckActivity` launched onto display 4 (Route A), and a
      `Presentation` (Route B, `files/deck-route` holding `presentation`). Route A is the
      default: it runs the deck in its own process (8.7). Its catch, found on the device
      (BUG-078): launching an activity on display 4 made it the focused display, so the
      controller's keys would go there - to a window that cannot take them; the deck
      brings the game's activity back to the front once connected (the game holds for
      0.0 s, once), and a tap on the deck, whose window is never focusable, leaves the
      focus with the game (run 455). The display is found by capability (a presentation
      display other than the default, switched on); the deck's own size is 1240x1025,
      landscape.
- [x] 8.2 `DeckActivity` on display 4, 1240×1080, touch-driven tab switching. **Done
      (session 37):** a tab rail (Party, Quests, Map, Tactics, Codex, Items, Perf; Data
      with `files/deck-trace`), the chosen tab cross-fading in, a footer; touch switches
      tabs. Options -> Second screen (Companion deck or Off, offered where there is a
      second display; on by default; taken as the game starts).
- [x] 8.3 **State mirror.** Tap the data the game *already* pushes to its own HUD,
      journal and map rather than reverse-engineering structs. The HUD already knows
      party health; the journal already knows quests. Mirroring that feed is far more
      robust than reading memory layouts, and it is read-only by construction. **Done
      (session 37, `warden_deck.cc`):** the movies do not have data pushed into them -
      they observe the engine's data model by token path through the TokenManager every
      movie shares (`Party.Members.0.Attributes.7.Current`, `GUI.SelectedChar.Name`,
      `Journal.PlotGivers`, `Conversation.NPCLine` ...). Two read-only hooks:
      `tu6_sub_825DC418(scene, id, path, delay)` registers an observer (named per
      movie), `tu6_sub_825DCFF0(scene)` pushes the changed (id, value-as-text) pairs once
      a frame - read before the engine sends them on. Found from the image's ASCII
      `TokenManager.*` paths and the code loading them, and the handler table
      (`tu6_sub_825E3400`, every ExternalInterface name with its function). The mirror
      keeps path -> latest text and publishes a snapshot at most every 100 ms with a
      try-lock; the deck pulls it through `DeckService` (Binder) ten times a second.
      Run 454: 533 observers registered by the HUD's movies alone.
- [x] 8.4 DAO visual language: parchment, aged gold leaf, the heraldic frames, the
      game's own typeface. **Done (session 37; ticked in session 40):** the typefaces, the
      HUD's art, the items' icons and the map's area image, below. Styling guide in `SECOND-SCREEN.md`. **The typefaces done
      (session 37):** the game's two UI faces - "Nogard" (headings) and "Dragon Text"
      (the rest) - are DefineFont3 outlines in `fonts_en-us.gfx` (guiexport.erf;
      `fonts.erf` is a 1 KB stub). Quadratic curves, as TrueType's: `DeckFonts` reads
      the movie from the owner's extracted data, makes each a TrueType font (the EM's
      20480 to 2048, the y axis turned, ten tables) into the deck's cache - 164 ms the
      first time - and the deck uses them as typefaces (prototyped on the computer first,
      `work/`). Nothing of them in the APK or the repository. Found on the way: the
      party's portraits are not files - `portrait_player0` and the companions' are drawn
      from the 3D models while the game runs (BUG-052's) - and the items' icons
      (`ico_clothing` ...) are in none of the archives by that name; the frames are the
      movies' own images in the atlases (`atl_shared_dxt5`, Xenos-tiled DXT5, which
      `tools/dao_xds.py` decodes). **The HUD's art done (session 37, run 464):**
      `DeckArt` decodes the shared atlas's page on the device (the texture fetch constant,
      Xenia's Tiled2D untiling, the byte swap, DXT5 blocks - 265 ms the first time, into
      the deck's cache) and takes nine cells: the party cards wear the HUD's portrait -
      its dark disc, the health arc on the left and the mana (blue) or stamina (gold) arc
      on the right each cut to the member's fraction, the gold ring, glowing for the
      selected one, the initial in Nogard - the chosen tab sits on the main menu's
      gold-edged bar, the rail ends in the popups' ornate divider, the headings sit on
      the journal's bar. **The items' icons (session 37):** 890 cells of the shared
      DXT1 atlas (`atl_shared_dxt1_00.xds`, 58x58 each, named as the items' Icon field -
      `ico_clothing` ...); the page decoded once into the cache, each icon cut out when
      first shown; the Items tab shows each item with its icon (run 468: the clothing,
      the Warden's Longsword, the Templar Shield, the splintmail; 177 `ico_` cells on
      the DXT1 page, 756 ms the first time); the plot items' too, the 120 `plt_ico_` cells
      (BUG-080, run 481: the Runic Worry Token's gold coin). **The map's area image (session 37):** the data
      model's `Area` gives it - `Area.Minimap` the texture ("ost000t_mmap_ph" at the King's
      Camp), `MinimapLeft/Right/Top/Bottom` its edges in the world's coordinates (the
      markers'), `MinimapWidth/Height` the share of the texture it fills (run 460's dump).
      The texture is in the area's GPU archive, `packages/core/env/<area>/<area>.gpu.rim`
      (1024x2048 DXT1, tiled - the game's own parchment map); `DeckMaps` decodes it at
      half size into the cache and the Map tab draws it under the markers, the edges
      checked on the computer (the world's span and the used pixels both 0.64 across to
      down; Loghain's Tent in the upper camp). Runs 469-472: the map with its markers on
      the deck - and beside the game's own map screen (Back, then LT twice, pressed by
      the route): the game turns this map a quarter turn clockwise, which `Area.North`
      (3: three quarter turns anticlockwise) says; the deck turns it the same way, the
      names upright - the road east to west, Jaden at the stair pyramids by the King's
      Highway in both.
- [x] 8.5 Tabs, in build order - **all six done (session 37; ticked in session 40).** Left
      from SECOND-SCREEN.md's sketches, small: the map's pan and pinch and its world-map
      toggle (the Map tab fits the area to the screen today).
  - [x] Party — avatars, health, mana/stamina. **Session 37 (runs 456, 458):** a card
        per member the HUD shows - health (`.Attributes.7`, the UI's PROP_HEALTH), mana
        or stamina (`.Attributes.8`, PROP_ENERGY; `.Stats.ClassHasMana` says which),
        poisoned, paralysed, level up, the selected one in brass; the game mode and the
        healing potions above. Names: the HUD observes only the selected character's
        (`GUI.SelectedChar.Name`), so each is learnt as its member is selected alone;
        until then the portrait's (`portrait_gen00fl_alistair` -> Alistair). Run 458, the
        Korcari Wilds' wolves: Alistair, Jory and Daveth live, Jory's stamina 87/115.
        The portraits' images are 8.4's. Each member's own name, race and class since
        the deck's reads (below): "Jaden, Human Warrior".
  - **The deck's own reads (session 37, runs 459-463).** The journal, the inventory and
        the tactics screen do not observe their data - they pull it while open (GetValue,
        GetNumMembers, ListMemberNames). So the mirror reads it itself, the way those
        handlers do (`warden_deck.cc`'s TokenReader: sub_8261CB80 an engine string from
        the path, sub_8224E240 the root, sub_82250208 the node, its vtable +48 the text,
        +68 the member count, sub_82255AF8/BD8 the members; sub_82626120 the talk
        table's strings) - getters only, on the game's own thread from the push hook,
        with scratch room on the guest stack. `files/deck-trace` dumped the tree once
        (2,500 nodes in 46 ms, run 459; targeted, run 460): `Party.Members.N` holds
        Name, Stats.ClassName/RaceName, Equipment.S (Name, Icon, Description, Cost ...),
        Tactics.TacticsList, WorldPositionX/Y; `Party.Backpack`, `Party.Money`;
        `Journal.PlotList`, `.PlotGivers` (name, position), `.CodexGroups.G.CodexList`,
        `.Conversations.C.ConversationLines` (Line, Speaker - talk table ids - and
        Player); `MapNoteList.MapNotes` (name, position, icon). Sixteen patterns
        (`Party.Members.*.Equipment.*.{Name,Icon,StackSize,Description}` ...), each
        read again at its own period (1 to 20 s), at most 0.5 ms of reading in each
        16 ms, only while the deck pulls and the game explores, fights, is in its menus
        or a conversation; a pass's keys replace its last pass's. Run 462 switched the
        patterns on one at a time (`files/deck-reads-staged`): the game's draws
        unchanged throughout (2,125-2,133 a frame) - the reads change nothing the game
        does (the camp's 749 against 2,130 draws in earlier runs was the camera's turn
        in the route, not the deck).
  - [x] Quests — list plus objectives. **Session 37:** the journal's plots (name,
        description, the rest of each plot's fields as the journal has them), the camp,
        the quest givers nearby - the debug start's journal has no active plot to show
        yet (run 460: `Journal.PlotList` empty).
  - [x] Map — markers and current location. **Session 37 (runs 461, 463):** the area's
        notes, the quest givers and the party by their world positions, fitted to the
        screen, north up - the King's Camp's tents, kennels, infirmary and highway, the
        Kennel Master, Jaden at the gate. The area's own map image, pan and pinch: 8.4
        and later.
  - [x] Codex & journal. **Session 37:** the codex's groups with their unlocked entries
        (a tap opens one's text on parchment; new ones marked), and the journal's
        conversations, their lines and speakers from the talk table (run 463: "Jaden:
        What do you need to do?" / "You: I'm all done.").
  - [x] Tactics & behaviour (read-only view first). **Session 37:** each member's
        slots - on or off, the target, the condition and the command by the names the
        tactics screen shows (run 465: Alistair's "Self: Being attacked by a ranged
        attack -> Activate mode"; "Enemy: Health >= 75% -> Use ability"), the item's name
        where there is one. The names as tactics.gfx's GetConditionName / GetActionName /
        GetTargetName make them: the 2DA tables 137, 141 and 140, their column hashed
        1080246718 the talk table's id - the mirror reads the 2DA cell as the UI's
        Get2DAValue does (`TwoDaInt`). Which ability "Use ability" uses: the abilities'
        M2DA table 1, its column 846460685 the talk table's id (tactics.gfx's
        GetAbilityName; the UI's GetM2DAValue looks the cell up as Get2DAValue does) -
        run 468: Alistair's "Activate mode: Shield Cover", "Use ability: Shield Bash",
        Jory's "Mighty Blow", Daveth's "Dirty Fighting".
  - [x] Inventory & equipment (read-only view first). **Session 37 (run 461):** the
        purse, what each member wears ("Commoner Clothing"), the backpack with stack
        sizes. The items' icons are 8.4's.
- [x] 8.6 Performance overlay as a developer tab. **Session 37:** Perf - the game's
      frames a second and the screen's (counted where `warden_frame_times.cc` records
      them), the last 128 frame times as bars (the scale following them; a frame half
      again the median's in red), the average and the slowest, the battery and the GPU's
      load and clock (run 456: 23.7 and 47.3 with Lossless 2x fast over FSR 3.1
      Balanced, GPU 99% at 680 MHz). Rebuilt in 8.10: native and total fps, the 1% low
      and the power drawn as figures, the frame generation, the upscaler and the sizes.
- [x] 8.7 Keep the deck strictly off the game's critical path — it must never be able
      to stall or crash the render loop. **Done (session 37, run 458):** the deck runs in
      its own process (`:deck`), the game's main thread never draws it, the mirror's
      hooks never wait on it; the deck's process killed mid-combat (the Korcari Wilds'
      wolves) - the game on at 294, 284, 288, 292 frames in 5 s against 292 and 326
      before, same process, nothing logged. An exception in the deck closes the deck;
      the game's process gone, the deck closes itself.
- [x] 8.8 **The touch quickbar - the deck's one write path - done (the owner asked,
      2026-11-01; session 40, runs 544-545).** A **Quickbar** tab after Party: the selected character's
      six quick slots as the HUD observes them (`GUI.SelectedChar.Stats.QuickSlots.N`:
      ItemIcon, CooldownPercentage, SourceItem.StackSize, IsError, PendingActivation,
      Status), large tiles with the game's own icons (the shared atlas's `tlt_`, `spl_`,
      `skl_` ... `_ico_` cells, now cut too), the cooldown shading the icon, the stack,
      and each tile's button named as the Button prompts option draws it (Nintendo:
      Y, X, A, ZR + ...). **A tap is the slot's own button press**, nothing else: the
      deck sends the slot over DeckService (a third transaction, `QUICKSLOT`), and the
      game side holds a short synthetic press merged into the pad the game polls
      (`warden_deck.cc`'s QueueQuickslot / ApplyQuickslot, called from
      XamInputGetState) - the engine sees a button, as from the controller. **The
      buttons, read off the HUD and proved on the Thor (run 544):** slot 0 is X (left),
      slot 1 Y (top) - Y and then a deck tap each toggled Alistair's Shield Cover (slot
      1's Status 0 -> 1 -> 0, its effect icon on and off the HUD); RT held brings the
      HUD's second set forward, slots 3-5 with X, Y, B. B (right) is the healing potion
      on the 360's HUD whatever slot 2 holds (Survival, a passive skill no button
      uses), so the third tile is the health poultice with the party's count, and its
      tap presses B. **Guarded:** taps only while the engine explores or fights - first
      built on the HUD's copy of the mode, which a tap in the pause menu got past (run
      544); now the engine's own mode (`warden::GameMode()`). Run 545: in the pause menu a tap
      on the poultice tile was refused ("Not pressed: only while exploring or fighting"), in
      play the same tap pressed B; the poultice tile shows the party's count.
      `files/deck-dump` (a test switch) writes the mirror's snapshot once to
      `files/deck-snapshot.txt`; `tools/areas/quickslot_probe.sh` presses a button and
      dumps the slots.
- [x] 8.9 **The party's faces on the Party tab (the owner asked, 2026-11-01; session 40,
      runs 546-548).** The rings held the member's initial; the HUD's portraits are GPU
      renders, not files (BUG-052), so the faces come from the GPU: each portrait is a
      146x146 resolve (destination pitch `009200A0`, 0x19000 bytes tiled), and while
      the deck pulls, the Vulkan backend copies each one out of the shared memory and
      waits for it (an edit in `tools/assemble_runtime.py`; a few milliseconds, once per
      face at an area load or a member joining - never while the deck is off).
      `warden_deck.cc` untiles it, keeps the head's alpha (the HUD lays the head over
      its disc) and gives it to a party slot: the slots whose
      `Party.Members.N.Portrait` name changed, else every named slot when the burst
      redrew them all, else by the address the slot had (a new one to a free slot). The
      deck fetches a face only when its generation changes (DeckService's `PORTRAITS`
      and `PORTRAIT`). Found on the way: the portraits' bytes are R G B A, not the
      front buffer's B G R A (run 546's faces were blue), and a burst redraws only the
      members who changed (run 546 gave Morrigan's and Alistair's faces to slots 0 and
      1). Run 548: Jaden, Morrigan and Alistair, each in their own ring.
      **Placed by name since session 41 (BUG-096, the owner: the hero's row showed
      Gorim's face and his hers):** the placement above was guesswork. The game's own
      portrait builder (`sub_824F5E50`) names each portrait as it makes it -
      "portrait_" + the creature's tag + a running count, the name the HUD's
      `Party.Members.N.Portrait` gives - and draws it right after to a texture of its
      own; `warden_game.cc` hooks its call to `sub_825FDDA8` (from `824F602C`) and hands
      the name to the deck. A resolve to an address not seen before is the oldest
      portrait made and not yet drawn; a redraw comes to its address again; an address
      whose portrait has left the party goes to the next one made. A slot shows the face
      whose name the HUD gives it, so order, joins and leaves cannot swap faces (the old
      guess stays for a face no name is known for). Faces are read back once the deck
      has pulled at all, not only lately, so a deck opened again misses no first draw.
      **The deck follows the game (BUG-097):** put away when the game goes to the
      background, back when it returns - one Home press shows home on both screens (the
      deck stayed, and a second press reached the home app already home, which can bring the
      game back on the main screen). The deck made again keeps the faces it was given
      (BUG-098). Checked with the morrigan route: the names bound in order
      (player0, morrigan1, alistair2), the rows right, and right again after Home and
      back.
- [x] 8.10 **The "Performance tab/overlay" option (the owner asked, 2026-11-01; session
      40, runs 546-549).** Options -> Performance tab/overlay, off by default, taken at
      once: off hides the deck's Perf tab; on shows it when the second screen shows the
      deck, and otherwise a box at the top centre of the main screen, between the HUD's
      portraits and its map, over the game and under the options panel. Both show the
      same (`PerfStats`): the game's native frames a second, the total on the screen
      with generated frames, the frame time, the 1% low (now over the last 1,000 frames,
      not 128), the power drawn ("on the charger" when it is), the frame generation and
      the upscaler with its quality as they run - not as the options say for the next
      start (`NativeBridge.deckRender`, set where each is decided) - and the rendered
      and shown sizes. Run 546's Perf tab: native 29.2, total 58.3, 33.5 ms, 1% low
      14.7, Lossless 2x balanced, FSR 3.1 Performance, 960x540 to 1920x1080; the option
      off, the tab gone and the deck on Party. Runs 547 and 549: the overlay with the
      second screen off, gone at once with the option off.
- [x] 8.11 **The Quests and Codex tabs fold as the journal does (the owner asked,
      2026-11-02; session 42, runs 602-603).** Quests has the journal's two pages, Current
      Quests (`Journal.PlotList`) and Completed Quests (`Journal.CompletedPlots`), each
      its `Journal.PlotGroups` by descending Priority, a group its plots with no
      `ParentPlot`; a subplot's text goes under its quest's, to two levels (journal.gfx's
      QuestList, QuestGroup, AddSubplotDescriptions; BUG-107). Codex has its categories.
      A tap unfolds a category or an entry; any number stay open (the journal keeps one
      group), and what is unfolded is kept from one start to the next (shared preferences
      `deck-folds`). A • on an entry and its category marks what the game calls unread;
      the deck never clears it. **The long texts are read while open:** `DeckSource.watch`
      (`DeckService.WATCH`, `warden::deck::SetWatches`) gives the game the open items as
      read patterns, each a job of its own, first in line and read every 5 s: a codex
      entry's `Descriptions.*`, a completed quest's `Description`, an item's `*`. All of
      them at once would pass Binder's limit late in the game (BUG-105). Run 602: the
      owner's journal and the tab alike, two groups, four quests. Run 603: The Korcari
      Wilds readable on a new character that had never opened the journal, the • still
      there. **Not seen yet:** a completed quest, as neither save has one.
- [x] 8.12 **The Items tab's details, Equip, Use and Unequip (the owner asked, 2026-11-02;
      session 42, runs 602, 608-610).** A tap on any item shows what the game's Inspect
      does: its stats, properties and requirement, then its text (`warden_deck.cc`'s
      `ItemDetails`, inventory.gfx's ItemStatsList, ItemPropertiesList and
      GetItemRequirements). The DA-only handlers are registered in `tu6_sub_82439B08`
      (found by the names in the image; `build/xex-check/image.bin` is the image from
      0x82000000): `GetItemStat` is `sub_82285ED0(id)` then `sub_822FB5D8(item, row)`, and
      `CreatureCanUseItem` is `sub_823C1898(creature, item)`. They are called through
      `PPC_LOOKUP_FUNC`, so no forwarder and the game's code stands. Table 109 is read by the
      row's place (vtable +24 a column's place from its hash, +84 the type, +56/+52/+60 the
      cell), 107 and 98 by row id. A backpack item with `EquippableSlots` has **Equip on
      <the selected character>**; one with neither slots nor an invalid `AbilityID` (0, or
      200000, ItemUtils.ITEM_ABILITY_INVALID) **Use on** them; what the character cannot use
      is greyed, as the game greys it. A worn item of the selected character has **Unequip
      from** them, unless `Irremovable`; another character's says to select them. These are
      the inventory screen's own messages, through the engine's SendMessage from the mode
      controller's update (`warden::QueueEngineMessage`): `GUI.SelectedChar.EquipItem <item>
      255` (255: the game's choice of slot), `GUI.SelectedChar.UnequipItem <item>` and
      `GameModeController.PerformAbility <ability> <GUI.SelectedChar.ID> <item>`. Only while
      the engine explores or fights, as 8.8. Run 602: the Mace in the Maul's place, the
      poultices 4 to 3. Runs 609-610 against the game's Inspect: the Maul (Damage 8, Critical
      chance 0.50%, Armor penetration 7, Strength modifier 1.25, Requires: 18 strength), the
      Dwarven Noble Armor, Blood Dragon Plate (Armor 18.38, Fatigue 26.25%, +3 strength, +3
      willpower, +3 armor, +50 health, -10% fatigue, Requires: 38 strength, Equip greyed);
      Unequip left both weapon slots empty in the game's own inventory, and was refused in a
      conversation. **Limits:** a trap's Use is sent and does nothing, as it wants a place on
      the ground; not on the deck: Inspect's first lines (the item's kind, its material and
      tier), its colours, and rune slots; the buttons act only for the selected character.
- [x] 8.13 **The Map tab's icons, zoom and pan (the owner asked, 2026-11-03; session 43).**
      `DeckView.MapView`: each marker an icon of what it is, chosen from the game's own map
      icon where it has one (`ico_mm_store`, the exits) and else from the name's words - a
      moneybag for a merchant, an anvil for a smith, an exclamation mark for a quest giver, a
      door, a tent, a bed, a mug, a pin for the rest - drawn as glyphs with a dark outline, as
      the names are. A tap on an icon shows its name beside it, and again hides it; the names
      keep clear of the icons. Pinch zooms up to 6x about the fingers and a drag pans, held
      inside the map; zoomed out, icons may overlap by a little (0.62 of their size) and spread
      apart as the map zooms. Checked on the device with raw multi-touch events on the second
      screen's touch panel (`sendevent`, two slots) and screenshots.
      **Later the same day (the owner):** the map turned as the game's map screen turns it
      (BUG-115), and more of the deck for it - a 4 dp margin while the Map tab shows (12 and
      10 for the others), the hint written over the map's foot, and the deck's footer (the
      title and the count of values) gone while the game is connected, on every tab; it says
      only "Waiting for the game…" now. Run 684.
      **Then (the owner, 2026-11-03): the party smaller, smooth, the controlled member marked.**
      Party icons 9 dp (13 before), the member the player controls 11 dp with a gold rim and a
      soft gold halo, drawn last (`Party.Members.N.UserSelected`, which the HUD's portraits
      observe; LB and RB move it - run 688). Positions are read on their own path every 100 ms
      ahead of the read jobs (`ReadPositions` in warden_deck.cc: a job of their own had waited
      behind the journal's long passes, 2.3 reads a second in run 686), about 9 reads a second;
      the game itself changes them about 6 times a second while walking (run 688: 73 changes in
      a 12 s walk). The deck glides each member's icon from where it was drawn to the new place
      over 110 ms, so it moves every frame (run 688's deck recording: in 153 of 260 frames,
      largest step 5.4 px), snapping on jumps over 15 world units. **The cost** (the new
      `deck: in 30 s, ...` line): game-thread reads 5.7-6.8 ms a second against 4.0-4.9 before
      (+1.5-2 ms/s), snapshots 0.17-0.18 ms each, up from about 1 to 6 a second while the party
      moves (+0.9 ms/s) - about 3 ms a second in all, 0.1 ms a frame at 30 fps, inside the reads'
      0.5 ms-per-16 ms cap; the frame times unchanged (p50 30.9-32.5 ms, run 686 against 688).
      The deck already pulled ten times a second, so its own side is unchanged.

---

## Phase 9 — Feature port

**Goal:** the selected features from the PC project plus the Android-specific ones.
Matrix in [`FEATURES.md`](FEATURES.md).

**3.8 comes back here (the owner, 2026-10-27):** the Adreno performance work - the GPU
headroom 60 fps and FSR will need, on top of a King's Camp already 92% busy at 30 fps.
See 3.8's notes for where the time goes and what is left.

**3.3b opens this phase (the owner, 2026-10-27):** the frame comparison against
DAORecomp's Windows build, which then stays the reference for 60 fps, HDR and FSR. It
builds and runs unattended - a scripted gamepad, captures of the guest output - but not
without a window: see 3.3b's notes. **Done (session 35):** nothing renders wrongly on the
Thor in the scenes compared. Every start of the reference goes through
`tools/dao_reference.py`: the owner's rule, a game check first and its window always
behind the owner's (CLAUDE.md).

- [x] 9.1 Framerate: 30 / 60 / 120 targets. Resolve Q3 — whether game logic is tied to
      a 30 fps tick. Check DAORecomp's patch set before writing anything.
      **60 done as a setting, 30 the default (session 35, runs 359-362).** DAORecomp's
      way (`warden_frame_rate.cc`): Direct3D's swap interrupt handler (`sub_82D9E0F8`)
      gets presentation interval 1 instead of the title's 2, and a late flip is shown at
      once; Settings -> Frame rate (`files/frame-rate`). Q3: no 30 fps logic tick -
      DAORecomp ships 60 on the same code, and the title runs at it. What the Thor's GPU
      makes of it: the title screen and the main menu 60 fps; the Korcari Wilds 43-44
      (the GPU's limit); the King's Camp 31.5 with the GPU 99% busy (93% at 30). Frame
      pacing is off at 60 unless `files/frame-pacing` says otherwise: the paced
      timeline expects a steady period, and at 44 fps it held frames two refreshes and
      then five (p95 42 ms, run 360); unpaced they take two or three (the 1% low 39.4
      fps, 9 ms less latency, runs 361-362). 120 is not offered: the GPU is at its limit
      under 60 in the game. Left, the owner's call: 60 as the default; a steady 40 (three
      refreshes, the render thread held to it as DAORecomp holds rates above 60), even
      where 60 is not.
      **The owner's call (2026-10-27, with 10.1): 60 the default, and 30, 45 and 120
      offered** - "just build the option and let me decide how smooth it is". 45 and 120
      are DAORecomp's way above 60: presentation interval 0, each frame flipped at once,
      and the title's render thread held to the rate once the frame is handed to the GPU
      (`FrameSwapped` in VdSwap, `FrameKicked` at the ring's write pointer; 0.5% fast so
      a display at the rate paces it). Character creation (run 395): 60.0 fps at 60, 68.8
      at 120 (the GPU's limit there), 45.2 at 45 with every frame three refreshes apart,
      30.0 at 30; the King's Camp stays at the GPU's 30.6 whatever the cap (run 394).
      Frame pacing follows the rate - paced at 30 only - unless `files/frame-pacing`
      says otherwise; the options panel (10.1) changes the rate while the game runs.
- [x] 9.2 HDR10 output path, **default off** (D8). **Re-scoped 2026-10-22 after
      measuring it (BUG-025).** `VK_COLOR_SPACE_HDR10_ST2084_EXT` is **not offered**
      on this device's Vulkan swapchain, even with `VK_EXT_swapchain_colorspace`
      enabled, although Android's own `Display.HdrCapabilities` for the same display
      reports `HDR10 HLG` at 420 nits. PQ is not mislabelled here (which is what
      BUG-009 described) - it is absent. Xenia's Vulkan backend also has no
      `VK_EXT_hdr_metadata` support at all; HDR on the D3D12 side is
      `gpu/d3d12/dao_upscale_hdr.cc`, with no Vulkan counterpart.
      What *is* offered is `R16G16B16A16_SFLOAT` + `EXTENDED_SRGB_LINEAR` (scRGB), a
      different submission model. So: either HDR goes via scRGB or it does not happen,
      and at 420 nits peak there is little to gain. Decide after 9.3 ships.
      **Decided after 9.3 (session 35): not built, the owner's to revisit.** The surface's
      formats (run 369's report) still offer no PQ or HLG colour space, only
      `R16G16B16A16_SFLOAT` with `EXTENDED_SRGB_LINEAR` (scRGB); on Android 13 an scRGB
      layer's values above 1.0 have no path to more brightness - the extended-range
      brightness that would let SurfaceFlinger show them arrived with Android 14 - so an
      HDR output would be clipped back to SDR, on a panel whose peak is 420 nits
      (D8's own reservation). Nothing on the device could show it working either: a
      screenshot records code values, not light. DAORecomp's way, when it can be shown
      (`dao_hdr_output.ps.hlsl`: the SDR image in scRGB, highlights expanded toward the
      display's peak), is the reference. Revisit if the Thor gets Android 14.
      **Built, the owner's ask (2026-10-27): HDR10 where the display takes it (run
      397).** The swapchain's colour space only decides the dataspace Android's Vulkan
      puts on the window's buffers, so the HDR10 swapchain is the 10-bit sRGB one and,
      once it is made, its buffers are tagged BT.2020 PQ directly
      (`ANativeWindow_setBuffersDataSpace` in `warden_swapchain_created`; BUG-025
      worked around). The presenter's last pass - bilinear, FXAA, CAS or FSR 1's RCAS -
      then draws with its HDR variant (`shaders/warden_hdr_*.ps.glsl`: Xenia's passes
      untouched, their main renamed and its result encoded; graft edits make the
      variants beside each effect's module and choose them while `warden_hdr_key` says
      the output is HDR10), DAORecomp's way for an SDR image: the sRGB curve undone,
      the highlights expanded toward the display's peak (Android's
      `Display.HdrCapabilities`: 420 nits) with the shadows and midtones untouched,
      white at the paper white (Settings -> HDR paper white: 150-300 nits, 200 by
      default), the primaries to BT.2020 - Display-P3's with Vivid - and the PQ curve;
      the constants are specialization constants. SurfaceFlinger then lists the layer
      as `dataspace=BT2020_PQ`, composed by the display hardware (`RGBA_1010102`,
      Device), and the Qualcomm composer's dynamic range goes from SDR to HDR, and back
      when it is switched off. The King's Camp: 30.9 fps with it against 31.1. Settings
      -> HDR, off by default (the owner), offered only where the display takes HDR10;
      the options panel switches it while the game runs (the swapchain made again).
      What it looks like on the panel is the owner's to judge - a screenshot is
      Android's tone-mapped copy. Found on the way: BUG-062.
- [x] 9.3 Wide-gamut Display-P3 SDR as the **default** presentation, 10-bit to avoid
      banding. **Done (session 35, runs 370-372).** What D8 took for granted holds: the
      panel runs in Display-P3 (`dumpsys SurfaceFlinger`: current colour mode
      DISPLAY_P3), and a frame tagged sRGB is converted into it by Android - so the
      game's own colours, made for sRGB televisions, already reach the panel as P3. The
      swapchain is now 10-bit by default (`A2B10G10R10_UNORM_PACK32`, tagged sRGB:
      Settings -> Colour -> Accurate; `warden_surface_format` in `warden_gpu.cc`, asked
      first by the graft's edit to the presenter's format choice): the colours the same
      (mean saturation 0.397 against 8-bit's 0.406, the scene's own movement), the
      upscaling's in-between values kept, for about 1% of the GPU at the King's Camp (95%
      against 94% by the same script; 29.9 fps both), the layer still composed by the
      display hardware (`RGBA_1010102_UBWC`, Device). **Vivid** tags the same values
      Display-P3: every colour stretched to the panel's wider gamut, more saturated than
      the game was made (+9% mean saturation at the dim camp; 30.0 fps). Not done:
      converting to P3 in the presenter's shader - Android's conversion is the same
      thing. The earlier notes: **Confirmed reachable and now a specific task.**
      `A2B10G10R10_UNORM_PACK32` + `DISPLAY_P3_NONLINEAR` is available, but only after
      the graft edit that enables `VK_EXT_swapchain_colorspace` (BUG-023) - without it
      the driver is permitted to report sRGB only, and did. The remaining work is that
      **Xenia's presenter still picks `R8G8B8A8_UNORM` + `SRGB_NONLINEAR`**: it has no
      notion of preferring a wide-gamut or 10-bit swapchain, so its format selection
      needs teaching. The Android side is already done
      (`android:colorMode="wideColorGamut"`, and the window reports that it got it).
- [x] 9.4 FSR 3.1 path, **default off** (D7). **Not built (session 35), the owner's to
      revisit:** FSR 3.1 would be new Vulkan code with motion vectors the game never
      produced, and it pays off only by rendering below 1280x720 - the title's render
      targets are the console's fixed EDRAM layout - on a GPU 93-95% busy at the King's
      Camp at 30 fps; FSR 1 (9.5) and FXAA (9.7) are the upscalers that fit. Needs motion vectors DAO never produced —
      reconstruct from depth plus camera matrices (Q7). Note this is **new Vulkan code,
      not a port**: the whole `dao_upscale*` family (FSR 3, FSR 4, XeSS, DLSS, motion
      vectors, frame generation) and `dao_streamline.cc` are Direct3D 12 only, and
      `xenia/dao_temporal_upscaler.h`, which looks like a shared interface, is 30 lines
      holding an enum and a cvar. The D3D12 code is reference material.
      **Done (session 36, runs 398-424) - the owner's ask of 2026-10-27: FSR 3.1 as a
      setting with Performance, Balanced and Quality.** Options -> FSR 3.1, from the
      next start; with the camera's motion vectors. At the King's Camp, still (runs
      421-423): Quality 23.4 fps (FSR 8.35 ms), Balanced 25.9 (7.55), Performance 28.8
      (6.78); off 31.1. How it looks in play is the owner's to judge; left, per-object
      motion (step 4). The history: Built and working, off by
      default (`files/fsr3` holding `quality`, `balanced` or `performance` turns it on,
      from the next start - session 36; no row in the options panel yet):
      - **The SDK, from source.** The FidelityFX SDK's v1.1.4 (`third_party/FidelityFX-SDK`,
        its `sdk` folder, MIT - THIRD_PARTY.md): the FSR 3.1 upscaler and its Vulkan
        backend compiled into the app (`warden_ffx` in CMakeLists.txt). Its shaders:
        `tools/build_fsr3_shaders.py` - the SDK's own compiler, `FidelityFX_SC.exe`, is a
        downloaded executable and is not run, so the NDK's `glslc` compiles the ten GLSL
        passes in all 64 permutations (32-bit and 16-bit: 1,280 compiles in 23 s) and
        the reflection the host code binds by (each descriptor's
        name, binding, set, array size) is read from `spirv-dis`; it writes the SDK's
        permutation-header layout into `build/fsr3-shaders`. Porting it took: the compat
        header (`wcscpy_s` and friends, `<locale>`, `FFX_UNUSED`), a stub for frame
        generation's swapchain hook (link), and two SDK faults (BUG-063: its context does
        not fit with Android's 4-byte `wchar_t` - a patched `ffx_types.h` copy, the size
        doubled, first on the include path; BUG-064: it picks shader paths by the
        extensions the GPU has, not the features the device enabled - its extension query
        answers none (`warden_fsr3.cc`), the 32-bit paths). The graft enables the two
        storage image features its passes use (extended formats, writes without a format).
        Run 399 (`files/fsr3-probe`): a context made on the game's own device, 60.8 MB
        of GPU memory.
      - **The game's frames - DAORecomp's design ported** (`warden_upscale.cc`,
        `xe::gpu::WardenUpscaler`, declared and hooked by graft edits; friend of the
        render target caches): the scene's depth-tested screen-wide draws jittered
        through the viewport's `ndc_offset` (Halton phases from FSR; y points down in
        this backend, so `+2*jy/h`); the scene's depth copied at the first resolve after
        its draws (the tile rectangle from `ResolveInfo::dao_rect`); the image before the
        UI copied after each full-screen pass reading the scene texture (the last one
        wins); at the swap FSR 3.1's dispatch - recorded by the SDK itself through a new
        callback in Xenia's deferred command buffer (`CmdWardenCallback`) - then
        `warden_upscale_composite.cs.glsl` puts the UI back (where the front buffer
        differs from the image FSR got, the front buffer scaled up instead) and the
        1920x1080 composite replaces the front buffer in Xenia's swap. Motion vectors are
        zero so far. `files/fsr3-show-ui` paints where the UI was found red.
      - **Measured at the King's Camp (runs 400-401):** every 3D frame upscaled (1,314 in
        a row, until a frame without the scene's depth, shown as it is); the picture right, the HUD
        sharp, the scene about 50% more edge detail than bilinear (edge energy 13.4-13.6
        against 9.0 in a UI-free crop at the same brightness); **but 23.1 fps against
        31.1**: FSR 3.1 8.4 ms of GPU a frame, the composite 0.72 ms, and six render
        target copies a frame (the depth tile and five "before the UI" captures).
      - **Run 398, the render size:** told a 960x540 video mode (`files/hack-video-mode`),
        the title still swaps 1280x720 - it never reads the mode's size. Its device
        set-up (`sub_82692230`) takes 1280x720 if the mode is HD or widescreen (else
        640x480), stores the size in the engine's globals (`[r29]`, `[r28]` there) and
        builds Direct3D's present parameters from it; and the scene tiling
        (`sub_826955E8`) asks the renderer for the screen size and tiles only 1280x720
        as the console does (any other size: one tile). So the engine is
        resolution-aware - the way to Balanced and Performance.
      **Next, in order (to resume):**
      1. **The 16-bit paths - done (session 36, runs 402-404).** The graft enables
         `shaderInt16` and Vulkan 1.2's `shaderFloat16` where supported (the 16-bit
         permutations declare `Float16` - accumulate, the luma and shading-change
         pyramids - and `Int16` - the reactive mask, RCAS; no 16-bit storage), and
         `warden_fsr3.cc` answers `VK_KHR_shader_float16_int8` (BUG-064's note): the
         SDK's `FFX_HALF` permutations, on by default; `files/fsr3-half` holding `off`
         keeps the 32-bit ones, `alternate` changes between the two each timed window.
         Run 403, alternating at the King's Camp (FSR then without its depth work -
         BUG-067; 9.2 ms with it): **FSR 3.1 7.8 ms against 8.4** - all of
         it in the accumulate pass (4.2 against 4.8 ms; SDK v1.1.4's other passes have
         next to no 16-bit arithmetic) - and the camp 23.3-23.4 fps against 23.0-23.1. Each
         pass is timed now (the SDK's `vkCmdDispatch` taken through the procedure lookup
         it is given, a timestamp after each - the `its passes` line): prepare inputs
         0.38, luma pyramid 0.15, **shading change pyramid 1.56**, shading change 0.08,
         prepare reactivity 0.52, luma instability 0.35, **accumulate 4.2**, RCAS 0.52 ms.
         The shading-change pyramid costs ten times the luma pyramid's similar
         downsample by design: its source, per pixel of 1280x720, is ten luma loads
         raised to a power and two five-element sorting networks (the current frame's
         and the reprojected previous one's), where the luma pyramid reads one value; the
         SDK runs both pyramids 32-bit on purpose (its blob accessor takes no 16-bit
         table for them - their 16-bit source loaders are stubs returning zero). Found
         on the way: BUG-065, the process's death at the first context or image made
         again.
      2. **One capture a frame - done (session 36, runs 403-407).** Traced
         (`files/fsr3-trace`), the King's
         Camp's post-processing is three passes reading the scene - `A91D0274EC946F8C`,
         `9BD4DE4D24A972A4`, `51B90349C6D6D3DA` (half size) - each resolved for the next
         (no resolve clears anything), then the tonemapping, `20BA772900FB8AA3`
         (DAORecomp's `kTonemapPixelShader`), not resolved: the UI (`138D82BC28F866CC`
         first) is drawn over it. All draw into EDRAM tile 1024 at pitches 8 and 16, so
         an earlier pass's image cannot be kept past the next. So the chain's last pass
         is learned from the frame before (the pass its image before the UI came from);
         while it is known, the passes before it are ordinary draws, never copied; a pass
         after it, or a 3D frame without it, has the next frame copy every pass and learn
         again. The kept render target is copied before the next draw into any render
         target (one may take its EDRAM and hand it back through a transfer this hook
         never sees), at a resolve that clears, or at the swap. Six copies a frame down
         to three: the image once, at the UI's first draw - the camp's conversation
         29.4 -> 30.6 fps, exploring 23.3 -> 23.7-23.9 (runs 403-405). Then the depth: a
         per-frame summary of the draws in the trace (run 406: grouped by depth test,
         colour writes, the depth and colour buffers' EDRAM bases, pitch, MSAA, and what
         the upscaler made of them) shows the camp's frame as a depth prepass (816 draws,
         depth at EDRAM 0), the shadow maps (pitch 800 at EDRAM 1024 - never taken for
         the scene), the scene's colour draws (1,122, depth at 0), then post-processing
         passes 640 wide with 2x MSAA, depth-tested - which the rule for the console's
         MSAA tiles took for the scene: jittered, and a depth copy from them (BUG-066).
         Fixed: with the single-sampled scene, multisampled draws are not the scene, and
         the depth is taken once, at the scene colour's resolve. **Two copies a frame,
         the camp 24.2 fps exploring, 30.6-31.0 in conversation (run 407)** - from 23.1
         at 32-bit with six (run 401).
      3. **The profiles' render sizes - done (session 36, runs 408-409).** Quality
         1280x720, **Balanced 1120x630** (1.71x - widths in whole 80-pixel EDRAM tiles,
         so not the 1129x635 of 1920/1.7), **Performance 960x540** - `files/fsr3`
         (`warden::fsr3::RenderSize`, read once). The game's device is made at the size:
         `warden_game.cc` wraps the device set-up (`sub_82692230`) and, inside it,
         Direct3D's device creation (`sub_82D8E8E0`, taken off `warden_d3d_probe.cc`'s
         list), rewriting the present parameters' back buffer size and the engine's two
         size globals as the device is made. The scene tiling's own rule at any size
         but 1280x720 is one tile with **4x** MSAA (type 2 - the console's 640x480), so
         the tiling hook now makes any MSAA tiling one tile without MSAA covering the
         screen at the device's size. Scaleform lays the UI out at the smaller size
         as it was (the HUD a little softer at Performance: drawn at 960x540, scaled up
         twice); the title screen's 3D background is upscaled too. At the King's Camp,
         exploring (the 60 fps setting, GPU-bound): Quality 24.2 fps (FSR 7.8 ms),
         Balanced 26.5 (7.05), Performance 29.3 (6.5), in conversation 30.7 / - /
         37.3; FSR off, 31.1 - with FSR's depth work skipped (BUG-067); with it (runs
         421-423): **Quality 23.4 (8.35 ms), Balanced 25.9 (7.55), Performance 28.8
         (6.78)**. About 5.3 ms of FSR's cost is at the 1080p output whatever
         the render size - accumulate 4.1, RCAS 0.5, the composite 0.7 - so even
         Performance stays just under FSR off at the camp, for the reconstructed
         detail; an output below 1080p, or no RCAS, would cut it (the owner's call).
      4. **Motion vectors - the camera's done (session 36, runs 410-420); per-object
         motion left - to revisit later (the owner, 2026-10-31: FSR 3.1's character
         motion).** The static meshes' vertex shaders (`F003B6862AAE7F16`,
         `737FF30C11453B86` - 600 of the camp's draws) compute the position as `c0 x + c1
         y + c2 z + c3`: their world-view-projection is in c0-c3. For any mesh that has
         not moved, WVP(before) x inverse(WVP(now)) is the camera's clip-space
         reprojection (the world matrix cancels), so the upscaler keeps each frame's
         static mesh draws (their vertices, indices and count, and c0-c3), matches the
         meshes drawn once in both frames (instanced copies - the same tree in several
         places - are ambiguous and skipped: 3-9 left a frame at the camp), and takes the
         reprojection most of them agree with within a quarter pixel at test points
         across the screen (a mesh that moved gives another). `warden_upscale_motion.cs.glsl`
         then reprojects every pixel's depth (un-jittered, the guest viewport's scale)
         into render-pixel motion, 0.32 ms; a cut (the centre more than a quarter screen
         away) restarts the history. The camera's planes come from the same matrices -
         near 0.15, far 4008, 60 degrees vertical at the camp - for FSR's metres instead
         of DAORecomp's stand-ins. Known in 96-100% of frames while turning (run 411);
         probed at five pixels (`the motion vectors at the centre and the quarter
         points`): 3-16 px a frame, the near ground against the far by the orbit's
         parallax (run 415). **And then BUG-067:** FSR 3.1 had never had its three shared
         resources, so it ran without depth and motion since it was first built - every
         camera motion smeared, vectors or not (runs 410-417, found with its debug view
         and a deliberately wrong 20 px). Passed now: a turn without streaks (run 420),
         FSR 9.2 ms at Quality (its reprojection no longer skipped). **Left:** what moves
         of its own - characters, creatures, swaying foliage - gets the camera's motion
         only; DAORecomp's way for it (each opaque draw's vertex shader run again with
         its positions captured, matched with the previous frame's draw, the motion
         drawn where it meets the scene's depth - `dao_upscale_motion.cc`) would need
         `VK_EXT_transform_feedback` in Xenia's shader translator. Test switches:
         `files/fsr3-no-motion` (zero, or N px everywhere), `files/fsr3-debug` (FSR's
         debug view).
      5. **The option - done (session 36):** Options -> FSR 3.1: Off / Quality / Balanced
         / Performance, from the next start (`Options.java`, the `fsr3` file) - the
         game's device is made at the render size, and the upscaler's resources with the
         first 3D frame. Not done: on and off while the game runs (the panel marks the row
         "from the next start").
      The probe marker and its function (`warden_fsr3.cc`, `Probe`) can go once the
      upscaler's own context creation is the test.
- [x] 9.5 FSR 1 spatial + sharpening as the cheap fallback for heavy scenes. **Mostly
      free**: Xenia's own guest-output FSR 1 (EASU + RCAS) already has `vulkan_spirv`
      bytecode generated and is already wired into `vulkan_presenter.cc`'s paint
      pipeline. This is the one presentation feature that does not need writing.
      **Done as a setting (session 35, runs 363-364):** Settings -> Upscaling
      (`files/upscaling`: `fsr`, `cas`, or `bilinear` - the default), set on the
      presenter as the title boots (`gpu::ApplyPaintSettings`). The title's 1280x720
      reaches the 1920x1080 panel: bilinear the softest; FSR 1 the sharpest - pillars,
      paving, statues - with the foliage's stair-steps sharpened too, and at the King's
      Camp the GPU 96-97% busy against 93%, 29.8 fps against 30.0; CAS between the two
      to the eye, but 28.3-28.8 fps at the camp with the GPU *less* busy (87-91%) -
      something on the presenting side, not the GPU, not followed up. Bilinear stays the
      default, as the plan had FSR 1: a choice.
- [x] 9.6 16× anisotropic and bicubic texture filtering. **16x anisotropic, on by default
      (session 35, runs 355-358):** Xenia's own `anisotropic_override` - every mipmapped,
      linearly filtered texture, as DAORecomp's Graphics tab sets it - from
      `files/anisotropic` (2, 4, 8 or "off"; no file, 16x), and the Settings' Texture
      filtering. At the King's Camp it costs nothing measurable: the GPU 93% busy with
      it and without, 30 fps both, 2,123 draws a frame; and the receding stone path,
      a smear without it, keeps its flagstones. **Bicubic: not done** - it would take
      four texture fetches for each of the game's, on a GPU 93% busy at the camp.
- [x] 9.7 MSAA / SMAA options (note the EDRAM interaction from 3.6). **FXAA as a
      setting; the game's own MSAA measured and left out (session 35, runs 382-383).**
      Xenia's Vulkan backend has no anti-aliasing (Direct3D 12's FXAA pass was never
      ported), so the app has its own: `shaders/warden_fxaa.ps.glsl`, NVIDIA's FXAA 3.11
      (PC quality preset 12, green as luma) and the bilinear upscale in one pass, with
      the bilinear shader's inputs - Settings -> Upscaling -> FXAA makes it that pass's
      module (`warden_paint_shader`, a graft edit to the presenter's shader creation).
      `tools/build_xenia_shaders.py` compiles it, against a copy of the header patched
      for Vulkan's separate texture and sampler. At the King's Camp: 28.2 fps against
      29.9 with plain bilinear (runs 382 and 370) - the edges a little smoother, the HUD's
      text untouched to the eye - the same kind of presenting-side cost as CAS (9.5).
      The game's own MSAA, the console's two 2x MSAA tiles (`files/scene-msaa`): 20.2 fps
      at the camp, the GPU 99% busy, 3,248 draws a frame against 2,118 (the second tile
      re-draws a third of the scene) - not offered. SMAA not done: three passes and two
      lookup textures, on a GPU with no room. **Since 10.1** the FXAA module is that pass's
      dithered variant, always - Xenia dithers only when asked, for 8-bit output - and
      FXAA is the bilinear effect with dithering on, so it switches while the game runs
      like the other effects (run 394).
- [x] 9.8 Shadow resolution and draw-distance increases, tuned per area so the
      streaming system isn't broken. **Closed the other way round (session 35, runs
      376-378).** No increase to be had: the engine reads no shadow or distance setting
      (its configuration reads, `sub_824C2F10`, ask for one debug key), DAORecomp offers
      none either, and the Thor's GPU is already 93-95% busy at the King's Camp at 30 fps
      - more shadow or distance would cost the frame rate. What the engine does have are
      the PC version's switches, left in the 360 engine, which DAORecomp's graphics menu
      uses: its dynamic shadows (bit 2 of the render flags' last byte, `0x832E238F`) and
      its post-processing (the frame buffer effects byte, `0x832E23D4`). Now Settings ->
      Dynamic shadows and Post-processing (`files/shadows`, `files/post-processing`
      holding `off`), cleared every frame before the scene's tiling as DAORecomp does.
      At the King's Camp at 60 fps, GPU-bound (98% busy): 31.6 fps; shadows off, 36.5
      (+16%: the trees' dappled shadows on the path go); both off, 41.5 (+31%: the haze
      and light shafts go too, the scene darker and flatter). Both on by default; with
      the 60 fps setting they are the owner's lever. The owner's call: reopen if more
      shadow or distance is wanted at a frame-rate cost.
- [ ] 9.9 **Revisit later (the owner, 2026-10-31) - the missing conversion found upstream: see 10.13 (session 40's plan update); 10.13 done (run 573): PC texture packs convert in the app.** Asset override folder for texture packs. **The mechanism exists (session 35):**
      the override tree over D:\ (7.3) - and through it the title's own patch folder,
      `packages/core/patch/`, where a file replaces the archived one of its name, textures
      included (the title update itself puts one there, `uh_tat_p01_0t.xds`). A file
      pushed to `files/override/packages/core/patch/<name>` is used from the next start.
      **Left:** packs made for the PC (DDS) would need converting to the 360's own `.xds`
      (tiled DXT with a Xenos fetch constant at the end: `tools/dao_xds.py` reads them,
      nothing writes them yet), and a place the owner can reach (the tree is in the app's
      private storage).
      **What a converter would face (session 35, looked at on the computer):** the
      game's world textures - 6,861 `.xds` in `models.erf` - carry their whole mip chain
      in the 360's layout after the top level (`bark02_d`, 256x512 DXT1: the top level
      64 KB, the file 98 KB; tiled, 8-in-16, the trailer's first 28 bytes the same in
      every one), so a writer has to reproduce Xenia's guest texture layout, the packed
      mip tail included - checkable on the computer by rebuilding the game's own textures
      byte for byte. And the game loads every texture into the console's 512 MB of
      memory, sized for the console's own textures: a pack at the PC's resolutions can
      run it out of memory, so only replacements of the same size are safe. Not started;
      the owner's call, if there is a pack worth having.
      **More than 512 MB? (session 35, the owner asked.)** DAORecomp keeps the console's
      512 MB (`kernel/memory.cpp`: one physical memory viewed at `0xA0000000`,
      `0xC0000000` and `0xE0000000`, the top 1.5 GB of the 32-bit space) and its mod
      loader warns that texture-heavy mods may run the game out of it. The game takes
      all its memory as physical memory in the `0xA` view, in a fixed pattern (run
      389): heaps of 214 MB (`0xD600000`), 2 MB and 18 MB, then `MmQueryStatistics`'
      remainder less 6 MB - 270 MB - as its last heap (BUG-043). Every texture of a
      traced frame at the King's Camp (run 391: 101 textures, 20 MB) lies in the first,
      fixed 214 MB heap; reporting more memory would only grow the last heap, which
      holds none. And more physical memory has no addresses to live at in the 360's
      layout, while Xenia's GPU side (its shared-memory buffer, its address masks) is
      built for 512 MB. What could work instead: moving memory from the last heap to
      the first by patching the game's `0xD600000` - after measuring how full each heap
      really gets. **Measured (run 393, `files/heap-scan`):** through the Korcari Wilds
      and its combat, the textures' heap reaches +127 MB of its 214 MB (106 MB of its 64
      KB blocks written; 49 MB at the title screen); the last heap has 135 MB of its 270
      MB written (and something at its very top); the 18 MB and 2 MB heaps are nearly
      full. So in an area like the Wilds a texture pack has some 85 MB of room in the
      textures' own heap before any rebalancing; the heavier areas (the cities) are
      still to measure.
- [x] 9.10 **Tactical camera unlock** — resolve Q2 first. Research spike to find out
      whether the PC camera path survives in the 360 build. Drop the feature if not.
      **Dropped (session 35, runs 379-380): Q2 answered - not in a usable form.** In the
      executable (`build/xex-check/image.bin`): `ToggleTacticalCameraMessage` is in the
      engine's list of message types; the controller binding table (`sub_823E0A48`,
      input codes in XInput's button order without its gaps - the d-pad 1-4, START 5,
      BACK 6, the sticks' clicks 7 and 8, the shoulders, A 0x0B, B 0x0C) binds it to code
      8, the right stick's click; and the game-mode factory registers it with
      `GameModeExplore`. But at the King's Camp neither R3 (run 379) nor the message
      sent through the engine's own SendMessage (`sub_825E47B0`, run 380) moves the
      camera, and no tactical-camera option or camera-distance setting exists by name -
      the PC's `EnableShadows` and `EnableFrameBufferEffects` do (9.8). The camera's
      zoom-out limit, a constant somewhere in its update, would be the next lever if
      this is ever reopened. New test marker: `files/send-message` sends message
      strings to the engine at set times in a mode (TESTING.md).
- [x] 9.11 Skip intro videos, faster loading. **The intro logos as a setting (session 35,
      runs 365-366):** Settings -> Intro logos - shown, the game's own, or skipped (the EA,
      BioWare and Edge of Reality movies left out of the movies folder's listing, which
      is all the title needs; `files/skip_intro_logos` holding `skip`, or empty as the
      capture scripts make it). Faster loading: nothing found to do - the loads read
      flash storage already, and the stored pipelines (10.3) took the compile stalls out.
- [x] 9.12 Headphone virtualization and 5.1 → stereo downmix. **Options -> Volume boost
      (the owner, 2026-11-02: "very low even with maxed device volume"; BUG-099):** the
      mixdown keeps the headroom combat's peaks need, so exploring peaks near -20 dBFS
      and quiet areas far below. Off, +6, +12 or +18 dB, taken at once: the stereo raised
      by that much and a lookahead limiter (1.3 ms ahead, 200 ms release; the least gain
      over the window, averaged over it, so a peak is always under the -0.5 dBFS ceiling
      and the gain glides) - the quiet and middle of the game only louder, the loudest
      moments held down while they last (`warden_audio_mix.cc`'s `Boost`). Off by
      default (the owner's rule: a change to the sound is an option); the owner's Thor
      set to +12 dB. Checked on the computer (a 0.05 tone exactly 4x, a 0.6 burst held
      at 0.944, back within 0.3 s, Off untouched) and on the Thor (the Wilds' peaks 0.3
      out against 0.08 before). The 5-second audio report now gives the output's peak
      too ("out"). **Done (session 35, runs
      367-369; `warden_audio_mix.cc`).** First measured: `files/audio-dump` records the
      title's six channels as submitted (run 367: menus, the Korcari Wilds, combat;
      `tools/audio_levels.py`). The intro movies use the front pair alone; the game all
      six - the back pair largely the front pair again, 7 dB down (correlation 0.83), the
      centre in use, the low-frequency channel hardly (-43.5 dBFS against -20); combat
      peaks past full scale, the menus sit 25 dB below it. Through Xenia's mixdown (the
      centre at -6 dB, the back at 0 dB, / 2.5) the whole run is -17.9 LUFS, a
      handheld's usual level, never clipping. **The speakers** now get ITU-R BS.775's
      (the centre and each back channel at -3 dB, the low frequencies left out, / 2.414):
      the same level, -17.7 LUFS, peaks -2.4 dBFS, and the voices in the centre 3 dB
      forward. **Headphones:** Settings -> Headphones -> Surround places the five
      speakers the title mixes for around the listener - Brown and Duda's spherical head:
      the far ear later by Woodworth's delay (to 0.55 ms) and shadowed by a first-order
      shelf, the back pair a further 3 dB off above 3-4 kHz; normalised so the five
      speakers' average is flat - at the speakers' level (-17.7 LUFS, peaks -1.7 dBFS,
      every octave within 1.7 dB of the mixdown), the two ears' signals 0.82 alike
      against 0.58. Only while the stream plays on headphones (wired, USB, Bluetooth -
      GameActivity's audio device callback against the stream's device); stereo, the
      mixdown, by default. On the Thor (the speaker counted as headphones with the test
      marker `files/audio-force-headphones`, run 368): 6 µs a 5.3 ms frame, nothing
      dropped through combat; the mixdown 0.5 µs (run 369). `tools/audio_host.cc` runs
      the same mixer over a recording on the computer (`audio_levels.py --render`).
      The low-frequency channel stays out of both (ITU's choice; the Thor's speakers
      cannot play it). Left, the owner's: listening on headphones, and the default.
- [x] 9.13 **An autosave as the game is put away - investigate (the owner,
      2026-10-27).** Put away, the game holds where it was (BUG-057), but Android can
      still end the process in the background when memory runs short, and everything
      since the last save goes with it. The owner does not want a foreground service
      keeping the process alive (STATUS Q12); the question instead is whether the game
      can save itself as it is put away. To find out:
      - whether the game's own autosave can be started from outside - through the
        script machinery `warden_game.cc` already drives (a script's native main called
        by name, the game's mode known; TESTING.md's `files/route`), and the scripting's
        own autosave call (`DoAutoSave` in the PC toolset's scripting, if the console
        build keeps it);
      - in which states the game allows a save - exploring, not in combat, a
        conversation or a cutscene, where the game itself refuses - and what to do in
        the others (save at the next state that allows it, or not at all);
      - how long a save takes - its files and the `.backup/` of the one it replaces -
        against what Android gives an app it has paused: the save has to finish before
        the hold (`warden_suspend.cc`, from `onPause`), and the process must not be
        ended mid-write;
      - that it is the autosave slot that takes it, never one of the player's saves.

      **Investigated (session 35, runs 374-375; the test marker `files/save-chain` logs
      the save path's calls with their guest call chains).** Three of the four answered:
      - *How long:* a save is quick. Entering the Korcari Wilds, the job is built at
        51.093 s, the 293 KB save, its 272-byte header and the 458 KB thumbnail are
        written by 51.526 and the container closed at 51.661 - 0.6 s, on a worker thread
        while the update thread goes on. *What Android gives:* the cached-app freezer is
        on (`dumpsys activity settings`: `use_freezer=true`); it freezes an app some
        seconds after it is put away and cached - about ten by Android 13's default - so
        a save started from `onPause`, the hold after it, fits with room to spare.
      - *The slot:* autosaves go to `<character>_1_a!0`, the player's saves to
        `<character>_N_u!0`; the app keeps the previous version of a replaced one in
        `.backup/`.
      - *The save path*, mapped: the save manager is the game object's +292 (the game
        object at `[0x832E20E8]`), its update `sub_82300B68` runs every frame
        (`sub_82284BD8`); `sub_823027E8(manager, user, slot, flag)` saves to a slot of its
        list and is called by two functions reached only through a table,
        `sub_82415048` and `sub_82415158`, which take a request record. The save itself
        is a worker job (`sub_822D0370` builds it, vtable `82174570`; `sub_822CFC30` runs
        it: creates the content, writes the files). The area-load autosave comes from a
        load/save state machine (an object of class vtable `82188678`, state at +144):
        its per-frame `sub_82390360` - from `sub_82348818`, from the game's update - runs
        state 3, `sub_82391E30`, which builds the job. Its state setter, `sub_823904D8`,
        is reached only through its vtable.
      - *Left:* the call that asks for that save - who puts the state machine in state 3,
        with what save type and slot (its fields at +136, +140, +160), or the scripting's
        `DoAutoSave` (the engine function table, by the function's index, which needs the
        PC toolset's list) - and the states the game refuses it in. Then: a test marker
        that asks for it in explore mode, a check that the save loads, and the pause path:
        ask, wait for the autosave container's close (under 2 s), then hold; outside
        explore mode, nothing.
      - Tried (run 381): the explore mode also accepts `"SaveGameMessage -1 2"` (the same
        registration list as the tactical camera's), but that message sent through the
        engine's SendMessage - with `-1 2`, `0 0` and no arguments - reaches none of the
        save path's functions and saves nothing. The trigger is still open.
      - **The mechanism, built and tried on the Thor (runs 384-386, `warden_autosave.h`).**
        As the app pauses, `Runtime::SetPaused` asks for the autosave - only while
        exploring - and the update thread sends the request on its next frame (90 ms
        later); the guest's hold waits on a thread of its own until an autosave
        container (`_N_a!0`, created "create always") is closed - `an autosave written`
        in the log - or 3 s have passed, and coming back before then cancels it (a pause
        generation); the sound stops at once. Behind the test file
        `files/autosave-on-pause`, which names the trigger; without it the hold is
        immediate, as before. Triggers tried, as the test character: the message
        `ExternalSaveMessage` (run 384, the first of the engine's message names) - sent,
        no save; the load/save state machine's own SetState(3) (`sub_823904D8`, vtable
        slot 14; "state3", run 385) - accepted, but nothing updates the machine outside a
        load; with its update called from the update thread each frame (run 386) - state
        3 opens the slot its last job left, `0xd5332a7_0_u!0` (a player slot that does
        not exist; opened to read, nothing written), and stays in state 3. So state 3 is
        not "autosave": it acts on fields its requester sets. **Next:** the area load's
        own sequence - the machine's states and its fields (+136, +140, +160 and the slot
        index) as it reaches state 3 - probed during a load into the Korcari Wilds.
      - **Done (runs 387-390): the trigger is the engine's own DoAutoSave.** Probed in the
        load into the Korcari Wilds (run 387, `files/save-chain` on the machine's
        SetState): the machine goes 1 -> 7 -> 4 -> 6 -> 1 for the load, then 1 -> 3 from
        its request method (`sub_8238FE48`, vtable slot 5: a request of type 3 is a save;
        it checks the current mode allows one, a byte at the mode's +109, and copies the
        request's fields to the save screen's object), and back to 1. The request came,
        through the message dispatcher (`sub_8262B670`), from `sub_824BB520` - called from
        the post-load scripts, and by some 200 compiled scripts in all: DoAutoSave. It
        checks the game allows saving (a byte at `[0x832E20EC]+216`) and sends the save
        manager the save message with the autosave's arguments (-1, 1, its name). Called
        from the update thread on the frame after the pause (run 388): state 3 as the
        area load's, the game in its saving screen (mode 9), `0xd5332a7_1_a!0` written -
        the previous one kept in `.backup/` - and closed 2.4-2.5 s after the request; the
        guest then holds, and coming back returns it to explore mode. The autosave loads
        (run 389: Resume, explore mode reached). **Settings -> Autosave when put away**,
        on by default (`files/autosave-on-pause` holding `off` turns it off), only while
        exploring (combat, conversations, cutscenes, menus and loads: none - the game
        saves at none of them either); run 390 checked the setting's own path. **Off by
        default since 10.1 (the owner, 2026-10-27):** `files/autosave-on-pause` holding
        `on` turns it on, and the options panel switches it while the game runs.
      - Found on the way: the `wilds` and `morrigan` routes start a new game as "Jaden"
        and autosave into his slot on entering the Wilds (runs 367-368, 374-375 did); the
        owner's own game is the owner's character (`0x36db42d6`, autosaved in the Fade on 2026-10-27),
        untouched; the profile came out byte-identical. A route that resumes loads the
        newest save, whoever's it is, so `tools/route.sh` now refuses one (`.resumes`)
        unless the newest save is Jaden's.
- [x] 9.14 **Frame generation - FSR 3.1's and lsfg-vk's (the owner, 2026-10-27), aimed at
      a frame rate target (the owner, 2026-10-28). Closed by the owner (2026-11-02): every
      option stays; the defaults are Rendering resolution 1280x720, Frame generation Off
      and Upscaling Bilinear (FSR 3.1 Off) - what they already were. The lag under the
      game's rate goes on as 9.21, the presenter on a second device.** Frames generated between the
      game's own and shown evenly among them: more frames on the screen for some
      latency. The frame rate the player sets is what the screen shows, generated
      frames included; the game itself runs at the rest.
      - **The target.** Options -> Frame rate keeps its values (30, 45, 60, 120) and its
        meaning to the player - the frames a second on the screen - and a new row,
        Frame generation, says how they are made: Off, FSR 3.1 2x, Lossless 2x,
        Lossless 3x, Lossless 4x. The game's own rate, the base, is the target divided
        by the multiplier, and generation makes up the rest: a target of 60 with
        Lossless 2x runs the game at 30 and generates one frame after each of its own,
        up to 60 on the screen; 120 with Lossless 3x runs it at 40 with two after each.
        Generation never covers for a slow game: below its base (generation's GPU time
        comes out of the game's), the screen shows the multiplier times what the game
        makes, never more. Off keeps today's meaning - the game at the frame rate.
      - **How the panel shows it** (the owner: understandably). Under the two rows, one
        line in plain words that changes with them: "The game runs at 30 fps, and 1
        frame is generated after each: 60 fps on the screen." With generation off: "The
        game runs at 60 fps." Where the base would be under 30 (60 with Lossless 3x
        makes 20), the line adds that the controls answer more slowly then: the game
        reads input at its own rate, and interpolation shows each of the game's frames
        late, after the ones generated before it (half a game frame with 2x - 17 ms at
        30). A choice that cannot be used now is dimmed, the line saying why: Lossless
        without `Lossless.dll` (and where to put it), FSR 3.1 2x without FSR 3.1
        upscaling on (it takes the upscaler's motion vectors and depth). Both rows
        change while the game runs, as the frame rate does now.
      - **The base rate:** `warden_frame_rate.cc` takes any rate, not only 30, 45, 60
        and 120: 30 stays the title's own (two vblanks a frame), 60 every vblank, and
        any other (15, 20, 22.5, 40) the render thread held to it, as 45 and 120 are
        now.
      - **The presenter** shows the multiplier's frames for each of the game's, 1/target
        apart - the generated ones, then the game's own (`warden_frame_pacing.cc`, its
        pacing at 30 made any rate's) - and the frame-time report counts the game's
        frames and the generated ones apart ("the game 30.0 fps, on the screen 60.0").
        Where generation cannot run - FSR 3.1's has nothing to work on without a 3D
        scene (menus, loading, the title) - the game itself runs at the target, so
        menus stay as smooth.
      - **FSR 3.1's frame generation, 2x only** (one frame interpolated between each two
        of the game's: the SDK's dispatch has one output) - the FidelityFX SDK's frame
        interpolation with its optical flow (the same SDK as 9.4, its
        `frameinterpolation` and
        `opticalflow` components and their shaders, through `build_fsr3_shaders.py`'s
        way). It takes the upscaler's motion vectors and depth, so it comes after 9.4's
        motion vectors; DAORecomp's `dao_upscale_fg.cc` (Direct3D 12) is the reference:
        without the SDK's swap chain (its pacing threads assume a game that owns its
        swap chain), on the upscaled scene before the UI - the UI put over the
        generated frame as over the real one, so it is never warped - and the
        generated frame shown half a frame before the real one
        (`ui/dao_frame_pacer.cc`).
      - **lsfg-vk's** - Lossless Scaling's frame generation, which needs no motion
        vectors (it works from the presented frames alone): any whole multiplier from 2
        (its `multiplier`, which it changes without a restart; the panel offers 2x, 3x
        and 4x), and no pacing of its own (v2.0.0-dev's only pacing mode is none - the
        presenter's, above, does it). Run on the image before the UI (9.4's capture)
        with the UI put over each generated frame, as FSR 3.1's is, so text and the HUD
        never warp as they do in Lossless Scaling's own capture of the whole screen;
        the whole frame where there is no 3D scene. **v2.0.0-dev** (`0213be9`,
        2026-08-27, GPL-3.0): the newest version the licence allows - v2.0.0 itself is CC
        BY-NC-ND 4.0, no derivative works (THIRD_PARTY.md has the history). A clone with its tags
        is at `C:\Repos\_ref\lsfg-vk` on the owner's PC. lsfg-vk is a Linux
        Vulkan layer around `vkQueuePresentKHR`; here its backend
        (`lsfg-vk-backend`) would run in the presenter instead, between the guest
        output's paint and the present. **The user supplies `Lossless.dll`** from their
        own copy of Lossless Scaling (lsfg-vk reads its shaders from it at run time):
        put into the app's files folder (adb, or a picker like the disc image's), never
        bundled with the app and never committed (`.gitignore`); without it, the setting
        says so and stays off. The owner's copy sits in the repo root, ignored.
      - Test at the King's Camp with each pairing of target and generation: the two
        rates logged, the GPU time generation takes, the latency it adds; the owner
        judges the smoothness (10.1's panel switches both rows while the game runs).
      - **What the Thor allows (session 36, read from `adb shell cmd gpu vkjson` and
        lsfg-vk `0213be9`'s source):** lsfg-vk's backend makes a Vulkan instance and device
        of its own and exchanges the frames with the application as external memory and a
        **timeline semaphore** passed as opaque file descriptors. The Adreno 740 exports
        semaphores only as sync fds (handle type 0x10, no opaque fd), and a sync fd carries
        only a binary semaphore - so lsfg-vk cannot run as built here. Its shared layer
        (`lsfg-vk-common`'s `vk::Vulkan`) already takes an existing device ("externally
        managed"), so the port runs the backend on the game's own device: a backend
        `Instance` made from Xenia's device, and a context that makes its own source and
        destination images instead of importing them, synchronized by an ordinary timeline
        semaphore of the same device. The shaders come from `Lossless.dll` as SPIR-V
        already (resources 49 on, the 16-bit and performance variants at fixed offsets) -
        no translator. Queue family 0 has three queues (graphics, compute, transfer) and
        Xenia makes one: a graft edit for a second gives generation a queue of its own, its
        submissions apart from the game's, ordered against the presenter by the timeline
        semaphore. External memory and semaphore fds exist (`VK_KHR_external_memory_fd`,
        `_semaphore_fd`, timeline semaphores) but are not needed on one device.
      - **Step 1, lsfg-vk on the game's device - done (session 36, runs 425-426).** The
        bootstrap fetches lsfg-vk at the pin (its shared layer and backend only);
        `tools/prepare_lsfg.py` makes `build/lsfg-vk` with five edits (the externally
        managed device takes a queue index and the 16-bit choice); `warden_lsfg.cc`
        (GPL-3.0, in place of the backend's `lsfgvk.cpp`) fills lsfg-vk's device function
        table itself - Vulkan 1.2's timeline names, no fd functions - and makes the
        context's source and destination images and timeline semaphore on the device;
        the graft makes a second queue in family 0 and enables timeline semaphores (206
        recorded edits). `Lossless.dll` goes into the app's files folder
        (`files/Lossless.dll`). Its SPIR-V shaders: version 1.0, GLSL450, a plain
        LocalSize, only `Float16`, `ImageQuery` and the storage image features already
        enabled. A test (`files/lsfg-test`, TESTING.md) feeds the image before the UI (9.4's
        capture, FSR 3.1 on) into the sources one frame at a time and generates on queue 1:
        the generator is up in 1.6 s (16-bit shaders), generation runs for minutes without
        a fault, made anew between settings three times, and the frame generated between
        two of the test area's lies between them as it should (mean difference 0.24 from
        their average, against 1.2 and 1.5 from each). **Its cost:** the timestamps around
        a generation span 24-29 ms in a light scene and 34-43 at the camp - but the Adreno
        time-slices its queues, so that span holds the game's own work as well; and the
        settings barely move it (quality 29.1-29.5, performance 25.3-25.9, half flow
        25.7-25.9, both 24.3-25.1). From the GPU-bound camp's frame rate instead (frames
        lost a second times their time, over generations a second): **about 4 ms a
        generated frame in performance mode at half flow, about 7 in quality at full
        flow**, at 1280x720 (runs 425-426). Next: step 2, the presenting - the game's
        source frame handed over on the GPU (Xenia's submission signalling the timeline,
        not the host after it completes, which costs two frames), the generated frames
        shown between the game's at the target's spacing, and the UI put over each.
      - **Step 2, Lossless 2x in play - done (session 37, runs 427-441).** No handover
        between queues after all: since the Adreno time-slices its queues anyway, the
        generation is recorded into the game's own command stream on queue 0
        (`warden_frame_generation.cc`; lsfg-vk's command buffer wraps Xenia's - a sixth
        and seventh edit in `prepare_lsfg.py`). In the swap's refresh, after the gamma
        ramp's pass: the guest output image (10-bit) blitted into the generator's next
        source, lsfg-vk's prepass and passes, the frame between the last two blitted into
        the mailbox slot's second image, which the Vulkan presenter now keeps per slot
        (DAORecomp's `dao_generated`; graft edits). So it works on the frames as shown, UI
        and all - as Lossless Scaling itself does; the image before the UI is for later.
        The presenting took five runs to get right (BUG-069 to BUG-073): DAORecomp's
        pacer, timed by the CPU, cannot space frames on a GPU-bound single queue (both
        paints run right after the title's frame whatever their submission time); so a
        thread of the presenter's (`dao_frame_pacer_android.cc`) presents each pair at
        once and the compositor spaces them - each present has a desired time on one
        timeline, the real frame half the title's frame interval after the generated one
        (`warden_frame_pacing.cc`, VK_GOOGLE_display_timing), with FIFO and four more
        swapchain images while frame generation presents. **At the King's Camp, the
        camera turning** (run 439 against run 440 without it): the title 31.8 fps against
        about 38-40 - lsfg-vk's quality mode at 1280x720 is 6.2 ms of GPU a generated
        frame by its own timestamps (8.8 at the 60 fps logos) - and **about 60 frames a
        second on the panel**, mostly two refreshes apart (some one or three); the title's
        swap to the panel 113-115 ms against 70-74 (the real frame held half a frame,
        and the queue). A generated frame from the turn lies between its two (mean
        difference 7.1 from their average, 9.4 from each). Untimed, the compositor drops
        nearly every generated frame (run 441). The options panel's row: Off, Lossless 2x,
        Lossless 2x fast - **lsfg-vk's performance mode at half flow is 1.17 ms of GPU a
        generated frame** (run 442; step 1's 4 ms, judged from the frame rate, was too
        high): the title at 38.4, as without generation, 70-90 frames a second on the
        panel, the swap to the panel about 95 ms; its generated frame lies between its two
        as well (6.4 from their average, 8.9 from each), but the floating name labels
        ghost in it - the UI interpolated with the scene.
      - **Step 3, the UI never interpolated - done (session 37, run 443).** lsfg-vk is
        given the image before the UI - 9.4's capture, which the upscaler now makes with
        FSR 3.1 off too while frame generation is set (no jitter, no upscaling: the
        post-processing's last pass learned as before) - with the game's gamma ramp
        applied as Xenia's swap applies it (a graft accessor for the ramp), and the real
        frame's UI is put over each generated frame, found as the upscaler's composite
        finds it (where the front buffer differs from the image before the UI, at the
        render size): `warden_framegen_ui.cs.glsl`, two passes in the same command
        stream, `warden_hudless.h` between the upscaler and frame generation. With FSR
        3.1 upscaling, its upscaled scene is the image before the UI. A swap without that
        image (menus, loading) is generated from the frame as shown, as before. At the
        camp every guest output was taken from the image before the UI; the HUD and the
        floating names are crisp in the generated frames, no ghost (run 443's dump); 6.8
        ms of GPU a generated frame in quality mode (0.6 ms more), the title at 30.7.
        `files/frame-generation-ui` holding `off` (a test switch) goes back to the frames
        as shown.
      - **Step 4, the frame rate a target on the screen - done for 2x (session 37, run
        444).** `warden_frame_rate.cc`: Frame rate is the rate on the screen; while
        frame generation presents, the game runs at half of it (`TitleFrameRate`) - 60
        is the title's own 30 (two vblanks a frame), 120 every vblank, 45 and 30 the
        render thread held to 22.5 and 15 (the hold that 45 and 120 already used). The
        panel's lines say what each choice gives with frame generation; the dynamic line
        and the dimming the plan describes are not done. At the camp with 60 and
        Lossless 2x: the game at its own 30 (157 of 159 frames two vblanks), **an even 60
        on the panel - 275 of 318 frames two refreshes apart**, generated to real 16.9
        ms; in the turn's heavier views the game at 27-28 and the spacing looser.
      - **Step 5, 3x and 4x - dropped by the owner (2026-10-30), after run 445; the
        presenting's groups stay (2x is a group of two).** Lossless
        3x fast and 4x fast (performance mode: quality's 7 ms a frame is too much three
        times over): the presenter keeps up to three generated images a mailbox slot
        (graft edits; 223 now), the UI pass and the copy run for each, the presenter's
        thread presents the group - the generated frames, then the real one - each a
        step of the title's interval divided among them after the one before, and the
        frame rate's multiplier is the group's size (120 with 4x: the title's own 30).
        **Run 445, 4x at 120:** the game at its own 30, 3.9 ms of GPU for the three
        generated frames, the frames within a group mostly a refresh apart - but 78-87
        frames a second on the panel, not 120: in FIFO each present waits for the one
        before to be latched, so a group of four takes the thread 28-43 ms and some are
        cut short or replaced by the next guest output. (The `swap to panel` figure, 200
        ms there, is not to be trusted with groups dropped: the title's swaps it pairs
        with real frames fall out of step.) 2x unchanged (run 446). To reach 120 the
        group's presents must not wait on one another: presenting from more than one
        thread, or each frame's present as its own paint is done rather than in a row.
      - **The owner in play (2026-10-30):** "feel bad with high input latency without
        looking smooth". With FSR 3.1 Quality on too, the game at 23 fps (FSR 8.2 ms,
        the generation 14 ms at 1920x1080), about 38 frames a second on the panel with
        a pair's frames a refresh apart and then four or five, 175 ms from the title's
        swap to the panel; without FSR 3.1, the game at 30 and 47-57 on the panel, only
        part of it two refreshes apart, 84 ms. The spacing holds while the game holds
        its rate and comes apart under it. Next: FSR 3.1's frame generation (the owner,
        2026-10-30) - DAORecomp's way, from the upscaler's motion vectors and depth.
      - **Upstream checked first (the owner, 2026-10-30).** DAORecomp, 14 commits since
        the pin (moved to `eb2360a`): its Aeducan Thaig fix (7da0e07) is BUG-051's own,
        the same function bounds and switch table - recompile.py's ADDITIONS dropped,
        the recompiled code byte-identical; its Ostagar load crash fixed in a game hook,
        ported (BUG-076); its pipeline storage seek taken with the pin (BUG-077); its
        cinematic depth of field (6be0e0f, Direct3D 12) noted as a later candidate; the
        rest Windows-only. Nothing on upscaling or frame generation. The FidelityFX SDK
        (v2.0-2.4): frame interpolation 3.1.6 against our 1.1.3, its shaders HLSL only
        (DXC, which the NDK has not); their changes are the letterbox's colour rect
        apart from the interpolation rect and fewer clears - nothing for a full-screen
        rect; optical flow unchanged. Stay on v1.1.4. xenia-canary: 85 commits since
        its pin, nine on the GPU (memexport write-back, packed mip tails, 16-bit EDRAM
        scaling, depth bias control) - candidates for a later move of the graft, none
        on presenting. lsfg-vk: nothing newer the licence allows.
      - **FSR 3.1's frame generation, step 0 - its components built (session 37).**
        DAORecomp (`dao_upscale_fg.cc`) drives it through AMD's DLL API on Direct3D 12:
        a prepare dispatch with the upscaler's depth, motion vectors and jitter, then the
        generation from the upscaled scene before the UI, without the SDK's swap chain.
        Here, as the upscaler is, from the SDK's source: its `frameinterpolation` and
        `opticalflow` components and blob accessors in `warden_ffx`, and their shaders by
        `tools/build_fsr3_shaders.py`, now by component (frame interpolation's eleven
        passes in its three options' 8 permutations, optical flow's seven in its one's
        2, 32- and 16-bit).
      - **FSR 3.1's frame generation, step 1 - hooked up, working (session 37, runs
        447-450).** In the upscaler's swap, after FSR 3.1's dispatch and the composite,
        one callback in the game's command stream: optical flow, then frame
        interpolation with the upscaler's own dilated depth, dilated motion and
        reconstructed depth (as the SDK's `ffx_fsr3.cpp`, without frame interpolation's
        prepare), the upscaled scene before the UI as its source, into a half-float image
        at the panel's size (`warden_upscale.cc`; one FFX backend for the two contexts,
        41 MB of GPU memory at 1080p). Frame generation's "fsr3" mode takes it from
        `warden_hudless.h` - no generator of its own - and its UI pass (mode 2) applies
        the game's gamma ramp and puts the real frame's UI over it in one pass into an
        RGBA8 image, blitted to the presenter's slot (0.9 ms); the presenting, pacing and
        frame rate are Lossless 2x's. No frame at a cut or a screen without a 3D scene: the
        real frame alone then. The options panel's Frame generation row gains **FSR 3.1
        2x** (`fsr3`; off unless FSR 3.1 is on). The device now enables
        `shaderStorageImageReadWithoutFormat` (frame interpolation reads its output back
        bound without a format; the graft's 9.4 feature edit). Run 447 (FSR 3.1
        Performance): it works first time - no errors, a frame for every 3D frame - but
        costs 14.4 GPU ms a frame at the camp, the game at 22.8 fps. Run 448 timed its
        passes (the FFX backend's job scheduling wrapped for the pipelines' names): the
        optical flow's search 5.3 ms of its 6.7 at 1920x1080. So the optical flow now runs
        on the render-size image before the UI (the upscaler's own input; 960x540 in
        Performance), its vectors scaled to it - `tools/build_fsr3_shaders.py` writes a
        copy of the SDK's `ffx_frameinterpolation.cpp` whose optical flow vector field
        pass is dispatched over the flow's own buffer rather than the display size over
        the block size (the same at the display size; at the render size half its
        threads read outside the flow). Runs 449-450: **9.5 GPU ms** (the flow 1.9; the
        game motion vector field 2.4, the interpolation 2.2, the previous depth 0.9, the
        inpainting 0.75 - AMD's algorithm as it is: atomics, and a walk along each motion
        vector that grows with the camera's turn); the game at **25.8 fps** at the camp
        during the turn (FSR 3.1 Performance alone 28.8), 45-62 frames a second on the
        panel, latency p50 54-65 ms (Lossless 2x without FSR 3.1, run 446: 55-63 frames,
        49-52 ms). Screenshots during the turn: clean - the colours, the HUD, no
        smearing. `files/fsr3-flow` holding `display` (a test switch) runs the flow on
        the upscaled scene as the SDK does. Runs 451-452, the other profiles (the cost
        follows the render size - the flow and the motion vector field run at it):
        Balanced 11.7 ms, the game at 22.2 fps, 39-52 on the panel, latency p50 59-71 ms;
        Quality 14.5 ms, 19.1 fps, 35-44, 71-84 ms. The options panel's text gives the
        three. Next: the owner's look and feel against Lossless 2x.
      - **Under the game's rate, measured (run 466, the owner's settings: FSR 3.1
        Balanced, Lossless 2x fast; the King's Camp, the game at 23.9 against its 30).**
        The pacing adapts as meant - a pair's frames asked 21 ms apart, half the game's
        measured 42 ms; 47.6 frames a second on the panel - but a 21 ms step on a 8.44 ms
        panel can only be two refreshes, then three, and a quarter of the pairs' frames
        land a single refresh apart, with four-refresh gaps around them; 34-67 frames in
        5 s late for their slot. The cost the owner felt is the latency: **130 ms from the
        title's swap to the panel** (p50; 150-160 p95), against 55 from present to panel.
        The GPU is the limit (99%), and three things add up: the timeline's readiness -
        frames asked for 40-50 ms ahead, grown by the late ones - the real frame held half
        a frame (21 ms), and Xenia's three frames in flight
        (`VulkanCommandProcessor::kMaxFramesInFlight`), each a 42 ms frame of waiting when
        the GPU is behind. Next, one run each, the owner willing: (1) two frames in flight,
        then one - a test switch the graft's frame close reads, waiting on the frame before
        rather than three back, the arrays left at three; expected about a frame (40 ms)
        less latency each, the throughput to be measured; (2) with the game under its
        rate, the pairs' step rounded to whole refreshes - two and three in turn
        deliberately, rather than the slot nearest 21 ms - so no pair's frames come a
        refresh apart; (3) a cap on readiness growth while frame generation runs.
      - **Experiment 1, frames in flight - no help (run 467, the owner's go-ahead).** A
        test switch (`files/frames-in-flight`: 1, 2, or `alternate` - 3, 2 and 1 in turn,
        15 s each) read where the command processor opens a frame (two graft edits, 225
        now: frame N waits for frame N - K's submission). At the camp, the same settings:
        3 and 2 alike - 238-240 frames in 5 s on the panel, the game at 24, 130-134 ms
        from the swap to the panel; 1 halves the game (15 fps, 150 frames) and the
        latency is worse (150 ms). So the queue is not where the time goes. Nor is the
        readiness a cause to cap (experiment 3): it measures how long a present really
        takes to reach the panel, 40-50 ms because the GPU is full - the presenter's
        paints wait behind the next frame's rendering - and capping it would only make
        frames late. The pairs' single-refresh gaps (experiment 2) come from generated
        frames finishing late for the same reason. Under the game's rate the latency is
        the GPU's: about a frame of rendering (42 ms), the half-frame hold (21 ms) and the
        paints' wait (40-50 ms). The levers: headroom (FSR 3.1 Performance, or Lossless
        without FSR 3.1, so the game holds its 30), or - a later change - the presenter's
        paints on a queue of higher priority than the game's rendering. The switch stays
        (3 without it, Xenia's own). **That change, for the owner to weigh:** the
        presenter paints on queue 0 with the game's rendering (every queue at priority
        1.0); queue 1 exists (frame generation's). Painting on a queue of its own means
        semaphores between the refresh (queue 0) and the paint, and the Adreno
        time-slices its queues rather than pre-empting them - a gain of up to the paints'
        40-50 ms wait, not certain.
      - **The game's menu (BUG-081, session 38):** the generator is made again only for
        a size that holds 10 guest outputs - the one frame after the menu, not upscaled,
        made it twice before (a 4-5 s freeze on closing the menu; run 483, none).
      - **The paints on a queue of their own - tried and dropped (sessions 37-38, runs
        473-474).** A test switch, `files/paint-queue` (commit 7b9d30a, removed after):
        the presenter's paints, the UI's fence and the present on a third queue of the
        graphics family, ordered with the refreshes on queue 0 by two timeline semaphores
        (a paint waited on the GPU for its slot's refresh, a refresh on the CPU for its
        slot's last paint - at most 0.1 ms), on queue 0 and on it in turn, 15 s each. At
        the camp (Lossless 2x performance over FSR 3.1), on its own queue: 95-98 frames in
        5 s on the panel against 234 on queue 0, 296-314 ms from the swap to the panel
        against about 182, the game at 24 either way, a paint still unfinished 40-83 ms
        after its submission - **the same with that queue at priority 1.0 and the others
        at 0.0 (run 473) as the other way round (run 474)**. So the driver ignores Vulkan's
        per-queue priorities, and a paint that waits for another queue's semaphore goes
        behind the frames already sent to the GPU rather than right after its own frame,
        where queue 0's submission order puts it. The one way left to pre-empt the game's
        rendering would be a higher global priority (`VK_EXT_global_priority`, present
        on the Thor), which is set for a whole queue family - so a second Vulkan device
        for the paints, with the guest output shared as external memory and synchronised
        by sync fds: a large change, not started. Under the game's rate the levers stay
        headroom (FSR 3.1 Performance, or Lossless without FSR 3.1).
- [ ] 9.15 **LS1, Lossless Scaling's own upscaler, as an Upscaling choice (the owner asked,
      2026-10-30) - looked into, not started: deferred by the owner ("don't do the upscaling
      for now"), to revisit after 9.14.** What `Lossless.dll` holds
      (read on the computer, `tools`-free scratch scripts; nothing from it in the repo):
      300 shader resources (RT_RCDATA) - 202 Direct3D 11 compute shaders as DXBC (ids
      101-302) and 98 as SPIR-V (303-400). The SPIR-V is only LSFG 3, the frame generation
      lsfg-vk uses (its shaders 255-279 at 49 on, 16/32-bit and performance variants; 303
      and 352 unused). The upscalers exist only as DXBC: by their reflection, the scaling
      family at 101-161 (`InputConsts1`, `InputConsts2`, `OutputConsts`, an
      `UpdateMapTexture`), AMD FSR 1 (137-140, 247-248: `Const0-3`, `Sample`), NVIDIA Image
      Scaling (243-246: `coef_scaler`, `coef_usm`); 162-242 an older LSFG and 254-302 LSFG 3
      for Direct3D. The DLL's code names its modes as C++ classes: `LS1`, `LS1_P`, `FSR`,
      `NIS`, `SharpBilinear`, `Bicubic_CAS`, `Anime4k_S/M/L/VL/UL`, `LSFG`, `LSFG2`, `LSFG3`.
      **What building LS1 would take:** (1) its shaders as SPIR-V - doitsujin's
      `dxbc-spirv` (DXVK's author: Shader Model 5.1 and older to SPIR-V 1.6 with the
      Vulkan memory model, a library and a compiler tool), run on the owner's DLL into
      the app's files, never committed; the Thor has Vulkan 1.3 and
      `vulkanMemoryModel` (vkjson). (2) Its host logic - which of the 101-161 shaders run
      in what order, the intermediate images, what `InputConsts`/`OutputConsts` hold, the
      update map, the dispatch sizes - lives only in the DLL's x64 code (the `LS1` and
      `LS1_P` classes); no open implementation exists (lsfg-vk covers frame generation
      only), so it would be reverse-engineered from the machine code, as lsfg-vk did for
      LSFG - more work than LSFG and less certain, and Lossless Scaling's licence on that
      is the owner's to weigh. LS1 is spatial, like FSR 1, CAS and FXAA (in), against FSR
      3.1's temporal reconstruction (in). An easier spatial addition if wanted: NVIDIA
      Image Scaling from NVIDIA's own MIT sources, no DLL needed. Best after 9.14's lsfg-vk
      port, which brings the DLL loading and the SPIR-V path.

---

- [x] 9.16 **Rendering resolution as an option (the owner, 2026-11-01: "1280x720 and
      1920x1080 must be available, as well as other common 16:9 resolutions"; then "dig
      in and do 1080p, more powerful devices than my current can benefit") - done:
      960x540, 1280x720 (the default), 1360x768, 1600x900, 1920x1080 and 2560x1440
      (session 40, runs 552-566).** Options -> Rendering resolution, from the next start,
      set aside while FSR 3.1 is on (its profile picks the size). The King's Camp uncapped
      on the Thor: **960x540 58.5 fps, 1280x720 44.0, 1360x768 about 43, 1600x900 29.4,
      1920x1080 23.6, 2560x1440 15.1** (each checked on screen; 1920x1080 against
      1280x720: the statues, bricks and arches resolved, the lighting the same; Lossless 2x
      on top at 1080p: 19.4 and 38.8 on the screen; the pause menu right).
      - **Up to 1360x768** the game's device is made at the size (`warden_fsr3.cc`'s
        RenderSize, FSR 3.1's mechanism).
      - **Why the larger sizes cannot be made that way (runs 552-564):** the 360's EDRAM
        holds 2,048 tiles of 80x16 samples, and the game lays it out the same at every
        size - depth at base 0, colour at base 1024 - in its full-screen passes. At
        1280x720 each needs 720 tiles; at 1920x1080, 1,632, so the two overlap and the
        render target cache gives each 1,024 tiles - 42 rows of 24 and 16 tiles more, so
        the picture broke at row 672 exactly at x=1280 (black below, a band of wrong
        colour above). All the targets are 32-bit, none 64-bit (an EDRAM map: the test
        switch `files/hack-edram-map` holding N logs the next N changes of render
        targets - bases, pitches, formats, the ranges the cache gives them - and every
        resolve). The scene itself drawn as two tiles (`warden_game.cc`, as the console
        does its two 2x MSAA tiles: rows 0-512 and 512-720, the table logged) was right;
        the game's second tile relies on the EDRAM's wrap (bases 208 and 1232 with a
        window offset of -816 tiles land on 0 and 1024). A larger emulated EDRAM would
        not separate depth at 0 from colour at 1024, and Xenia's transfer constant has no
        bit left for a 12-bit base.
      - **So above 1360x768 the device is made at half the size and Xenia renders every
        render target at twice it** (its draw resolution scale; the Thor has the sparse
        binding the scaled resolve memory needs): 800x450, 960x540 and 1280x720 at 2x -
        the EDRAM addressed at the half size, where the game's layout fits (960x540 is FSR
        3.1 Performance's size, long proved), the pixels the full size's. Surfaces 160
        pixels wide or less stay at the game's size (Xenia's threshold), so the deck still
        reads the HUD's 146x146 portraits back. The performance numbers show the scaled
        size. Kept for the record: the two-tile path and `native-1600x900` /
        `native-1920x1080` in `files/resolution` (drawn wrongly, not offered).
- [x] 9.17 **Field of view, from upstream (the owner picked it, 2026-11-01) - done
      (session 40, run 554): Options -> Field of view and Field of view in
      conversations, 80-120 degrees (91, the game's own, the default), live
      (`warden_camera_fov.cc`, upstream's hook on this build's same functions and
      main camera). At 120 the King's Camp shows the guard at the left edge and the
      columns either side, the picture's proportions kept.**
      DAORecomp's camera hook (`gpu/camera_fov.cpp`): the game's camera update gives
      the main camera its FOV every frame through SetFov (vtable slot 3), and the
      hook scales the tangent of the half angle, so proportions keep - a gameplay FOV
      (80-120 degrees horizontal at 16:9, the game's own 91 the default) applied in
      exploration and combat, and a separate conversation FOV for conversations and
      cutscenes. Options rows, live, as upstream; its TU6 addresses (`82284BD8`) are
      this build's.
- [x] 9.18 **Motion blur, from upstream (the owner picked it, 2026-11-01) - done (session
      40, runs 557-558).** Options -> Motion blur, Off (the default, as upstream), Low or
      High, live, with FSR 3.1 on (set aside without it: the blur follows the camera's
      motion FSR 3.1 is given). Upstream's passes ported to Vulkan: a tile pass (each
      pixel's blur and log depth, each tile's longest blur, camera cuts counted; only
      motion that lasted a frame is blurred) and a neighbourhood pass
      (`warden_motion_blur_tile`, `_neighbor`), then McGuire's reconstruction filter in
      the composite (`warden_upscale_composite`), the UI kept sharp. Low and High: the
      shutter open a quarter or half of the frame, 8 or 12 samples. The motion vectors are
      the camera's (characters get the camera's motion), so turning and walking blur,
      a swinging sword does not. The King's Camp turning, FSR 3.1 Performance: the
      composite with the blur 1.03 ms against 0.72, 51.8 fps against 51.6 - free in
      practice. Upstream's description follows: Off (the
      default, as upstream), Low or High; costs GPU while the scene moves, so the
      cost is measured at the camp turning before the choices ship. Upstream's
      implementation is in its effect parameters and frame history
      (`gpu/effect_params.cpp`, `gpu/frame_history.*`) - port what applies to the
      Vulkan backend.
- [x] 9.19 **Depth of field in conversations, from upstream (the owner picked it,
      2026-11-01) - done (session 40, runs 559-560); Off by default, the owner's decision
      (2026-11-01).** Options -> Depth of field in conversations, Off or On, live, with
      FSR 3.1 on. Upstream's four passes ported to Vulkan (`warden_dof*.glsl`): the
      focus from a sorted 16x16 grid of the scene's distances (the nearer quarter),
      eased, taken at once on a cut; the scene at half size with each pixel's circle of
      confusion; a golden-angle disc blur; the blend over the sharp scene - before the
      composite, so the UI stays sharp and motion blur takes the result. The camera hook
      (9.17) publishes the shot (mode 4 or 5) and its field of view; the effect eases in
      over 300 ms. **Measured talking to Duncan at the King's Camp, FSR 3.1 Performance
      to 1080p: about 2.1 ms of GPU a frame (the composite 2.81 ms against 0.72), 44.0
      fps against 48.7** - the wall behind him and the Warden soft, both of them sharp,
      the dialogue untouched. `files/dof-show` (a test switch) shows the blur instead of
      the scene, red behind the focus, blue in front. Upstream's note: Upstream draws depth of field in conversations and cutscenes
      only (about 0.85 ms a frame at 4K on a desktop GPU; the FOV hook publishes the
      shot state it keys on). An Options row; whether the Thor's default is on (as
      upstream) or off waits on the measured cost at 1080p in a conversation - the
      owner decides with the number in hand.
- [x] 9.20 **Photo mode, from upstream (the owner picked it, 2026-11-01) - done (session
      40, runs 561-562).** L3 and R3 together, or the options panel's new **Photo mode**
      row (in the game, above Quit), pause the world and switch to the game's own
      development fly camera (mode 6), the HUD hidden (`warden_photo_mode.cc`, upstream's
      hooks on the mode controller's update, the fly camera's update and the analog
      actions). The sticks move and turn it (LB and RB slower and faster, as the fly
      camera has it), LT and RT lower and raise it, the D-pad zooms (20-120 degrees) and
      rolls (up to 45), X steps the speed (x0.25 to x4), Back puts it back, Y hides the
      hints, B or START leaves; those buttons are photo mode's, not the game's. **A saves
      a photo:** the game's surface alone (PixelCopy of the SurfaceView, so the app's
      overlays are not in it) as a 1920x1080 PNG in **Downloads/Dragon Age Origins**,
      named by the area and the time (MediaStore, no permission) - run 561: "Dragon Age -
      Ostagar - 2026-11-01 21.35.48.png", the camp without the HUD or the on-screen
      controls; the left stick flew the camera past the party; B came back to exploring.
      The hints (`PhotoOverlay.java`) name the buttons as the prompts do, or Xbox's with
      the on-screen controller (then at the top, clear of its Back and Start). Upstream's
      description follows: Pause the
      world, fly the camera (the game's own fly camera, mode 6), hide the HUD, and
      keep a shot - upstream's `ui/photo_mode.cpp`, `gpu/held_shot.cpp` and its
      overlay, opened from the pause menu. On the Thor the shot lands in **Downloads**
      (MediaStore, no permissions), named by area and time. Reuses 11.1's Downloads
      plumbing.
- [ ] ~~9.21 The presenter on a second Vulkan device of higher priority~~ **Dropped by the
      owner (2026-11-02) after step 1 below: priority gives no advantage on the Thor.**
      **(The owner asked, 2026-11-02, after 9.14.)** Under the game's rate the time from a swap to the panel
      is the GPU's: the presenter's paints wait behind the next frame's rendering on the
      game's queue (9.14's measurements: 40-50 ms of the 130). A queue of its own did not
      help - the Adreno driver ignores per-queue priorities (runs 473-474). The way left:
      a second `VkDevice` whose queue family has a higher global priority
      (`VK_EXT_global_priority`, present on the Thor), painting and presenting; the guest
      output and the generated frames shared with it as external memory
      (`VK_KHR_external_memory_fd` / Android hardware buffers) and ordered by sync fds
      (`VK_KHR_external_semaphore_fd`). Expected: up to the paints' 40-50 ms back, not
      certain. Started after BUG-091/BUG-092 (the owner's order).
      - **Step 1, its premise measured first (runs 580-582, `warden_priority_probe.cc`,
        files/hack-priority-probe: the seconds to wait).** The Thor's driver has every
        extension it would need (global priority and its query, external memory as fds
        and Android hardware buffers, external semaphores and fences as fds). A device
        with a **realtime** queue: "not permitted" (an app may not). **High**: made. Then
        a paint-sized job (a 1920x1080 RGBA8 blit, filtered) every 50 ms for 10 s on each
        device, submission to fence on the CPU. At the King's Camp (the game GPU-bound at
        about 29 fps): high p50 18.7 ms, p99 20.3; medium p50 18.7, p99 20.3 - the same.
        At the title (the GPU nearly idle): both p50 about 6.5 ms, the fastest 0.37. **So
        the GPU does not let a high-priority device's work cut in** - it waits for what
        is running, about half a game frame at the camp, whatever its priority. The
        presenter on a second device would gain nothing from priority; what it might
        still gain is not waiting behind frames already queued on the game's queue (the
        paints' 40-50 ms in 9.14 against 18.7 here), but the same job could not be
        timed on the game's own queue (submitting there from the probe hung: Xenia's
        command processor holds it), so that gain is not measured. Waiting on the owner:
        go on (the large change - shared images, sync fds, the swapchain on the second
        device - for a gain that is now uncertain) or stop here.
- [x] 9.21 **The corner vignette as a switch (the owner asked, 2026-11-03; session 43, runs
      677-680).** The game darkens the frame's upper corners in play and conversations: a draw
      of the HUD's - four rectangles blended over the frame with the texture `da_vignette`
      (DXT5, 256x512), pixel shader `138D82BC28F866CC`, draw 45 of the camp's traced frame
      (run 677). The PC mod "Vignette Be Gone" (theskymoves, 2017) blanks that texture in
      `packages/core/override`; the owner's copy was read for that pointer only, nothing of
      it is in the repository. **Corner vignette** (`files/vignette`, On · Off, live) in the
      launcher's settings and the options panel: while off, the draw is dropped in the
      command processor (`WardenUpscaler::SkipDraw`, known by the shader and the texture's
      format and size), so it switches at once and works with FSR 3.1 on or off. Run 678
      (off from the start): the corners as bright as the rest; run 679 (on): the vignette.
      With the vignette off, BUG-113's shade has nothing to find in the corners.

## Phase 10 — Polish

- [x] 10.1 In-game ImGui settings overlay covering every option, live. **Restart marks
      (the owner, 2026-11-02):** an option the game takes only as it starts has an
      asterisk after its name - Rendering resolution, FSR 3.1, FSR 3.1 sharpening, Frame
      generation, Shader math, Texture filtering, Button prompts, Intro logos, Second
      screen, Language, Debugging Mode - and the panel's hints, in the launcher and in
      the game, say "* Needs a restart of the game: changed while playing, it applies at
      the next start"; such an option's description in the game ends "Needs a restart
      of the game (*)". The " •" after a value still marks one changed and waiting for
      that restart. **Done (session
      35, runs 394-396), as the owner asked first (2026-10-27): Select and Start pressed
      together during play open the options panel**, and so does Back, then Settings.
      Every option - frame rate, upscaling, colour, dynamic shadows, post-processing,
      texture filtering, button prompts, intro logos, autosave when put away,
      headphones - and a row to quit. Up and down choose (the D-pad, the hat or the left
      stick), left and right or A change, B, Start or Select close; rows answer a tap
      too. While it is open the game holds - its threads, clocks and sound, as when put
      away, without the autosave (`Runtime::SetHeld`). Live (`NativeBridge.applyOption` ->
      `Runtime::ApplyOption`): the frame rate (`SetFrameRate`, the pacing with it), the
      upscaling (FXAA too, 9.7's note), the colour (the swapchain made again - the
      surface let go and taken back, as when the app comes back - and the last frame
      painted again), the engine's shadows and post-processing (back on, the engine's own
      option, as DAORecomp's graphics menu does), the headphone mix, the autosave. From
      the next start, and marked so: texture filtering, the button prompts (BUG-061), the
      intro logos. **Plain Android views over the SurfaceView, not ImGui:** nothing in the
      presenter changes, the panel costs the game no frame (it holds), and touch works;
      the views never take focus - the activity hands them the pad's keys and motion
      (`dispatchKeyEvent`), so a ScrollView cannot eat the D-pad. **The combination:**
      Start and Select are held back from the game 150 ms (or until let go: a tap reaches
      it at its release, held 80 ms) - pressed together, neither reaches the game; pressed
      one after the other, the first has reached it and is let go there. Releases of keys
      pressed before the panel opened still reach the game, and the sticks' last state
      while it was open is handed over as it closes. Checked with the Thor's own
      controller - presses written into its input node (`sendevent`, `docs/TESTING.md`) -
      at the King's Camp (run 396): Start alone opens the game's menu 0.28 s later, B
      leaves it; Select + Start opens the panel with the game in explore mode throughout;
      the hat and the D-pad key move the selection. The owner's defaults (2026-10-27): 60
      fps (9.1), autosave when put away off (9.13); stereo on headphones and accurate
      colour, as before.
      **The app opens on its settings (the owner, 2026-10-30; session 37, run 453).** A
      launch from the icon shows the options panel before the game, with **Launch Dragon
      Age**, **Disc image (.iso)** and **Title update** at its top: the image's row says
      Extracted, Extracting N% or Choose..., the update's In place or Choose...; either
      opens the document picker on Download, and whatever is chosen is taken - a disc
      image extracted (again, it checks the files: those already whole are kept), a title
      update copied into app storage - the rows saying what each file was. Launch is
      offered once the data is extracted and a title update is in place; A on it, or
      Start anywhere, launches. Every option is taken as the game launches (no "from the
      next start"); B does nothing there and Back closes the app. As the game launches
      the panel becomes the in-game one. To make that true, **the title now loads as the
      game launches** (`NativeBridge.loadTitle`, from `bootWhenReady`) rather than at
      startup inside `extractIsComplete`: the button prompts, the intro logos and the
      title update are read then; `WARDEN_FILES_DIR` is set at startup; and frame
      generation's setting, which the bring-up presenter's swapchain latched at startup,
      is read again as the game boots (`warden_frame_generation_reread`) - run 453 chose
      Off in the pre-launch panel over Lossless 2x fast: the bring-up swapchain FIFO, the
      game's MAILBOX, `Frame generation: off`. The test scripts' `--ez boot true` still
      starts the game at once; `--ez boot false` keeps the report (the title loaded at
      startup for it, as before). **A fourth row, the owner's (2026-10-30): Lossless
      Scaling (Lossless.dll)** - the document picker, the file checked as a Windows DLL
      ("MZ", then "PE" where its header says) and copied into the files folder, where
      frame generation reads it; the row says In place or Choose.... **The owner picked
      theirs on the Thor (2026-10-30): `Lossless.dll in place (7345 KB)`** - 7,521,280
      bytes in the files folder.
- [x] 10.2 Thermal and battery aware resolution/framerate scaling. **An option the player
      turns on, Off by default (the owner, 2026-10-31):** nothing changes the frame rate or
      the quality on its own unless the Options row says so; measuring is always on.
      **Done for the frame rate (session 38, runs 475-477).** Android's thermal service has
      no HAL on the Thor (`dumpsys thermalservice`: "HAL Ready: false", the status always
      0), so PowerManager's thermal status and headroom say nothing; the app reads the
      kernel's instead (`warden_thermal.cc`): the thermal zones by type (`gpuss-*`, the big
      cores' `cpu-1-*`, the board's `xo-therm`) and the Adreno's `gpuclk` and
      `thermal_pwrlevel` (0 uncapped), every 5 s in the `GPU:` status line's `thermal:`
      part. **Run 475, 14 minutes at the King's Camp at the heaviest settings (Lossless 2x
      over FSR 3.1): no throttling** - the GPU at 680 MHz and thermal level 0 throughout,
      settling at 82 C, the big cores at 94-95, the board at 61, the game at 23.8 from the
      first minute to the last (the fan in the Thor's mode 4, on the charger). So the guard
      is a safety net, for a quieter fan or a warm room: **Options -> Heat guard** (`files/
      thermal-guard`, live), Off by default; On, the frame rate on the screen held a step
      lower (120 -> 60 -> 45 -> 30, `SetFrameRateCap` over the chosen rate, which stays as
      set) when the kernel caps the GPU or it reaches 90 C, a step at most every 30 s, and
      lifted a step after a minute at 80 C or less; and 30 while Android's battery saver
      is on (GameActivity watches it). Run 476 (test temperatures 78/76 C,
      `files/thermal-guard-test`): off, nothing changed; on, 60 -> 45 -> 30 when hot, 45
      again a minute cool, and so on - the game at 22.5 and 15. Run 477: the battery saver
      on, 30; off, 60 again (it cannot come on while the Thor is powered - the run reported
      the charger unplugged, `dumpsys battery unplug`, and reset it). Resolution scaling is
      not part of it: FSR 3.1's mode is taken at the start.
- [x] 10.3 Vulkan pipeline cache pre-warm to kill first-run shader stutter. **Session
      34:** pipelines are created when first drawn with (upstream's background
      compilation drew a placeholder pixel shader in one-off renders - BUG-049, BUG-052),
      so an area's first load is slower (the King's Camp 4.4 s against 1.7) and its first
      seconds can hitch (0.48 s, 0.57 s; run 314). **Since session 35** the stored
      pipelines are created when first drawn with, not at startup (BUG-060: 1.1 GB of GPU
      memory at each start) - hits in the driver's cache, the camp's load as smooth as
      before. **Done (runs 315-318):** Xenia's shader
      and pipeline storage set up in the files directory, with three faults on its way to
      the disk fixed (BUG-055); the pipelines of earlier sessions are created at startup in
      the background, so an area seen before loads without the compile stalls (the King's
      Camp's worst frame 112 ms against 3,474; startup 0.4 s slower). What no session has
      seen yet still compiles when first drawn - 10.4 would ship those.
- [x] 10.4 Shader cache shipped or built on first run. **Looked into - nothing to gain
      beyond 10.3 (session 38).** The game's compiled shaders are in
      `packages/core/data/shaders.erf`: 499 `.fxo` files (zlib), Xenos microcode in the
      360's Direct3D container (magic `0x102A1100` pixel, `0x102A1101` vertex; the
      microcode's offset and size at bytes 4 and 8, big-endian) - and Xenia's storage
      keeps that same microcode, big-endian. Against the Thor's storage after every
      session so far (180 shaders: 102 pixel, 78 vertex): 30 of the archive's 331 distinct
      pixel shaders found byte for byte, **none of its 116 vertex shaders** - the game's
      Direct3D patches a vertex shader's fetches as it binds it to a vertex declaration,
      so what the GPU gets exists only in play - and 82 stored pixel shaders come from
      elsewhere (the executable's own: the UI's renderer, the post effects). A first
      visit's hitches are the driver making pipelines, which takes both shaders as drawn
      and the draw's state (targets, blending, topology), none of it in the data. So the
      archive could give only the pixel shaders' translations ahead, the cheap part.
      10.3's storage keeps everything seen, created when first drawn through the driver's
      cache (the camp's worst frame 112 ms against 3,474), and survives updates
      (`adb install -r`). Nothing shipped either way: the repository and the APK hold no
      game material.
- [ ] ~~10.5 Long-session stability — 4+ hour soak~~ **Dropped by the owner (2026-10-31).**
- [ ] ~~10.6 Battery life measurement and tuning~~ **Dropped by the owner (2026-10-31).**
- [x] 10.7 Crash reporting and diagnostics. **Done (session 38, run 479):** how the last
      session ended, from Android's own record of the app's exits
      (`getHistoricalProcessExitReasons`, `LastExit.java`). The app tags its process with
      what it is doing (`setProcessStateSummary`: `pre-launch`, `playing`, `held` while
      put away, `ending` just before it ends its own process), and Android returns the
      tag with the exit, so the app's own end - a kill signal in the record - is told
      apart from anyone else's. Every other exit but a stop from outside (a force stop,
      swiping it away, an update) is written to `files/exits/` at the next start
      (`<time>-game|deck-<reason>.txt`: the reason, signal, memory, the app's tag,
      Android's description) with its trace when Android keeps one - an ANR's thread dump
      (`.anr.txt`), a native crash's tombstone (`.tombstone.pb`, Android's protobuf) - and
      the launch screen's Launch row says how the last session ended ("The last session
      ended 31 Oct 11:37: the game crashed (signal 11), in play."). Run 479: the game
      killed in play (signal 9) and then sent SIGSEGV - both written, the tombstone with
      the second (393 KB), and each told at the next start. Nothing leaves the device;
      crashes still reach logcat and the system's tombstones as before (the fault
      handler chains to them).
- [x] 10.8 Final pass over every open question in `STATUS.md` §6. **Done (session 38):**
      Q7 answered by 9.4 (the camera's motion vectors from depth and the static meshes'
      matrices; per-object motion not reconstructed), Q8 and Q11 by 3.3b (nothing
      renders wrongly against DAORecomp's build), Q10 (NaN, infinity and denormal
      payloads) closed without its own test - nothing has pointed at it; Q5 (Awakening's
      disc addin) stays deferred; Q1-Q4, Q6, Q9 and Q12 were answered before.
- [x] 10.10 **(Done: the add-in packs seen in play since by the areas' tour, 10.11 - Warden's
      Keep, the Stone Prisoner, Return to Ostagar; session 40.)** **Awakening and the downloadable content (the owner, 2026-10-31: "I want
      awakening and DLC in this as well").** DAORecomp (upstream, `eb2360a` - the same
      repository as github.com/96dot/Dragon-Age-Origins-Recomp) installs DLC as Xbox Live
      packages (LIVE/PIRS, STFS) into `userdata/content/454108C0/00000002/<package>/` - a
      package's folder holds its add-ins, each a folder with a `manifest.xml` - and its
      XAM lists that folder's packages for content type 2, mounts each, and grants every
      licence (the game loads a package only when the licence covers its manifest's
      `LicenseMask`); Ultimate Edition's disc 2 carries the packages and Title Update 6.
      Our runtime does the same (`warden_xam.cc`: `files/content/00000002/`).
      **The owner's Awakening image is not Ultimate disc 2** but the standalone Awakening
      disc: its own executable (media `2E6F7560`, version 0.0.0.9, "disc 1 of 1"), the
      game's packages, and Awakening as an add-in in loose files,
      `Addins/DAO_PRC_EP_1/` (2.0 GB, 3,474 files: `manifest.xml` - UID `DAO_PRC_EP_1`,
      `GameVersion` 11105, `LicenseMask` 0 - `core/`, `module/`, `packages/`); no Live
      packages, no title update.
      **Awakening works from it (session 38, runs 487-490):** the TU7 executable already
      looks for `GAME:\addins\DAO_PRC_EP_1\manifest.xml` and lists `D:\addins\` - with
      the add-in there, New Game offers Awakening, but Origins then "Requires Dragon Age:
      Origins disc to play" (it takes itself for the Awakening disc). As a package -
      `files/content/00000002/DAO_PRC_EP_1/DAO_PRC_EP_1/` and `DAO_PRC_EP_1.name` (its
      display name, UTF-16BE) - the game enumerates it, mounts it as `PRC0000:`, reads the
      add-in, and New Game offers both campaigns; Awakening's character creation, its
      opening cutscene, the Vigil's Keep fight and the first conversation play (run 490).
      **The launcher's row - done (runs 491-492):** Awakening (disc image) on the
      settings before the game: the image picked, checked by its own executable's header
      (media `2E6F7560`), its add-in extracted by the disc extractor (now able to take one
      folder of a disc, `Extractor::Begin(fd, destination, source)`; JNI `extractAddin`)
      into `files/content/00000002/.DAO_PRC_EP_1.installing/` - a name the game does not
      list - and renamed into the package's place when whole (its completion stamp), the
      display name written beside it (`GameData.finishAwakening`). The row says Choose...,
      Installing N%, or Installed. On the Thor: 3,474 files, 2.0 GB, in seconds; New Game
      offers both campaigns after (run 492). The image needs to be on the device (the
      owner's was put in its Download folder).
      **The other DLC - the installer done (session 38); installed from the Ultimate
      Edition's disc 2 below:** Leliana's
      Song, The Darkspawn Chronicles, The Golems of Amgarrak, Witch Hunt, Return to
      Ostagar, Warden's Keep, The Stone Prisoner and the item packs come as Live packages
      (Ultimate Edition's disc 2, or a console's
      `Content\0000000000000000\454108C0\00000002\`). Downloadable content, the
      settings' next row: package files picked (several at once), or a disc image holding
      them - each this game's package of type 2 extracted by the app's STFS reader
      (`warden_stfs.cc`, the title update's; now with the header's content ID and display
      name) into `files/content/00000002/<content ID>/` (outside the listed folder first,
      moved in whole) with its display name beside it, as DAORecomp's `InstallDlc` does
      (`warden_dlc.cc`; JNI `installDlc`, on a thread of its own). Checked on the Thor by
      a test program built from the same sources (run 493): a copy of the title update
      marked as type 2 installed - its 26 files, `default.xexp` byte for byte the computer's
      extraction - and named; again, "installed already"; the title update itself, "a title
      update - the Title update row takes it"; the Awakening disc, searched in 2.7 s, none
      for this game (its `nxeart` is a package, passed over). No real DLC package yet: the
      owner's disc images hold none.
      **The Ultimate Edition's disc 2 - done (session 38, runs 502-504; the owner's image,
      2026-10-31).** "Dragon Age - Origins - Ultimate Edition (USA, Europe) (Disc 2)
      (Additional Content Packs Install Disc)": its executable the content installer
      (title `FFED2000`, media `6FB2B193`); in `content/0000000000000000/FFED2000/FFFFFFFF/`
      eleven Live packages for this game, type 2 - Dragon Age: Origins - Awakening (2.0 GB),
      Leliana's Song, The Golems of Amgarrak, Witch Hunt, The Darkspawn Chronicles, Return to
      Ostagar, Warden's Keep, The Stone Prisoner, Blood Dragon Armor, Feastday Gifts and
      Feastday Pranks; and Title Update 6 for three editions (`ESI/`, ours; `Fr/`, `De/`;
      `title_update.bin`) - the owner's TU7 is newer and stays. **The launcher, as the owner
      asked:** four file rows - Disc image (.iso), Ultimate Edition disc 2 (.iso), Title
      update, Lossless.dll; the Awakening (disc image) and Downloadable content rows are gone
      (and the standalone Awakening's code with them). Disc 2 picked, checked by its
      executable, every package installed by `warden_dlc.cc` (now with its progress for the
      row, `dlcProgress`), and the standalone disc's Awakening package, if there, taken away
      - disc 2's Awakening (content ID `E92188F5...`) carries the same add-in. Run 502: the
      eleven in about 6 s on the Thor (4.1 GB), "11 installed". Run 503: all eleven listed
      and mounted (`PRC0000:` to `PRC0010:`); New Game offers Origins, Awakening, The
      Darkspawn Chronicles, Leliana's Song, The Golems of Amgarrak and Witch Hunt; Leliana's
      Song starts (its opening). Run 504: Awakening from disc 2's package - its character
      creation and opening cutscene. The other packs join the Origins campaign (Warden's
      Keep, The Stone Prisoner, Return to Ostagar, the items) - not yet seen in play.
- [x] 10.11 **The areas' tour - done for every area the debug menus reach (session 40): 118 areas, the main campaign's 58, the add-ins' 13, Awakening's 28, the four DLC campaigns' 19; the two black scenes it found (BUG-085, BUG-086) are the game's own, the reference shows the same (the owner, 2026-10-31: a debug function to load into many
      more areas, then the profiling of every major area with many screenshots, visual bugs
      and performance discrepancies noted, not fixed).** **The loader (session 38):** the
      game's own per-module debug scripts - the 360 build registers 26 of them natively
      (`zz_pre_debug`, `zz_arl_debug`, `zz_cir_debug`, `zz_orz_debug`, `zz_den_debug`,
      `zz_ntb_debug`, `zz_urn_debug`, `zz_lot_debug`, `zz_cli_debug`, `zz_epi_debug`,
      `zz_camp_debug`, the origins' `zz_bhm/bdn/bdc/bec/bed/bhn_debug`, `zz_ran_debug`,
      `zz_mtn_debug`, `zz_dlc_debug`, `zz_wmp_debug`, `zz_app/shale/oghren/rumour/banter_debug`,
      `zz_jump_to_map` and `zz_goto`; no `zz_dae_debug` or main debugger) - each opening a
      numbered dialogue whose "Jump to area" (or "Area Jump", "Go to an area", "Jump to a
      location", a plot jump) lists the module's areas in pages of three. `files/route-call`
      (`warden_game.cc`, read about once a second from the per-frame mode hook) calls any
      script's main by name while the game runs, a leading `?` only looks it up, so one boot
      chains jumps; the `probe` route boots to the default start area with nothing else.
      The tour's scripts (`tools/areas/`): `jump.sh` (the call, the menu's presses, each
      state checked), `area_probe.sh` (an arrival conversation ended with B and A, then the
      arrival screenshot, 10 s standing, a 6 s walk and a 4 s camera turn with screenshots,
      the frame rates from the status lines' timestamps, the GPU's busy percentage),
      `tour.sh` over a list. Uncapped for it (Frame rate 120, Frame generation off, FSR
      3.1 Performance, Exact). **The downloadable content's areas (session 40, the owner
      asked for Awakening and the DLC in the tour):** the DLC modules ship their scripts
      as `.ncs` resources in ERF V2.0 archives (the PC release's layout - `dao_erf.py`
      reads it now), their debug scripts among them - Warden's Keep `zz_gwb_debug`, The
      Stone Prisoner `zz_shl_debug`, Return to Ostagar `zz_kcc_debug`, Awakening's
      `zz_vgk_debug`, `zz_coa_debug`, `zz_aoa_debug`, `zz_trp_debug`, `zz_stb_debug`,
      `zz_ltl_debug`, `zz_ltm_debug`, `zz_rxa_debug`, Leliana's Song `zz_prc_lel_debug`,
      The Golems of Amgarrak `zz_gib_debug`, Witch Hunt `zz_str_debug`, The Darkspawn
      Chronicles `zz_drk_debug` - which the registry does not hold, so `files/route-call`
      takes `!name`: the script run through the engine's own RunScript as the player's
      character (the PC console's `runscript`, DAORecomp's cheats' way; its addresses in
      `warden_game.cc`) - Warden's Keep's menu opened at the first try, where a dispatch
      through the event dispatcher (`~name`) had run the script without a menu. The
      `probe-dlc` route keeps the DLC (`.keep-dlc`: no `hide-dlc`)
      and answers the campaign chooser; `skip-start` now hides any `*module_start.ncs`
      (Awakening's `gxa_module_start`, Leliana's `lel_module_start`). The list is
      `tools/areas/tour-dlc.txt`. **Results:** docs/AREAS.md. **Session 40:** BUG-084 fixed first; the main campaign finished (the menus the first pass could not read photographed with `menu_read.sh`, a second pass, `tour-redo.txt` and `tour-towers.txt`) and all the DLC toured on its own routes (`tour-dlc.txt`, `tour-awakening.txt`, `tour-drk.txt`, `tour-lel.txt`, `tour-gib.txt`, `tour-str.txt`); `!name value` passes RUNSCRIPT_VAR for the console scripts (`zz_drk market`, `zz_gib_bdebug warp_gib100ar_smithy`). Not reachable from the menus: the signal tower, the Dead Trenches, the Anvil, the climax, a few battles the test character loses - docs/AREAS.md. **Progress (session 38, the night of
      2026-10-31; wrapped up in session 39 after the computer crashed mid-tour):** 28 areas
      loaded, 27 profiled - the Circle's twelve, Lothering's three, the Dalish camp,
      Denerim's four, Orzammar's four, the Urn's four (the mountain top's probe cut short) -
      40-68 fps uncapped, the GPU at 99%, all above the camp's 37; nothing renders wrongly
      but the Arl of Denerim's estate (BUG-085, black); BUG-084 three times, each during a
      load (fix it before the tour goes on). The list with what is left is
      `tools/areas/tour.txt` (the Urn's last two, Haven, the prelude, the party camp, the
      origins, Brecilian's and the Deep Roads' sub-lists, Eamon's estate, Redcliffe's plot
      jumps, then the climax and the epilogue by hand); docs/AREAS.md says how each failed
      jump is to be redone.
- [x] 10.9 **GPU headroom - done (session 38, runs 494-521): the King's Camp at the
      game's own 30 fps with Exact shader math and Lossless 2x, balanced; the inexact
      levers the owner's Options (Shader math Fast; Frame generation's faster modes).** To be fixed later (the owner, 2026-10-31: "something we must
      fix later in the plan"). Under the owner's settings (FSR 3.1 with Lossless 2x) the
      game holds about 24 of its 30 at the King's Camp, and below its rate frame
      generation's latency is the GPU's (9.14: about a frame of rendering, the half-frame
      hold and the paints' wait - the paints' own queue did not help). More headroom lifts
      the rate and cuts that latency. Where the camp frame's GPU time goes, from the pass
      timing (`files/gpu-timing.txt`, the traced frame): 44 ms in 78 render passes, 2,111
      draws - the main pass 18.2 ms for 723 draws, another 6.1 ms for 172, the depth
      prepass 3.8 ms for 721, 5.1 ms between passes; 38 of the 44 ms in the 1280x2048
      targets. To look at: the translated shaders of the main pass (3.8's work went
      there before), the render-target emulation's copies and the gaps between passes.
      **Started, then paused for the Ultimate Edition's DLC (session 38, runs 494-501) -
      where the time goes under the owner's settings** (FSR 3.1 Performance, so the
      scene at 960x540; Lossless 2x quality; Frame rate 60; the King's Camp):
      - **A game frame costs about 45 ms of GPU - 22 fps:** the game's own rendering
        about 23 ms untraced (34.7 ms in a traced frame, run 495: the main scene pass
        20.9 ms for 914 draws, the depth prepass 3.7 ms for 721, 16 render-target
        transfers 1.4 ms, 3.1 ms between passes; the post-processing chain small at this
        size); **FSR 3.1 7.7 ms** (its passes 6.83 - accumulate 4.17, shading change
        pyramid 0.92, RCAS 0.54, prepare inputs 0.48, reactivity 0.37 - the motion
        vectors 0.18, the composite 0.72); **Lossless 2x quality 13.95 ms** a generation
        (1920x1080, flow 1/1). To hold the game's 30 (33 ms) about 12 ms must go, and
        half of the 45 is the upscaling and the generation.
      - **The scene's draws** (run 497, `hack-split-pass 2:850:2100`; each piece of a
        split pass costs about 0.08 ms of its own, so the ranking counts, not the sum): no
        one shader rules - pixel shaders `2D0B7CE2` 4.3 ms (156 draws), `CBA8DC01` 3.0
        (332), `1EFA9B46` 1.8 (106), `55026100` 1.7 (6, the terrain), `4334F94A` 1.1 (90).
      - **The driver's statistics** (run 499, `hack-pipeline-stats`, now a line a stage -
        one line for all was cut short by the log): the heavy pixel shaders are 1,600-2,900
        32-bit ALU instructions for 10-20 texture reads, 16-34 registers, 12-37%
        utilization, **no scratch memory (no spills)** - arithmetic, not memory.
      - **The multiplication rule** (run 500, `hack-no-mul-zero ieee`, inexact, to
        measure): 31-44% fewer ALU instructions in every heavy shader, a few registers
        fewer; the traced frame 34.7 -> 26.4 ms, **the game 22 -> 24 fps**.
      - **The texture fetches** (run 501, `dump-shaders`, `CBA8DC01`'s SPIR-V, 4,805
        operations for 13 fetches): besides the rule's compares and selects (219
        FOrdEqual, 118 LogicalOr, most of 390 Select), each fetch decodes its fetch
        constant per pixel - bitfield extracts of the sizes, conversions, `1 / size`
        divisions (57 FDiv) - values the same for every pixel of a draw, which the
        driver does not hoist (18 preamble instructions).
      **Levers, for when this resumes (biggest first):** (1) Lossless 2x quality at
      1080p, 14 ms - the fast mode is the owner's choice; or generation at a lower size;
      (2) FSR 3.1, 7.7 ms - its accumulate pass at the output size; (3) the multiplication
      rule, about 4 ms of the game - IEEE (inexact, the owner's call) or kept only where
      an operand can be infinite (after rcp, rsq, log, exp or a float texture - exact
      unless the game feeds infinities through constants); (4) the fetch constants
      decoded once a draw instead of every pixel - exact, size to measure; (5) the depth
      prepass, 3.7 ms, vertex fetch emulation (its vertex shaders 1,500 instructions).
      How to measure: `bash tools/gpu_profile.sh <run>` (docs/TESTING.md section 4).
      **Resumed, then paused again (session 38, runs 505-508):**
      - **Lever (4) tried in the shader alone - no gain, removed.** A pass over each
        translated module (after the builder's dump) moved every pure operation whose
        operands were constants or loads from uniform buffers - about 1,100 in
        `CBA8DC01`, valid SPIR-V by the NDK's `spirv-val` - into the entry block, for the
        driver to compute once a draw. The Adreno's compiler did not take them into its
        preamble: the ALU counts unchanged, the registers up (22 -> 42 in one shader), the
        utilization down (25 -> 12%), the game at 22 fps as before (run 505). Never
        committed. Done right, the decoded fetch constants would come from the CPU per
        draw through a uniform buffer - a larger change, its gain bounded by that uniform
        share (about a quarter of the shader's operations; roughly 1 fps, from the
        multiplication rule's 40% for 2 fps).
      - **The depth prepass:** its vertex shader (`C8CD5CF9`) 365 instructions, 167 ALU,
        8 fetches, full utilization - the game's own pass, little translation overhead.
      - **Frame generation is the big lever** (the camp, FSR 3.1 Performance, Frame rate
        60; only the generation changed, the owner's setting put back after each):
        Lossless 2x (quality) 13.95 ms a generation - the game at 22 fps; **Lossless 2x,
        fast 3.27 ms - 28.7 fps** (run 506); FSR 3.1 2x - 24.3 fps (run 507); Lossless's
        quality network with the flow at half the size (a test switch, `files/lsfg-flow`)
        **5.67 ms - 27.2 fps** (run 508).
      - **Lossless 2x, balanced - done (run 509):** a fourth Frame generation choice,
        `lossless-balanced` (the quality network, the flow at half the size;
        `warden_frame_generation.cc` `half_flow_`), the choices' descriptions with the
        camp's numbers. At the camp: 5.6 ms a generation, the game at 26-27 fps (130-135 of
        its frames in 5 s), 52-54 on the screen. (Before the run the setting already held
        `lossless-balanced` - left by the run the owner stopped at 14:00 - and was put back
        to the owner's `lossless` after.)
      - **The owner's choices (2026-10-31): Lossless 2x, balanced, and a Shader math
        option.** The owner's Frame generation is now `lossless-balanced` on the Thor.
        **Options -> Shader math: Exact (the default) or Fast** (`files/shader-math`, from
        the next start - the shaders are translated then, and the driver compiles each
        pipeline once more): Fast is IEEE multiplication in place of the Xenos rule that
        zero times anything is +0 (`warden_gpu.cc` sets the translator's
        `DAO_HACK_NO_MUL_ZERO` to `ieee`; `files/hack-no-mul-zero` still decides when it is
        there). At the camp with Lossless 2x, balanced: **Fast 30 fps** - the game's own cap,
        149-151 frames in 5 s (runs 510-512), the traced frame 25.8 ms against 32.1 (runs 510,
        509) - **against Exact 26-27** (130-135). The picture (runs 511-512, screenshots 25 s
        after arrival, Fast and Exact each twice): no difference beyond what two boots of
        the same setting show - the light shafts between the trees vary from boot to boot,
        Fast's a little fainter in both pairs (luma 63.5 and 68.3 against 69.4 and 73.0,
        where the same setting's two boots differ by 3.6-4.8); the rest of the frame within
        1.5 of 255 in all four pairs. Inexact where the game multiplies zero by an infinity
        (NaN, not 0) - not seen at the camp.
      - **FSR 3.1's accumulate pass: the Lanczos lookup table - done (runs 513-515).**
        The pass reprojects the history with Lanczos weights, which the SDK computes with
        sines (its reference) unless the GPU's subgroups are 32 to 64 lanes - AMD's - when
        it reads them from a 128-entry table it makes at the start. Its Vulkan backend,
        told of no subgroup size control, reports 32 to 32 here, so the Thor got the sines.
        `warden_upscale.cc` now answers 32 to 64 through the backend's capability query
        (nothing else follows from it: 64-lane subgroups are forced only with shader model
        6.6, and the backend reports 5.1). The weights within about 0.0003 of the sines.
        Run 513, alternating each 150 frames at the camp (`files/fsr3-lut alternate`):
        **accumulate 3.70 ms against 4.17**, every window; FSR 3.1 6.35 ms against 6.81;
        the game about 0.2-0.3 fps up (27.0-27.1 against 26.8-26.9). The driver's
        statistics (run 514, FSR's own pipelines now logged with
        `files/hack-pipeline-stats`): the pass 1,468 instructions with the table against
        2,116, 29 complex ones against 112, 34 texture reads against 26; both 64 lanes
        wide, 23-24 registers, a quarter of the shader capacity busy. Run 515, the
        default now: accumulate 3.69 ms, the game 27 fps (Exact shader math, Lossless 2x,
        balanced); its screenshot as near the two reference ones (mean difference 1.77 of
        255) as they are to each other (1.47), nothing out of place. `files/fsr3-lut`
        holding `off` keeps the sines.
      - **FSR 3.1 has no further knob:** of the SDK's `*_SAMPLERS_USE_DATA_HALF` options
        only the reprojection's is still read by 3.1's shaders, and it is on.
      - **The fetch constants decoded on the CPU - done (the owner asked, 2026-10-31; runs
        516-518).** Measured first (run 516's dumped shaders, optimized by `spirv-opt -O`
        as a driver would): of the heaviest pixel shader's (`CBA8DC01`) 1,911 operations,
        157 depended on the fetch and system constants alone - per texture fetch the width
        and height (two bitfield extracts, two adds, two conversions), their reciprocals
        (two divisions - 12 of its 31 complex instructions), the LOD bias and the gradient
        exponent biases (sign extracts, conversions, a max, an add), the result exponent
        bias (an ldexp) - the same for every pixel of a draw. Now the command processor
        decodes each of the 32 fetch constants as it uploads them
        (`SpirvShaderTranslator::DecodeFetchConstants`, three float vectors each after the
        constants in their uniform buffer, 1.5 KB) and the texture fetches read the values:
        the 2D and cube width, height and reciprocals (not for 1D fetches, unnormalized
        fetches with a locked mip, or a resolution scale - those decode as before), the LOD
        bias, the gradient biases and their larger, the LOD bias with it, the exponent bias.
        The same arithmetic, so the same values - the integers and sums are exact - except
        the reciprocals, now rounded correctly where the GPU's division may be off by an
        ulp (power-of-two sizes exact either way). Ten graft edits (234 now), 3.8's
        reciprocal edit extended. `files/hack-fetch-raw` decodes per pixel as before.
        **Run 517 against 518 (`hack-fetch-raw`), the camp, Exact shader math, Lossless 2x,
        balanced:** `CBA8DC01` 1,510 ALU instructions against 1,631, 19 complex against 31,
        its fetch-and-system-only operations 157 -> 42 (the texel-snap flag and the offsets'
        scaling left); the traced frame 29.7 ms against 31.3; **the game 28 fps (139-140
        frames in 5 s) against 27.2 (136)**. The screenshots alike - each as near run 515's
        (mean difference 1.59 and 1.57 of 255) as two boots of one build are to each other;
        the new SPIR-V valid (`spirv-val`, Vulkan 1.2).
      - **The multiplication rule's FMin form measured again (run 519):** 28.6 fps
        against the Or form's 28 (the traced frame 29.1 ms against 29.7) - set aside: exact
        only if the hardware's min returns the other operand for a NaN, which is not
        verified on the Adreno.
      - **The vertex fetches' binding and endianness known per draw - done (the owner asked
        for more, 2026-10-31; runs 520-521).** Where the GPU time went (run 517's traced
        frame, 29.7 ms): the scene pass 12.6 ms (742 draws), the foliage's alpha-tested
        passes 7.3 (345 draws), the depth prepass 3.7 (721 draws, the game's own
        position-only vertex shaders), post-processing about 6 with 3.5 between the passes.
        The driver's statistics for the vertex shaders: the depth prepass's (`C8CD5CF9`, a
        16-instruction guest shader) 365 host instructions with 8 memory loads for its 2
        words, the foliage's (`90DEF6B7`) 2,038 with 76 loads for 19 words and 168 flow
        control instructions, the heaviest skinned ones 2,250-3,200 with spills. The cause:
        the Adreno's storage buffers are at most 128 MB (`maxStorageBufferRange`), so the
        512 MB of guest memory is four bindings, and the translator loaded **every vertex
        fetch word through a switch over the four** (a branch, a load in each case, a phi)
        - the loads serialized by control flow, one memory round trip after another - and
        swapped each fetch's bytes under branches on the fetch constant's endianness. Now
        the pipeline cache reads, per draw, each of the shader's first four vertex
        bindings' fetch constant - the binding its buffer lies within (unknown when it
        crosses a 128 MB boundary, then the switch as before) and its endianness - into
        the vertex shader's modification (`vfetch_binding`, `vfetch_endian`, 3 bits a
        slot; `Modification::kVersion` 21 - the pipeline storage's version is the larger
        of two, so it kept its entries: the old descriptions no longer match, and run
        520 stored 112 new ones), and
        the translator loads straight from that binding and swaps without branches
        (`DaoEndianSwap32UintStatic`, the same operations). Exact: the same words, the
        bounds check per word kept. Nine graft edits (243). Run 520 against 517: the depth
        prepass's shader 365 -> 254 instructions (loads 8 -> 2, flow control 23 -> 5), the
        foliage's 2,038 -> 1,088 (loads 76 -> 19, flow 168 -> 7), every vertex shader
        together 56,397 -> 39,418 (-30%); the depth prepass 3.69 -> 3.34 ms, the scene pass
        12.55 -> 10.87, the comparable foliage passes -30% (67 draws 1.15 -> 0.78 ms, 79
        draws 1.43 -> 1.02); the traced frame 29.7 -> 25.9 and 28.4 ms (runs 520, 521).
        **The game 28 -> 30 fps at the camp - its own cap - with Exact shader math and
        Lossless 2x, balanced** (149-150 frames in 5 s in both runs against 139-140); the
        screenshots alike (the grass sways between any two frames), no validation errors.
      - **Measured uncapped (the owner: Frame rate 120, Frame generation off, for
        measuring; runs 523-525).** The camp at **36.8 fps**, the GPU 99% busy at 680 MHz:
        about 27 ms a frame, FSR 3.1 Performance 6.3 of it and the composite 0.7, the
        game's own rendering about 20. The game's post-processing off (Options): 40.0 fps
        - the whole chain, its copies and transfers included, costs **2.2 ms a frame**, far
        less than the traced frame's 7-9 (its timestamps inflate small passes), so it is
        not a lever worth deep work. The scene and foliage draws traced two to a pass (run
        525, 568 passes): the terrain's `CBA8DC01` (332 draws) and `2D0B7CE2` (156) the
        heaviest pairs, then `BD73F697`, `1EFA9B46`, `4334F94A`; the per-pass tile traffic
        swamps small draws in that method, so only the order is trustworthy.
      - **The multiplication rule's FMin form proved exact on the Adreno 740 and made the
        default.** `tools/mintest` (a standalone Vulkan compute program for the Thor, built
        with the NDK; docs/TESTING.md) runs `min` on every pair of +0, -0, 1, -2, +inf,
        -inf, quiet NaN, signalling NaN, negative NaN and a denormal as runtime inputs: with
        one NaN operand the hardware returned the other operand in all 84 cases, both
        orders, and the FMin form of the rule equalled the Or form on all 100 pairs (the
        denormal flushes to zero in both). So `min(|a|, |b|) == 0` decides exactly as
        `|a| == 0 || |b| == 0` here, with one instruction fewer a multiplication, and
        `DaoMulZeroMode` returns it without `files/hack-no-mul-zero` (`or` keeps the Or
        form). Run 526 against 523, uncapped: **37.3-37.4 fps against 36.8** (FSR's own count over 150 frames, the GPU 99% busy in both), the traced camp frame 27.2 ms; the screenshot within the same-build spread of the Or form's runs (a mean difference of 1.3-2.1 of 255, against 1.9 between runs 520 and 521).
      - **The options' costs uncapped (runs 527-530), and two more options.** Shader
        math Fast: **42.2 fps against 37.3** (+13%; run 527) - the biggest lever left, the
        owner's. Texture filtering off: 37.7 against 37.3 - 16x anisotropic stays free (run
        528). Two Options rows added under the owner's rule (speed that costs the picture is
        a switch, today's look the default): **FSR 3.1 -> Ultra performance** (640x360
        rebuilt to 1080p, FSR's 3x ratio; `warden_fsr3.cc` `RenderSize`, the game's device
        made at that size as for the other profiles): **46.6 fps** (run 529), the scene whole, the picture and the HUD's text noticeably softer (the HUD is drawn at 640x360 and scaled three times; a mean difference of 3.7 of 255 from Performance's frame against about 2 between two boots of one setting);
        and **FSR 3.1 sharpening: On/Off** (`files/fsr3-sharpen`; off leaves out the RCAS
        pass, about 0.5 ms at 1080p): **38.1 fps** (run 530).
      - **16-bit shader arithmetic tried as an option and dropped (runs 531-532).** A pass
        after the translator decorated the pixel shaders' float results RelaxedPrecision
        - all of them, or all but what reaches a texture fetch's operands, a depth or
        sample-mask output, memory or a bitcast, found backwards through phis, selects and
        the register variables (valid SPIR-V, 134 shaders on the device). The Adreno's
        16-bit rate did not show: 38.5 and 38.6 fps against 37.3, and the scene rendered
        black with only the HUD left, in both forms - a driver behaviour of the kind
        BUG-039 and BUG-045 were, not worth chasing for 3%. Nothing of it kept.
      - **What is left, exact:** little. The heaviest pixel shaders' register spills (the
        driver's "scratch" 6 at 18% occupancy) and FSR 3.1's own 6.3 ms are what remain
        of the uncapped frame. The inexact levers are all Options now: Shader math Fast
        (+13%), FSR 3.1's profiles (Ultra performance the fastest), its sharpening,
        Post-processing (+9%), Dynamic shadows, Texture filtering (free).

---

- [x] 10.12 **PC mods, upstream's way - done; converting PC-format mods is 10.13, done too
      (session 40, run 572).** The launcher's **Mods** page (`ModsPage.java`, `Mods.java`):
      the mods in `<app storage>/mods/<folder>/`, each mirroring the game folder, in their
      load order - on, off, or "[PC version]" (PC GFF files, .dds, PC ERF archives,
      .dazip: not loaded); per mod: about (mod.toml's name, version, author, description,
      the file count, warnings - PC files, add-in modules - and the other loaded mods
      changing the same files), on or off, move up or down, delete. **Adding one:** a
      .zip (or .dazip) through Android's file picker, or a folder (a document tree),
      unpacked into `mods/.adding/` (entries outside the mod refused, 8 GiB at most) and
      sorted as upstream's mod_install.cpp sorts it: wrapping folders unwrapped (a Nexus
      download's "-id-version-time" taken off the name), `packages/`, `core/` and
      `override/` put where they belong, read-mes and mod.toml on top, a DAModder
      Manifest.xml made into mod.toml, the rest into `packages/core/override/<mod>/`
      (`override/` itself for a CoreOverrideAddIn), optional parts noted. **The game
      side:** the page writes `files/mods-enabled.txt`, the loaded mods' folders, the
      one that wins first; as the game starts they are laid over the disc under the
      override tree (`warden_fs.cc`'s mod roots: a file from the first tree that has it,
      directory listings merged). Run 572: a test zip sorted and named from its manifest;
      a dummy .dds mod marked PC and left out; a mod holding a copy of a game patch file
      - the game read it from the mod's folder ("override: coreupdate_en-us.tlk from
      .../mods/Probe Mod/..."). The owner's words: "it has a feature for
      using PC mods, i want that for this app as well").** Upstream's model
      (`mods/README.txt`, `DAORecomp/mods/`, about 4,000 lines of portable C++): each
      mod is a folder mirroring the game folder, layered over the disc in load order
      (the port's override tree, 7.3, already does one layer; mods make it a stack),
      the engine's own `packages/core/override` taking loose files; a `mod.toml`
      names it; enable, disable, order and delete from a **Mods page** on the launch
      screen. Import on the Thor: a .zip or folder picked through Android's file
      picker (SAF) in place of upstream's drag-and-drop, sorted into place the same
      way (packages/, core/ and override/ where they belong, the rest into
      `packages/core/override/<mod>/`). **PC-format files** (PC GFF models and
      tables, .dds textures, PC .erf/.rim/.dazip) would crash the 360 engine, so they
      are marked, not loaded, and **converted** as upstream converts them (10.13's
      converter); originals kept in `mods/.originals/` for undo; add-in modules (new
      campaigns) stay out - the 360 build cannot load them. Overlap warnings as
      upstream (two mods changing one file, the lower winning). The DLC packages and
      the glyph redirects keep working under the stack.
- [x] 10.13 **Texture packs: upstream's PC-to-360 converter - done (session 40, run 573).**
      A mod the Mods page marks "[PC version]" offers **Convert for this version**:
      DAORecomp's converter runs in the app, native, off the UI thread
      (`dao_convert_android.cc`, `NativeBridge.convertMod`), and the page says what it did -
      the files written, problems, what was left out (eight named, then a count), notes.
      Five of upstream's files build as they are (gff, mesh, texture, game_files, ui/erf);
      three (convert.cpp, mod_files.cpp, files.cpp) with `tools/assemble_dao_convert.py`'s
      13 recorded edits - narrow paths, getpid, a marker file for Windows' exclusive file
      lock (the page converts one mod at a time), no junctions - written to
      `build/dao-convert/` (the bootstrap's step 6 runs it; CMake stops without it). The rest
      of upstream they reach is stood in for: `dao_compat/` (text.h, mods/mods.h,
      mods/mod_install.h, stb_image.h's declarations) and `dao_convert_android.cc` (stb's
      zlib decoders over zlib, a small zip reader for .dazip packages in place of Windows'
      tar.exe - entries outside the mod refused, 8 GiB at most - a DAModder manifest as
      mod.toml, IsInAddinModule). std::stop_token needs libc++'s `-fexperimental-library`
      (that object library only). The PC files are kept in `mods/.originals/<folder>/`;
      the mod is [on] after. **Run 573:** a PC-style mod of three magenta A8R8G8B8 .dds
      textures named after Duncan's body (pm_bdy_duna_0d, 0dl2, 0dl3;
      `tools/make_test_texture_mod.py`) converted in 0.15 s - 3 .xds written, and upstream's
      notes that the mod's own half- and quarter-size copies were kept over the ones it
      would make - and at the King's Camp (the launcher's test launch) Duncan stood there
      magenta, the game reading pm_bdy_duna_0d.xds from the mod's folder. Not exercised on
      the Thor: GFF, mesh and archive conversion (upstream's code, unchanged; no PC mod of
      those kinds at hand). The plan as written: Upstream converts a PC mod's files to the
      360's formats once, at import (`mods/convert/`, about 2,500 lines): GFF files
      to the 360's byte order and struct layouts taken from the game's own files of
      the same type, mesh vertices to its vertex formats, **.dds textures to .xds** -
      texels tiled for the 360's GPU with the packed mip tail, laid out by Xenia's
      `texture_util`, which the runtime tree already has; PC DXT5 normal maps to DXN;
      and the half- and quarter-size copies the game uses for distant models made
      from the mod's, so the game's own don't show instead. DXT1/3/5, BC5, A8R8G8B8
      and the common uncompressed layouts carry over; what cannot convert is left out
      and listed. **Efficient on the Thor:** a one-time file-to-file pass at import
      (the work in a `.converting/` folder, nothing changed if interrupted; storage
      named before it starts), no cost per frame after. Prove it with one known PC
      texture mod end to end.
- [x] 10.14 **Cheats menu, from upstream (the owner picked it, 2026-11-01) - done (session
      40, run 569).** Five rows at the end of the in-game options panel (greyed in the
      launcher): **Cheat: immortal party** and **pass every skill check** (On or Off;
      each session starts Off, the game's state being no setting's), and three done on
      demand - **kill all hostiles**, **add gold** (+1 to +1,000) and **add experience**
      (+1,000 to +100,000): the value chooses the amount, A or a tap on the name does it.
      Each runs the developers' own script at the next frame of play (`warden_game.cc`'s
      RunCheats, through RunScriptAsHero, now able to run as any party member):
      pc_immortal for each member not yet immortal (or each immortal one, off; the party
      from GetPartyList and IsImmortal), zz_override_skills 1 or 0, killallhostiles,
      zz_money in coppers, addxp. Run 569 at the King's Camp by touch: +100 gold (the
      deck's purse 100 gold, 1,000,000 coppers), the party immortal, +5,000 XP - each
      script returned 1. Left out: upstream's super-critical (per party member's tag).
      Upstream's description:
      runs the developers' own scripts over the paused game (`ui/cheats.cpp`): god
      mode (immortal, per party member), gold, pass every skill check, and a value
      the scripts read (the console's `runscript <name> <value>`). The port already
      calls scripts that way (`RunScriptAsHero`, RUNSCRIPT_VAR - session 40), so this
      is a page in the options panel over what works: Immortal, Gold, Skill checks,
      and a script-by-name row for the adventurous.
- [x] 10.15 **A save restore page, from upstream (the owner picked it, 2026-11-01) - done
      (session 40, run 570).** The launcher's **Restore an earlier save** row (before the
      game only) lists the saves kept in `.backup/` - the character and the chapter from
      each save's `savegame.met` (a GFF file: its strings after the header), and when it
      was written - newest first, and puts one back after a confirmation: the kept save
      and the one that replaced it change places (through a `~current` middle step), so a
      restore is undone the same way (`SaveRestore.java`). Run 570: Gerod - Deep in the
      Wilds restored over Gerod - Dragonbone Wastes, which then appeared as the kept one,
      and restored back. Not ported: upstream's mark on a save being written (a crash
      mid-save seen at the next start). Upstream's description:
      The port already keeps an overwritten save aside (`.backup/`, session 32);
      upstream lists the kept saves with character, area and time and puts one back,
      setting the save it replaces aside as `~current` while it works, and marks a
      save being written so a crash during saving is seen (`content/save_backups.*`).
      The same page on the launch screen, beside 11.2's export and import.
- [x] 10.16 **Language as a setting (the owner picked it, 2026-11-01) - done (session 40,
      run 571).** Options -> Language: English (default), Español, Italiano, from the next
      start (`files/language`); `ExGetXConfigSetting`'s language answers the console's 1, 5
      or 6 (`warden_imports_kernel.cc`). The disc's text is in all three (`core_es-es.tlk`,
      `core_it-it.tlk` and their feminine forms); its voices are English only, so the
      voices stay English. Run 571, Español: "Presiona START", the copyright line in
      Spanish, the Spanish talk tables loaded. Not yet checked: the keyboard dialog and the
      save names with accented text. The plan's note:
      carries English, Spanish and Italian; the title asks XAM for the console
      language (the port answers English today). An Options row - English (default),
      Espanol, Italiano - answered from `files/language` from the next start; the
      talktables and voices are per-language in the game data, so the extraction
      already holds them. Check the save display names and the keyboard dialog carry
      the accented text.
- [x] 10.17 **The on-screen controller (the owner asked, 2026-11-01; session 40, run
      551).** Options -> On-screen controller, off by default, taken at once: an Xbox
      controller drawn over the game, translucent, laid out around the HUD - the left
      stick floating where the thumb lands in the lower left, the D-pad beside it
      (eight ways, following the finger), the right stick floating in the lower
      middle-right, A B X Y in Xbox's colours and places on the right, LT/LB and RT/RB
      on the edges between the portraits and the map, Back and Start at the bottom
      centre, L3 and R3, and ≡ for the options panel (`TouchControls.java`). Every
      finger is its own control. Its state goes to the native side in the 360's terms
      (`NativeBridge.touchPad`) and is merged into the first pad as the game polls it -
      buttons OR-ed, a touched stick in place of the pad's, the triggers the larger -
      so the held controller still works, and with no controller the first pad counts
      as connected (`warden_input.cc`, `warden_xam_input.cc`). **Turned on, it sets
      the Button prompts to Xbox** (the owner's ask; from the next start in the game,
      and still changeable). Run 551: from the launcher by touch, the on-screen Start
      opened the title's main menu, the D-pad and the left stick moved its selection,
      ≡ opened the options panel (the controls hidden and the game held while it is
      open) and its Resume closed it; the prompts Xbox's.
- [x] 10.18 **The launcher and the options panel by touch (the owner asked, 2026-11-01;
      session 40, runs 550-551).** The panel (the launcher before the game, the options
      during it) scrolls by swipe; a tap on Launch, a file row or Close acts at once
      (Quit in the game asks for a second tap); a tap on an option chooses it and shows
      what it does, then a tap on the left half of its value steps back (‹) and on the
      right half on (›) - the value's box is wide enough for a thumb either side; the
      hints say so. During the game a **Resume ✕** in the panel's header closes it, and
      the panel opens by three fingers on the game's screen (or ≡ when the on-screen
      controller is on, where three fingers are play). Run 550: the launcher scrolled
      by swipe, the On-screen controller row chosen by one tap and turned on by a
      second, Launch tapped. The three-finger tap is not checked on the device (adb
      cannot press three fingers at once).
- [x] 10.19 **PC add-ins and .7z/.rar mods (the owner asked, 2026-11-01: "a way to import
      that and similar mods" - the No Helmet Hack, Nexus mod 202, the test case; the mod
      itself is the owner's and never in the repo) - done (session 40, run 574).** Most
      PC mods that change how the game plays are **add-ins** (a .dazip: Manifest.xml and
      Contents/addins/<id>/{core,module}/ with scripts, plots, tables, items), and many come
      as .rar or .7z. **Imports:** the Mods page's "Add a mod from a file" takes .zip, .7z,
      .rar and .dazip (told apart by their first bytes; any file can be picked - a type
      list greyed .rar out, BUG-089): .7z through 7-Zip's C decoder (tag 26.05, public
      domain), .rar through UnRAR 7.3.2's library (its licence: reading RAR only; replaced by
      libarchive's RAR readers in 11.3, BUG-094), both
      fetched by the bootstrap (`dao_archive_android.cc`); a .dazip is kept whole.
      **Add-ins:** the 360's downloadable content is the same thing - each console package
      holds <id>/manifest.xml (the same manifest format) and <id>/core/, <id>/module/ with
      ERF archives and talk tables, and the game lists `files/content/00000002/`, mounts
      each package and loads its add-ins (10.10). So "Convert for this version" now first
      installs a mod's add-ins as packages (`dao_addin_android.cc`): each add-in's archives
      unpacked, its files converted by upstream's converter (GFF to the 360's byte order and
      layouts - tables, plots, items, the module's .cif, talk tables; compiled scripts are
      the same on both, so copied; script sources left out), packed again as ERF V2.0
      (the 360's own scripts archives' format, entries sorted), talk tables also loose in
      data/talktables/ as the 360's add-ins keep them, the add-in's manifest entry
      written, and the whole moved into `content/00000002/mod-<id>/` with its display name;
      the mod's addins.txt lists its packages. The engine reads both talk table forms
      (V0.2 plain, V0.4/V0.5 Huffman - the 360 loader branches on the version), so
      upstream's conversion of them is enough. **Upstream bug fixed (BUG-088):** a generic
      field holding a string (a module's string variable) points at the string itself, on
      the PC and the 360 alike; upstream's GFF code read and wrote one pointer too many,
      and the add-in's .cif was "not a well-formed GFF file" - two recorded edits to
      gff.cpp in `tools/assemble_dao_convert.py`. An add-in whose .cif cannot convert is
      not installed. **The page:** a mod's add-ins are named in its About; turning it off
      lists its packages in `files/dlc-off`, which the game's content list skips;
      deleting it uninstalls them (and its kept PC files); both say that saves made with
      an add-in need it. The launcher's disc-2 count leaves mod packages out, and
      `files/hide-dlc` (the test launch, the routes) now hides BioWare's packages only
      (BUG-090). **Run 574:** both No Helmet Hack downloads imported through the file
      picker (the .rar, then the .7z: 1.6 for the main campaign, 1.5a for Awakening),
      converted (6 and 4 files; the .cif's string variables read "NONE" as the 360's
      own), the King's Camp test launch: the game mounted both packages and read the
      add-in's module, event script, tables and item; the inventory held the **No Helmet
      Rulebook** (its name from the converted talk table); `zz_givearmor` (the game's own
      debug script, through files/route-call) dressed Jaden in Dragonbone Legion armour,
      helm included - on leaving the menu the helm was hidden, in the inventory it was
      back in its slot, and the rule book from the radial menu said "Showing Helmets on
      Jaden" (the helm visible) and, used again, "Hiding Helmets on Jaden" (hidden).
      Not exercised: an add-in with its own areas (a new campaign) - the mechanism is the
      DLC's, which load theirs.
- [x] 10.20 **Naming a manual save on the on-screen keyboard (the owner, 2026-11-02) -
      done (session 41).** The 360's Save Game box shows a name - the area's - that only a
      keyboard plugged into the console changed: the title imports no keystroke input,
      and no button on the box reaches its keyboard (a probe on its keyboard routine,
      `sub_822C63E8`, reached through `sub_822C63B0` and `sub_823D7748`: never called
      from the box). So, reusing character creation's keyboard - the app's text dialog
      with the system keyboard (`warden_keyboard.h`, `GameActivity.showKeyboard`) - each
      manual save, once committed (`_u!` content, `XamContentClose`), brings the dialog
      "Name this save" with the game's name selected; OK renames the save, Cancel keeps
      the game's. A prompt can now carry its own answer instead of the title's buffer.
      The rename: `savegame.met` ("GFF V4.0X360SVMT" with a string table) gets the new
      name appended and field 0x41A8 pointed at it, its `.x` CRC-32 written again; and
      the content's display name (`<folder>.name`, "<character>_<name>_<time>", which
      the lists show) its middle renamed. Printable ASCII, 40 characters, an underscore
      typed becomes a space. Checked with the test character: "Save (23) - At the
      ruins" in Load Game, in the same session and after a restart, and loaded.


## Phase 11 — Release

**Complete (session 41, 2026-11-02): version 0.1.0-alpha, "Open Alpha" - released as
source only, each player building from their own disc (DECISIONS D11).** Superseded by
Phase 12 the same day: the APK holds no game code and is published (D14).

Getting the app ready to hand to an end user (the owner, 2026-11-01). "Release" here
means: a person with their own disc image installs the APK, feeds it their files, and
plays - and when something breaks, they can send a report worth having. The repository
and the APK still carry no game material (the standing rule), and the app still has no
online functionality: upstream's update checker stays out by the owner's choice
(2026-11-01), per the no-online rule. The committed debug keystore never signs anything
that leaves the owner's devices, so release builds get their own key (11.3).

- [x] 11.1 **Debugging Mode, and a bug report anyone can send - done (session 40, run
      584).** Options -> **Debugging Mode** (Off by default, from the next start;
      `DebugReport.java`, `warden_debug_mode.cc`): the run's log also to
      files/debug/log-current.txt (Xenia's FileLogSink, added in logging.cc when
      WARDEN_LOG_FILE is set - two recorded edits; flushed after each batch), the last
      run's kept as log-previous.txt, the budgets of the runtime's repetitive lines lifted
      (warden_game.cc's Budget, warden_fs.cc's ShouldLog; a file's reads still sampled -
      the first few, powers of two, every 1024th - or a streamed file would fill the
      storage), headed by the app's version and commit (BuildConfig.GIT_COMMIT, "+changes"
      for an uncommitted tree), the device, its SoC, Android and its build, the GPU and its
      driver, every option and the marker files; the save being loaded copied aside as its
      savegame.das is first read (files/debug/loaded-save/). In the game the panel's
      **Exit and export save/logs** (between Photo mode and Quit, shown only then) closes
      the panel, asks the game for its own autosave whatever Autosave when put away says
      (NativeBridge.saveForReport: written, none possible, or not in 6 s), copies the
      screen, holds the game, packs warden-report-<date>-<time>.zip into
      Downloads/Dragon Age Origins (readme, both logs and the header, the loaded save as it
      was, the fresh save, files/exits/ with its tombstones, the screenshot), names it, and
      ends the game. **Mods named (the owner, 2026-11-02):** the log says at each
      start which mods are on - "MODS LOADED: ..." lines at warning level, each mod by
      its name, its folder and its place in the load order, each add-in from a mod by
      its title and package, on or off (warden_runtime.cc's LogMods), or "Mods: none -
      the game as on the disc" - and the report's header and readme say it too ("Mods:
      ..."). **Since session 41's close (the owner: reports with mods might not be looked
      into):** both answers on the one marker - "MODS LOADED: none - the game as on the
      disc" or "MODS LOADED: yes - N mods loaded, M add-ins from mods on, K off" and a line
      each - and the readme's and header's "Mods:" a block, a mod a line in the game's
      order (the one that wins first, its version, author and folder; from
      `files/mods-enabled.txt`, what the game loads), each add-in on or off, and the mods in
      the list not loaded; with "Game's code: made from Title Update N" above it. Run 584 (the load route, Jaden at Ostagar): the header and the GPU
      line at the log's head, the loaded save kept as it loaded, the export - the save
      written in 1.3 s, the 7.6 MB zip (2.3 MB packed) a second later with all of it, the
      game ended with "the player exported a bug report". The log grows about 70 MB an
      hour in this mode. The plan as written: An Options row, **Off
      by default**. On, the **next run** of the game records everything: the full
      unbudgeted log (file IO, GPU, kernel, the title's own lines) written to a
      **file in the app's storage** as well as logcat - an end user cannot reach
      logcat, so the file is the record - with the device's model, Android version
      and GPU driver, the app's version and commit, and the options in effect written
      at its head. While Debugging Mode is on, the settings show one more row:
      **"Exit and export save/logs."** It makes a fresh save where the player stands
      (the game's own save, where the game allows one), ends the game cleanly, and
      packs **one .zip into Downloads** (MediaStore - reachable by a non-rooted user,
      no permissions) holding: this run's log and the previous run's, **the save that
      was loaded at the start of the session** (copied aside at load time, so the
      reproduction point survives every autosave since), **the fresh save** just
      made, the exit reports and tombstones (`files/exits/`), a screenshot, the
      options and markers in effect, and a readme naming the app version, commit,
      device and driver. A dialog names the file
      (`warden-report-20261101-1830.zip`) so the player can share it from any file
      manager or messenger. One toggle and one button turn "it broke" into a report
      that reproduces: the save to load, the log of what happened, and the machine it
      happened on - no adb, no root, no instructions to follow.
- [x] 11.2 **Export and import saves - done (session 40, run 585).** The launcher's row
      is now **Saves - export, import, restore** (`SavesPage.java`; before the game only):
      **Export all saves** or **Export one save** (each listed by character, area and
      time from its savegame.met) packs the save folders and their display names, as
      they are, into "Dragon Age save(s) - ....zip" in Downloads/Dragon Age Origins;
      **Import saves from a .zip** (Android's file picker) finds every folder in it
      holding Saves/savegame.das headed "GFF V4.0X360SAVE" - this game's, the 360's (a PC
      save says PC there and is refused) - and puts each in its own slot, or, when that
      slot is taken, as the player chooses: **Keep both** (the character's next manual
      slot, `_<n>_u!0`), **Replace** (the one there kept in .backup/, as the game's own
      overwrites are) or **Skip**; **Restore an earlier save** is 10.15's page. Files'
      times are kept both ways (the game's lists show when a save was written). Run 585:
      Leliana's Denerim Market save exported (44.6 KB), imported back as Keep both - it
      became slot 2 - listed in the game's Load Game as "Save (2)" with her location and
      time played, and loaded into Denerim Market with her party; the round trip again
      with the times kept, and Skip. The test copy and zips removed. The plan as
      written: Settings rows: **Export saves** packs a chosen
      save (or all of them) as a .zip into Downloads, named by character, area and
      time from the save's own display name; **Import saves** opens Android's file
      picker, takes such a .zip (or one made by hand from a save folder), checks it
      is this game's save (savegame.das and the title's files, the right title id),
      and puts it in - a free slot by default, or over a chosen save after a
      confirmation, the replaced one kept in `.backup/` as ever. Shares 11.1's
      zip-and-Downloads plumbing. No root, no adb.
- [x] 11.3 **A release build, signed with its own key - done (session 41).** The
      owner's answers (2026-11-02): I make and keep the key; the app goes to a public
      repository, under whichever licence lets everything be shared; the clean run on a
      second copy on the Thor. **The licence:** GPL-3.0-or-later (`LICENSE`) - lsfg-vk is
      GPL-3.0 and compiled in, and everything else built in allows it, but for UnRAR,
      whose licence forbids using it to make a RAR-compatible archiver: .rar is now read by
      libarchive v3.8.11's readers (BSD 2-Clause, a static library of its read side;
      BUG-094), checked on the Thor - the No Helmet Hack .rar through the Mods page, its
      .dazip byte-identical to Windows' tar.exe's. README, THIRD_PARTY.md, About this app
      (the licence, DAORecomp at github.com/96dot/Dragon-Age-Origins-Recomp as the base,
      the source's address from gradle.properties' `warden.sourceUrl`, empty until the
      public repository existed; https://github.com/mabencs/Dragon-Age-Origins-Recomp-Android-Duo since 2026-11-02) and the licences page (the app's own GPL text first,
      libarchive in UnRAR's place) say so. **The key:** RSA 4096, 30 years, at
      `~/.android/dao-release.jks`, its passwords in `~/.android/dao-release.properties`
      (outside the repository; app/build.gradle.kts reads it, `-Pwarden.releaseSigning`
      names another; without it a release is unsigned and says so). **The version:**
      gradle.properties' `warden.version` (0.11.0), the code from it (major*1,000,000 +
      minor*1,000 + patch); debug builds are "-debug". **Symbols:** kept in the release's
      libwarden.so as in debug; the release keeps its assertions too (the build type's
      -DNDEBUG stripped in CMakeLists.txt, so it runs the code the debug build was tested
      as). **The proof:** `python tools/release.py` (after `gradlew :app:assembleRelease`)
      checks the APK - signed, by the release key, never the debug one; no file of the
      game's types; Lossless.dll neither by name nor by its bytes; none of the owner's disc
      or title update file names - and keeps it in build/releases/<version>/ with the
      unstripped libwarden.so and its build id. Proven both ways: 0.11.0 and 0.11.0-clean
      pass; a copy with an .erf and Lossless.dll's bytes planted fails on both, and on its
      broken signature. BUG-095 on the way (the deck's task affinity named the package).
      The plan as written: A release signing config with
      a new keystore (kept out of git; the committed debug key never signs anything
      that leaves the owner's devices - CLAUDE.md), native symbols kept so crash
      reports stay readable (and each release's unstripped `libwarden.so` kept with
      its build id, so a tombstone from it can be read later), a version name and code scheme, and the pre-commit rule
      proven against the APK itself: the bundle holds no game material, no disc
      paths, nothing of the owner's (Lossless.dll stays user-supplied, never
      bundled).
- [x] 11.3a **The package ids name the project, not a person (the owner, 2026-11-02).**
      `dev.daorecompandroid.duo` for the debug build, `prod.daorecompandroid.duo` for the
      release (`androidComponents` sets it; 11.4's clean copy
      `prod.daorecompandroid.duo.clean`), and the Java package `dev.daorecompandroid.duo`
      (the sources moved; `jni_bridge.cc`'s class name; every tool's `PKG`). The Thor
      moved over without losing anything: the old app's private files (saves, options,
      the 4.1 GB of installed DLC, Lossless.dll) copied on the device from one `run-as`
      to the other (`tar` piped), the mods list's paths rewritten, its external files
      (the 6.4 GiB extraction, the title update, the mods) renamed into the new app's
      folder (one `mv`, instant); a full copy of the private files kept on the computer
      first (build/migrate/old-files.tar). The new app: Ready, Extracted, 11 installed,
      In place; the mods loaded from its folder; the owner's save loaded at Orzammar, the
      deck's rows right. the former package then uninstalled, as the owner asked.
      **No build machine paths (the owner asked next):** the native library's debug
      sections held the NDK's path, under the user's home (C:/Users/<name>/...), and
      the repository's. Every C and C++ unit is now built with `-ffile-prefix-map` (the
      NDK as `ndk`, the repository as `src`, the rest of the home folder as `home`) and
      FFmpeg's assembly with `-fdebug-prefix-map` (`app/src/main/cpp/CMakeLists.txt`);
      both builds rebuilt in full (13 min 45 s). Both APKs: no "Users" or "Repos" path,
      no user name. `tools/release.py` now fails a release holding any such path. Run
      591: the debug build boots as before.
- [x] 11.4 **The first run on a clean install - done (session 41).** The release built
      with `-Pwarden.clean` is a second app, `<the former id>.clean` ("DAO clean test"),
      installed beside the owner's and walked as a player would, with the Thor's own
      controller (its input device written with `sendevent`; the game ignores injected
      virtual keys, as it should): the launcher "Setting up", every source "Choose...";
      the disc image through Android's picker - extracted with its progress on the row
      and the panel's foot, 9,450 files, 6.86 GB (14 s here, the image in the page cache
      after the morning's reads); the title update through the picker, "In place",
      "Ready"; Launch - the title screen, START, the game's own downloadable content
      notice (shown with none installed), the storage prompt answered by the app (the
      hard drive, no question), the main menu with Load Game greyed, New Game, the
      intro, character creation. The options panel by Select + Start: the defaults the
      owner set (60 fps, Bilinear, 1280x720, FSR and frame generation off) and only
      player options - Debugging Mode the one diagnostic; the launcher's test launch row
      is 11.6's. The clean copy removed after (`adb uninstall <the former id>.clean`, its
      package named exactly); the owner's install and its 6.4 GiB untouched. The plan as
      written: The whole path an end
      user walks, re-checked on a wiped install: the file picker for the disc image
      and title update, the eight-minute extraction with its progress, the storage
      prompt, play - and the developer surfaces (the bring-up report, test markers,
      capture schedules) invisible unless asked for. Debugging Mode (11.1) is the one
      diagnostic a user sees.
- [x] 11.5 **Licences and credits in the app - done (session 40, run 586).** The panel's
      **About and licences** row (before Quit, in the launcher and in the game;
      `AboutPage.java`): "About this app" - the version and commit, the plain sentence
      that Dragon Age: Origins is BioWare's and EA's, that the app holds none of it and
      runs the player's own copy, that it is not made or endorsed by them, that it never
      connects to the internet, and where the GPL and LGPL code's sources are - then
      every project built into the app with its licence, each tapped for its full text
      headed by what the app uses it for, its version and its source: Xenia, DAORecomp,
      XenonRecomp, SIMD Everywhere, lsfg-vk (GPL-3.0), the FidelityFX SDK, CAS and FSR 1,
      FFmpeg and libmspack (LGPL-2.1), glslang, Vulkan-Headers (both its licences), Vulkan
      Memory Allocator, fmt, Snappy, xxHash, disruptorplus, date, UTF8-CPP, cxxopts,
      toml++, stb_image, tiny-AES-c, TinySHA1, 7-Zip's C decoder and UnRAR (libarchive
      since 11.3) - 26 in all.
      `tools/gather_licences.py` copies the texts from the pinned checkouts into
      app/src/main/assets/licences/ (committed; the bootstrap's step 6 refreshes them,
      `--check` says whether they match). **For the owner, before any build leaves their
      devices (11.3):** lsfg-vk is GPL-3.0 and compiled into the app, so a distributed
      build would be a GPL-3.0 work whose complete source must be offered to whoever gets
      it; FFmpeg and libmspack (LGPL-2.1, linked statically) ask that a recipient can
      relink against changed versions. The repository had no licence of its own yet
      (11.3 settled it: GPL-3.0-or-later). The
      plan as written: A settings page surfacing
      THIRD_PARTY.md: Xenia (BSD), DAORecomp, lsfg-vk (GPL-3.0, with the source
      offer), FidelityFX (MIT), FFmpeg and the rest, with versions - and the plain
      sentence that the game is the player's own and nothing of it ships with the
      app.
- [x] 11.6 **Remove the launcher's test launch (the owner, 2026-11-01) - done (session
      41).** The launcher's
      "Test: launch to the King's Camp" row - the owner's testing button, added in session
      40 - goes before release: `DebugLaunch.java`, the `DEBUG_LAUNCH_ROW` in
      `OptionsPanel.java` (the rows below it move up one) and `onDebugLaunchChosen` in
      `GameActivity.java`. What it does, for the record: it writes the ostagar route
      (`route`, `inject-pad` adapted as `tools/route.sh` does - New Game past Resume when
      there are saves - and `hide-dlc`) with a `debug-launch` marker, launches, and clears
      them four minutes later or at the next ordinary launch (run 568: the King's Camp,
      hands off, about two minutes; the files gone after). **Done (session 41):** the
      three removed; on the Thor the launcher's rows run from Launch to Mods, then the
      settings, with no test row - the ostagar route (`tools/route.sh`) does its job from
      the computer. With it, the release: version **0.1.0-alpha, "Open Alpha"**
      (`gradle.properties`' `warden.version` and the new `warden.releaseName`, shown on
      the About page), and the docs brought up to date for a public repository - the
      README as the user's manual (every setting, the files to supply, mods and which
      work), nothing private in the repository, DECISIONS D10-D13.

## Phase 12 — A published APK: the game's code made on the device

**Goal (the owner, 2026-11-02): an APK that can be published, which a player installs and
gives only their own discs.** The player supplies the base game's disc image and the
Ultimate Edition's disc 2 - which carries Title Update 6 and all the downloadable
content; Title Update 7 is optional (recommended), `Lossless.dll` optional as before. The
app makes the game's code on the device itself, once, after the discs are chosen:
XenonRecomp translates the disc's executable with the title update, and a compiler in the
app compiles that ("a long compile on the device is fine"). The APK then holds none of
the game - neither its code nor its data - and replaces 11's source-only release
(DECISIONS D11).

**Complete (session 41, 2026-11-02):** every criterion met in 12.7 (run 599).

**Exit criteria:** the release APK, holding no game code (`tools/release.py`), installed
on a clean device, given the two disc images, makes the game's code and plays.

- [x] 12.1 **The compiler on the device - done (session 41).** LLVM 19.1.7 (the major
      version of the NDK r28c clang the code was tested with) built for Android arm64 by
      `tools/build_device_toolchain.py`: LLVM's tablegen tools for the computer first
      (clang-cl against the MSVC sysroot, `tools/cmake/clang-cl-xwin-host.cmake`; BLAKE3's
      MASM assembly left out - no Visual Studio), then one program, `llvm` (LLVM's
      multicall driver), holding clang and lld and nothing else, the AArch64 target only:
      89.8 MB stripped, with clang's headers (14 MB). Named `libllvm.so` in the APK it
      takes the tool from its first argument (`libllvm.so clang ...`). **Timed on the Thor
      (`tools/gamecode_probe.py`):** the 208 generated files compiled with the guest's own
      flags, 6 at a time, in **8 min 42 s**, none failing; the largest compile 402 MB;
      2,432 s of compiles in all. One file is most of it: `ppc_recomp.17` alone takes
      521 s (330 s on the computer), the rest are done long before. This computer: 6 min,
      1,662 s.
- [x] 12.2 **The game's code as a library of its own - done (session 41, runs 593-594).** `libwarden.so` without it;
      `libdaogame.so` loaded at boot from the app's files (`warden_game_library.cc`). The
      runtime's reach into it resolved at the load: the function table, the image's
      layout (Title Update 6's image is 0x13F0000 bytes, 7's 0x1400000, and the
      indirect-call table sits right after it), and the ~70 game functions the runtime
      calls (`WARDEN_GAME_FUNC`). The other way, its 56 overrides (32 written out, 24 made
      by the probe macros in warden_game.cc; `WARDEN_OVERRIDE`) reached through forwarders
      compiled with the game's code (`assets/gamecode/shim.cpp`), the list checked both
      ways at the load. What the game's code needs from outside is small (14 symbols:
      libc, libm, the compiler's atomics helpers, `PPCQueryTimebase`, the trace, the GPU
      register hook, and the 251 imports, whose stubs move into the runtime). The
      recipe - hooks header, shim, compile and link flags - committed in the APK's assets
      (`tools/gen_gamecode_recipe.py`), the computer's build of the library reading the
      same. The BUG-052 Direct3D probe (540 wrappers, logging only, behind the portrait
      investigation's marker) retired rather than forwarded. **Done:** the APK holds no
      game code (its libwarden.so, 133.9 MB in the debug build, exports only the 56
      `warden_override_*`); Gradle builds `libdaogame.so` (76.8 MB) beside it and leaves it
      out; `python tools/push_gamecode.py` puts it in `files/gamecode/` (read-only, the
      folder 0700). One more call needed moving: photo mode called the field-of-view
      override by the game's name - it calls `warden_override_tu6_sub_82683E60` now. Run
      594: the library loaded (ABI `1961314d8a1bb6ae`, 87 game functions the runtime calls
      resolved), Title Update 7 picked as the one it was made from, the intro movie and the
      title screen, no fault. Run 593, before the TU7 file was back in the app's files, said
      exactly that: "none of Title Update 7 - the one the game's code was made from".
- [x] 12.3 **The title update from disc 2 - done (session 41, runs 597-598) (the owner: "Title update 7 should be
      optional"; "marked as optional (recommended)").** Disc 2's `title_update.bin` is
      Title Update 6 for this disc (PIRS, media `186A1115`), its `default.xexp` the very
      TU6 DAORecomp's config was made for (SHA-1 `4141b608...`). Used when no TU file is
      chosen; a chosen Title Update 7 wins. The launcher's row "Title update (optional,
      recommended)"; the runtime takes TU6's patch as well as TU7's (it took TU7's alone,
      by its size). TU6's translation differs from TU7's in 3 of 211 files: the image
      size, one global 4 bytes along, a version number. **In:** the runtime takes TU6's
      patch as well as TU7's and picks the one the loaded game code was made from (by its
      image size; with no game code, the newest); disc 2's install copies its Title Update
      6 (media `186A1115` - not the French or German ones) to `title_update_disc2.bin` in
      the app's files (`warden_dlc.cc`); the launcher's row "Title update 7 (optional,
      recommended)" (the owner's words) says "In place", "disc 2's TU6 instead", or what
      to choose; Launch asks for disc 2 or Title Update 7. **Tried as a player would
      (runs 597-598):** the TU7 copy removed - Launch "Choose disc 2 (or Title Update 7)
      first."; disc 2 chosen through the picker - "Dragon Age: Origins Title Update #6 taken
      from the disc image" (the DLC installed already), the row "disc 2's TU6 instead", and
      the app made the game's code again from Title Update 6 by itself (7 min 30 s; the
      translation matched TU6's hashes). Run 597 then hung at startup: BUG-103, the runtime's
      `PPC_LOOKUP_FUNC` with TU7's image size; fixed. Run 598: TU6's code, disc 2's patch,
      the ostagar route to explore mode at the King's Camp, no fault.
- [ ] 12.4 **XenonRecomp on the device.** Built for Android from the computer's patched
      sources (`build/device-toolchain/bin/xenonrecomp`, 1.3 MB). DAORecomp's
      `scripts/prefix_symbols.py` (the `tu6_` names) is now XenonRecomp's own third
      argument (`tools/patch_xenonrecomp.py`, PLAN 12.4) - the device has no Python: the
      computer's 211 files came out byte for byte as the script made them. **On the
      Thor:** the disc's executable with Title Update 7 translated in 8.0 s (69 MB), all
      211 files identical to the computer's. Left: DAORecomp's configuration in the APK,
      and the device checking its output against the computer's hashes
      (`assets/gamecode/sources.json`, both title updates).
- [x] 12.5 **Making the game's code on the device - done (session 41, runs 595-596).** A service of its own (foreground,
      its own process): the executable and the title update's patch from the player's
      files, XenonRecomp, the output checked, every file compiled (resumable), linked
      against `libwarden.so`, kept read-only with what it was made from; progress on the
      launcher's row, Launch waiting for it. The app's native libraries extracted at
      install (legacy packaging) so the compiler can run from them. **Done:**
      `GameCode.java` in `GameCodeService` (process `:gamecode`, foreground, a partial wake
      lock, a heartbeat on `files/gamecode/state.txt` every 10 s so the launcher tells a
      making from a dead one). The steps: `assets/gamecode/toolchain.zip` (6.9 MB, 3,320
      headers and link inputs, `tools/stage_gamecode_toolchain.py`) unpacked to
      `files/gamecode/toolchain/` when its version changes; the extracted `default.xex`
      (SHA-1 checked) and the title update's `default.xexp` (out of its package,
      `NativeBridge.titleUpdatePatch`; SHA-1 TU6 or TU7); DAORecomp's TOML pointed at the
      work folder; `libxenonrecomp.so`; the 211 files against `assets/gamecode/sources.json`
      (both title updates' SHA-256s, `tools/gen_gamecode_recipe.py --sources`); `libllvm.so
      clang`, 6 at a time, the slowest first, each object kept (resumable); `libllvm.so
      ld.lld` with recipe.json's line against the installed `libwarden.so`; read-only,
      `manifest.json` (ABI, title update, made where); the work folder deleted. The
      launcher starts it on its own once the disc is extracted and a title update is in
      place, shows "making N%" on Launch, and makes it again when the app's ABI or the
      title update changes; a failure is tried again once each start. The APK: 101 MB,
      with `libllvm.so` and `libxenonrecomp.so` and no game code. **Run 595:** from opening
      the app to "The game's code is made (Title Update 7)" in **7 min 30 s** on the Thor
      (the translation and its check, all 211 files as expected; 209 compiles; the link),
      the device-made `libdaogame.so` (76.5 MB) booted to the title screen, no fault. **Run
      596:** the ostagar route on it to explore mode at the King's Camp, no fault; the game
      at 25 fps with the owner's settings (120, FSR 3.1 Balanced, Lossless 2x, Fast shader
      math, shadows off), as before.
- [x] 12.6 **The published APK.** The release build without the game's code, proven by
      `tools/release.py`; the README for players (install, two disc images, wait once,
      play - no computer); DECISIONS D14 (D11 superseded); LLVM in the licences page.
      **Done:** `release.py` now also fails an APK with `libdaogame.so`, with a symbol of
      the game's translated functions in any library (`llvm-nm`: `sub_XXXXXXXX`, with or
      without `tu6_`, plain or mangled), or with a game file inside
      `assets/gamecode/toolchain.zip`, and one without the making's parts (`libllvm.so`,
      `libxenonrecomp.so`, the toolchain, `recipe.json`, `shim.cpp`, `hooks.h`,
      `sources.json`). Release builds leave out the Khronos validation layer. The About
      page and the licences: LLVM and the NDK's parts (28 projects), the text saying the
      game's program is translated on this device. The README is for players (What you
      need, Installing the app from Releases, Setting up with the making as step 7;
      Building from source under For developers); the bootstrap builds the device's
      toolchain (step 6b). The 0.1.0-alpha release: 75.6 MB, `release.py` clean, kept in
      `build/releases/0.1.0-alpha/`.
- [x] 12.7 **A clean install from the published APK** on the Thor (11.4's second copy):
      the disc images from Download, the game's code made on the device, play.
      **Done (run 599):** the 0.1.0-alpha release (`prod.daorecompandroid.duo`) installed
      beside the owner's debug build, which was not touched. The base disc chosen from
      Download, extracted; disc 2 chosen, the 11 packages installed and its Title Update
      6 taken ("disc 2's TU6 instead"); the making started by itself and logged `made
      .../libdaogame.so from Title Update 6` **7 min 43 s** later; Launch, the intro, the
      title screen, START, the downloadable-content notice and check, the main menu, no
      fault - pressed with the Thor's own controller (`docs/TESTING.md`). Lossless.dll,
      optional, was not given: the Android picker's storage provider stopped answering
      (an ANR in `com.android.externalstorage`). **Phase 12 is complete.** Before the
      repository is published: its git history holds the old LAN address, home paths,
      the earlier package id and the screenshot before the hero's name was pixelated, so
      it is best published with a fresh history.
- [x] 12.8 **Build, when the player is ready (the owner, 2026-11-02):** "under Launch
      Dragon Age there should be a Build button that starts the building process once the
      user has added all their files, to prevent the building starting before the user is
      ready." The launcher's second row, **Build**: offered once the disc image is
      extracted and a title update is in place, with no extraction or disc 2 install
      going on; its value "Start" (or "Build again" when the code made earlier is for
      another title update or app version), "building N%", "Done", "failed" (Build tries
      again). Nothing starts by itself any more - so a player with Title Update 7 adds it
      before building, rather than the code being made from disc 2's TU6 first. Launch
      waits for it. The row is greyed when there is nothing to build.

## Phase 13 — Updates from the launcher

- [x] 13.1 **The update check and install (the owner asked, 2026-11-02; session 42, runs
      614-618, DECISIONS D15).** `AppUpdate.java`: at each launcher start, off the main thread,
      the releases list; a row under Launch when one is newer (`Update available: <version>`).
      **Download & install** fetches the APK with its progress, checks the size and SHA-256
      from the API's `digest`, and gives it to `PackageInstaller`. A release whose notes hold
      `<!-- manual-update -->`, or with no APK or digest, and any build but the published one,
      show **Manual update**, which opens the release's page. Launching the game cancels it.
      Checked with a build of the published package at `-Pwarden.version=0.1.0-alpha`: the real
      0.1.1-alpha found, 75,661,083 bytes verified, Android's first-time permission, cancel and
      try again, and installed in place (version code 1000 to 1001). With a debug build's
      `files/update-feed`: manual, nothing newer, drafts, the highest of several, no APK, an odd
      tag, an empty list and a broken answer. The manual path on the published package was not
      run: only a debug build takes a test list.
      **Session 43 (the owner asked, 2026-11-03):** the release's notes shown in a dialog before
      the download starts (and before a manual update's page opens): the Markdown's headings in
      bold, its bullets, links and code marks plain, HTML comments and the notes' own
      "Installing" section left out (`AppUpdate.formatNotes`, `GameActivity.showReleaseNotes`).
      Checked with a test feed on the debug build.
- [x] 13.2 **See releases on GitHub (the owner asked, 2026-11-03; session 43).** A launcher row
      under Launch Dragon Age (`OptionsPanel.RELEASES_ROW`) that opens `warden.sourceUrl` +
      `/releases` in the browser (`ACTION_VIEW`): every release and its notes. The browser
      fetches the page, not the app; the row is hidden in the game and in a build without a
      source URL. Shipped in 0.1.4-alpha - the owner asked for "0.1.3.5", but the update check
      in 0.1.2 and 0.1.3 reads three numbers and the version code follows three, so a fourth
      would never be offered to 0.1.3; the owner chose 0.1.4.

---

## Risk register

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| ~~R1~~ | ~~Xenia's Vulkan backend doesn't cover DAO's Xenos usage~~ | ~~Project-threatening~~ | **RETIRED 2026-10-22.** Probe answered green — see [`PROBE-Q1.md`](PROBE-Q1.md). |
| **R1b** | **`kHostRenderTargets` EDRAM path is inaccurate for this game** (Q8) | **Moderate — downgraded 2026-10-22** | Measured on the device ([`EDRAM.md`](EDRAM.md)): **all three accuracy switches land on the good side and all nine render target formats blend natively**, so the specific degradation upstream warns about — blending in a substituted format — does not apply here. What remains is structural (tile ownership transfer, resolve semantics, MSAA layout) and still needs frame comparison against the D3D12 build. |
| **R1c** | **DAO's rendering features must be rewritten, not ported** | Medium | 7,178 lines of upscaler / motion-vector / HDR / motion-blur code sit inside `gpu/d3d12/` with no API-agnostic abstraction. Build that abstraction in Phase 3.5; it is why D7 defaults FSR off. |
| R2 | EDRAM emulation too slow on a mobile tiler | High | Exploit that we need only one game correct, not all of them. Tiler-friendly render-pass design in 3.8. |
| R3 | simde gaps for VMX128 instructions DAO uses | High | Audit in 2.2; hand-write NEON for any gaps. |
| R4 | Upstream config assumes DXIL, not SPIR-V | Medium | Surface in Phase 1.4 rather than Phase 3. |
| ~~R5~~ | ~~Cross-architecture behavioural divergence~~ | ~~Medium~~ | **RETIRED 2026-10-22.** Measured across 6,635 functions on real hardware: 98.70% agree and all divergences are explained architectural differences, with zero logic bugs. See [`PROBE-ARM64.md`](PROBE-ARM64.md). |
| R6 | Sustained thermal throttling makes 60 fps unreachable | Medium | Phase 10.2; 30 fps remains a valid target. |
| R7 | Glyphs partly baked into non-atlased art | Medium | Phase 7.1 survey across all 37 atlases before building. |
| R8 | Second-screen state mirror proves fragile | Low | Read-only by design (D6); deck stays off the critical path (8.7). |
| R9 | DAORecomp upstream changes break our fork | Low | Pin a specific upstream commit; rebase deliberately. |
| R10 | Scope creep from the feature list | Medium | Nothing in Phases 7–10 starts before Phase 6 passes. |
| R11 | Guest memory ordering on ARM64: XenonRecomp compiles `sync`, `lwsync` and `eieio` to nothing, and ARM64 lets other cores see a thread's stores out of order | Medium | Found in BUG-047 (run 297) where our runtime crossed threads on the title's stores - the ring poller now waits a poll. If rare corruption shows elsewhere: emit `dmb` for the barriers in XenonRecomp (a full recompile). |

---

## Ordering rules

1. **Nothing from Phases 7–10 begins before Phase 6 passes.** The features are the
   point of the project, but they are worthless on a game that doesn't run.
2. **Q1 gets answered in Phase 0.** It is the cheapest possible place to discover a
   project-threatening problem.
3. **Prove on x86-64 host first, then ARM64.** Phase 1.5 exists so that when something
   breaks in Phase 2, architecture is the only new variable.
4. **Frame pacing is not polish.** It goes in at 4.7.
5. **`STATUS.md` is updated in the same commit as the work it describes.**
