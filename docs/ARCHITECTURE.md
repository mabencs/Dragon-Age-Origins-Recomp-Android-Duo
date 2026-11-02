# Architecture

How the pieces fit together, and why each one is where it is.

---

## 1. Static recompilation, not emulation

This is the premise the whole project rests on, so it's worth being precise about it.

An emulator reads Xbox 360 PowerPC instructions at runtime and interprets or JIT-compiles
them. A **static recompiler** translates the game's machine code to C++ **ahead of time**,
on a workstation, and that C++ is then compiled by a normal compiler into native ARM64
code. At runtime there is no instruction decoding, no dispatch loop, no JIT.

What that buys us:

- Native ARM64 performance, which is what makes 60 fps plausible on a handheld
- Normal debugging tools work on the result
- Compiler optimisation applies to the game's own code

What it costs:

- Everything the game's code *calls into* must still be provided: the Xbox 360 kernel,
  XAM, the filesystem, audio, input, and the Xenos GPU. That's where the work is.
- It is per-title. The output is specific to one executable — which is exactly why
  Awakening's different media ID matters (see [D2](DECISIONS.md#d2--base-game-plus-tu7-only-awakening-deferred)).

---

## 2. Build pipeline

**As built (PLAN Phase 12):** on the player's device, by the app, once - when the player
chooses Build in the launcher, after the disc image is extracted and a title update is in
place - and again (Build again) only when the title update or the app's runtime ABI
changes. A developer's computer can run the same steps with the
NDK (`tools/bootstrap.py`, `tools/recompile.py`) and push the result
(`tools/push_gamecode.py`). The planning-era picture had a shader recompiler beside
XenonRecomp; it was dropped (PLAN 1.4): Xenia translates the game's shaders to SPIR-V at
run time, and keeps them.

```
  YOUR DISC (media 186A1115)         TITLE UPDATE: disc 2's TU6, or TU7 (optional)
            │                                      │
            │  extracted by the app                │  its default.xexp, out of the package
            ▼                                      ▼
      default.xex (SHA-1 checked) ──────────── default.xexp (SHA-1: TU6 or TU7)
                              │
                              ▼
                 XenonRecomp (libxenonrecomp.so, in the APK)
                 PowerPC → C++, guided by DAORecomp's TOML config
                 (function database, patches); names prefixed tu6_
                              │
                              ▼
                 211 files, checked against assets/gamecode/sources.json
                              │
                              ▼
                 clang + lld from LLVM 19.1.7 (libllvm.so, in the APK)
                 arm64-v8a, API 33, VMX128 through SIMD Everywhere,
                 with assets/gamecode/ (hooks.h, shim.cpp, recipe.json)
                              │
                              ▼
                 files/gamecode/libdaogame.so  (never in the APK)
                 loaded by libwarden.so at boot, its ABI checked
```

`GameCode.java` runs the steps in `GameCodeService`, a process of its own (about 7.5
minutes on the Thor). The runtime (`libwarden.so`) holds no game code: it resolves its
calls into the game's library by name at load (`WARDEN_GAME_FUNC`) and takes the image's
layout from it, and its overrides of game functions (`WARDEN_OVERRIDE`) are reached
through forwarders compiled with the game's code (`shim.cpp`, written by
`tools/gen_gamecode_recipe.py`).

Two divergences from upstream DAORecomp are baked in from the start:

1. **SPIR-V instead of DXIL** for shaders: Xenia's Vulkan backend and its SPIR-V shader
   translator in place of the Direct3D 12 one (Phase 3).
2. **ARM64 instead of x86-64** for the game code, routed through SIMD Everywhere
   (Phase 2.2).

Phase 1.5 builds the generated code for **x86-64 host first**, deliberately. When
something breaks in Phase 2, architecture is then the only new variable.

---

## 3. Runtime structure

