# Vendored upstream sources

`third_party/` is gitignored — these are fetched, not committed. Pin them to the exact
commits below so a build on another machine reproduces the same output, and so an
upstream change can never silently alter a recompilation.

Pinned **2026-10-22** (Phase 0.4).

| Project | Commit | Date | Role |
|---|---|---|---|
| [96dot/Dragon-Age-Origins-Recomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) (DAORecomp; it was 96dot/DAORecomp, which still redirects) | `eb2360a435da409082a0ed82acdfb0f4b7b3fbab` | 2026-10-29 | Recompiler config, function database, game patches, vendored Xenia subset. **The irreplaceable piece.** Moved from `1a6c668` (2026-10-21) on 2026-10-30 for its fixes: the Aeducan Thaig switch (config), the pipeline storage's seek (its Xenia subset), and the Ostagar load crash (a game hook, ported into `warden_game.cc`) |
| [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp) | `ddd128bcca99fe8bfbb99bea583c972351fa6ace` | 2025-08-04 | PowerPC → C++. Routes ARM64 through SIMD Everywhere. **Same commit DAORecomp pins.** Patched in place by DAORecomp's `patch_recompiler.py`, then by ours, `tools/patch_xenonrecomp.py` (PLAN 2.4: `blrl` and the Xenos half-float packs it left as traps). |
| [xenia-canary/xenia-canary](https://github.com/xenia-canary/xenia-canary) | `1c81b867473806b2845a1946b2d185c9d888a319` | 2026-10-10 | **Source of the Vulkan backend**, SPIR-V shader translator and `ui/vulkan/` presentation layer. **The commit DAORecomp's vendored Xenia subset came from** — see below. |

### Why this exact Xenia commit

DAORecomp's `installer/deps.py` pins xenia-canary at `1c81b867`, and the Xenia code it
vendors in-tree was taken from that commit. Grafting the Vulkan backend from a
*different* commit would mean reconciling two sets of changes at once: ours and
upstream Xenia's drift.

This was measured. Against the latest canary at the time (`e6cd2e4`), DAORecomp's
frontend showed **2.4%** divergence; against the pinned `1c81b867` it drops to
**2.1%**, with 226 of 256 files byte-identical rather than 220. Small, but free —
match the pin.

Re-check at any time with `python tools/probe_divergence.py`.

### XenosRecomp is not used

[hedge-dev/XenosRecomp](https://github.com/hedge-dev/XenosRecomp) was vendored
initially and then dropped: **DAORecomp does not use it.** Grepping the whole
repository finds no reference, and it is absent from `installer/deps.py`'s pinned
sources.

DAORecomp translates the game's shaders through **Xenia's runtime shader translator**
(`xenia/gpu/dxbc_shader_translator*`), not ahead-of-time through XenosRecomp the way
Unleashed Recompiled does. For this port that is good news — it means the shader work
is the same backend swap as everything else (`dxbc_shader_translator` →
`spirv_shader_translator`, both of which Xenia ships) and belongs in Phase 3 rather
than being a separate ahead-of-time pipeline in Phase 1.

## Fetching them

**`python tools/bootstrap.py` does this** (step 2), at the pinned commits with every
submodule below, and skips a repository that is already at its pin. It is the way to
set up a new machine (`RESUMING.md`). By hand, the same thing, for each project:

```bash
mkdir -p third_party/<name> && cd third_party/<name>
git init -q && git remote add origin <url>
git fetch -q --depth 1 origin <commit> && git checkout -q FETCH_HEAD
git submodule update --init --depth 1 <submodules...>   # retry without --depth if one refuses
```

The submodules: **DAORecomp** none. **XenonRecomp** `thirdparty/fmt`,
`thirdparty/libmspack`, `thirdparty/simde`, `thirdparty/tiny-AES-c`,
`thirdparty/tomlplusplus`, `thirdparty/xxHash` - what its CMake build needs (SIMD
Everywhere among them). **xenia-canary** the fourteen in the table at the end.

`git init` plus a fetch of the commit, rather than `git clone --recurse-submodules=no`,
because that clone option sets `submodule.active=no` and makes the next `git submodule
update` a silent no-op (BUG-008). XenosRecomp was cloned at first and is not used
(above), so it is no longer fetched.

Approximate sizes: DAORecomp 26 MB, XenonRecomp 1.4 MB plus its submodules,
xenia-canary 103 MB plus its submodules.

## Why Xenia Canary rather than mainline

Canary is the actively developed line and the one prioritising Vulkan. Its Vulkan
backend is structurally complete and at parity with its D3D12 backend — verified in
[`docs/PROBE-Q1.md`](docs/PROBE-Q1.md). Xenia Edge is a further fork focused even more
narrowly on Vulkan and Linux; worth revisiting if Canary's Vulkan path disappoints
during Phase 3.

## Licences

**This repository and the app: GPL-3.0-or-later** ([`LICENSE`](LICENSE); PLAN 11.3, the
owner's choice of 2026-11-02 for sharing it in a public repository). lsfg-vk is GPL-3.0
and compiled into the app, so the app as a whole is GPL-3.0, and whoever gets a build
can get its complete source. Every other project built in has a licence GPL-3.0 can
include: MIT, BSD 2- and 3-Clause, ISC, BSL-1.0, Apache-2.0 (glslang's parts,
Vulkan-Headers; LLVM's, with the LLVM exception, since PLAN 12), LGPL-2.1 (FFmpeg, libmspack: section 3 lets LGPL-2.1 code be used under
the GPL), the Unlicense and public domain. UnRAR could not be (its licence forbids using
the code to make a RAR-compatible archiver, a restriction GPL-3.0 does not allow), so .rar
imports use libarchive's readers (BSD 2-Clause) since 11.3.

| Project | Licence |
|---|---|
| DAORecomp | MIT |
| XenonRecomp | MIT |
| Xenia (and the subset DAORecomp vendors) | BSD 3-clause |
| LLVM (clang and lld in the APK, PLAN 12) | Apache-2.0 WITH LLVM-exception |

None of them ship game content, and neither does this repository. See
[`docs/ASSETS.md`](docs/ASSETS.md).

## Xenia submodules fetched for the Vulkan graft

Phase 3.1. DAORecomp fetches only the submodules its Direct3D build needs, so the
Vulkan-related ones had to be added. All are at the commits **xenia-canary itself
records**, so they match the pinned Canary checkout:

| submodule | commit | needed for |
|---|---|---|
| `Vulkan-Headers` | `2cd90f9d20df57eac214c148f3aed885372ddcfe` | `vulkan.h`, Vulkan-Hpp |
| `VulkanMemoryAllocator` | `1d8f600fd424278486eade7ed3e877c99f0846b1` | GPU memory allocation |
| `glslang` | `a57276bf558f5cf94d3a9854ebdf5a2236849a5a` | SPIR-V shader translator |
| `SPIRV-Tools` | `33e02568181e3312f49a3cf33df470bf96ef293a` | SPIR-V validation/disassembly |
| `FidelityFX-CAS` | `9fabcc9a2c45f958aff55ddfda337e74ef894b7f` | presenter's sharpening filter |
| `FidelityFX-FSR` | `a21ffb8f6c13233ba336352bdff293894c706575` | presenter's FSR output filter |
| `fmt` | `407c905e45ad75fc29bf0f9bb7c5c2fd3475976f` | formatting, used throughout |
| `cxxopts` | `f029892dab60526184ee42ae63a7f6d19a6b0e49` | reached via `base/cvar.h` |
| `date` | `f94b8f36c6180be0021876c4a397a054fe50c6f2` | reached via `base/chrono.h` |
| `disruptorplus` | `bf9c70c0cc92b6435f7edec75e12751ea37e5c2f` | base threading |
| `tomlplusplus` | `708fff700f36ab3c2ab107b984ec9f3b8be5f055` | config |
| `utfcpp` | `65701fe00700577f9f4e9b96d682bd7cfcc0b76e` | string handling |
| `xxhash` | `e626a72bc2321cd320e953a0ccf1584cad60f363` | hashing |
| `snappy` | `6af9287fbdb913f0794d0148c6aa43b58e63c8e3` | trace compression |
| `FFmpeg` | `d980192e175e6ff95bcd287af77e16fcb6597974` | has207's fork (branch `xmaframes`), the XMA decoder's codec - the subset DAORecomp's `xenia/ffmpeg.cmake` builds, configured by xenia-canary's own `third_party/ffmpeg-xenia` (task 4.5, session 32) |

```bash
cd third_party/xenia-canary
# a clone made with --recurse-submodules=no sets submodule.active=no, which makes
# the next command a silent no-op. Clear it first (bootstrap.py's git init avoids it).
git config --local --unset submodule.active
git submodule update --init --depth 1 \
    third_party/Vulkan-Headers third_party/VulkanMemoryAllocator \
    third_party/glslang third_party/SPIRV-Tools \
    third_party/FidelityFX-CAS third_party/FidelityFX-FSR \
    third_party/fmt third_party/cxxopts third_party/date \
    third_party/disruptorplus third_party/tomlplusplus \
    third_party/utfcpp third_party/xxhash third_party/snappy
```

**Not needed, deliberately:** `imgui` (only for the Phase 10 settings overlay),
`DirectX-Headers` and `DirectXShaderCompiler` (the D3D12 backend is not built for
Android), `xbyak` (x86 code generation), `SDL2`, `libusb`. (`FFmpeg` was on this
list until the XMA decoder needed it.)

No Vulkan SDK is required. The Android NDK ships `glslc`, `spirv-opt` and `spirv-dis`,
which is what `tools/build_xenia_shaders.py` uses.

## Still to vendor

Not yet fetched. (SIMD Everywhere comes with XenonRecomp's submodules and
VulkanMemoryAllocator with xenia-canary's, both above.)

| Project | Purpose | Phase |
|---|---|---|
| [Swappy / AGDK](https://developer.android.com/games/sdk/frame-pacing) | Frame pacing — mandatory, not optional | 4 |
| [Oboe](https://github.com/google/oboe) | Audio fallback behind AAudio | 4 |
| [Dear ImGui](https://github.com/ocornut/imgui) | Settings overlay - not needed: 10.1's options panel is Android views | 10 |
| [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) | Frame generation with Lossless Scaling's shaders (PLAN 9.14) | 9 |

**lsfg-vk: v2.0.0-dev, `0213be9ff93121283ad46a27119f40ece0e6f301` (2026-08-27), GPL-3.0** -
the newest version whose licence allows this port. Its licence history: v1 (v1.0.0,
`7113d7d0`, 2025-07-30) MIT; the v2 development GPL-3.0, to the v2.0.0-dev tag (the archived
GitHub repository's one commit after it, `0af010d`, a notice of the move, has no licence
file; the archive is also at git.lsfg-vk.dev/lsfg-vk-archive); v2.0.0, released from https://lsfg-vk.dev, CC BY-NC-ND
4.0, which forbids derivative works - and an Android port of it is one. GPL-3.0 code may
be modified freely; distributed, the app carries GPL-3.0's terms: the app is
GPL-3.0-or-later and its source is offered with it (PLAN 11.3). lsfg-vk carries none of Lossless Scaling's code: it reads the shaders from
`Lossless.dll` of the user's own copy of Lossless Scaling at run time. **That DLL is the
user's to supply** (into the app's files folder), is never committed (`.gitignore`) and
never bundled with the app.

**In the build (session 36):** `tools/bootstrap.py` fetches `lsfg-vk-common` and
`lsfg-vk-backend` at the pin (sparse) into `third_party/lsfg-vk`; `tools/prepare_lsfg.py`
copies them to `build/lsfg-vk` with seven recorded edits to the shared layer (its
externally managed device's queue index and 16-bit choice; a command buffer that wraps
the game's, session 37); the app compiles the shared
layer's Vulkan wrappers and the backend's DLL reader, shader registry and shader chains
(`warden_lsfg` in `app/src/main/cpp/CMakeLists.txt`). The backend's `src/lsfgvk.cpp` is
not compiled: `app/src/main/cpp/warden_lsfg.cc` takes its place - its context adapted to
run on the game's own device and record into the game's command stream - and is
GPL-3.0-or-later like what it adapts (its header says so).

## The FidelityFX SDK (FSR 3.1, PLAN 9.4)

| | |
|---|---|
| Source | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK |
| Pin | **v1.1.4**, `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55` - its `sdk` folder only (a sparse, blob-filtered checkout, 216 MB), `python tools/bootstrap.py` fetches it |
| Licence | MIT |
| Built | the FSR 3.1 upscaler, frame interpolation and optical flow (FSR 3.1's frame generation, PLAN 9.14) and the Vulkan backend from source (`app/src/main/cpp/CMakeLists.txt`, `warden_ffx`), with `ffx_compat/warden_ffx_compat.h` for the Microsoft C runtime calls; frame interpolation's `ffx_frameinterpolation.cpp` from the copy `tools/build_fsr3_shaders.py` writes into `build/fsr3-shaders/frameinterpolation/`, its optical flow pass dispatched over the flow's own buffer (the flow runs at the render size here) |
| Not used | the SDK's own shader compiler (`sdk/tools/binary_store/FidelityFX_SC.exe` and the `glslangValidator.exe` beside it: downloaded executables, not run). `tools/build_fsr3_shaders.py` does its work with the NDK's `glslc`: every pass, every permutation, and the reflection the host code binds by, into `build/fsr3-shaders` |

## In the app: licences (PLAN 11.5)

The app's About and licences page shows every project above whose code is built into the
app, with its full licence text: `python tools/gather_licences.py` copies them from the
pinned checkouts into `app/src/main/assets/licences/` (the bootstrap's step 6 runs it;
`--check` compares). A new dependency built into the app gets an entry there.

## Mod archives (.7z and .rar, PLAN 10.19)

| | 7-Zip's C decoder | libarchive |
|---|---|---|
| Source | https://github.com/ip7z/7zip | https://github.com/libarchive/libarchive |
| Pin | tag **26.05**, `b88c6bc09339de32f7d18bbab900686b244f42ba` - its `C` folder only (sparse) | tag **v3.8.11**, `03036ffa409a2aa1c5bbad35be2202641a040fda` - its `libarchive` and `contrib/android` folders (sparse) |
| Licence | the files built (7z*, Bra*, Bcj2, Delta, Lzma*, Ppmd7*, CpuArch) are public domain | BSD 2-Clause |
| Built | `warden_archives` in `app/src/main/cpp/CMakeLists.txt`, extract only (Z7_EXTRACT_ONLY, PPMd on) | `warden_libarchive`: its read side, configured by its own `contrib/android/config/android.h`, unmodified - a static library, so only the RAR 4 and RAR 5 readers and what they reach are linked |

Both fetched by `python tools/bootstrap.py` into `third_party/7zip` and
`third_party/libarchive`; `app/src/main/cpp/dao_archive_android.cc` calls them.
**UnRAR** (7.3.2, RARLAB's licence) read .rar from 10.19 until 11.3: its licence forbids
using the code to make a RAR-compatible archiver, a further restriction GPL-3.0 does not
allow, so a build that included it could not be shared (BUG-094). libarchive's readers
unpack the same archives (the No Helmet Hack .rar: its .dazip byte-identical to what Windows' own tar.exe unpacks).

Note the vendor SDKs DAORecomp uses for DLSS (NVIDIA Streamline), XeSS (Intel) and
neural upscaling (AMD RDNA 4) are **deliberately not listed** — they are desktop
vendor-specific and unportable to this target. See
[`docs/PROBE-Q1.md`](docs/PROBE-Q1.md) §4.

## The compiler on the device (LLVM, PLAN 12)

| | |
|---|---|
| Source | https://github.com/llvm/llvm-project |
| Pin | **llvmorg-19.1.7**, `cd708029e0b2869e80abe31ddb175f7c35361f90` - its `llvm`, `clang`, `lld`, `cmake`, `third-party` and `libunwind` folders (a sparse, blob-filtered checkout), fetched by `python tools/build_device_toolchain.py` |
| Licence | Apache-2.0 WITH LLVM-exception |
| Built | for Android arm64, as one program (LLVM's multicall driver, `bin/llvm`) holding clang and lld and nothing else, the AArch64 target only, with clang's own headers: `build/device-toolchain`, 89.8 MB stripped. LLVM's tablegen tools are built for the computer first (clang-cl against `toolchain/winsysroot`, `tools/cmake/clang-cl-xwin-host.cmake`) |
| Why 19.1.7 | the major version of the NDK r28c clang (19.0.1) the generated code has been compiled and tested with since Phase 2 |

The published APK makes the game's code on the device (PLAN 12): XenonRecomp - built for
Android from the same patched sources as the computer's (`build/device-toolchain/bin/
xenonrecomp`) - translates the player's own executable, and this compiles it.
