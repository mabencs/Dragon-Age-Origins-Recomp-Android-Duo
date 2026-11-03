# Resuming on another machine

Written for Claude (or anyone) starting a fresh session on a computer that has never
seen this project, with an AYN Thor plugged in. Read this file first, then the header of
[`STATUS.md`](STATUS.md) (**Next action**), then whatever §5 below points at.
[`README.md`](README.md) is the user's manual: what the app does, every setting, the
files a player supplies, mods.

`STATUS.md` is the living record of where the work is and how it got there (its §9 is
the session log). This file is how to get a new machine to the point where you can
continue, what the device holds, how the daily loop works, what comes next, and the
things that cost an hour each when nobody said them.

Brought up to date on 2026-11-02, at version 0.1.2-alpha ("Open Alpha"; 0.1.0-alpha, when the
repository went public, for most of this file): §0-§3 and §5 describe the project as it is now; §4 and §6
onwards are the loop and the lore, unchanged in substance since session 32.

---

## 0. The short version

```bash
# tools first (§3.2), then in a new shell:
git clone <repository URL> DragonAgeOriginsAndroid
cd DragonAgeOriginsAndroid
# copy the disc image and the TU7 package into this folder (§3.3)
python tools/verify_assets.py .                    # "Ready: ..."
python tools/bootstrap.py --check                  # what is there, what is missing
python tools/bootstrap.py --accept-msvc-license    # everything git does not hold, to the APK
adb install -r -d app/build/outputs/apk/debug/app-debug.apk   # NEVER uninstall (§3.1)
python tools/push_gamecode.py                      # the game's code built here (§3.5)
bash tools/boot_capture.sh <run> 60                # one boot; the title screen in the log (§3.5)
```

Then §5.

---

## 1. What this is

A port of Dragon Age: Origins (Xbox 360) to the AYN Thor Android handheld, built from
each player's own disc. Not emulation: the 360 executable (with Title Update 6 or 7
applied) is **statically recompiled** to C++ by XenonRecomp ahead of time, using
DAORecomp's function database and patches; the C++ is compiled for arm64-v8a into the
game's own library, `libdaogame.so` - on the player's device by the app (PLAN Phase 12),
or with the NDK on a developer's computer; and
Xenia's Vulkan GPU backend is grafted onto DAORecomp's Xenos frontend to render it. The
kernel and XAM imports the title calls are implemented by this project
(`app/src/main/cpp/warden_*.cc`). Beyond "it runs": controller-glyph remapping, a
second-screen companion deck on the Thor's lower panel, and the PC project's
enhancements where they port.

Design rationale: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md),
[`docs/DECISIONS.md`](docs/DECISIONS.md). Task breakdown and statuses:
[`docs/PLAN.md`](docs/PLAN.md).

How it is shared: the code is GPL-3.0-or-later and public, and so is the release APK,
because it holds none of the game: the app makes the game's program on the player's
device from their own disc (README; `docs/DECISIONS.md` D14). Only an APK that
`tools/release.py` passes is published; a developer's build with `libdaogame.so` pushed
to it stays on their own devices.

---

## 2. Where the work stands (2026-11-02, 0.1 Open Alpha)

### 2.1 What works, on the Thor

- **The whole game:** the main campaign, Awakening and every DLC from the Ultimate
  Edition's disc 2 (PLAN 10.10), from New Game; all 118 areas the game's debug menus
  reach, loaded and profiled (PLAN 10.11, `docs/AREAS.md`). Character creation with the
  name typed on the keyboard; the Mage origin played by hand, Ostagar and the Korcari
  Wilds by script, with combat and spells (Phase 6); conversations with their voices;
  saving and loading.
- **The app:** the launcher - the four file rows (disc image, disc 2, title update,
  `Lossless.dll`), the game's code made on the device from them (Phase 12), the Saves and
  Mods pages, every setting; the options panel over the
  held game (Select + Start, or three fingers); put away and back, both screens
  together; Debugging Mode's one-file reports; About and licences.
- **The picture:** 30, 45, 60 or 120 fps with frame pacing; FSR 1, CAS, FXAA; FSR 3.1;
  Lossless 2x and FSR 3.1 2x frame generation; rendering resolutions to 2560x1440; field
  of view; motion blur and depth of field; 16x anisotropic filtering; 10-bit and Vivid
  colour; HDR10; photo mode (Phase 9).
- **The second screen:** the companion deck in its own process - Party (the faces read
  back from the GPU), Quickbar (taps use the slots), Quests, Map, Tactics, Codex, Items,
  Perf (Phase 8).
- **The rest:** Xbox, Nintendo or PlayStation button prompts (Phase 7); the on-screen
  controller; PC mods with the in-app converter, add-ins installed as DLC (10.12, 10.13,
  10.19); the cheats; Spanish and Italian text; saves named on the keyboard, with
  thumbnails, exported, imported and restored (10.15, 10.20, 11.2); the autosave when
  put away (9.13).

### 2.2 What does not, yet

