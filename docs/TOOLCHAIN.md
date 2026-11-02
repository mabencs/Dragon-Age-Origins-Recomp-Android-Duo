# Toolchain setup

Everything needed to build this project, and how to get a new machine to parity.
Current state of *this* workstation is tracked in [`../STATUS.md`](../STATUS.md) §7.

---

## 1. Required

| Tool | Version | Purpose |
|---|---|---|
| Python | 3.11+ | All the tooling in `tools/`, `tools/bootstrap.py` first |
| git | 2.40+ | Version control, and fetching `third_party/` |
| CMake (on PATH) | 3.28+ (4.4 here) | Building XenonRecomp on the host |
| Ninja (on PATH) | 1.11+ | Build driver for XenonRecomp |
| LLVM / Clang | 23 here, at `C:\Program Files\LLVM` | `clang-cl` builds XenonRecomp against the MSVC sysroot |
| Android NDK | **exactly 28.2.13676358** | ARM64 cross-compilation; ships its own Clang 19 (`app/build.gradle.kts` pins it) |
| Android SDK | platform **36**, build-tools **36.1.0**, CMake **3.31.6**, platform-tools | APK build (`compileSdk 36`, `minSdk`/`targetSdk 33`) |
| JDK | 17 (Temurin) | Gradle 9.8.2 / AGP 9.4.1 |
| adb | from platform-tools | Device deployment and testing |

Gradle itself is not installed: the committed wrapper (`gradlew`, `gradlew.bat`)
downloads 9.8.2 and checks its SHA-256 (`gradle/wrapper/gradle-wrapper.properties`).
The MSVC CRT and Windows SDK headers XenonRecomp builds against are fetched by
`tools/setup_winsysroot.py` (xwin) into `toolchain/winsysroot` - no Visual Studio.

Upstream DAORecomp uses Clang 22. Match it if practical — the recompiled output is large
and unusual, and codegen differences between Clang versions have been known to matter for
this kind of generated code. Record the exact version used in `STATUS.md` §4 so builds
stay reproducible across machines.

---

## 2. Current workstation state — complete

Installed and verified 2026-10-22 (Phase 0.3). These are the exact versions this
project is known to work with; pin them when setting up another machine.