```
┌───────────────────────────────────────────────────────────────────┐
│  Android app  (API 33, arm64-v8a)                                 │
│                                                                   │
│   GameActivity                        DeckActivity                │
│   display 0, 1920×1080                display 4, 1240×1080        │
│   Vulkan SurfaceView                  touch, DAO-styled UI        │
│        │                                     ▲                    │
│        │                                     │ pull, 10–15 Hz     │
│        │                       ┌─────────────┴──────────┐         │
│        │                       │  state snapshot        │         │
│        │                       │  (double-buffered)     │         │
│        │                       └─────────────▲──────────┘         │
│   ┌────┴─────────────────────────────────────┼──────────────┐     │
│   │  JNI bridge  (narrow, explicit)          │              │     │
│   └────┬─────────────────────────────────────┼──────────────┘     │
└────────┼─────────────────────────────────────┼────────────────────┘
         │                                     │
┌────────▼─────────────────────────────────────┼────────────────────┐
│  libwarden.so + libdaogame.so  (native)      │                    │
│                                              │                    │
│  ┌────────────────────────────────────────────┴───────────────┐    │
│  │  state mirror  ── taps the Scaleform data feed the game    │    │
│  │                   already emits for its own HUD/journal    │    │
│  └────────────────────────────────────────────────────────────┘    │
│                                                                    │
│  ┌──────────────────────┐   ┌──────────────────────────────────┐   │
│  │ recompiled game code │   │  glyph redirection layer         │   │
│  │ (libdaogame.so)      │   │  atlas cells · text · GDA        │   │
│  └──────────┬───────────┘   └──────────────────────────────────┘   │
│             │                                                      │
│  ┌──────────▼───────────────────────────────────────────────────┐   │
│  │  Xbox 360 environment shims                                  │   │
│  │  kernel · XAM · filesystem · save data · threads             │   │
│  └──────────┬───────────────────────────────────────────────────┘   │
│             │                                                       │
│  ┌──────────▼────────────────┐   ┌───────────────┐  ┌───────────┐  │
│  │  Xenos GPU frontend       │   │ audio         │  │ input     │  │
│  │  (Xenia, UNCHANGED)       │   │ AAudio        │  │ Android → │  │
│  │  PM4 · EDRAM · registers  │   │               │  │ x360 pad  │  │
│  └──────────┬────────────────┘   └───────────────┘  └───────────┘  │
│             │                                                       │
│  ┌──────────▼────────────────────────────────────────────────────┐  │
│  │  VULKAN BACKEND  ◄── Phase 3, the bulk of the project         │  │
│  │  Adreno 740 · VMA · pipeline cache · P3/HDR10 present         │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│                       Swappy frame pacing                           │
└──────────────────────────────┼──────────────────────────────────────┘
                               ▼
                          display 0
```

---

## 4. The key structural decision: frontend versus backend

Xenia's GPU emulation splits into two parts, and we treat them completely differently.

**The Xenos frontend — leave alone.** The PM4 command processor, the EDRAM model, the
register state machine, the shader translator's front half. This is where years of
accumulated emulation accuracy lives. We touch it as little as possible.

**The graphics backend — replace.** This is the part that talks to an actual GPU API.
Xenia has both a D3D12 and a Vulkan backend behind the same frontend, which is precisely
why this is feasible: we are **swapping a backend that already exists**, not writing one
from nothing.

```
         Xenos frontend  (keep — accuracy lives here)
                 │
      ┌──────────┴──────────┐
      ▼                     ▼
  D3D12 backend        Vulkan backend
  (upstream uses)      (we use)
```

This is what makes D1 a tractable decision rather than a wish. It also focuses the
project's main risk into one answerable question — **Q1: does Xenia's Vulkan backend
cover the Xenos features Dragon Age: Origins actually uses?** — which is why answering it
is scheduled in Phase 0.4, before any port code is written.

### The simplification available to us

Xenia must be correct for the whole 360 library. **We only need one game to be correct.**

That is a genuine and significant advantage, and the main mitigation for risk R2
(EDRAM emulation being too slow on a mobile tiler). If accurate EDRAM emulation proves
intractable on Adreno, we can fall back to an approximation that is correct *for Dragon
Age: Origins* and wrong elsewhere. Xenia cannot take that shortcut; we can.

---

## 5. Mobile GPU considerations

Adreno is a **tiling** GPU. Desktop-oriented Vulkan code runs on it but often badly,
and the differences are structural rather than a matter of tuning:

- **Render passes matter enormously.** Tilers want few, large render passes with
  explicit load/store intent. Needlessly storing an attachment that is about to be
  recomputed, or loading one that is about to be fully overwritten, costs real bandwidth.
- **Bandwidth is the binding constraint**, not raw shader throughput. The 360's EDRAM
  existed for exactly this reason, which makes the EDRAM emulation path the most
  bandwidth-sensitive part of the renderer.