- **The hand playthrough to Ostagar** (Phase 6's last check) has not been done; the
  alpha's players are its testers.
- **Open bugs** (`docs/BUGS.md`): BUG-042 (rarely the frames stop), BUG-047 (rarely a
  malformed GPU packet ends the process), BUG-058 (about 11% of a core spent spinning),
  BUG-034 (startup can skip mounting the packages; not seen in many boots).
- **Deferred by the owner:** 9.15 (Lossless Scaling's LS1 upscaler) and 9.9 (texture
  packs beyond what the converter does); 3.8's Adreno render-pass work.
- **One device tested:** the AYN Thor.

### 2.3 Where the history is

What each session found and changed is `STATUS.md` §9, newest first (sessions 31 and 32,
once summarised here, are there too); each phase's detail is `docs/PLAN.md`; each bug's
story is `docs/BUGS.md`; the reasoning for each change is in its commit message.

---

## 3. Setting up the new machine

### 3.1 The Thor - read this before touching it

- **AYN Thor**, Snapdragon 8 Gen 2 (QCS8550, Adreno 740), Android 13 (API 33). USB
  debugging is on; on a new computer the Thor asks to **allow USB debugging** from it -
  accept on the device, then `adb devices` lists it. Do not hardcode its serial: the
  scripts assume one device.
- The app is **`dev.daorecompandroid.duo`**, holding the **extracted game data: 6.4 GiB**
  in `/storage/emulated/0/Android/data/dev.daorecompandroid.duo/files/gamedata`, with the
  mods beside it (`files/mods/`), and in its internal files directory
  (`/data/user/0/dev.daorecompandroid.duo/files`, reached with `adb shell run-as
  dev.daorecompandroid.duo ...`) the installed DLC, the options, and **the saves**
  (`content/00000001/`). **Never uninstall the app** (`adb uninstall`, or uninstalling
  from the launcher): that deletes all of it, and re-extraction takes the disc image, the
  file picker and about eight minutes. Install updates with `adb install -r -d`. On the
  home screen it is **DAO Debug**.
- Beside it is the **published app** (`prod.daorecompandroid.duo`, from the Releases page),
  where the owner plays their own game; it updates itself from its launcher. Development,
  tests and the scripts use the debug app only: never install over, stop or touch the
  published one or its saves.
- **The debug signing key is shared and committed** (`app/debug.keystore`, password
  `android`). An APK signed with any other key fails to install over the one on the
  Thor with `INSTALL_FAILED_UPDATE_INCOMPATIBLE` - and the only device-side fix is the
  uninstall above. `bootstrap.py` copies the key to `~/.android/debug.keystore` when
  none is there, and **stops before Gradle** if a different one is there (Android
  Studio or an earlier Gradle run makes one): then follow
  [`docs/SIGNING.md`](docs/SIGNING.md) §3. Release builds (`prod.daorecompandroid.duo`)
  are a separate app, signed with the builder's own key (§5 there).
- The disc images, the title update and `Lossless.dll` are in the Thor's `Download/`
  folder, where the launcher's file rows were pointed at them. Nothing needs them while
  the extraction is intact.
- **Whose saves are whose.** The routes make test characters of their own (Jaden is the
  routes' character). The game keeps saves for a limited number of characters, so test
  characters' saves are moved aside to `files/test-saves-moved/` when New Game says
  "Character limit reached" (`docs/TESTING.md`, "The character limit"). The other saves
  are the owner's own games: never delete, move or overwrite them.
- **Power, and adb over Wi-Fi.** The computer's USB port gives the Thor about 3.9 W;
  the game draws about 10 W, so long runs on the cable drain the battery. Put it on its
  own charger and run adb over Wi-Fi: `adb tcpip 5555` over USB, then
  `adb connect <the Thor's address>:5555` (`adb shell ip -f inet addr show wlan0` over
  USB tells the address). After a reboot the Thor listens on USB only. **Or Android's
  wireless debugging** (Developer options): it listens on a port of its own choosing,
  which `adb mdns services` lists - `adb connect` to that, once the computer is paired.
  With both cable and Wi-Fi connected, adb needs `-s`; the scripts assume one device.

### 3.2 Install the tools

Exact versions in [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md) §2; these are what the old
workstation had, and the build is known to work with them.

```powershell
winget install Python.Python.3.14        # 3.11+ works; 3.14.6 here
winget install Git.Git
winget install Kitware.CMake             # 4.x, for the host build of XenonRecomp
winget install Ninja-build.Ninja
winget install LLVM.LLVM                 # clang-cl; bootstrap looks in C:\Program Files\LLVM\bin
winget install EclipseAdoptium.Temurin.17.JDK
winget install Google.AndroidCLI         # the `android` CLI; sdkmanager is gone
```

Then the Android SDK pieces - the ids are **slash**-separated, and the versions are
exact (the NDK in particular: the recompiled code was differentially tested with its
Clang 19):

```powershell
android sdk install platforms/android-36 build-tools/36.1.0 cmake/3.31.6 ndk/28.2.13676358 platform-tools
```

User environment variables (adjust the JDK folder to what got installed), and
`%ANDROID_HOME%\platform-tools` on PATH for `adb`:

```powershell
[Environment]::SetEnvironmentVariable('ANDROID_HOME', "$env:LOCALAPPDATA\Android\Sdk", 'User')
[Environment]::SetEnvironmentVariable('ANDROID_SDK_ROOT', "$env:LOCALAPPDATA\Android\Sdk", 'User')
[Environment]::SetEnvironmentVariable('ANDROID_NDK_HOME', "$env:LOCALAPPDATA\Android\Sdk\ndk\28.2.13676358", 'User')
[Environment]::SetEnvironmentVariable('JAVA_HOME', 'C:\Program Files\Eclipse Adoptium\jdk-17.0.20.101-hotspot', 'User')
```

**Open a new shell afterwards** (and restart VS Code / Claude Code): a shell started
before the variables were set does not have them, and the JDK looks missing when it is
not. Gradle is not installed: the committed wrapper downloads 9.8.2 and checks its
SHA-256. No Visual Studio and no Vulkan SDK are needed.

Per the owner's standing rule (§8): if asked only to install tools, stop here and say
so before building anything.

### 3.3 Clone, and put the game files in place

```bash
git clone <repository URL> DragonAgeOriginsAndroid
cd DragonAgeOriginsAndroid
```

Copy into the **repo root** (all gitignored, never committed):

| File | Size | Needed |
|---|---|---|
| `Dragon Age - Origins (World) (En,Es,It).iso` | 7,838,695,424 | yes - title `454108C0`, media `186A1115` |
| `TU_12K2260_0000004000000.00000000001O1` | 3,506,176 | yes - Title Update 7, the build target |

Disc 2 and `Lossless.dll` are not the build's: they go to the device (README, "Setting
up on the device").

```bash
python tools/verify_assets.py .
# expect: "Ready: you have a supported disc and a supported title update."
```

The files may stay elsewhere: `bootstrap.py --iso <path> --tu <path>`.

### 3.4 Run the bootstrap

```bash
python tools/bootstrap.py --check                  # reports each step, changes nothing
python tools/bootstrap.py --accept-msvc-license
```

`--accept-msvc-license` accepts Microsoft's licence for the MSVC CRT and Windows SDK
headers XenonRecomp is built against (fetched by xwin, as installing Visual Studio
would); without it the run stops at step 4 and says so. Each step is skipped when its
output is already there, so it is safe to run again after fixing whatever stopped it.
Python's own lines are buffered when the output is redirected - the step headers can
appear only at the end of a log file.

What git does not hold, and what makes it:

| Ignored path | Made by (bootstrap step) | Notes |
|---|---|---|
| `third_party/DAORecomp`, `XenonRecomp`, `xenia-canary` | 2: `git init` + fetch of the pinned commit, then the submodules | pins in [`THIRD_PARTY.md`](THIRD_PARTY.md); ~700 MB with their git data (FFmpeg is 107 MB of it, since session 32) |
| `third_party/FidelityFX-SDK` | 2: the v1.1.4 commit, its `sdk` folder only (sparse, blob-filtered) | FSR 3.1 (PLAN 9.4); 216 MB |
| `third_party/lsfg-vk` | 2: v2.0.0-dev `0213be9`, `lsfg-vk-common` and `lsfg-vk-backend` only (sparse) | frame generation (PLAN 9.14), GPL-3.0 (THIRD_PARTY.md) |
| `third_party/DAORecomp/game/default.xex` | 3: `tools/dao_iso.py` from the disc image | SHA-1 `532c0ec1...` checked |
| `third_party/DAORecomp/title_update/default.xexp` | 3: DAORecomp's `stfs.py` from the TU7 package, via `work/tu7` | SHA-1 `dd5b493f...` checked |
| `toolchain/winsysroot`, `toolchain/xwin` | 4: `tools/setup_winsysroot.py --accept-license` | MSVC CRT 14.44 + Windows SDK 10.0.26100, ~85 MB |
| `build/xenonrecomp/` | 4: DAORecomp's `patch_recompiler.py`, then `tools/patch_xenonrecomp.py` (PLAN 2.4, 12.4), then CMake + Ninja with clang-cl | `XenonRecomp.exe` |
| `recomp/generated/ppc_tu6/` | 5: `tools/recompile.py tu` | the TU7 build Gradle compiles into `libdaogame.so` (the developer's own, `tools/push_gamecode.py`): 211 files, 265 MB; `--disc-build` adds `ppc` (for `check_xex.py`) |
| `build/runtime-tree/` | 6: `tools/assemble_runtime.py` | Xenia's Vulkan backend grafted onto DAORecomp, with the **223 recorded edits** |
| `build/lsfg-vk/` | 6: `tools/prepare_lsfg.py` | lsfg-vk's two folders with seven recorded edits (its externally managed device: a queue index, the 16-bit choice; its command buffer wrapping the game's); the build stops without it |
| `build/dao-convert/` | 6: `tools/assemble_dao_convert.py` | DAORecomp's PC mod converter: four of its files with 15 recorded edits (PLAN 10.13, 10.19); the build stops without it |
| `third_party/7zip`, `third_party/libarchive` | 2: 7-Zip's tag 26.05 (its C folder, sparse); libarchive's tag v3.8.11 (its libarchive and contrib/android folders, sparse) | .7z and .rar mod imports (PLAN 10.19; libarchive since 11.3, in UnRAR's place); the build stops without them. An old `third_party/unrar` can be deleted |
| `build/fsr3-shaders/` | 6: `tools/build_fsr3_shaders.py` (the NDK's `glslc`; the upscaler, frame interpolation and optical flow) | FSR 3.1's passes as the SDK's permutation headers, and its `ffx_types.h` with the context size doubled (BUG-063); the build stops without them |
| Xenia's shaders as SPIR-V headers (inside `build/runtime-tree`), `build/deps-gen` | 6: `tools/build_xenia_shaders.py` (the NDK's `glslc`, Vulkan 1.1 target), `tools/prepare_deps.py` (snappy's header, the stb unit) | |
| `build/gen/` | 8: the CMake configure runs `gen_cmake_sources.py` (the runtime's source list), `gen_import_stubs.py` (a weak stub per import) and `gen_xex_patcher.py` (XenonRecomp's patcher, trimmed for Android) | |
| `third_party/llvm-project`, `build/llvm-host`, `build/llvm-android`, `build/device-toolchain/` | 6b: `tools/build_device_toolchain.py` (LLVM 19.1.7, sparse; tablegen for this computer; clang and lld for Android as one program; XenonRecomp for Android) | what the app makes the game's code with (PLAN 12); about 25 minutes the first time |
| `build/gamecode-stage/` | 6b: `tools/stage_gamecode_toolchain.py` | `libllvm.so`, `libxenonrecomp.so` and `assets/gamecode/toolchain.zip` (6.9 MB) for the APK; a build without them works but cannot make the game's code on the device, and `release.py` refuses it |
| `local.properties` | 7: `sdk.dir=` the SDK | |
| `~/.android/debug.keystore` | 7: copied from `app/debug.keystore` if absent | §3.1 |
| `app/src/main/jniLibs/arm64-v8a/libVkLayer_khronos_validation.so` | only with `--validation-layer`: Vulkan SDK 1.4.363.0's Android release, SHA-256 checked | optional, for Vulkan debugging |
| `build/gfxr/`, and on the Thor the GFXReconstruct layer in the app's data directory and the replay app | `bash tools/gfxr.sh setup`: GFXReconstruct's Vulkan SDK 1.4.363.0 Android release, SHA-256 checked | optional, for frame captures (PLAN 3.10, `docs/TESTING.md`) |
| `app/build/`, `app/.cxx/`, `.gradle/` | 8: `gradlew :app:assembleDebug` | the APK, and `libdaogame.so` beside it (the 208 generated units, never packed); the runtime's units at -O2 |
| `build/run*.log`, captures, `work/sp/` | not rebuilt: scratch from earlier runs | run logs stay on the old machine; `work/sp/` was scripts and dialogues unpacked for reading (`tools/dao_erf.py`) |

Measured on the old workstation from a fresh clone (2026-10-25): the fetches a few
minutes (network-bound; xenia-canary and its submodules are most of it), the
extractions seconds, XenonRecomp under a minute, the recompile 40 seconds, the runtime
tree and its shaders a minute or two, and the Gradle build **18 minutes** - the native
code is large and built at -O2. About half an hour in all. After that, a change to one
`warden_*.cc` rebuilds in a minute or two.

**On a machine that has been set up before** (the original workstation, in session 32),
the skips can trust stale outputs. Step 6 runs `assemble_runtime.py --check`, which
proves the recorded edits apply to pristine upstream - not that `build/runtime-tree`
matches them - so a tree assembled before a pull passes it. After a pull that changes
`tools/assemble_runtime.py`, run `python tools/assemble_runtime.py` (it rebuilds the tree)
before the bootstrap; likewise `python tools/recompile.py tu` if `tools/recompile.py` or
DAORecomp's TOML changed (session 32's BUG-051 fix changed it). After
`assemble_runtime.py` rebuilds the tree, run `python tools/build_xenia_shaders.py` too:
the rebuilt tree has lost the generated SPIR-V headers six backend files include
(session 32's BUG-052 instruments changed `assemble_runtime.py`). What CMake generates (`gen_import_stubs.py`,
`gen_xex_patcher.py`, `gen_cmake_sources.py`) is made again on every configure, and
Gradle always runs, so those are never stale. The build after a fresh assembly took
6 minutes there.

### 3.5 Install, and check one boot

```bash
adb install -r -d app/build/outputs/apk/debug/app-debug.apk
python tools/push_gamecode.py
bash tools/boot_capture.sh <run> 60
```

The APK never holds the game's code (`libdaogame.so`): `push_gamecode.py` puts the one the
bootstrap built on the device, and only when it changed. Without it, choose **Build** in
the launcher and the app makes its own from the disc, as a player's does - about 7.5
minutes, once (`docs/TESTING.md`, "The
game's code made by the app").

`boot_capture.sh` clears logcat, starts the title, waits for the guest entry point,
lets it run 60 s, saves the log to `build/run<run>.log`, stops the app and prints the
parts that matter. A good boot shows the entry point, `GPU:` status lines with swaps
counting up, `game: mode is now 10 (movie)` then `8 (pregame)` (the title screen), and
no `fault on guest thread`. (The `F Warden` lines from `sample-threads` are the thread
sampler's stack chains, not faults.) The title screen is in the app's captures, which
are named by 5-second status tick (`guest-output-2.png` at 10 s, `-4` at 20 s, `-8` at
40 s, `-12` at 60 s): `adb exec-out run-as dev.daorecompandroid.duo cat files/guest-output-8.png >
build/run<run>-8.png`. **Run numbers** go on from the highest `build/run*.log` on the
machine (592 at 0.1 Open Alpha); the docs cite runs by number, so a new machine starts
above that.

### 3.5b The reference renderer (Phase 9, the owner's PC)

DAORecomp's Windows build, the same recompiled game on Direct3D 12, installed on the
owner's PC (`tools/dao_reference.py` knows where): `python tools/reference_build.py` (a clone of the pinned DAORecomp
in `%LOCALAPPDATA%\DAORecomp-src` with the port's recompiler corrections; the tools and
the build in `%LOCALAPPDATA%\DAORecomp-build`, about 25 GB, 30 minutes on the original
workstation; the game check first; detached, its output in `build/daorecomp-install.log`).
**Start it only with `python tools/dao_reference.py run ...`** - the owner's rule
(CLAUDE.md): a game check first, its window behind the owner's, muted, windowed.
`docs/TESTING.md` has the comparison's tools.

### 3.6 If a step fails

| Symptom | Cause, and what to do |
|---|---|
| `Could not find toolchain file` from CMake | a relative `CMAKE_TOOLCHAIN_FILE`; bootstrap passes absolute paths since d99f5ef - pull |
| `no ninja on PATH` / `no clang-cl` | §3.2; LLVM elsewhere: `--llvm-bin <dir>` |
| XenonRecomp exits `0xC0000409` with no output | a path mistake in its TOML (BUG-004); `tools/recompile.py` prints what it expects |
| `git submodule update` does nothing, CMake misses `thirdparty/fmt` | `submodule.active=no` from a `--recurse-submodules=no` clone (BUG-008); bootstrap uses `git init` + fetch, which avoids it |
| `assemble_runtime.py --check` fails an edit | upstream text changed under an edit; read the edit's description in `EDITS` before touching anything |
| Gradle: `SDK location not found` / NDK missing | `local.properties`, or the SDK pieces in §3.2 (`bootstrap.py --check` names them) |
| `JAVA_HOME is not set` | an old shell (§3.2) |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | the wrong signing key (§3.1). **Do not uninstall.** |
| the app starts but nothing is drawn | the Thor's screen is asleep (no surface is created); wake it |

Checks that prove the environment rather than the code, if something looks wrong:

```bash
python tools/assemble_runtime.py --check           # all 225 edits apply
python tools/check_xex.py --build ppc_tu6 --patch  # the TU7 image vs its recompilation (843/843)
python tools/check_xex.py --build ppc              # needs --disc-build (839/839)
python tools/find_missed_switches.py               # no jump table emitted as a call (BUG-051): 636 switches, 0
python tools/gen_xex_patcher.py --check            # the patcher edits apply
python tools/check_xdvdfs.py --all                 # disc reader == dao_iso.py
python tools/check_stfs.py                         # STFS reader == stfs.py
python tools/verify_extraction.py                  # 9,450 files on the Thor, exact sizes
```

`check_xex.py` groups recompiled entry points by the instruction XenonRecomp quotes
above each and requires every site in a group to hold identical bytes - it catches
decryption, key, header and block-assembly errors together. `--build ppc_tu6` without
`--patch` fails on purpose and says so.

---

## 4. The daily loop

All from Git Bash in the repo root. `adb` is a Windows binary: `export
MSYS2_ARG_CONV_EXCL='*'` before `adb shell` commands (the scripts do it themselves).

### 4.1 Build and install

```bash
./gradlew.bat :app:assembleDebug        # or ./gradlew from Git Bash
adb install -r -d app/build/outputs/apk/debug/app-debug.apk
python tools/push_gamecode.py           # when the game's code changed (it checks)
bash tools/boot_capture.sh <run> <settle-seconds> --install   # installs, then boots
```

The daily build is the debug APK (`dev.daorecompandroid.duo`, the Thor's own install),
built at -O2 with symbols kept, which is what makes native crashes readable. The release
APK (`prod.daorecompandroid.duo`) is the published one, made only to publish
(`docs/TESTING.md`, "A release build"). `boot_capture.sh --install` installs the APK but
does not push the game's code.

### 4.2 Booting and reading a run

Boot with `bash tools/boot_capture.sh <run> <settle>` **as its own short command**, then
read `build/run<run>.log` with separate short greps (§8 says why). Useful lines:

| grep for | What it says |
|---|---|
| `game: mode is now` | the title's mode: 0 explore, 1 combat, 3 menu, 4 conversation, 5 cutscene, 8 pregame, 9 loading, 10 movie, 11 character generation, 12 level-up (`warden_game.cc` has all 14) |
| `game: GUI open`, `game: GUI command` | the Scaleform screens and what was pressed |
| `title log` | the title's own log lines |
| `GPU:` | every 5 s: the ring, swaps, draws |
| `route:`, `inject-pad:` | what a route did, which presses were read |
| `XamContent`, `content root`, `save:` | saves |
| `waiting for the app's keyboard` | a name prompt is up |
| `Warden:F` (logcat) / `fault` | a guest fault report: the faulting thread's name first, then the guest call chain |
| `guest threads, the last` | one line per guest thread every 5 s: calls since the last report and the last functions entered |

`adb logcat -d -s Warden:F` shows a fault report on its own. Identify a crash by the
**guest call chain** in that report, never by the host backtrace (§7).

### 4.3 Routes and timed presses

```bash
bash tools/route.sh ostagar     # tools/routes/ostagar/* into the app's files directory
bash tools/route.sh wilds       # the same debug menu -> the Korcari Wilds, and a fight
bash tools/route.sh morrigan    # the end of the Prelude: the same fight with Morrigan's spells
bash tools/route.sh chargen     # New Game -> intro skipped -> character creation
bash tools/route.sh load        # START, then A on Resume: the newest save
bash tools/route.sh show        # what is on the device now
bash tools/route.sh off         # remove inject-pad, route and hide-dlc: hands back on the pad
```

**Always end with `route.sh off`.** A route's files stay for every start until then, and on
the owner's Thor that means New Game going to the debug start and the DLC hidden (BUG-104);
the launcher says "Test files in place" while any are there.

`files/inject-pad`: one press per line, `<X_INPUT buttons, hex> <delay s> [<hold s>]`,
timed from the title's first input poll. START `0010`, BACK `0020`, A `1000`, B `2000`,
X `4000`, Y `8000`, d-pad up/down/left/right `0001`/`0002`/`0004`/`0008`. A real pad's
buttons are OR-ed in. `files/route`: `skip-start`, `call <script> [mode]`, `run <script>
[mode]`. A press is a button mask and may go on with the triggers and the sticks:
`<buttons> <delay> <hold> <lt> <rt> [<lx> <ly> [<rx> <ry>]]` (`docs/TESTING.md`). With a save on
the device the main menu starts with Resume, so `route.sh` turns the 56 s A of a route
that starts a new game (`ostagar`, `wilds`, `morrigan`, `chargen`: a `.new-game` file) into a d-pad down. The timings assume this Thor's load times; if a step lands early or late, the
`guest-output-N.png` captures show where (`files/capture-every N` for more of them).
Details: [`docs/TESTING.md`](docs/TESTING.md).

### 4.4 Marker files

A marker is a file in the app's files directory that shapes the next boot. Make one with
`adb shell run-as dev.daorecompandroid.duo touch files/<name>`, write one with `adb shell
"run-as dev.daorecompandroid.duo sh -c 'echo 0 > files/trace-mode'"`, remove with `rm -f`.
Remove experiment markers when done - several cost frame rate or change rendering.

| Marker | Effect |
|---|---|
| `inject-pad`, `route`, `capture-every` | §4.3 |
| `skip_intro_logos` | the EA / BioWare / Edge logo movies are hidden, as DAORecomp's option |
| `keyboard-default` | the name prompt takes the default name at once (unattended runs) |
| `scene-msaa` | the console's two 2x MSAA scene tiles instead of one tile (slower) |
| `trace-swap`, `trace-mode <mode> [swaps [nth]]` | trace one frame's draws in full (`FT` lines): at a swap number, or `swaps` swaps after the nth entry into a mode |
| `dump-shaders` | every translated shader's microcode and SPIR-V to `files/shaders/` |
| `sample-threads` | the guest thread sampler |
| `occlusion-queries` | the game's occlusion-query culling kept (off by default since session 40, as DAORecomp: BUG-085's notes) |
| `frame-pacing` | `off`, `paced` or `alternate` (a minute of each): 4.7's pacer, `warden_frame_pacing.cc`; paced without it |
| `hack-gpu-loss` | the host GPU reported lost at 60 s, to try 4.9's message and exit |
| `probe.txt` | memory probes (addresses and word dumps) |
| `hack-<name>` | GPU experiments: `resolve-dump`, `vs-probe`, `skip-vs`, `ps-red`, `ps-paramgen`, `tiny-scissor`, `no-float-controls`, `no-sample-mask`, `no-cull`, `no-depth-test`, `depth-always`, `rt-readback`, `resolve-readback`, `no-sparse`, `msaa-2x-as-4x`, `no-a0`, `no-nmin`, `rect-in-vs`, `vk-validation` - each in `docs/TESTING.md` |

### 4.5 Measuring at Ostagar

```bash
bash tools/route.sh ostagar
bash tools/measure_ostagar.sh <run>                # fps, GPU busy and clock, CPU clocks, temperature
bash tools/measure_ostagar.sh <run> --simpleperf   # per-thread CPU profiles to build/perf<run>/
bash tools/boot_capture.sh <run> 660               # the baseline (6.7): eleven minutes at the camp;
bash tools/sample_power.sh <run> 11                #   alongside, GPU load, temperatures and power;
python tools/frame_report.py <run>                 #   then frame times, 1% lows and battery hours
```

Reference: 30 fps, the game's cap, with the GPU 98% busy (one tile, since session
33's shader work; 20 before), 13.5
(`scene-msaa`, before the fix), 8 at -O0; the GPU at 99% and 680 MHz. Where the GPU's time
goes, per render pass: `files/trace-mode` for the frame, then `files/gpu-timing.txt`
(§5.3). The baseline (runs 277-278, `docs/TESTING.md` §4): 29.9 fps, 11.2% of the presents
off the 4-refresh cadence, 1% low 22 fps, the GPU at 680 MHz throughout at 79-83 C,
about 10 W (2.2 hours of battery).

### 4.6 Frame traces and resolve dumps

`trace-mode` / `trace-swap` write one frame's draws, shaders, constants and bindings to
the log. `hack-resolve-dump` writes every resolve of the traced frame to
`files/resolve-<n>-<address>.raw`; pull one with `adb exec-out run-as dev.daorecompandroid.duo
cat files/<file> > build/<file>` and `python tools/resolve_to_png.py build/<file>
[width]` (tiled 32bpp; a colour resolve reads B G R A). This is how BUG-045 was found:
if a 3D scene goes black again, the dumps show which pass loses it. To find what writes
a texture: a traced frame's `FT bind tf<n> key @<address> ...` lines name each binding's
texture, and every boot logs its first 256 distinct `GPU: resolve to` destinations and
`GPU: memory export to` ranges (BUG-052, `docs/TESTING.md`). A `hack-resolve-dump`
holding a destination pitch dumps the resolves with that pitch in any frame and logs the
draws before each, which `python tools/recent_draws.py` decodes.

### 4.7 The name keyboard, unattended

When the log says `waiting for the app's keyboard`: `adb shell input text <name>` then
`adb shell input keyevent 66` (Enter). Or `keyboard-default` for no dialog at all.

### 4.8 Saves on the device

See `docs/TESTING.md` "Saves". Back them up before a test that may overwrite one:

```bash
adb exec-out run-as dev.daorecompandroid.duo tar c files/content > build/saves-<run>.tar
adb exec-in run-as dev.daorecompandroid.duo tar x < build/saves-<run>.tar     # put them back
```

Both verified on 2026-10-25 (the hashes matched after a round trip).

### 4.9 Changing the grafted runtime

`build/runtime-tree` is generated. A change to Xenia or DAORecomp code is an **edit
recorded in `tools/assemble_runtime.py`'s `EDITS`**, anchored on the pristine upstream
text (each edit is checked alone, so anchor on upstream text, not on another edit's
output), with a description of what it is for. Re-assemble (`python
tools/assemble_runtime.py`) or apply the same change to `build/runtime-tree` directly
while iterating, then record it; `--check` proves the recorded set reproduces the tree.

### 4.10 Implementing an import, hooking a title function

- **An import:** a strong `PPC_FUNC(__imp__Name)` in a `warden_*.cc` overrides the
  generated weak stub at link time - no registry. Arguments in `ctx.r3` onwards (the
  ninth and later on the guest stack, `r1 + 0x54`, `+0x5C`, ...), the result in
  `ctx.r3`, a pointer is `base + ctx.rN.u32`, and everything in guest memory is
  **big-endian** (`PPC_LOAD_U32` / `PPC_STORE_U32`). Before deciding a layout is
  unknown, look in `third_party/xenia-canary/src/xenia/kernel/` - and DAORecomp's own
  `DAORecomp/kernel/` - since between them they implement most of the kernel and XAM.
  Unimplemented imports return their family's **failure** value
  (`tools/gen_import_stubs.py --list`); a stub returning 0 would claim success.
- **A title function:** `WARDEN_OVERRIDE(tu6_sub_XXXXXXXX) { ... }` in a runtime file
  overrides the recompiled one (the game code's symbols carry the `tu6_` prefix, from
  either title update). The game's code is its own library (PLAN 12.2), so the override
  is reached through a forwarder compiled with it: `python tools/gen_gamecode_recipe.py`
  writes the forwarders (`app/src/main/assets/gamecode/shim.cpp`) and the runtime ABI -
  configuring stops until it is run, and the game's code must then be built or made
  again (the runtime refuses one with another ABI). Declare the original with
  `WARDEN_GAME_FUNC(__imp__tu6_sub_X);` and call it by that name; the forwarder takes the
  direct calls and the function table's indirect ones alike. To call a guest
  function from the runtime, `PPC_LOOKUP_FUNC(base, address)` and put scratch space
  below the guest stack pointer (callees' frames start at `r1 - 512`). `warden_game.cc` has working examples (the script registry calls, the
  dispatcher hook, the mode hook).

---

## 5. The next tasks, in order

**Resume here (2026-11-02, session 41, the end): version 0.1.0-alpha, "Open Alpha",
with a published APK.** Phase 12 is complete: the APK holds none of the game, and the
player's device makes the game's program (`libdaogame.so`) from their own disc and title
update - XenonRecomp and clang/lld for Android in the APK (`libxenonrecomp.so`,
`libllvm.so`), run by `GameCodeService`, about 7.5 minutes on the Thor. Disc 2's Title
Update 6 is enough; Title Update 7 is optional. On the computer the developer's own build
still makes `libdaogame.so` with the NDK and `python tools/push_gamecode.py` puts it on the
device. A release is `./gradlew :app:assembleRelease` then `python tools/release.py`,
which must pass before an APK is published (`docs/TESTING.md`). Next, in order:

1. **Published (2026-11-02):** the repository is https://github.com/mabencs/Dragon-Age-Origins-Recomp-Android-Duo - a fresh
   history, one commit to start; `origin` is GitHub and only `main` is pushed there
   (`remote.origin.push`). The old history stays on the owner's private server and in the
   local branch `archive-main`; never push it to GitHub. The 0.1.0-alpha APK is on its
   Releases page (tag `v0.1.0-alpha`). A later release: bump `warden.version`, build from a
   committed tree, `python tools/release.py`, a new GitHub release with the APK.
2. **What the alpha's players report.** Debugging Mode's .zip holds the logs, the saves
   and how the runs ended; `docs/TESTING.md` says how to read them.
3. **The open bugs:** BUG-042 and BUG-047 (rare; a reproduction first), BUG-058.
4. **The owner's deferred items,** when asked: 9.15 (LS1), 9.9.

**Session 42 (2026-11-02):** the deck's Quests and Codex fold, its long texts are read while open, and the Items tab equips and uses (PLAN 8.11, 8.12; BUG-105 to BUG-107), and a conversation's 4 s freeze with frame generation is gone (BUG-108). Left there: a trap's Use. Released as 0.1.1-alpha (`CHANGELOG.md`). Then BUG-110 and the launcher's updates (PLAN 13.1, D15): a release that must not install from the launcher has `<!-- manual-update -->` in its notes.

Below are the earlier resume notes, newest first, kept for their detail.

**Before that (2026-11-01, session 40, last): PLAN 10.11 done.** The areas' tour covered 118 areas - the main campaign, Awakening and every DLC campaign (docs/AREAS.md). BUG-084 is fixed; the two black scenes the tour found (BUG-085, BUG-086) are the game's own - DAORecomp's Windows build, now with the port's `DAO_RUN_SCRIPT` patch and Awakening installed, shows the same. Occlusion-query culling is off as in DAORecomp. The DLC's debug scripts run through `files/route-call` holding `!name [value]`; `files/dlc-only` limits the DLC a test game sees. The Thor is back on the owner's settings (60, Lossless 2x balanced, FSR 3.1 Performance). Phase 8 is done, 8.8's touch quickbar included (the deck's Quickbar tab; `files/deck-dump` writes the mirror's snapshot once, `tools/areas/quickslot_probe.sh` presses a button and dumps the slots). Then 8.9 and 8.10 (runs 546-549): the Party tab's rings show the members' faces (the HUD's portrait renders read back while the deck pulls), and Options -> Performance tab/overlay (off by default) puts native and total fps, frame time, 1% low, power, frame generation, upscaler and sizes on the deck's Perf tab, or on the main screen when the second screen is off; `files/perf` holds on or off. Then 10.17 and 10.18 (runs 550-551): the launcher and options panel by touch (a tap chooses, the value's halves change it, Resume ✕, three fingers open it in the game) and Options -> On-screen controller (`files/touch-controls`; turning it on sets the prompts to Xbox). `adb shell input tap` is shorter than a game frame: press on-screen buttons with `input swipe x y x y 150`. Then the rename (Dragon Age Origins Recomp Android Duo; the package id kept) and Phase 9: 9.16 Rendering resolution (`files/resolution`: 960x540, 1280x720, 1360x768, 1600x900, 1920x1080, 2560x1440 - the last three through Xenia's 2x draw resolution scale, PLAN 9.16; `files/hack-edram-map` logs the EDRAM's use), 9.17 Field of view (`files/fov`, `files/fov-conversation`, degrees). 9.18 Motion blur (`files/motion-blur`: off, low, high; needs FSR 3.1). 9.19 Depth of field in conversations (`files/dof`: on or off; needs FSR 3.1; `files/dof-show` draws its blur). 9.20 Photo mode (L3+R3 or the panel's row; photos in Downloads/Dragon Age Origins). The launcher's **Test: launch to the King's Camp** row (the owner's, removed before release - PLAN 11.6) runs the ostagar route from the launcher, no adb needed. Phase 10: 10.14 the cheats (`files/cheat-*`, the panel's last rows before Photo mode). 10.15 the launcher's Restore an earlier save (`SaveRestore.java`), 10.16 Language (`files/language`: en, es, it). 10.12 the launcher's Mods page (`Mods.java`, `ModsPage.java`; mods in `<external files>/mods/`, the game's list `files/mods-enabled.txt`). 10.13 the converter: the Mods page's Convert for this version on a "[PC version]" mod (`dao_convert_android.cc`; `python tools/assemble_dao_convert.py` writes `build/dao-convert/`, which the build needs - the bootstrap's step 6 runs it; run it by hand after a pull that changes the script; `tools/make_test_texture_mod.py` makes a test mod). 10.19: PC add-ins (.dazip) install as downloadable content packages (`dao_addin_android.cc`; `content/00000002/mod-<id>/`, the mod's addins.txt; `files/dlc-off` for mods turned off), and .7z/.rar imports (`dao_archive_android.cc`; third_party/7zip and third_party/unrar from the bootstrap). The owner's No Helmet Hack (both versions) is installed on the Thor. PLAN 2.4: `tools/patch_xenonrecomp.py` patches XenonRecomp after DAORecomp's script (six instructions translated, BUG-093); after a pull that changes it, run it, `ninja -C build/xenonrecomp XenonRecomp` and `python tools/recompile.py tu` (the bootstrap does all three when the recompiler lacks the patch or the generated code is older). Phase 10 is done. 9.14 is closed (2026-11-02, the owner: options kept, defaults 1280x720, frame generation Off, Bilinear). BUG-091 and BUG-092 fixed (a conversation's stutter with frame generation; `docs/TESTING.md` has how to reproduce a conversation). 9.21 dropped (the owner). Phase 11: 11.1 done - Options -> Debugging Mode (`files/debug-mode`; the log in files/debug/, the panel's Exit and export save/logs packs a .zip into Downloads/Dragon Age Origins). The panel can be opened from adb with `adb shell input keycombination KEYCODE_BUTTON_SELECT KEYCODE_BUTTON_START` and driven with KEYCODE_DPAD_* and KEYCODE_BUTTON_A (injected pad presses go to the game, not the panel). 11.2 done - the launcher's Saves page (`SavesPage.java`: export to Downloads, import from a .zip, restore). The log names the mods and mod add-ins loaded at each start ("MODS LOADED"). Session 41 also fixed the owner's reports: the deck's faces by the portraits' names (BUG-096), the deck following the game on Home (BUG-097/098), Options -> Volume boost (BUG-099). The game limits how many characters have saves (seven was too many) - routes need a free one (docs/TESTING.md). 11.3 and 11.4 done (session 41): the code is GPL-3.0-or-later (`LICENSE`), .rar read by libarchive (`third_party/libarchive`; delete an old `third_party/unrar`), the release key at `~/.android/dao-release.jks` with its passwords in `~/.android/dao-release.properties` (never in the repo - copy both to a new machine by hand; without them a release is unsigned), a release made with `gradlew :app:assembleRelease` then `python tools/release.py` (it checks the APK and keeps it with its symbols in build/releases/<version>/), and 11.4's clean copy with `-Pwarden.clean` (docs/TESTING.md). The Thor keeps the debug build (`installDebug` / `adb install -r -d` as ever): a release is signed by another key and never installs over it. Next: 11.6 when the owner releases. Earlier: 11.5 done - About and licences (`AboutPage.java`, the texts in app/src/main/assets/licences/ from `python tools/gather_licences.py`, which the bootstrap runs; run it after a pin moves). Next: 11.3/11.4 (the owner's input: the release key, GPL-3.0 and the source offer for a distributed build, a clean device), 11.6 last. Connect over wireless debugging (`adb mdns services`; 5555 refuses).

**Before that (2026-11-01, session 39): PLAN 10.11, the areas' tour - in
progress; the computer crashed at 01:40 with the tour running and session 39 wrapped it
up.** 28 areas loaded through the game's own debug menus (`files/route-call`,
`tools/areas/`), 27 profiled uncapped: the Circle, Lothering, the Dalish camp, Denerim,
Orzammar, the Urn - docs/AREAS.md has the table (40-68 fps, the GPU at 99%, the Urn's
ruined temple the heaviest), the screenshots in `build/areas/`. Found: **BUG-085**, the
Arl of Denerim's estate renders black (to check against DAORecomp's Windows build with the
same jump first), and **BUG-084 three times in a night**, each during a load, the fault
address 0x1 twice (a null destination in the log ring's memcpy, not an overrun) - fix it
before the tour goes on, it kills one load in ten. **The Thor holds the tour's uncapped
options** (Frame rate 120, Frame generation off, FSR 3.1 Performance; the owner's: 60,
Lossless 2x balanced, Performance - put back when the tour is done); the game is stopped
and the route markers are off. **To resume the tour:** `bash tools/route.sh probe`, a boot
(`bash tools/boot_capture.sh <run> 60`, the default start area about 45 s after the first
poll), then `bash tools/areas/tour.sh tools/areas/tour.txt` - the list's done lines are
commented out, the first live line is the Urn's mountain top (its probe was cut short),
then the Gauntlet, Haven, the prelude's five, the party camp, the origins' five, Brecilian's
and the Deep Roads' sub-lists (one more press than was tried - the forest's sub-list is
unread), Eamon's estate (ended in his conversation with no load seen: the loading wait
before the last press), Redcliffe's plot jumps; the climax and the epilogue by hand. Read
each area's contact sheet (`python tools/areas/sheets.py <label> ...`) for visual bugs and
put the rows into docs/AREAS.md (`results_table.py`). The Thor answered only on its
wireless-debugging port after the crash (`adb mdns services`, then `adb connect` to the
port listed; 5555 refused).