| Tool | Version | Location |
|---|---|---|
| Python | 3.14.6 | on PATH |
| git | 2.54.0 | on PATH, user `mabencs` |
| CMake | **4.4.4** | winget packages dir, on PATH |
| Ninja | **1.13.2** | winget packages dir, on PATH |
| LLVM / Clang (host) | **23.1.4** | `C:\Program Files\LLVM\bin` |
| JDK | **Temurin 17.0.20.101** | `C:\Program Files\Eclipse Adoptium\jdk-17...` |
| Android SDK platform | **36** | `%LOCALAPPDATA%\Android\Sdk\platforms` |
| Android build-tools | **36.1.0** | SDK `build-tools` |
| SDK CMake | **3.31.6** | SDK `cmake` (Gradle's; the 4.x on PATH is for the host build) |
| **Android NDK** | **28.2.13676358** (Clang **19.0.1**) | SDK `ndk\28.2.13676358` |
| platform-tools / adb | **37.0.1** | SDK `platform-tools` |
| Android CLI (`android`) | 1.0.16625370 | winget `Google.AndroidCLI` |
| AYN Thor | over USB or Wi-Fi (`RESUMING.md` §3.1) | `adb devices` |

Environment variables set at **user** scope: `ANDROID_HOME`, `ANDROID_SDK_ROOT`,
`ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, `JAVA_HOME`. PATH extended with CMake, LLVM,
JDK `bin`, `platform-tools` and `cmdline-tools\latest\bin`.

### Why NDK 28.2 and not 29 or 30

Decision [D9](DECISIONS.md). The SDK repository lists NDK 29.0.x and 30.0.x, but
almost all of those are release candidates (`-rc.1` … `-rc.4`). `28.2.13676358` is the
newest *stable patch* release, which matters for a codebase that generates well over a
hundred thousand functions of machine-translated C++. Revisit if ARM64 codegen turns
out to need a newer Clang.

### Verified working

Not assumed — a C program was cross-compiled for `arm64-v8a` / API 33, pushed to the
Thor and executed, producing the capability report in
[`../device/vulkan-capabilities.txt`](../device/vulkan-capabilities.txt). Reproduce with:

```bash
./tools/vkprobe/run.sh
./tools/deploy.sh check
```

## 2b. Platform gotchas

Each of these cost real time to discover; don't rediscover them.

- **CMake is 4.x.** It rejects `cmake_minimum_required(VERSION <3.5)`. Upstream
  CMakeLists files may need `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` or similar.
- **Host Clang is 23, DAORecomp expects 22.** Close enough, but note it if generated
  code misbehaves.
- **The new Android CLI replaced `sdkmanager`.** Package IDs are now **slash**-separated:
  `android sdk install ndk/28.2.13676358`, *not* the old `sdkmanager "ndk;28.2..."`.
  The legacy `sdkmanager.bat` shim silently reports "Package not found" for old syntax.
- **Git Bash path mangling.** The NDK toolchain and `adb` are native Windows binaries
  and cannot read MSYS `/c/...` paths — convert with `cygpath -w`. Also export
  `MSYS2_ARG_CONV_EXCL='*'` so MSYS stops rewriting `adb shell` arguments.
- **`screencap -d` wants the PHYSICAL display id** (e.g. `4630946482288158084`), not
  the logical id (`4`) that `dumpsys display` and `am start --display` use.
  `tools/deploy.sh` resolves it by hardware port (131 = main, 132 = deck).
- **A sleeping panel screenshots empty.** Wake the device first.
- **`git clone --recurse-submodules=no` sets `submodule.active=no`** in that clone's
  config, which makes every later `git submodule update --init` a **silent no-op**:
  no output, exit 0, nothing fetched, and `git submodule status` still showing `-`.
  Clear it with `git config --local --unset submodule.active` before initialising.
  This cost a confusing CMake failure about missing `thirdparty/fmt`.
- **XenonRecomp needs its output directory to exist**, and resolves every TOML path
  against the **config file's** directory. Getting either wrong aborts with
  `0xC0000409` and no message. `tools/recompile.py` handles both.

---

## 3. Install — Windows

```powershell
winget install Python.Python.3.14
winget install Git.Git
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install LLVM.LLVM
winget install EclipseAdoptium.Temurin.17.JDK
winget install Google.AndroidCLI
```

The SDK pieces, with the Android CLI (no Android Studio needed; accept the licences when
asked). The ids are **slash**-separated, as `android sdk list` prints them:

```powershell
android sdk install platforms/android-36 build-tools/36.1.0 cmake/3.31.6 ndk/28.2.13676358 platform-tools
```

### Environment variables

```powershell
[Environment]::SetEnvironmentVariable('ANDROID_HOME', "$env:LOCALAPPDATA\Android\Sdk", 'User')
[Environment]::SetEnvironmentVariable('ANDROID_SDK_ROOT', "$env:LOCALAPPDATA\Android\Sdk", 'User')
[Environment]::SetEnvironmentVariable('ANDROID_NDK_HOME', "$env:LOCALAPPDATA\Android\Sdk\ndk\28.2.13676358", 'User')
[Environment]::SetEnvironmentVariable('JAVA_HOME', 'C:\Program Files\Eclipse Adoptium\jdk-17.0.20.101-hotspot', 'User')
```

Adjust the JDK folder to whatever got installed, add `%ANDROID_HOME%\platform-tools` to
PATH for `adb`, and open a **new** shell afterwards: one started before the variables
were set does not have them.

### Verify

```powershell
cmake --version; ninja --version; clang --version; javac -version; adb version
python tools/verify_assets.py .
```

---

## 4. Install — Linux

```bash
sudo apt install cmake ninja-build clang lld python3 openjdk-17-jdk
# Android SDK/NDK: use sdkmanager from the command-line tools package
sdkmanager "platforms;android-33" "ndk;27.0.12077973" "build-tools;34.0.0"
```

The host-side recompilation (Phase 1) is platform-agnostic. Only the APK build needs the
Android SDK.

---

## 5. Vendored upstream

Phase 0.4. These live in `third_party/` and are gitignored — fetch rather than commit,
and **pin exact commits** so a rebuild on another machine produces the same output.

| Project | Why we need it |
|---|---|
| [`96dot/DAORecomp`](https://github.com/96dot/DAORecomp) | **The irreplaceable part** — its `config/` TOML holds the function database and game patch set for this exact executable. Also `DAORecompLib`. |
| [`hedge-dev/XenonRecomp`](https://github.com/hedge-dev/XenonRecomp) | PowerPC → C++. Routes ARM64 through SIMD Everywhere. |
| Xenia | Xenos GPU frontend **and its Vulkan backend** — the basis of Phase 3; shaders are translated at run time by its SPIR-V translator, so XenosRecomp is not used |
| [SIMD Everywhere](https://github.com/simd-everywhere/simde) | x86 intrinsics → NEON, a XenonRecomp submodule |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Vulkan memory management, a xenia-canary submodule |
| [Swappy / AGDK](https://developer.android.com/games/sdk/frame-pacing) | Frame pacing — mandatory (task 4.7) |
| [Oboe](https://github.com/google/oboe) | Audio fallback behind AAudio |
| [Dear ImGui](https://github.com/ocornut/imgui) | In-game settings overlay (task 10.1) |
| [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | FSR 1 and FSR 3.1 |

The pinned commits, and every submodule fetched, are in
[`../THIRD_PARTY.md`](../THIRD_PARTY.md); `tools/bootstrap.py` fetches them. The last
four rows are not vendored yet.

### Note on upstream licences

DAORecomp, XenonRecomp and XenosRecomp are MIT. Xenia's vendored GPU/UI code is BSD.
None of them ship game content, and neither do we — see
[`ASSETS.md`](ASSETS.md).

---

## 6. What this project does *not* use

Worth recording, because both look promising at first glance and neither is usable.
Researched 2026-10-22.

| Project | Why not |
|---|---|
| [`deivid22srk/ForzaHorizon2Recomp-Android`](https://github.com/deivid22srk/ForzaHorizon2Recomp-Android) | **Scaffold, not a working port.** Produces an installable APK with a setup screen and native status, but gameplay is blocked on kernel-import and Vulkan/SPIR-V backend work, with no ETA. Its Android shell patterns (SAF, JNI bridge, gamepad abstraction) are worth *reading* as reference. |
| [`nimauria/Xenon-Recomp`](https://github.com/nimauria/Xenon-Recomp) | **ARM64 is a placeholder directory.** Its own docs state no ARM64 codegen, CI or building has occurred, and bring-up is deferred until a title runs end-to-end on x86-64. No confirmed playable titles at all. |

The useful conclusion from both: **nobody has shipped a working Xbox 360 recomp on
Android yet.** This project is not following a trodden path, which is why the Phase 0.4
risk probe matters so much.

---

## 6b. MSVC CRT and Windows SDK (no Visual Studio)

Building the host recompiler on Windows needs Microsoft's C runtime headers/libraries
and the Windows SDK. We have LLVM with `clang-cl`, `lld-link`, `llvm-lib` and
`llvm-rc`, which is what upstream accepts in place of Visual Studio — only the
Microsoft headers and import libraries are missing.

```bash
python tools/setup_winsysroot.py --accept-license
python tools/setup_winsysroot.py --check        # report state, change nothing
```

This mirrors DAORecomp's own installer: it downloads **xwin 0.10.0** (sha256 verified)
and splats **MSVC CRT 14.44.17.14** + **Windows SDK 10.0.26100** — the versions
upstream pins — into `toolchain/winsysroot/`. About 85 MB.

Those headers and libraries are Microsoft's, under
[their license](https://go.microsoft.com/fwlink/?LinkId=2086102); running the script
accepts it, exactly as installing Visual Studio would. `toolchain/` is gitignored.

## 7. Build commands

### Phase 1 — host-side recompilation (one time per game revision)

`python tools/bootstrap.py` runs all of this, in order, and every step after it up to
the APK (§8). The commands, for when one step needs running by hand:

```bash
# 1. Extract the executable from your disc
python tools/dao_iso.py extract "<your>.iso" default.xex \
    third_party/DAORecomp/game/default.xex

# 2. Extract Title Update 7's patch from its LIVE package (the developer's build is made
#    from TU7; the app on a player's device takes TU6 or TU7)
python -I third_party/DAORecomp/scripts/stfs.py extract "<TU package>" work/tu7
cp work/tu7/default.xexp third_party/DAORecomp/title_update/default.xexp

# 3. Apply DAORecomp's patch to XenonRecomp (adds the PPC/VMX128 instructions DAO
#    needs; idempotent, keeps .orig backups)
python -I third_party/DAORecomp/scripts/patch_recompiler.py \
    third_party/XenonRecomp/XenonRecomp/recompiler.cpp
#    ...then this project's own edits after it (six more instructions, PLAN 2.4; the
#    tu6_ names written by XenonRecomp itself, PLAN 12.4)
python tools/patch_xenonrecomp.py

# 4. Build XenonRecomp with clang-cl against the winsysroot (setup_winsysroot.py
#    --accept-license fetches it). The toolchain path must be absolute: CMake reads a
#    relative one against the build and source trees, not the working directory.
cmake -G Ninja -S third_party/XenonRecomp -B build/xenonrecomp -Wno-dev \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/third_party/DAORecomp/cmake/clang-cl-xwin.cmake" \
  -DDAO_LLVM_BIN="C:/Program Files/LLVM/bin" \
  -DDAO_WINSYSROOT="$PWD/toolchain/winsysroot"
ninja -C build/xenonrecomp XenonRecomp

# 5. Translate PowerPC to C++ (verifies input hashes first)
python tools/recompile.py --check
python tools/recompile.py both          # or: tu / disc
```

### Phase 1.5 / Phase 2 — compile the generated code

```bash
python tools/compile_check.py --target host       # x86-64, the sanity build
python tools/compile_check.py --target android    # arm64-v8a via the NDK
python tools/compile_check.py --target both
python tools/compile_check.py --target android --limit 10   # quick check
```

Compile-only, never linked: the point is to find code the compiler rejects. Objects
land in `build/compile-check/`.

### The app

```bash
./gradlew :app:assembleDebug     # libwarden.so and, beside it, libdaogame.so (never packed)
python tools/push_gamecode.py    # the computer-built libdaogame.so onto the device
./gradlew :app:assembleRelease   # the published APK; then python tools/release.py
```

Gradle runs CMake on `app/src/main/cpp/CMakeLists.txt` with the NDK. Configuring stops
when the game code's recipe (`app/src/main/assets/gamecode/`) is stale against the
runtime's overrides: `python tools/gen_gamecode_recipe.py` writes it again.

---

## 8. New machine checklist

```bash
git clone <repository URL> DragonAgeOriginsAndroid
cd DragonAgeOriginsAndroid

# 1. Install §1's tools (§3), in a new shell afterwards
# 2. Copy the disc image and the TU7 package into the repo root (never committed)
python tools/verify_assets.py .
#    expect: "Ready: you have a supported disc and a supported title update."
# 3. Everything git does not hold, up to the APK - about half an hour, most of it the
#    native build (measured on the old workstation, 2026-10-25)
python tools/bootstrap.py --check                  # what is there, what is missing
python tools/bootstrap.py --accept-msvc-license    # the MSVC sysroot is Microsoft's licence
```

`RESUMING.md` is the full walk-through, including the device side. Then continue from
**Next action** at the top of `STATUS.md`.

## The device toolchain (PLAN 12)

The published APK makes the game's code on the device, with a compiler and XenonRecomp
built for Android: `python tools/build_device_toolchain.py` (after the bootstrap, and
after `python tools/game_check.py` on the owner's PC - about 25 minutes the first time).
It fetches LLVM 19.1.7 (THIRD_PARTY.md), builds LLVM's tablegen tools for this computer
with clang-cl against `toolchain/winsysroot` (`tools/cmake/clang-cl-xwin-host.cmake` - a
native build, unlike DAORecomp's toolchain file, which names the system and so makes
LLVM set up a second build of its own), then clang and lld for Android as one program,
and XenonRecomp for Android from the patched sources. Output: `build/device-toolchain/`
(`bin/llvm`, `bin/xenonrecomp`, clang's headers). `python tools/stage_gamecode_toolchain.py`
then puts them where the APK build takes them (`build/gamecode-stage/`: `libllvm.so`,
`libxenonrecomp.so`, `assets/gamecode/toolchain.zip`); the bootstrap's step 6b runs both.