- **Unified memory.** No discrete VRAM, so the host/device transfer strategy that suits
  a desktop GPU is wrong here. VMA needs configuring for a unified heap (task 3.7).
- **Format support differs** from desktop. Some Xenos formats that reinterpret cleanly
  on desktop will need conversion on Adreno (task 3.5).

Addressed in tasks 3.7 and 3.8.

---

## 6. Threading and core topology

The Thor has three CPU clusters and **which core a thread lands on matters a great
deal**:

| Cores | Model | Use |
|---|---|---|
| 7 | Cortex-X3 | **Game main thread** — pin here |
| 3–6 | A715 / A710 | Render thread, audio, streaming |
| 0–2 | A510 | OS and background only — **keep game threads off these** |

The 360's Xenon was a 3.2 GHz in-order triple-core PowerPC. Static recompilation removes
all interpretation overhead, so the X3 should comfortably exceed original single-thread
throughput — *provided the scheduler doesn't migrate the main thread onto an A510*,
which will visibly tank framerate. Explicit affinity is task 4.8.

---

## 7. Presentation path

```
  game renders  ──►  internal render target
                          │
                          ├─► optional: FSR 3.1 (default off, D7)
                          ├─► optional: FSR 1 + sharpen (heavy-scene fallback)
                          ├─► MSAA / SMAA resolve
                          ▼
                     tonemap / colour transform
                          │
            ┌─────────────┴─────────────┐
            ▼                           ▼
   Display-P3 SDR 10-bit         HDR10 PQ (default off, D8)
   ** the default **             ST2084 + VK_EXT_hdr_metadata
            └─────────────┬─────────────┘
                          ▼
                 Vulkan swapchain
                          │
                 Swappy frame pacing   ◄── mandatory, not optional
                          ▼
                 display 0 @ 60 or 120 Hz
```

Both panels already run in **Display-P3**, so colours must be authored and tagged for P3
rather than assumed sRGB — otherwise everything shifts, including the deck's brass tones.

Swappy is in Phase 4.7 rather than Phase 10 on purpose: a 60 fps game presented without
pacing on a 120 Hz panel judders badly enough to feel worse than the 360 original, no
matter what the framerate counter says.

---

## 8. Second screen isolation

The deck is **strictly non-critical**. Architecturally enforced, not just intended:

- The state mirror writes a **double-buffered snapshot**; the deck reads the stable side.
- The deck **pulls** at its own cadence. It is never called from the game thread.
- No lock exists that the game thread can block on.
- Deck failure closes the deck, not the game.
- Deck runs at 10–15 Hz, not 60 — quest text does not need 60 Hz, and the power saved
  matters on a handheld.

Phase 8 is read-only by design ([D6](DECISIONS.md#d6--read-only-mirror-first)). The
single sanctioned write path is the touch quickbar (task 8.8), added only after the
read-only mirror is proven, and tested on its own.

Design detail in [`SECOND-SCREEN.md`](SECOND-SCREEN.md).

---

## 9. Repository layout

```
├── STATUS.md              living handoff - read first, update every session
├── RESUMING.md            setup, the device, the daily loop, the next tasks
├── README.md              the user's manual
├── THIRD_PARTY.md         the pinned upstream sources
├── docs/
│   ├── PLAN.md            authoritative phase and task breakdown
│   ├── ARCHITECTURE.md    this file
│   ├── DECISIONS.md       decision log with rationale
│   ├── BUGS.md            every bug found, with its cause
│   ├── IMPORTS.md         the kernel and XAM imports and where each is implemented
│   ├── TESTING.md         device loop and test strategy
│   ├── TOOLCHAIN.md       build environment setup
│   ├── SIGNING.md         the debug and release keys
│   ├── ASSETS.md          disc formats, verification, pipeline
│   ├── GRAFT.md, EDRAM.md, PROBE-*.md   the GPU backend's graft and early probes
│   ├── GLYPHS.md, SECOND-SCREEN.md, FEATURES.md, AREAS.md
│   └── images/
├── device/                measured AYN Thor capabilities
├── tools/                 recompiler, asset, build, release and device tooling
├── app/                   the Android app: Java launcher and deck, the native runtime
│   └── src/main/assets/gamecode/   the recipe the device makes the game's code with
├── recomp/generated/      XenonRecomp output on a developer's computer - gitignored
└── third_party/           pinned upstream sources - gitignored (the bootstrap fetches them)
```