**Before that (2026-10-31, session 38): PLAN 10.9, GPU headroom - paused (runs
494-509).** Frame generation is the big lever: at the camp Lossless quality 22 fps,
the new Lossless 2x, balanced 26-27 (run 509), fast 29. The owner chose balanced (set on
the Thor) and a Shader math option: Options -> Shader math, Exact by default, Fast = IEEE
multiplication from the next start - the camp at 30 fps with balanced, the picture the same
but for boot-to-boot variation in the light shafts (runs 510-512; the owner's is Exact). FSR 3.1's
accumulate pass now reprojects with the SDK's Lanczos lookup table (AMD's path) instead of
sines - 3.70 ms against 4.17 (runs 513-515). The shader-side try (uniform hoisting) gave
nothing. Then the fetch constants decoded on the CPU once a draw (runs 516-518; the
owner asked): the camp 27.2 -> 28 fps with Exact shader math, the picture the same. Then
the vertex fetches' shared-memory binding and endianness known per draw (runs 520-521):
**the camp at the game's own 30 fps with Exact shader math and Lossless 2x, balanced.**
Then uncapped for measuring (Frame rate 120, Frame generation off; the Thor is back on
the owner's 60 and Lossless 2x, balanced): the camp 36.8 fps, the game's post-processing
2.2 ms of it; the multiplication rule's FMin form proved exact on the Adreno
(`tools/mintest`) and made the default: 37.3 fps; Shader math Fast 42.2, texture filtering
free; two options added with today's look the default - FSR 3.1 Ultra performance
(640x360: 46.6 fps, softer) and FSR 3.1 sharpening Off (38.1) (runs 523-530). 10.9 is
done; 10.5 and 10.6 were dropped by the owner. Every trade of fidelity for speed is an
Options row (the owner's rule, 2026-10-31).
Measured before it: The Ultimate Edition's disc 2 is done (runs 502-504: Awakening and
every DLC installed from it by the launcher; the file rows are the disc image, disc 2, the
title update and Lossless.dll). GPU headroom: a camp frame is about 45 ms of GPU under the owner's settings
(the game 23, FSR 3.1 7.7, Lossless 2x quality 14); the levers, biggest first, are in
PLAN 10.9; `bash tools/gpu_profile.sh <run>` measures. FSR 3.1's character motion, texture
packs (9.9) and the deck's touch quickbar (8.8) are marked to revisit later.

**Before that: PLAN 10.10, Awakening and the DLC (the owner asked).** Awakening plays when the standalone Awakening disc's
`Addins/DAO_PRC_EP_1/` is a DLC package (`files/content/00000002/DAO_PRC_EP_1/`) -
installed on the Thor by hand (runs 487-490), and the launcher's Awakening row installs it
from the disc image (runs 491-492); the Downloadable content row installs Live packages or
a disc image holding them (run 493, a test program - no real package yet). Next: the
owner's DLC packages (Ultimate Edition's disc 2, or a console's) when there are any. GPU
headroom is 10.9, for later.

**Before that (2026-10-31, session 38): Phase 10 is done but for 10.9 - 10.5 (a 4-hour
soak) and 10.6 (battery life) were dropped by the owner - and 9.14 waits on the owner's look and feel at FSR
3.1 2x against Lossless 2x.** 10.7 is done (run 479: how the last session ended, on the
launch screen, reports in `files/exits/`). 10.4 was looked into and closed (nothing to
build before play beyond 10.3's storage). 10.2 is done (runs
475-477: no throttling at the heaviest settings; Options -> Heat guard, off by default).
PLAN 9.14's paint queue was tried and dropped (runs 473-474: the paints on a queue of their own reach the
panel later, whatever the queues' priorities); 9.14 now waits on the owner's look and
feel at FSR 3.1 2x against Lossless 2x.

**Before that (2026-10-30, session 37): 9.14, frame generation - Lossless 2x is in play
(PLAN 9.14, step 2 done): Options -> Frame generation, about 60 frames a second on the
panel at the King's Camp (runs 439-440), Lossless 2x fast 1.17 ms of GPU a generated
frame (run 442), the UI never interpolated - the frames made from the image before the UI
and the real frame's UI put back over them (run 443), and the Frame rate what the screen
shows - the game at half of it, 60 its own 30 (run 444: an even 60 on the panel), and 3x
and 4x (dropped by the owner after run 445). The owner in play: laggy and not smooth yet
(the spacing comes apart when the game falls under its rate). Upstream checked: DAORecomp
moved to `eb2360a` (BUG-076, BUG-077; its Aeducan Thaig fix is BUG-051's). FSR 3.1's
frame generation works (Frame generation -> FSR 3.1 2x, with FSR 3.1 on; runs 447-450,
PLAN 9.14): 9.5 GPU ms a frame, its optical flow on the render-size image; the camp with
FSR 3.1 Performance at 25.8 fps, 45-62 on the panel. Then **Phase 8, the second screen
(the owner asked)**: the companion deck on the lower screen in its own process
(`DeckActivity`, `DeckService`, `DeckView`), fed by a state mirror of the token paths the
game's UI observes (`warden_deck.cc`, two read-only hooks; SECOND-SCREEN.md §3) - Party
and Perf done, 8.1/8.2/8.3/8.6/8.7 done, BUG-078 fixed; then Quests, Map, Codex, Tactics
and Items from the deck's own reads of the data model, and 8.4 - the game's own typefaces
and HUD art made on the device (`DeckFonts`, `DeckArt`). Phase 8 is done for its exit
criteria; left: the items' icons, the map's area image, the abilities' names, and 8.8
(the touch quickbar) later.
The owner's look and feel at FSR 3.1 2x against Lossless 2x is still to come. How it presents, and the five
bugs on the way: BUG-069 to BUG-073.** Before that, session 36: step 1 - lsfg-vk's
backend on the game's own device (`warden_lsfg.cc`, `tools/prepare_lsfg.py`), good
frames from the owner's `Lossless.dll` (runs 425-426). LS1 upscaling (PLAN 9.15) is
deferred by the owner, to revisit later.
Before that, FSR 3.1 (PLAN 9.4) was finished:
The owner asked for 10.1 (done: the options panel, Select + Start), HDR (done, 9.2), 60
fps by default with 30/45/120 (done), and FSR 3.1 with Performance, Balanced and Quality
- in progress. What works: the SDK built from source (`warden_ffx`,
`tools/build_fsr3_shaders.py`), its 16-bit passes, and the game's frames upscaled to
1080p (`warden_upscale.cc`) at Quality, Balanced or Performance - `files/fsr3` holding the
profile makes the game's device at 1280x720, 1120x630 or 960x540 (`warden_game.cc`), from
the next start - with two render target copies a frame, the camera's motion vectors (from the static
meshes' matrices and the depth) and the UI put back over the upscaled scene; Options ->
FSR 3.1 chooses it. The King's Camp: 23.4, 25.9 and 28.8 fps (FSR off 31.1; runs
421-423). BUG-067 is the one to know: FSR 3.1 takes three shared resources from the
caller with each dispatch, and without them it silently runs without depth or motion.
Left: per-object motion (characters, foliage in the wind have the camera's only). After that, 9.14 (frame generation, FSR 3.1's and lsfg-vk's, with the frame rate
setting as the target on the screen - the game at the target divided by the multiplier;
the owner supplies `Lossless.dll`, which stays out of git). Before building on the owner's PC,
`python tools/game_check.py` as a step of its own (CLAUDE.md).

The owner's order (PLAN Phase 6): build and test as much as possible before a hand
playthrough to Ostagar, which is the final check of the Phase 6 exit criteria.
**Next: Phase 9** (the owner, 2026-10-27) - 3.3b is done (session 35: DAORecomp's
Windows build installed, nothing renders wrongly on the Thor against it) and 9.6 (16x
anisotropic filtering by default), 9.1 (60 fps as a setting), 9.5 (FSR 1 and CAS as
settings), 9.11 (the intro logos), 9.12 (ITU's mixdown, the headphone surround) and 9.3
(10-bit colour, Vivid), 9.8 (shadows and post-processing switches, no increases), and
9.13 (an autosave when put away: the engine's own DoAutoSave, then the hold), 9.10
dropped (the tactical camera is inert in the 360 build), 9.7 (FXAA), and 9.2
and 9.4 not built (no HDR path on Android 13; FSR 3.1 unaffordable); next 3.8's GPU
headroom - and the owner's hand playthrough to Ostagar, the last of Phase 6's checks (the voice-over
heard, 6.4, 2026-10-27), which starts from the app icon since session 35. Done in
session 35: **Phase 7, the button prompts** - Xbox, Nintendo or PlayStation, Back ->
Settings -> Button prompts, from the next start (`docs/GLYPHS.md`; switching while the
game runs crashed its next load, BUG-061). Phase 3 is done: 3.3b, the frame comparison, opens Phase 9 with
DAORecomp's Windows build - it builds and runs unattended, not windowless (PLAN 3.3b) -
and 3.8's Adreno performance work comes back there (the owner, 2026-10-27). Done: 3.7 and BUG-060 (session 35 -
the stored pipelines created when first drawn with, not at startup: a pipeline whose
shaders spill holds 24 MB of the driver's memory; `files/hack-precreate-pipelines`
brings the startup creation back), 3.10, frame captures with
GFXReconstruct (session 35, `tools/gfxr.sh`; a King's Camp capture, `camp.gfxr`, in the
Thor's Download folder), 3.9, validation-clean (session
35, runs 323-327 - core and synchronization validation, no error; `docs/TESTING.md` says
how to switch the layer on), suspend and resume (BUG-056,
BUG-057, session 35 - `warden_suspend.cc`; `bash tools/suspend_check.sh <run>` beside a
capture checks it), the shader and pipeline storage (PLAN 10.3, BUG-055,
session 34 - `files/pipeline-storage-v1/`; bump `kPipelineStorageVersion` in
`warden_gpu.cc` when the shader translation's modification bits change meaning), the
save thumbnails and the HUD portraits (BUG-049, BUG-052, session 34 - the pipeline
cache's placeholder pixel shader), the resolve straight from the render target (3.8, run
296), frame pacing (§5.5, three faults fixed in runs 296-301), the app's end (4.9) and
character creation checked screen by screen (6.1, run 292).

### 5.1 Audio (PLAN 4.5)

**Done** (session 32, runs 207-209). The output: `XAudioSubmitRenderDriverFrame`'s frames
are mixed down to stereo and played through an AAudio stream that paces the title's
audio clock (`warden_audio.cc`; the `XAudio:` 5 s line says how much played, how much of
it was silence for want of a frame, and the peak). The decoder: DAORecomp's
`apu/xma_decoder.cpp` over FFmpeg's `xmaframes` (xenia-canary's `third_party/FFmpeg`
submodule, which the bootstrap now fetches), compiled from DAORecomp's tree behind
`app/src/main/cpp/dao_compat/` and `warden_xma_real.cc`. The voice-over: heard by the
owner (6.4, 2026-10-27). The level, measured (run 367, `tools/audio_levels.py`): -17.7
LUFS over the menus, the Korcari Wilds and combat, a handheld's usual level; the menus
sit 25 dB below combat, which is the game's own range. The mixdown is ITU-R BS.775's
since session 35, and headphones can have the surround (9.12, `warden_audio_mix.cc`).
Left: the latency - the Thor's stream is shared and normal-latency, about 40 ms plus the
queue's 48 ms.

### 5.2 Tests at Ostagar (PLAN 6.2, 6.3) - done

`bash tools/route.sh ostagar` reaches the King's Camp, `bash tools/route.sh wilds` the
Korcari Wilds (run 218: explore mode about 108 s after the first input poll); with saves
on the device each makes its 56 s A a d-pad down, onto New Game below Resume (the
route's `.new-game` file). Done in session 32: the tutorials switched off (below), a save
at the King's Camp loaded back through Resume (runs 215-216, 6.5 complete), the
Korcari Wilds (runs 217-218, BUG-051), combat, spells included (6.3, runs 219-223),
and areas streamed and left on foot (6.2, runs 220, 224): walking in the Wilds opens new
grid cells of the environment (`ost100d_5_6_0.rim`, run 220), and walking back from the
Wilds' start brings up "Gather your party and venture forth?" - Yes loads the King's
Camp in 3.6 s (run 224). Nothing in this section is left.

The two-hour stability soak (PLAN 6.6) is dropped - the owner's call (session 32).

**The fights** (6.3, done). The `wilds` route walks up the path at 114 s (the left stick
forward for 6 s; explore mode comes at about 108 s), meets wolves at about 120 s and
presses A at 122, 126 and 130 s; the party's AI wins in about 52 s (runs 219-220,
24-27 fps). The `morrigan` route takes the end of the Prelude, with Morrigan in the
party, from the same spot: explore at about 119 s, the walk at 125 s, the fight from
about 130 s, with her Winter's Grasp (run 223, 18.6 fps). Further up the path is hard
to find blind: the left stick moves relative to the camera, which does not turn by
itself (the right stick turns it), and run 220 walked into a cliff.

**The prelude debug menu** (`zz_pre_debug`, runs 215 and 218): "What do you need to
do?" 1 Jump to an area, 2 Do something not specific to an area, 3 I'm all done. Jump to
an area: 1 Start in the King's camp, 2 Start in the Korcari Wilds, 3 Start in the night
version of the King's camp (after Wilds), 4 Start in the signal tower, 5 Start at the
top of the signal tower, 6 (MORE). After the King's camp the menu comes back with four
items (2 "Do something in the King's camp (day version)", 4 I'm all done - the
`ostagar` route's last presses); after the Korcari Wilds it does not, and explore mode
follows the load. "Do something not specific to an area" is quest shortcuts (darkspawn
blood, the treaties, flowers, kill all hostiles, start over); the area list's (MORE)
offers Flemeth's hut, the end of the Prelude, its very end with Morrigan, the Tower of
Ishal's third level and the King's camp with its opening cutscene (run 221).

**In and after a fight** (runs 219-222): `mode is now 1 (combat)` while it lasts, and
`2 (dead)` when the whole party has fallen; when the controlled character falls, control passes to another; A on a
corpse opens its loot (mode 3) and A again closes it; A on a party member walks over and
starts a conversation, whose option list wraps (up from the first selects the last).

**The tutorials are off** (run 214, with the owner's permission - it is their game
option, so it holds for hand play too). They had held every other button at the King's
Camp until the one they named was pressed: "Level up now by pressing ◀" (the Back
button's glyph, so BACK), then in the Character Record A, Y (auto level up), A and B,
then "Mapping Abilities ... open the radial menu with LT" (an injected LT), then "Open
the Spells or Talents menu", never found (runs 205-206, 210). The switch is Options ->
Game -> Interface -> **Show tutorials**, the page's last item (runs 211-213). With saves
on the device: `0010 45 0.15`, four `0002` (Options is the fifth item under Resume, at
53, 54.5, 56, 57.5), `1000 60` (the Game page), `1000 64` (into it), `0002 67 8.0`
(held, the list repeats down to Show tutorials), A at 77 to untick it, and B at 80 and
83 to leave - the profile is written at the last B. Only `files/profile/63E83FFD.bin`
changed; copies from before and after are in `build/profile-before-tutorials-off.tar`
and `build/profile-after-tutorials-off.tar` on the original workstation (`adb exec-in
run-as dev.daorecompandroid.duo tar x < <copy>` puts one back). The same presses switch them
on again.

### 5.3 The GPU's cost (PLAN 3.8)

30 fps at the King's Camp since session 33 (20 before) - the game's own cap - with the
GPU 98% busy at 680 MHz, so no headroom there. **How to see the GPU's
time:** a traced frame (`files/trace-mode`, e.g. `0 300 2` on the `ostagar` route) times
each render pass with timestamp queries and writes `files/gpu-timing.txt` (logcat loses
lines after a trace); `files/hack-split-pass` holding `n` cuts every pass into pieces of n
draws, or `1:<first>:<last>` times each of those draws alone; a pass's first draw's shaders
name it. **What session 33 found** (runs 240-255, PLAN 3.8): the main scene pass is two
thirds of the frame; per-draw setup is small (about 4 µs); per-pixel shading is the cost,
and the translator's texture-fetch emulation was a third of it. The unused per-component
sign handling went (shader modification bits, exact); explicit-gradient sampling went for
2D fetches with equal axis adjustments (also exact); normalization and snapping multiply by
each size's reciprocal; the post-fetch conversion is a select when only its 16-bit rounding
can apply (a third bit). The rounding itself is Xenos behaviour and stays
(`hack-no-integer-scale` measures it, about 0.5 fps). The other third was the
multiplication rule: Xenos gives `0 * anything = +0`, even for inf and NaN, and the
translator keeps it at every multiplication, mad and dot product - upstream as `NMin(|a|,
|b|) == 0`, whose NaN handling the Adreno compiler expands. Tested as `(|a| == 0) || (|b|
== 0)` (exact, `spirv_shader_translator_alu.cc`), it costs about half as much: 30 fps.
`hack-no-mul-zero` selects other forms for measurement (`nmin`, `fmin`, `parity`, IEEE).
Narrowing each pass's render area to what it touched gained nothing (run 252; reverted -
upstream's TODO in `vulkan_command_processor.cc` is not worth doing on this driver).
**Next, for headroom:** the rule's remaining cost - a register can only hold inf or NaN
downstream of rcp, rsq, log, exp or a texture holding such values, so most
multiplications could skip the test and need only `+0.0` for the sign of a zero product
(an analysis of the shader's registers before translating it); then the 16-18
render-target transfer passes (3.4 ms traced; `hack-no-transfers` measures them) and
the time between passes. The terrain draw (pixel shader `F540BD461D6C5638`, 21 fetches)
and the alpha-tested foliage (`2D0B7CE20EE68C88`) are the biggest single draws. Combat in
the Korcari Wilds (`morrigan` route, run 256) runs at the cap with about three quarters of
the camp's load. The exact saving left that is known: DAORecomp's
`dao_resolve_direct.xesli` (a resolve straight from a single-sampled 8888 render target,
used only by its D3D12 backend) wired into the Vulkan resolve, one full-image copy less
per resolve, about 1 ms. The FMin form of the rule (about 1.5 ms) and IEEE multiplication
(about 6 ms) are faster but not exact - the owner's call. To compare frame rates by game
mode in any run: the swap counts in the `GPU:` status lines between `game: mode is now`
lines. Static view of a shader: pull `files/shaders`
(with `files/dump-shaders`) and run the NDK's `spirv-dis` - most switches left in it test
constants, which the driver folds. Then frame pacing (4.7, §5.5).

### 5.4 Smaller things, when they get in the way

- **BUG-052, the party portraits - fixed in session 34** with BUG-049: drawn once at an
  area load, they drew the pipeline cache's placeholder pixel shader (`docs/BUGS.md`).
  What session 33 found, kept for its instruments. Each HUD
  portrait shows its coloured disc but no face. Each face is a 146x146 `k_8_8_8_8`
  texture the GPU renders and resolves when an area loads (the `GPU: resolve to ...
  pitch 009200A0` lines), and the HUD samples it (runs 225-227). Session 33 found why
  (runs 228-239, `docs/BUGS.md`): every part of the portrait is drawn - head, facial
  meshes, armour, hair - but at the load some 43 of the head's 256 vertex constants are
  zero vectors that are set when the game redraws a portrait in a fight (it does so on
  its own; that redraw has the face). The face's bones, about fourteen 3x4 matrices, are
  not yet posed when the load draws the portraits, so the face collapses; faces in the
  scene and in conversations are fine. Runs 257-258: the face's part of the portrait's palette
  is vertex constants c186-c204 and c209-c232, zero at the load, set in the fight's
  redraw; the CPU writes them into the command buffer (no GPU constant load), and a save
  load shows the same bare discs. Run 259: the face's matrices exist only in the command
  buffer (computed into it as the portrait is recorded; nothing in the title's heap to
  watch), and the fight's redraw followed an ability's impact - a status change
  re-renders a portrait. Runs 260-267: the update thread draws the portraits during
  the load, from the GUI's per-frame 3D render (`sub_82685330`) down to the mesh draw
  `sub_82698558` and the Direct3D draw `sub_82D9DB20`; the vertex constants go up
  through `sub_82D941F8` (device, start, data, count), and the head's skinning palette is
  c33 + 103 vectors - 34 bones - where the face needs bones to about 66: **the face's
  bones have not joined the character's skeleton when the load renders the portraits.**
  Guest functions can be wrapped from our code without a recompile (then
  `WARDEN_OVERRIDE(tu6_sub_X)` calling `__imp__tu6_sub_X`, declared with
  `WARDEN_GAME_FUNC`, as `warden_game.cc` does since PLAN 12.2; a new override changes
  the game code's recipe - `python tools/gen_gamecode_recipe.py` - so the game's code is
  made again).
  Runs 268-275: the palette records never reach past c185, and
  no upload through the vertex-constant setter (`sub_82D941F8`) or a GPU constant load
  fills c186-c232 in any phase - yet the fight's working redraw has them, and it comes
  from the render thread's side (the kicks carrying a portrait), while the loads'
  portraits are the update thread's. **Next:** what writes c186-c232 for the working
  redraw (the device's shadow constants watched for writes; or the redraw's GPU-side
  values compared with the render thread's uploads), and whether the console's load
  renders a scene frame before the GUI draws the portraits. The
  setup: the `wilds` route (its first six presses for the load alone), `files/hack-
  resolve-dump` holding `009200A0`, then `python tools/recent_draws.py build/run<N>.log
  --pitch 160` - the head is vertex shader `0CA594B641A10C7E`, 10,788 indices.

- **BUG-049, the save thumbnails - fixed in session 34:** the thumbnail is a quad
  sampling the 1280x720 copy of the scene at `00DC3000`, drawn into a 448x256 target at
  the save and resolved to `01E13000`; the quad, drawn once, drew the pipeline cache's
  placeholder pixel shader (runs 309-313). The setup that found it: the `load` route,
  `hack-resolve-dump` holding `010001C0 00DC3000`, `hack-log-bindings` holding
  `8D5822CC49BEC996`, then START, A (Save Game), A (New Save), A; `docs/TESTING.md` has
  the markers, `docs/BUGS.md` the detail.
- **BUG-047:** if it recurs, the assertion's packet and the ring position are in the
  log; `docs/BUGS.md` has what was seen.
- **Two Vulkan devices** are alive (the bring-up provider from `Runtime::Startup` and the
  graphics system's own); fold them into one.
- **`LoadTitle` blocks the main thread** for ten seconds or more (reading the
  executable, applying TU7, building the 31 MB call table) from `extractIsComplete`'s
  JNI call - an ANR waiting to happen; move it to a thread.
- Stubs still reached, in `boot_capture.sh`'s summary: `KeSetBasePriorityThread`,
  `MmQueryAddressProtect`, `MmFreePhysicalMemory`, `KeEnterCriticalRegion` /
  `KeLeaveCriticalRegion`, `ExRegisterTitleTerminateNotification`,
  `VdCallGraphicsNotificationRoutines`, `KeEnableFpuExceptions`,
  `KiApcNormalRoutineNop` - none has mattered yet.
- **5.6** whether to commit a screenshot (game imagery - the owner decides). (4.9, the
  app's end, is done: `warden_lifecycle.cc`; device-loss recovery is out of reach, a lost
  GPU ends the app with a message.)

### 5.5 Frame pacing (PLAN 4.7) - done

The baseline (6.7, runs 277-278): at the King's Camp the presents are 4 refreshes of
the 120 Hz panel apart for 88.8% of frames and 3 or 5 for 11% - judder at an even 29.9
fps; the title's own swaps are 1, 2 or 3 vblanks apart (11%, 78%, 9%). The thread that
presents is the GPU command processor's (paint mode `kGuestOutputThreadImmediately`) and
the GPU is 97% busy, so pacing must not make it wait - Swappy sleeps until the target
refresh. The Thor's driver has `VK_GOOGLE_display_timing` (`adb shell cmd gpu vkjson`):
a present can carry a time before which it must not reach the panel, and
`vkGetPastPresentationTimingGOOGLE` reports when each frame did, the earliest it could
have, and how long the finished frame waited for the compositor.

**Runs 279-283 (session 33):** the extension is enabled and `warden_frame_pacing.cc` reads
the panel's own times into a `pacing:` line every 5 s (`python tools/frame_report.py <run>`
splits it per mode); `files/frame-pacing` holding `paced` or `alternate` turns the pacer
on. The panel refreshes every **8.445 ms (118.4 Hz)**, not 8.333 - so 4.2% of the title's
frames at three refreshes is the floor. Unpaced: 11-12% off the four-refresh cadence; the
title's swap to the panel 77 ms. Paced (each frame held for the refresh nearest its place
on a timeline at the title's measured rate, the offset regulated from late and early
frames): 7.4% at 16 ms more latency (run 283). **Runs 284-285:** the offset tracks a
late-frame rate in small steps - paced 6.8% against 11.1%, at 7 ms more from the title's
swap to the panel (run 285), and **on by default** since. `PLAN.md` 4.7 has the runs.
Bigger levers, the owner's call: the title's vblank at half the panel's rate (59.2 Hz:
every frame four refreshes; what the title counts in vblanks 1.4% slower), or GPU
headroom (3.8) - 9% of the title's frames take three vblanks.
**Runs 296-301 (BUG-054):** with the title's frames even after the resolve straight from
the render target, the pacer's own faults showed - every frame had been shown a refresh
before its slot (so a frame a refresh late went uncounted), the period's correction wound
up, and catch-up frames after a stall left the latency high. The desired time is now a
quarter of a refresh after the slot (`files/frame-pacing` holding `phases` compares four,
`phase4` to `phase1`), and a timeline a period ahead of the frames starts again. Run 301:
6.4% off the cadence, 38 ms from present to panel (p50), no frame early, the period's correction within 0.025 ms a frame either way, two new timelines once at the camp.

---

## 6. Where the code lives

| Path | What |
|---|---|
| `app/src/main/java/dev/daorecompandroid/duo/` | `GameActivity` (the launcher, surface, extraction, boot, the keyboard dialog, Back and the app's end), `NativeBridge` (the 44 JNI methods), `ControllerManager`, `GameData`, `GameSurfaceView`, `GameCode` and `GameCodeService` (the game's code made on the device, PLAN 12.5) |
| `app/src/main/cpp/jni_bridge.cc` | the JNI side, registered in `JNI_OnLoad` |
| `warden_runtime.cc`, `warden_title.cc`, `warden_boot.cc` | startup, loading the title (with the title update the game's code was made from), starting guest code |
| `warden_game_library.cc`, `warden_game_library.h` | loading `libdaogame.so` (PLAN 12.2): its ABI, its layout, `WARDEN_GAME_FUNC` and `WARDEN_OVERRIDE` |
| `warden_dlc.cc` | disc 2's install: the DLC packages and its Title Update 6 |
| `app/src/main/assets/gamecode/` | the recipe the device makes the game's code with: `hooks.h`, `shim.cpp`, `recipe.json`, `sources.json` (`tools/gen_gamecode_recipe.py`) |
| `warden_xex.cc`, `warden_stfs.cc`, `warden_xdvdfs.cc`, `warden_extract.cc` | the executable and the title update on the device, the disc reader, first-run extraction |
| `warden_guest.cc`, `warden_guest_heap.cc`, `warden_memory.cc` | guest memory, the indirect-call table, per-thread PCR/KTHREAD/TLS, heaps, the GPU's write watch, read-only code |
| `warden_imports_kernel.cc`, `warden_threads.cc`, `warden_objects.cc`, `warden_kernel_variables.cc` | the kernel: strings, memory, threads, waits, handles, imported variables |
| `warden_fs.cc` | `D:\` onto the extracted disc (read-only) and the content roots (writable) |
| `warden_xam.cc`, `warden_xam_input.cc`, `warden_keyboard.h` | XAM: users, content and saves, the system UI, the keyboard, input |
| `warden_video.cc`, `warden_gpu.cc`, `warden_dao_host.cc` | the video driver's answers, Xenia's graphics system, interrupts, the GPU markers |
| `warden_audio.cc`, `warden_xma.cc`, `warden_fmod.cc` | the audio clock, XMA contexts, FMOD diagnostics |
| `warden_net.cc` | a console with no cable (§8) |
| `warden_game.cc` | hooks into the title: its modes, log, scripts, GUI, cutscenes, the route |
| `warden_fsr3.cc`, `warden_upscale.cc`, `ffx_compat/`, `shaders/warden_upscale_*` | FSR 3.1 (PLAN 9.4): the FidelityFX SDK's set-up, and DAORecomp's temporal upscaling ported - the hooks in Xenia's draws, resolves and swap (graft edits), the copies, the dispatch, the composite |
| `OptionsPanel.java`, `Options.java` | the options panel (PLAN 10.1): Select + Start during play |
| `warden_trace.cc`, `warden_guest_registers.cc`, `warden_memory_check.cc` | the fault report: thread, guest chain, registers, function trace |
| `app/src/main/cpp/CMakeLists.txt` | the native build: `warden` (the runtime, the runtime tree, -O2) and `daogame` (the generated code, never packed) |
| `tools/` | §3.4's pipeline, the checks, the device scripts (`boot_capture.sh`, `route.sh`, `measure_ostagar.sh`, `deploy.sh`, `appbringup/run.sh`), `resolve_to_png.py`, `dao_erf.py` (ERF archives) |
| `tools/routes/` | the routes: plain text, no game material |
| `docs/` | `PLAN`, `BUGS`, `IMPORTS`, `TESTING`, `TOOLCHAIN`, `SIGNING`, `ARCHITECTURE`, `DECISIONS`, `EDRAM`, `GLYPHS`, `SECOND-SCREEN`, ... |

---

## 7. Things about guest execution that are not obvious

1. **Indirect calls go through a table in guest memory.** XenonRecomp does not resolve
   them at translation time: `PPC_LOOKUP_FUNC` reads a host pointer from `base +
   IMAGE_BASE + IMAGE_SIZE + (uint32_t(y) - CODE_BASE) * 2`. `warden_guest.cc` writes the
   53,108 entries (31 MB at `0x83400000` for TU7, `0x833F0000` for TU6). `PPC_CODE_BASE`
   is `0x82210000ull`, so the subtraction is 64-bit and a null call faults 2^33 away from
   where a 32-bit model puts it - the fault handler decodes it. The image's size differs
   by title update, so the runtime's `PPC_LOOKUP_FUNC` takes the layout from the loaded
   game library (`warden_game_library.h`, force-included into every runtime file):
   XenonRecomp's own bakes in the size it was generated with (BUG-103).
2. **A stub returning 0 is not neutral.** For an `NTSTATUS` it says the call worked, with
   its outputs unwritten. Both early startup crashes were this (`MmQueryStatistics`, then
   `NtQueryDirectoryFile` - BUG-030); `MmGetPhysicalAddress` → 0 set the ring buffer up
   at physical 0. When a thread does something senseless, look at the last stub it
   reached and read the title's call site.
3. **The title keeps events inside its own structures** and hands the kernel their
   addresses; `ObjectAtGuestBody` adopts them from the dispatcher header. It also calls
   imports **through function pointers**; the function table maps every import thunk to
   its `__imp__`, so that already works.
4. **Xenia's logger buffers, and `xe::FlushLog()` sleeps 10 ms.** Never flush per line in
   an import: it made each thread's pace depend on how much it logged (BUG-034). The
   fault handler drains the logger once before its report.
5. **A SIGSEGV on a guest store may be the write watch working:** physical pages the GPU
   watches are read-only and a write is serviced and retried (`warden_memory.cc`). The
   title's **code is read-only from boot**, so a fault there is always corruption and the
   handler prints the writer's chain.
6. **Identify a crash by the guest call chain.** The host backtrace changes between
   builds (inlining at -O2), and `ctx.lr` / `ctx.r1` can lag the code; the guest stack
   cannot.
7. **The Adreno 740 has quirks that look like translator bugs:** coverage undefined
   unless the shader writes `gl_SampleMask` (session 29), compute shaders must target
   SPIR-V for Vulkan 1.1 (`LocalSizeId` is ignored - `build_xenia_shaders.py`), and
   `DenormFlushToZero` zeroes `gl_FragCoord` (BUG-045). Suspect the driver when the
   validation layer is silent and the state looks ordinary.
8. **Every guest thread gets a block with its PCR (r13), KTHREAD and TLS**
   (`warden_guest.cc`): the title reads its TLS, current thread and current core
   straight through r13.

---

## 8. Working agreements with the owner

These were stated by the owner in earlier sessions and kept in the old workstation's
Claude memory, which does not move with the repo. They are also in `CLAUDE.md`.

- **Commit and push every finished piece of work without asking**, with the doc updates
  in the same commit: `STATUS.md` (header, phase board, §4 facts, §9 session log),
  `docs/PLAN.md` ticks and corrections, `docs/BUGS.md` for every bug found (upstream and
  our own, with what it causes), `docs/IMPORTS.md`, `docs/TESTING.md` and this file when
  they describe what changed. End commit messages with `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`.
- **The never-commit check before every commit** - it must print nothing:

  ```bash
  git diff --cached --name-only | grep -Ei '\.(iso|xex|xexp|erf|gda|tlk|apk|so)$|^TU_|^build/|^recomp/|^work/'
  ```

  The repo holds no game material, ever. Only an APK that `python tools/release.py`
  passes is published - no game code or files in it (DECISIONS D14).
- **Nothing private in the repository** (it is public): no server or LAN addresses, no
  device serials, no account names in paths, no games or apps anyone uses. Write
  placeholders (`<repository URL>`, `<the Thor's address>`); machine-local settings go in
  gitignored files such as `tools/game_check.local.txt`.
- **A push can fail once** with "Failed to authenticate user" (the credential manager
  refreshing its token) and succeed on an immediate retry. Retry once before reporting.
- **No online functionality in the app.** Network imports answer like a console with
  no cable (`warden_net.cc`); never open a host socket, resolve a name, or add the
  INTERNET permission. Winsock events as real wait objects are fine - that is not
  networking. (Fetching tools onto the workstation, as `bootstrap.py` does, is not the
  app and is fine.)
- **Scope setup requests literally.** "Install the tools" means the tools, then stop and
  report; ask before running the build or verification pipeline.
- **Boot the device with `bash tools/boot_capture.sh <run> <settle> [--install]` as a
  short command of its own**, and read the log with separate short greps. Long chained
  adb-and-grep one-liners were repeatedly refused by the tool's safety check.
- **Bash heredocs mangle backslashes** in this environment: a `"\n"` in C++ written
  through `python - <<'EOF'` becomes a real line break. Write source edits with the
  Edit tool, or a script file written with the Write tool. Never run an edit and the
  Gradle build in the same parallel batch - the build compiles the stale file.
- **Never uninstall the app on the Thor** (§3.1).
- **On the original workstation - the owner's gaming PC - check for a running game
  before any build** (Gradle, `bootstrap.py`, `recompile.py`, other long compiles): a
  process under a game library, one a launcher started, or a big user of the GPU. If one
  is running, ask before building. A launcher open with nothing launched is not a game
  (session 32). `python tools/game_check.py` names candidates; the games a builder lets
  run during a build go in `tools/game_check.local.txt` (gitignored).

---

## 9. Gotchas that cost an hour

- **The signing key** (§3.1) - and `tools/appbringup/run.sh` carries on after a failed
  install and reports on the old build (BUG-028); `boot_capture.sh --install` shows the
  install's output.
- **Windows binaries cannot read MSYS paths.** `adb`, the NDK tools and `python` are
  native; `/c/...` fails. `cygpath -w` / `-m`, and `MSYS2_ARG_CONV_EXCL='*'`.
  `adb push` from Git Bash wants a Windows path (`route.sh` copies through `/sdcard`).
- **User-scope environment variables are not in an old shell** (§3.2).
- **CMake 4 is on PATH, the SDK's CMake 3.31.6 is Gradle's.** CMake 4 rejects
  `cmake_minimum_required(VERSION < 3.5)`, which vendored files still declare; that is
  why Gradle is pinned to the SDK's.
- **XenonRecomp resolves TOML paths against the config file's folder** and needs its
  output folder to exist; either mistake aborts with `0xC0000409` and no message.
  `recompile.py` handles both.
- **`lld` reports 20 errors by default**, a different arbitrary twenty each time;
  `--error-limit=0` is set - leave it.
- **The Thor's screen must be awake** or no surface is created and it looks like a
  surface bug. **Never put a background on the `SurfaceView`** (BUG-026).
- **`screencap -d` wants the physical display id**, not the logical one (`deploy.sh`
  resolves it); a sleeping panel screenshots empty. The guest's own frames are better:
  `files/guest-output-N.png`.
- **`project(... CXX)` does not enable C**; a `.c` file then compiles as C++.
- **One Gradle build at a time.** A second one started while the first still links
  writes the same ninja log and leaves it truncated ("premature end of file").
- **A release build has no run-as**: its files are only reachable through the app, and
  the making on the device is followed in logcat (`docs/TESTING.md`, "The first run on
  a clean install").
- **Read the record, not the sequence:** a raise after `NtOpenFile` looked like the title
  reacting to a failure; it was the title naming its thread (`0x406D1388`).

---

## 10. Keeping the record straight

- **`STATUS.md`** - every session: the header (date, phase, state, **next action**,
  blockers), the phase board, §4 facts, a §9 entry. Its §10 says how.
- **`docs/PLAN.md`** - tick tasks; correct the plan when measurement contradicts it, and
  say so in the plan.
- **`docs/BUGS.md`** - every bug, with what it causes; mark it fixed when fixed; keep IDs
  stable (the index at the top, the entry below). 102 entries at 0.1 Open Alpha: 81
  fixed (one as an option), 5 open, 4 watched, 9 worked around, 1 won't fix, 2 the
  game's own.
- **This file** - when the setup, the device's state or the next task changes.
- Commit messages carry the reasoning; they are long on purpose.

---

## 11. Context worth having before decisions

1. **DAORecomp's presentation features are Direct3D 12 only.** HDR, FSR 3/4, XeSS,
   DLSS, motion vectors and frame generation live under `gpu/d3d12/` with no Vulkan
   counterpart. **FSR 1 is the exception** - already built for Vulkan and wired into
   the presenter.
2. **HDR10 has no Vulkan colour space on this device** (BUG-009, BUG-025): no
   `VK_COLOR_SPACE_HDR10_ST2084_EXT` even with `VK_EXT_swapchain_colorspace`. Since
   session 35 it is reached anyway (PLAN 9.2): the 10-bit swapchain's buffers are tagged
   BT.2020 PQ directly and the last pass encodes PQ. Wide-gamut Display-P3 SDR stays the
   default (task 9.3).
3. **The EDRAM risk was downgraded** ([`docs/EDRAM.md`](docs/EDRAM.md)): no format
   degradation is active. The one-tile default (§2.3) changes the scene's render target
   layout from the console's; keep that in mind if a pass looks wrong (`scene-msaa`
   compares).
4. **The handheld's pad names itself an "Odin Controller"** (vendor `0x2020`, product
   `0x0111`), not a Thor - Phase 7 keys on vendor/product.

---

## Appendix - how the work got here

The session log, newest first, is `STATUS.md` §9; each bug's story is in
`docs/BUGS.md`; the reasoning for each change is in its commit message. The arc: the
recompilation was verified on the host and on the Thor (Phases 1-2: 52,857 functions,
6,635 differentially tested, 98.70% identical, no logic bugs); Xenia's Vulkan backend
was grafted and accepted by the Adreno (Phase 3); the app extracts and verifies the disc
and presents (Phase 4); the kernel and XAM imports took the title through startup, its
packages, the title screen and the main menu (Phase 5, sessions 22-31); session 31 took
it through character creation and a scripted route to Ostagar, fixed two driver
behaviours and the ARM64 title update, raised the frame rate, and added the keyboard,
saving and the bootstrap.
