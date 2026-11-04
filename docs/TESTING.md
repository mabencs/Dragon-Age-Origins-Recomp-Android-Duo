# Testing and the device loop

How we find out whether anything actually works.

---

## 1. Test device

| | |
|---|---|
| Device | AYN Thor (`adb devices` lists it) |
| Connection | USB, adb available |
| Android | 13 / API 33, arm64-v8a |

Full capabilities in [`../device/thor-profile.md`](../device/thor-profile.md).

This is a **single-device project**. We are not chasing broad Android compatibility —
we target one handheld with known capabilities. That is a significant simplification and
we should use it: no need to handle GPUs other than Adreno 740, no need to support
single-screen devices beyond degrading gracefully, no need for 32-bit.

The one place to stay generic anyway: **discover the second display by capability, not
by model name** (see [`SECOND-SCREEN.md`](SECOND-SCREEN.md) §2). It costs nothing and
avoids a brittle device check.

---

## 2. Test layers

Ordered cheapest-first. Each layer catches things the one below it cannot.

### Layer 1 — asset verification (working now)

```bash
python tools/verify_assets.py .
```

Confirms the disc and title update are the right editions before anything else runs.
Catches the single most expensive mistake available: recompiling against the wrong disc.

### Layer 2 — host build (Phase 1.5)

Build the recompiled code for **x86-64 host** before ever targeting ARM64. This exists
so that when Phase 2 breaks, architecture is the only new variable.

Also serves as the **reference implementation** for Phase 2.3's divergence hunt: when
ARM64 behaviour looks wrong, diff it against the host build rather than guessing.

### Layer 3 — CPU self-tests on device (Phase 2.5)

A bare native harness that loads `libdao.so` and runs CPU-only tests with no GPU
involvement. Isolates recompilation and SIMD correctness from renderer problems.

Particularly important for the Phase 2.3 hazards — `char` signedness, `long double`
width, denormal handling — because those produce *subtly* wrong results rather than
crashes, and a renderer bug and a SIMD bug look identical once a frame is on screen.

### Layer 4 — renderer validation (Phase 3.9)

```bash
# enable the Vulkan validation layer on device
adb shell setprop debug.vulkan.layers VK_LAYER_KHRONOS_validation
```

Run validation-clean. Capture frames with RenderDoc or GFXReconstruct (task 3.10) and
compare against a known-good D3D12 capture from the Windows build where possible —
having an upstream reference renderer to diff against is a real advantage and we should
exploit it.

### Layer 5 — milestone gates

| Gate | Criterion |
|---|---|
| **Phase 5 — First light** | Main menu renders correctly, audio plays, controller navigates it. Commit the screenshot. |
| **Phase 6 — Playable** | Character creation → Ostagar, no crashes, stable framerate |

### Layer 6 — soak testing

| Phase | Duration | Watching for |
|---|---|---|
| 10.5 | 4+ hours | Long-session stability, memory growth |
| 10.6 | full charge | Battery life |

---

## 3. Device loop

`tools/deploy.sh` — build, install, launch, tail logcat in one command.

```bash
./tools/deploy.sh              # debug build, install, run, follow logs
./tools/deploy.sh --release
./tools/deploy.sh --no-build   # just reinstall and run
```

### Useful adb incantations

Note for Git Bash on Windows: MSYS rewrites anything that looks like a Unix path, which
mangles `adb shell` arguments. Either quote the whole remote command or set
`MSYS2_ARG_CONV_EXCL='*'`.

```bash
export MSYS2_ARG_CONV_EXCL='*'

# Logs, filtered to us
adb logcat -s DAORecomp:V DAO:V Vulkan:V AAudio:V

# Clear then follow
adb logcat -c && adb logcat -s DAO:V

# Native crash backtraces
adb logcat -b crash

# Screenshot the MAIN screen
adb exec-out screencap -p > main.png

# Screenshot the SECOND screen (display 4)
adb shell "screencap -d 4 -p" > deck.png

# Display state (second-screen debugging)
adb shell dumpsys display | grep -E "mDisplayId|Screen-2|FLAG_PRESENTATION"

# Thermal status
adb shell dumpsys thermalservice | head -30

# GPU/CPU frequencies
adb shell "cat /sys/class/kgsl/kgsl-3d0/gpuclk"

# Frame timing
adb shell dumpsys gfxinfo <package> framestats

# Memory
adb shell dumpsys meminfo <package>
```

### Reading a guest crash (Phase 5)

The fault handler writes a report to logcat under the `Warden` tag at fatal level:

```bash
adb logcat -d -s Warden:F
```

It holds, in order: what the fault was (a null indirect call is decoded to the guest
address it tried to call; a write into the title's read-only code is named as such),
the guest registers, the **guest call chain** - each frame named by the recompiled
function holding its return address, checked against the `bl` above it - the newest
frame's saved r26-r31 with the words each points at, and the **guest function trace**:
the last 512 translated functions the faulting thread entered, oldest first. The trace
is the `WARDEN_GUEST_TRACE` option in `app/src/main/cpp/CMakeLists.txt` (on by default;
a thread-local store per guest call - turn it off once the title runs). It is
force-included into the generated sources; a trace that shows only kernel import names
and no `sub_XXXXXXXX` means the prologue is not being compiled in (BUG-037 was exactly
that, for a day). `llvm-nm` on a guest object must list `U warden_guest_trace`.

Alongside, under `warden:V`, every wait, signal, thread creation, open, read and
directory entry is logged with the calling thread's name in `[brackets]` - the only
attribution there is, since Xenia's logger writes every line from its own thread and
logcat's tid names that thread, not the caller. Budgets: per call site for opens and
reads, per listing for directory entries (48, then the end of the listing), so a walk
of the voice-over directory cannot hide the package mount that follows it.
`./tools/appbringup/boot_repeat.sh 3` compares boots by the stubs reached; that count
varies with thread scheduling now that the title is multi-threaded, so a difference of
one is not a finding.

**Where every guest thread is:** every 5 s a `guest threads, the last 5 s:` block lists
each thread with the calls it made since the last report and its last functions
entered, repeats collapsed (`KeDelayExecutionThread*128`). "0 calls - parked" ending in
a wait's name is a thread blocked in that wait; millions of calls ending in a delay is a
spin, and the one-off `KeDelayExecutionThread(...) from A in sub_X < B in sub_Y` lines
name the loop. `grep -A14 'guest threads, the last'` on the log.

**Where a thread is, exactly:** a file `files/sample-threads` holding a thread name (a
substring; `Update`) makes each 5 s report interrupt that thread with a signal and
print its guest registers and call chain (`[Eclipse::Update] sampled here:` then the
fault handler's `#0 ... in sub_X called sub_Y` frames with saved registers and the
words they point at) - what a parked thread's trace ring cannot show, since a thread
spinning inside one guest function enters nothing new. Without the file, threads
parked without a recorded wait are sampled. **Who signals whom:** the report is
followed by `signals and blocking waits since the last report:` - a count per
(kind, handle, object body, thread) of `set`/`pulse` (events), `KeSet`, `release` and
`KeRelSem` (semaphores), `unmutant`, `wsa-set` and `wait` (a wait of more than a
millisecond). A thread waiting on a semaphore nobody releases is read straight off
it. **XAudio:** every 5 s `XAudio: N callbacks (H held for the device), N frames
submitted (S silent, D dropped), peak; output T s played, U s of it silence for want of
a frame, Q queued, consuming` - the render worker and the sound output: `held` counts
callbacks delayed while the stream's queue was full (the device setting the pace), `U`
should stay 0, and `peak` above zero means sound; `XAudio output:` lines say when the
AAudio stream opens (its rate, burst and sharing mode), pauses, resumes or is reopened.
**XMA:** once the title has touched a context, every 5 s `XMA: N contexts
allocated, passes (packets through the silent pass); calls in the last 5 s:` counts each of the
sixteen `XMA*` imports and prints each allocated context's fields. **FMOD's memory:**
`FMOD memory:` lines (`warden_fmod.cc`, a hook on the recompiled allocator) describe
FMOD's memory system on its first request - a fixed pool with its block count and
size, or the title's own allocator - then every failed allocation with FMOD's source
file and line and the running totals, and every allocation of 32 KB or more. BUG-043
was read off these lines.

**What the game is doing:** `warden_game.cc` hooks the title's own code and logs, with
bounded budgets: `game: mode ... requested` (with the chain that asked, the first
times) and `game: mode is now N (name)`; `title log LEVEL: event - message`, the
title's own debug log; `game: event type N -> script "name"`; `game: config section /
key -> "value"`; `game: GUI open "screen"`; `game: play cutscene`; `game: start menu
command`; `game: GUI command` (the Scaleform menus' calls); `game: StartCharGen` and
`game: character generation mode entered`. The header comment of the file names each
function and the mode numbers. **The guest image as loaded:** a file
`files/dump-image` makes the loader write the guest image to `files/image-at-load.bin`
and, 40 s later, `files/image-at-40s.bin` (20 MB each); compare with
`build/xex-check/image.bin` from `python tools/check_xex.py --build ppc_tu6 --patch
auto` - only the 12 import-variable slots near `0x820007F0` should differ at load
(BUG-046).

**Pressing buttons without hands on the device:** `files/inject-pad` holds one press per
line, `<buttons in hex> [<delay s> [<hold s>]]` - the `X_INPUT` button mask (START
`0010`, BACK `0020`, A `1000`, B `2000`, X `4000`, Y `8000`, the d-pad
`0001`/`0002`/`0004`/`0008`), pressed in slot 0 `delay` seconds after the file is first
seen (the title's first poll, about two seconds after the entry point) for `hold`
seconds (default 1). While the file exists slot 0 counts as a
connected pad, and a real pad's buttons are OR-ed in. A line may go on with the
triggers and the sticks, held as long: `<buttons> <delay> <hold> <lt> <rt> [<lx> <ly>
[<rx> <ry>]]` - triggers 0-255, sticks -32768..32767 with up and right positive (`0000 1
1.0 255 0` holds LT for a second). Injected triggers take the larger of the real and the
injected value; an injected stick that is not centred replaces the real one. Rewriting the file (a new modification time) starts the
sequence again. `0010 45 1` then `1000 56 1` takes the title from its title screen
through "You have not selected a storage device... A Yes", and `1000 66 1` presses A on
"New Game"; `4000 120 0.3` then presses X, which skips the intro movie and reaches
character creation at about two minutes. The log says `inject-pad: N presses: ...`
when the file is read. **The name keyboard** is an Android dialog the app polls for:
a run that types into it unattended uses `adb shell input text <name>` then `adb
shell input keyevent 66` once the log says `waiting for the app's keyboard`;
`files/keyboard-default` makes the title take its default name without a dialog,
as before.

**Saves** live in the app's internal files directory, one folder per save:
`files/content/00000001/<the title's file name>/Saves/` (`savegame.das`, `savegame.met`,
`scrnshot.bin`, each with a `.x` checksum file), the display name beside the folder as
`<name>.name`, and a replaced save in `files/content/00000001/.backup/`. Type
`00000002` would be downloadable content. The log lines are `XamContentCreateEx(...)`,
`content root save: is ...`, `NtCreateFile("save:\...")` and `XamContentClose`. Look
with `adb shell run-as dev.daorecompandroid.duo find files/content`; keep a copy before a test
that may overwrite one with `adb exec-out run-as dev.daorecompandroid.duo tar c files/content >
build/saves-<run>.tar` (put one back with `adb exec-in run-as dev.daorecompandroid.duo tar x <
build/saves-<run>.tar`). Deleting `files/content` deletes every save. **The menus** (runs 202-203): with a
save on the device the main menu starts with **Resume** (Resume / New Game / Load Game /
Downloadable Content / Options / Extras), which loads the newest save - `bash
tools/route.sh load` presses START and then A on it (with no save, that A answers the
storage prompt instead). START in the game opens Resume Game / Save Game (focused) /
Load Game / Options / Achievements / Exit Game. Save Game lists New Save first, then the
saves; a new save asks for a name (A keeps "Dragon Age"), an existing one "Are you sure
you want to overwrite this saved game?" - A. Load Game lists the saves newest first; in
the game it then asks "You will lose any unsaved progress", which can take seconds to
appear - A. **Names:** `0xd5332a7_<n>_u!0` is "Save (n)", `0xd5332a7_<n>_a!0`
"Auto-save (n)"; the display name in `<name>.name` is `<character>_<save
name>_<YYYYMMDDhhmmss, UTC>` (garbage in the saves from before BUG-048's fix). **A
load:** `XamContentCreateEx(... flags 00000013) -> 0 (disposition 2)`, then
`savegame.das.x` and `savegame.das` - read whole, then partly again after a seek (the
read log's running total passes the file size; seeks are not logged) - then mode 9 and
mode 0. A list preview reads `savegame.met` and `scrnshot.bin` (all zeros so far:
BUG-049). **Driving menus live:** rewriting `inject-pad` restarts its sequence, timed
from when the new file is seen, so a test can push one press at a time (`1000 1 0.15`)
and read the captures (`files/capture-every 1`, one every 5 s) in between - or, quicker,
`adb exec-out screencap -p > shot.png` a few seconds after each press. **Character
creation** (run 292, the `chargen` route): one Character Generation screen - Gender,
Race, Class, Background in turn, the d-pad moving along a row, A selecting (the model
changes on A), B back a row; then the name dialog (Enter, `adb shell input keyevent
66`, takes the background's default); then Appearance and Voice - Preset, Next and the
categories (Skin, Hair, Eyes, Nose, Mouth, Jaw / Cheeks, Neck / Ears, Portrait, Voice):
the d-pad picks one, A opens its sliders, the d-pad picks a slider and **the left stick
moves it** (`0000 1 0.9 0 0 -32000 0` holds it left for 0.9 s), B leaves; Next goes on
to Attributes, Skills and Talents, each moving on once its points are spent (A adds),
then the difficulty (A) and the origin.

**Scripted routes:** `bash tools/route.sh <name>` puts `tools/routes/<name>/*` in the
files directory (`off` removes `inject-pad` and `route`, `show` prints them). `ostagar`
reaches explore mode at the King's Camp in about two minutes with no hands, `wilds`
at the Korcari Wilds in about 108 s and then walks into the first fight there, wolves
(runs 218-220), `morrigan` the same fight from the end of the Prelude with Morrigan's
spells (run 223), `chargen` reaches character creation, `load` presses START and then A on Resume. With a save on
the device START goes straight to the main menu and Resume is its first item, so for a
route that starts a new game (a `.new-game` file in its folder: `ostagar`, `wilds`, `morrigan`, `chargen`)
`route.sh` turns the 56 s A into a d-pad down, onto New Game. At the King's Camp the
title's forced tutorials held every button but the one they named: "Level up now by
pressing" the ◀ glyph - the Back button, so BACK - then in the Character Record A, Y
(auto level up), A and B; then "Mapping Abilities ... open the radial menu with LT",
which an injected LT does (run 210: `RadialQuickbar` opens); then "Open the Spells or
Talents menu", which B, BACK, RT and the left stick right with A did not do. START opens
the pause menu only once the chain is through. The switch for all of it is Options ->
Game -> Interface -> Show tutorials, the page's last item (runs 211-213; the presses are
in `RESUMING.md` §5.2) - the owner's profile setting, **off since run 214** (copies of
the profile from before and after: `build/profile-before-tutorials-off.tar` and
`build/profile-after-tutorials-off.tar` on the original workstation), so START opens the
pause menu at the King's Camp at once. **The prelude debug menu** (`zz_pre_debug`):
"What do you need to do?" 1 Jump to an area, 2 Do something not specific to an area, 3
I'm all done; Jump to an area lists 1 Start in the King's camp, 2 Start in the Korcari
Wilds, 3 the night version of the King's camp, 4 the signal tower, 5 the top of the
signal tower, 6 (MORE). After the King's camp the menu comes back with four items, the
fourth I'm all done; after the Korcari Wilds it does not (runs 215, 218). Its "Do something not specific
to an area" is quest shortcuts; the area list's (MORE) has 1 Flemeth's hut, 2 the end of
the Prelude, 3 its very end with Morrigan, 4 the Tower of Ishal's third level, 5 the
King's camp with the opening cutscene, 6 (Start over.) (run 221). `files/route` is what `warden_game.cc` does on its own:
`skip-start` gives the module's start script (`sp_module_start.ncs`) a name that does
not exist, so New Game falls back to the default start area with a default player;
`call <script> [mode]` calls a script's native main, found by name in the engine's
script registry, once the game is in that mode (default 0, exploring) - the console
build keeps BioWare's debug scripts (`zz_pre_debug` for Ostagar, `zz_bhm_debug` and
the like per origin), each opening a debug menu; `run <script> [mode]`
dispatches `<script>.ncs` through the engine's dispatcher instead, which refuses the
debug scripts' names. `files/capture-every` with N captures the guest output every N
status ticks (5 s) instead of the default schedule - for reading menus a route steps
through. The log lines are `route: ...`.
**Fights and conversations** (runs 219-220): the log says `mode is now 1 (combat)` when
a fight starts and `0 (explore)` when it ends. The left stick moves relative to the
camera, which does not turn by itself (the right stick turns it). A on a target attacks
it, and X, Y and B cast what is on the quickbar; when the controlled character falls,
control passes to another party member, and when all have fallen the log says `mode is
now 2 (dead)` and the death screen offers Load Last Save (A) / Load Game / Change
Difficulty / Quit (runs 221-222). After
a fight, A on a corpse opens its loot (mode 3) and A again closes it, and A on a party
member walks over and starts a conversation. A conversation's option list wraps - up
from the first option selects the last. The voice-over is read from FMOD banks,
`audio/vo/en-us/vo/<area>_<speaker>.fsb` in the module (`pre100_jory.fsb`), with the
lip sync from `audio/vo/en-us/facialanimations/modulefacialanimations.erf`; a voiced line
shows in the `XAudio:` peak (0.36-0.43 for Jory, against 0.02-0.09 between lines).
**One boot, captured:** `bash tools/boot_capture.sh <run> [settle-seconds] [--install]`
starts the title, waits for the entry point, lets it run `settle` seconds, saves the
whole logcat to `build/runN.log`, stops the app and prints the swaps and input lines,
spin chains, the stubs reached and the last thread report. The `guest-output-N.png`
captures are in the app's files directory afterwards, N being the 5-second status
tick: `-2` at 10 s, `-4` at 20 s, `-8` at 40 s, `-12` at 60 s, then 80, 100, 120 and
150 s, and no more (BUG-079; `files/capture-every N` for every N ticks all the way instead -
later captures of earlier sessions are removed at the start unless that file is there).
**A debug jump in the reference** (session 40): the installed reference build carries
the port's patch `DAO_RUN_SCRIPT=<script>[@<seconds>]` (tools/reference_build.py's
PATCHES), which runs a script as the player's character that many seconds after the game
is first playing; `DAO_INPUT_SCRIPT` then drives its menu. From the owner's camp save:
`DAO_RUN_SCRIPT=zz_den_debug@10 python tools/dao_reference.py run <name> 150 --input
"20 START; 38 A; 56 DOWN 0.2; 58 A 0.2; ..." --capture 2` - the menu is up at about 51 s.
The original executable is kept as `C:\Games\DAORecomp\DAORecomp.exe.orig`. How BUG-085
was found to be the game's own.
**Occlusion-query culling** is off since session 40, as in DAORecomp (BUG-085's notes):
`files/occlusion-queries` keeps the game's queries, for comparison.

**The reference renderer** (PLAN 3.3b; RESUMING.md 3.5b): DAORecomp's Windows build at
`C:\Games\DAORecomp`, **started only with `python tools/dao_reference.py run <name>
<seconds> [--input <script>] [--capture <s>] [--set section.key=value]`** - the owner's
rule: `tools/game_check.py`'s verdict first (a possible game stops it; `--game-ok` once
the owner says so), the window behind the owner's (checked every second; the summary
says whether it stayed there), muted, windowed 1280x720, no launcher, the owner's
`config.toml` put back; the captures (`capture_N.png`, every 2 s) and logs in
`build/reference/<name>/`. `--input` is DAORecomp's scripted gamepad, `"<time s>
<BUTTONS> [hold s]; every <period s> <BUTTONS> [hold s] [from <s>] [until <s>]"`: with the
intro logos skipped the title says Press START at about 16 s, the main menu is up by 34 s
(`"20 START; 38 A"` resumes the newest save; a fresh profile shows the DLC notice
first). `bash tools/reference_save.sh '<save folder>'` copies a save and the game's
profile from the Thor (the same scene, the same settings: the tutorials off).
`bash tools/scene_capture.sh <run> <presses file> <last tick> [first tick]` captures the
Thor's side every 5 s (it wakes the Thor first: launched with the screen off the app is
paused and the game held). `python tools/compare_frames.py <port.png> <reference.png>
<out.png>` sets the two beside each other with their difference.
**Saves and the routes** (session 35): the `wilds` and `morrigan` routes start a new game
as the test character, "Jaden", and autosave into his slot (`0xd5332a7_1_a!0`) on entering
the Wilds; `ostagar` jumps by debug script and saves nothing. The owner's own game is
the owner's character (`0x36db42d6`). A route that resumes (`.resumes`: `load`) loads the newest save,
whoever's it is, so `tools/route.sh` refuses it unless the newest is Jaden's
(`OWNER_SAVE_OK=1` overrides, with the owner's word); a scratch script that presses
Resume must check the same. `files/save-chain` (9.13) logs the save path's calls - the
save manager's, the save job's, the load/save state machine's - with their guest call
chains.
**How full the heaps get** (9.9): `files/heap-scan` holding a period in seconds (none:
15) has a thread walk every physical allocation of 1 MB or more in 64 KB blocks and log
`heap-scan: <address> <size> - <MB> of 64 KB blocks written, the highest ending at +<MB>`
- a rough high-water mark (zeroed blocks in use read as empty, freed ones still holding
data as full).
**The Ultimate Edition's disc 2** (PLAN 10.10, runs 502-504): the settings' Ultimate
Edition disc 2 row, the image picked (it must be on the device - the Thor's Download
folder holds it); logcat's `image identified ... title FFED2000 media 6FB2B193`, `DLC:
installed <name> (<content ID>, N files)` for each of the eleven, `the standalone disc's
Awakening package removed`; the row says 11 installed. In the game: New Game's carousel
(right on the d-pad) Origins, Awakening, The Darkspawn Chronicles, Leliana's Song, The
Golems of Amgarrak, Witch Hunt. The rest of this paragraph is the standalone Awakening
disc's way, which the launcher no longer offers.

**Awakening** (PLAN 10.10): installed as a DLC package -
`files/content/00000002/DAO_PRC_EP_1/DAO_PRC_EP_1/` (the Awakening disc's
`Addins/DAO_PRC_EP_1/`) and `DAO_PRC_EP_1.name`; the log's `XamContentCreateEnumerator(...
content type 2 ...) -> ..., 1 item(s)` and `XamContentCreateEx(user 0, "PRC0000:", type 2,
"DAO_PRC_EP_1" ...)`. From the main menu (START at the title; with saves, Resume first):
New Game shows Origins and Awakening; right and A pick Awakening, then Create New
Character, A through the screens, Quick Play (Xbox Y), A on the difficulty (run 490).
Keep a copy of the saves first and remove the test character's afterwards. **The
launcher's row** (runs 491-492): Awakening (disc image) - the picker's tap on the image
(it must be on the device), then `image identified: ... media 2E6F7560`, `extracting 3474
files ... .DAO_PRC_EP_1.installing/DAO_PRC_EP_1`, `extraction complete`, `Awakening
installed in .../files/content/00000002/DAO_PRC_EP_1`; the row says Installed. **The
Downloadable content row** (run 493): logcat's `DLC: Installed <name>.` or why not;
`files/content/00000002/<content ID>/` and its `.name`. The installer alone, on the Thor:
build `warden_dlc.cc`, `warden_stfs.cc` and `warden_xdvdfs.cc` with a `main` calling
`warden::dlc::Install(open(argv[1]), argv[2])` and a stub `xenia/base/logging.h` (the NDK's
`aarch64-linux-android33-clang++ -std=c++20 -static-libstdc++`), push it to
`/data/local/tmp` and run it on a package or a disc image. A title update with its content
type (offset 0x344) set to 2 stands in for a DLC package. `adb push` of
a folder into `Android/data` fails ("failed to read copy response"): stream a tar with
`adb exec-in "tar -x -C <dir>"` instead.

**The game's menu, opened and closed by script** (BUG-081): at the camp, `echo 0020 0.5
0.15 > files/inject-pad` (Back) opens it and `2000` (B) closes it - each write restarts
the presses; the `frames:` line's `max` in the 5 s after a close shows a freeze, and
`Frame generation: the generator is up ...` would show a generator made again (run 483:
none).

**How the last session ended** (10.7): `files/exits/` holds a report for every exit that
was not the app's own end or a stop from outside (`<time>-game|deck-<reason>.txt`, and
`.tombstone.pb` or `.anr.txt` beside it when Android kept a trace); the launch row says
how the last session ended; logcat's `WardenExit` lines. Android's own record: `adb shell
dumpsys activity exit-info dev.daorecompandroid.duo` (the app's tag shows as `state=N bytes`). To
try it: `adb shell run-as dev.daorecompandroid.duo kill -9 <pid>` (signaled) or `kill -SEGV <pid>`
(a native crash with a tombstone) during play, then start the app (run 479). Script
force stops are not reported.

**The heat guard** (10.2, BUG-117): Options -> Heat guard opens three options, all live: the
frame-rate guard off by default, the two rests on. `files/thermal-guard` holding `on` (Lower the frame rate when hot); the log:
`heat guard: on|off`, then `heat guard: hot (GPU 75.4 C, big cores 80.1, thermal level 0) - the
frame rate held to 45 (60 chosen)`, `heat guard: cool again ...`,
`heat guard: battery saver on ...`, each with `frame rate: 45 on the screen (held there; 60
chosen)`. To see it work at the temperatures a run reaches, `files/thermal-guard-test`
holding the hot and cool temperatures (`78 76`; run 476). Android keeps the battery saver
off while the Thor is powered: `adb shell dumpsys battery unplug`, then `adb shell cmd
power set-mode 1` (and `0`), then `adb shell dumpsys battery reset` (run 477).
`files/heat-cpu-rest` and `files/heat-gpu-wait` are the other two, on unless they hold `off` (the
log at the first wait: `heat guard: heat-cpu-rest on`). Heat levels out slowly: measure at least 12
minutes and read the last four (the fan's own curve decides much of it - say what it was).

**An autosave as the game is put away** (9.13): on by default; `files/autosave-on-pause`
holding `off` turns it off. Put away while exploring, the log says `autosave on pause:
asked for at the next frame`, `DoAutoSave called`, the game's saving screen (mode 9),
`an autosave written: <name>` (a `_N_a!0` container closed, about 2.5 s later) and `the
guest holds`; anywhere but exploring, `not exploring ...; none` and the hold at once. To
try it: the ostagar route, Home (`adb shell input keyevent KEYCODE_HOME`) 12 s into the
camp, the app brought back 10 s later - as the test character only: it writes the
autosave of whoever is playing. `files/save-chain` adds the load/save state machine's
SetState calls, with its fields and the call chain into states 2 and 3.
**Messages to the engine** (9.10, 9.13): `files/send-message` holds lines `<mode> <seconds>
<message...>`, each sent once through the engine's own SendMessage (`sub_825E47B0`, what
the UI's ExternalCommands.SendMessage calls) when the game has been that many seconds in
that mode without leaving it - `0 6 ToggleTacticalCameraMessage`. The log: `send-message:
"..." sent`.
**Corner vignette** (9.21): `files/vignette` holding `off` drops the HUD's vignette draw
(the log: `vignette: the corner vignette skipped (files/vignette)` at its first draw); the
options panel switches it live (`options: vignette is now off`). The draw is known by its
pixel shader and its 256x512 DXT5 texture: a frame trace at the camp (`files/trace-swap`
4000 on the ostagar route, run 677) shows it as `FT draw 45 ... PS 138D82BC28F866CC ... tex0
fmt 20 (8bpp bc) 256x512`.
**Shadows, post-processing** (9.8): `files/shadows` and `files/post-processing` holding
`off` clear the engine's switches every frame (the log: `engine: dynamic shadows off`).
**Colour** (9.3): `files/colour` holds `vivid` (10-bit, tagged Display-P3: the colours
stretched) or `8bit` (Xenia's own 8-bit sRGB, for comparing); none: 10-bit sRGB. The log:
`GPU: colour ...` and the swapchain's format (64, the 10-bit one) and colour space
(1000104001, Display-P3). `adb exec-out screencap -p` gives the panel's picture in
Display-P3 (its PNG says so: "sRGB Transfer with Display P3 Gamut"), so compare
screenshots with each other, not with the guest-output captures.
**Audio** (9.12): `files/headphones` holding `surround` gives headphones the headphone
surround (the log: `XAudio output: device N is headphones - the headphone surround`; the
5 s `XAudio:` line ends with the mix and its cost a frame); the test marker
`files/audio-force-headphones` counts the speaker as headphones. `files/audio-dump`
holding seconds (none: 120) records the title's six channels as submitted to
`files/audio-dump.wav` - pull it with `adb exec-out run-as dev.daorecompandroid.duo cat
files/audio-dump.wav > build/audioN.wav` (1.15 MB a second) and delete it after.
`python tools/audio_levels.py build/audioN.wav` measures each channel and the loudness of
the candidate mixdowns; `--render` runs the recording through the device's own mixer
(`tools/audio_host.cc` with `warden_audio_mix.cc`) and compares the speakers' output with
the headphones'.
**Heat and power** (BUG-117): with the battery full (`/sys/class/power_supply/battery/current_now`
0), the USB-PD input's current times voltage (`/sys/class/power_supply/usb/current_now`,
`voltage_now`) is the device's whole draw - about 10 W at the camp. Per-thread CPU from
`/proc/<pid>/task/*/stat` over a window; `simpleperf record -t <tids> -e cpu-cycles` as the app
(`run-as`; cpu-clock is not offered). `files/hack-spin` keeps both waits spinning whatever Heat
guard says, `files/guest-trace` the guest trace outside Debugging Mode - for an A/B with one
build. Busy loops started over adb must be detached (`nohup ... > /dev/null 2>&1 < /dev/null &`)
or `adb shell` never returns - and killed after.
**4:3** (PLAN 9.22): Options -> Aspect ratio, `files/aspect` holding `4:3` (the log: `video mode:
4:3 - the title is told 960x720, not widescreen (Aspect ratio)`; FSR 3.1's `the upscaled image,
1440x1080` on the Thor). The test switch before it: `files/hack-video-mode` holding `960x720 4:3` (or `1280x960 4:3`)
tells the title a 4:3 mode that is not widescreen and shows it at 4:3 (the log: `video mode:
the title is told ..., 4:3, not widescreen`, `GPU: the presenter shows the title at 4:3`), and
`files/resolution` holding `960x720` or `1280x960` makes the device at that size; FSR 3.1 and
frame generation off. On the Thor's 16:9 screen the picture is pillarboxed. Remove all of them
after.
**Upscaling** (9.5, 9.7): `files/upscaling` holds `fxaa`, `fsr` or `cas` (none: bilinear), set on the
presenter as the title boots (the log: `GPU: upscaling ...`). The guest output captures
are taken before it: to see it, `adb exec-out screencap -p` the panel.
**Frame rate** (9.1, 10.1): `files/frame-rate` holds 30, 45, 60 or 120 - 60 without it
(the log: `GPU: frame rate ...`); frame pacing is paced at 30 only, unless
`files/frame-pacing` says otherwise. The GPU status line every 5 s counts the title's
frames (`N swap packets`): their change over 5 s is its frame rate, scene by scene. Since
BUG-112 the same line counts, per tick, the host's waits for the GPU and their time (`GPU
waits N for X ms (longest Y)`), Xenia's render passes and barriers (`passes N, barriers N (N
image, N buffer)`), and the submits and presents with the host's time inside them. The
King's Camp is the GPU's at about 31 whatever the cap; character creation (the `chargen`
route, mode 11) has room - 60, 69 at 120, 45 even (run 395).
**FSR 3.1** (9.4, in progress): `files/fsr3` holding `quality`, `balanced` or
`performance` turns it on, from the next start - the game's device is made at 1280x720,
1120x630 or 960x540 (`FSR 3.1: the game's device made at WxH`). The log: `FSR 3.1: on
(quality) - the game renders at WxH`, `16-bit arithmetic on the device`, `the copies and
the composite are ready`, then at the first 3D frame `the upscaler is made - WxH to
1920x1080, 16-bit passes` and its images; `FSR 3.1: upscaling` or `not upscaling - <why>`
whenever that changes (a menu or loading has no 3D scene); `the image before the UI is
pixel shader X's` when the post-processing's last pass is learned (`20BA772900FB8AA3`,
the tonemapping, at the camp); and every 150 timed frames `FSR 3.1: GPU ms a frame - the
motion vectors' clear .., FSR 3.1 .. (16-bit), the composite ..; N render target copies a
frame; F fps` (2 copies: the depth and the image before the UI) with `its passes, GPU ms a
frame` - each of FSR's eight passes. `files/fsr3-half` holding `off` takes the SDK's
32-bit passes, `alternate` switches between the two each window (run 403 compares them on
one scene). `files/fsr3-lut` holding `off` has the accumulate pass reproject the history
with the SDK's reference Lanczos weights (sines) instead of its lookup table, `alternate`
switches each window the same way (run 513; the passes line says `the Lanczos lookup
table` while it is used). `files/hack-fetch-raw` has the translated shaders decode each
texture's size, reciprocals, LOD and exponent biases from the fetch constant per pixel, as
before PLAN 10.9, instead of reading what the command processor decoded - one boot each
(the shaders are translated at the start), compared by the `GPU swaps` lines and the
driver's statistics (`files/hack-pipeline-stats`: `CBA8DC016428665B` 1,510 ALU
instructions decoded, 1,631 raw; runs 517-518). In those statistics a vertex shader's
`Texture read instruction count` is its shared-memory loads (the vertex fetches): the
depth prepass's `C8CD5CF9901DFCE3` 2 loads for its 2 words, the foliage's
`90DEF6B7D7356A17` 19 for 19 (run 520) - four times those, with `Flow control` in the
tens or hundreds, means the per-word switch over the shared memory's bindings is back.
A change to the modification's bits leaves the stored pipelines in place (the storage's
version is the larger of the description's and the modification's): the old descriptions
stop matching, the next boot compiles and stores the new pipelines at the camp (run 520:
112 added), and the one after is clean (run 521).

**The areas' tour** (PLAN 10.11, session 38): any of the game's scripts can be called by
name while it runs - `files/route-call` holding the name (`warden_game.cc`, read about
once a second from the per-frame mode hook; a leading `?` only looks the name up and
logs its registry slot and main; a leading `!` runs the script through the engine's own
RunScript (`sub_8263FFD8` in the TU build: engine, a wide-string name, the object to run
as, 1, event -1 - what the PC console's `runscript` called, and what DAORecomp's cheats
call) as the player's character, which reaches the downloadable content's `.ncs`
scripts as well as the native ones: `!zz_gwb_debug` opens Warden's Keep's debug menu,
`!zz_vgk_debug` Awakening's Vigil's Keep's; a leading `~` instead dispatches
`<name>.ncs` through the event dispatcher with the module script's next event, which
ran the DLC's scripts but opened no menu - session 40). The 360 build registers its developer debug scripts, so
`zz_cir_debug`, `zz_den_debug`, `zz_orz_debug` and the other modules' (PLAN 10.11 lists
them) open their numbered debug dialogues anywhere, and their "Jump to area" (named
differently per module) lists the module's areas in pages of three with "more...",
"Back" and "Exit" after them - a d-pad down moves one item, A selects, and the last
page's item 4 can be an area. A call while a conversation is open is ignored; up from
the first item wraps to the last, an exit where the last item is one. `bash
tools/route.sh probe` boots to the default start area with nothing else. The tour's
scripts are `tools/areas/`: `live.sh` (`call`, `press`, `walk`, `shot`, `mode`, `fps`
against the running game), `jump.sh <script> "<presses>"` (ends an open conversation
with B and A, calls, waits for the menu, presses, waits for the load - `jumped <rim>` or
`failed <where>` with a screenshot in `build/areas/`), `area_probe.sh <label>` (ends an
arrival conversation, then the arrival screenshot, 10 s standing, a 6 s walk with
screenshots every 2 s, a 4 s camera turn, a 6 s walk; the frame rate of each part from
the `GPU swaps` lines' own timestamps, the GPU's busy percentage, the game's error
count; a line in `build/areas/results.txt`), `tour.sh <list>` (lines `label|script|presses`,
stopping at the first failed jump). The game's mode line (`game: mode is now N`) is no
longer budgeted, since the tour reads it: 0 explore, 4 conversation, 9 loading. Set
Frame rate 120 and Frame generation off first, and put the owner's back after. The list in
use is `tools/areas/tour.txt` (the areas done commented out, so the tour goes on from the
first live line; the game must be on the probe route first). Learned in the night of
2026-10-31: a menu item that opens a sub-list (Brecilian's forest and ruins, the Deep
Roads, Denerim's estates) needs its own A; a load that finishes between two presses is
missed by the loading wait (Eamon's estate: the wait before the last press); an arrival
conversation the game loops (Zathrian's at the Dalish camp) is not ended by the 120
presses, and the probe then measures the conversation's cameras; the probe's `File` is
the last area file read, at times a neighbour's; BUG-084 killed three of about thirty
loads, which the tour reports as `still-loading`, reboots and retries. `sheets.py` skips
a truncated screenshot; `results_table.py` strips only the leading `al_`. Learned in
session 40 (the menus photographed with `menu_read.sh`, `build/menu-*-sheet.png`): the
menus are not all pages of three - the prelude's "Jump to an area" is five items and
(MORE), Orzammar's main menu six (1 Character/Party Setup, 2 Plot Jump, 3 Turn
Auto-Complete OFF, 4 Area Jump, 5 External Plot Status, 6 Exit) and its Area Jump list
six (the city's four, Deep Roads, Back); a Deep Roads area asks "Which?" and then "Where
in?" (Caridin's Cross: Enter or Hunting Party; Ortan Thaig: Shale Thaig or Deeproad
Outskirts), and from inside the Deep Roads the jump opens the world map instead; the
prelude's signal tower and tower top are **plot-gated** ("The red shirts are required
for this portion of the plot, you will need to start in the King's camp night and use
the debugger again from there"), so they are jumped to from the night camp in one game;
the origins' menus are plot points (Dwarf Commoner: Beginning, Finding Oskias, ...; Human
Noble: Starting the plot -> Without the opening cutscene; City Elf: To a place -> home,
the alienage, the exterior of the palace, the palace; Dalish Elf: To an area -> the
ruins, the forest clearing, the Dalish camp), and Redcliffe's and the Dwarf Commoner's
plot jumps end on a "Jumping. 1. GO!" or "(Continue.)" prompt - one more A each, which
`jump.sh` takes as extra presses (an A after the load starts is harmless). A `.` among
the presses is a 2 s pause for a sub-list that opens slowly, though in Orzammar's Area
Jump list a pause lost the presses after it (the pause goes before the deeper lists).
`jump.sh` counts any loading mode seen since the call, so a load that ends between two
presses is no longer missed (Eamon's estate). A menu is closed through its own last
item (up from the first wraps to it: U then A), never with B and A, which pick the first
item. `zz_cli_debug`'s main runs but opens nothing outside the climax's module.

**Measuring uncapped** (PLAN 10.9, runs 523-526): `files/frame-rate` holding `120` and
`files/frame-generation` holding `off` let the game run as fast as the GPU allows (the
camp 36.8 fps, run 523); the `GPU swaps` differences over 5 s and FSR's `... fps` line
are the measure, `/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage` says whether the GPU is
the limit (99% at the camp). At that rate the camp arrives near swap 9,200 on the ostagar
route, so a traced frame there needs `files/trace-swap` about 10,100 (`gpu_profile.sh`'s
4,000 fires on the way). Put the owner's `files/frame-rate` (absent: 60) and
`files/frame-generation` back after.

**The hardware's min with NaN** (PLAN 10.9, run 525): `tools/mintest/min_test.comp` and
`min_test.cpp` are a standalone Vulkan compute program for the Thor. Build with the NDK
(`glslc -O --target-env=vulkan1.1 -o min_test.spv min_test.comp`, the SPIR-V's bytes
written comma-separated into `min_test.spv.inc`, then
`clang++ --target=aarch64-linux-android29 -O2 -std=c++17 -static-libstdc++ -o min_test
min_test.cpp -lvulkan` from the NDK's `toolchains/llvm/prebuilt/windows-x86_64/bin`),
`adb push` it to `/data/local/tmp` (with `MSYS_NO_PATHCONV=1` in Git Bash) and run it
there. It prints `min(a, b)`, `min(b, a)` and the multiplication rule's Or and FMin forms
for every pair of special values, and the two counts that matter: how often `min` with
one NaN operand returned the other (84 of 84 on the Adreno 740) and the forms'
mismatches (0 of 100). Run it again after any GPU driver update before trusting the FMin
form, which is the default (`files/hack-no-mul-zero or` compares).

**The options uncapped** (runs 527-530, Frame rate 120, Frame generation off, FSR 3.1
Performance, Exact shader math): `files/shader-math fast` 42.2 fps against 37.3;
`files/anisotropic off` 37.7; `files/fsr3 ultra` (FSR 3.1 Ultra performance, 640x360)
46.6; `files/fsr3-sharpen off` 38.1. Each one boot on the ostagar route with the
file set before it and the owner's value put back after. `files/fsr3-trace` traces one frame every 1000 upscaled, eight at most (`FSR
3.1 trace:` lines): the passes after the scene's resolve with their pixel shaders and
render targets, the resolves with their clears, the copies, and at the swap every draw
grouped by depth test, colour writes, depth and colour 0 EDRAM bases, pitch, MSAA and
what the upscaler made of it - how BUG-066 was found (run 406) - and, in the traced frame,
each static mesh matched for the camera's motion with where it puts the screen's centre
and a corner. **Motion vectors** (the camera's): every window `FSR 3.1: the camera's
motion in N of M frames (C cuts); X static meshes matched a frame, Y agreeing`, `the
motion vectors at the centre and the quarter points - depth .. motion (x y) ..` (five
pixels the motion pass writes to a host-visible buffer), and once `the camera, from a
static mesh's matrix - near .., far .., .. degrees vertical`. `files/fsr3-no-motion`
makes the vectors zero, or N px to the right everywhere if it holds a number (wrong on
purpose: run 417 found BUG-067 with 20). `files/fsr3-debug` draws FSR 3.1's own debug
view over the frame - a 3x3 grid: its dilated motion vectors (grey is none), protected
areas, depth in metres (top); the upscaled frame (middle); disocclusion, shading change,
detail protection (bottom) - and its pass, 0.76 ms. A camera turn for a run: the
`ostagar` route's presses, then `0000 135 45 0 0 0 0 14000 0` (the right stick held
from 135 s for 45 s, after the camp's explore mode at about 122 s). `files/fsr3-show-ui`
paints where the composite found the UI red over the scene in gray, and a shade - the game's
corner vignette, a dark panel's shadow, put on the upscaled scene as a factor since BUG-113 -
blue. `files/fsr3-probe`
only makes a context on the game's device at start and logs it (run 399). Measure at the
King's Camp: the profile into `files/fsr3` (`adb shell "run-as dev.daorecompandroid.duo sh -c
'echo performance > files/fsr3'"`), then `bash tools/measure_ostagar.sh <run>` (runs
421-423: Quality 23.4 fps, Balanced 25.9, Performance 28.8; FSR off 31.1); remove the
file after, or set Off in the options panel (it writes `off`).
`files/hack-video-mode` holding `WxH` tells the title another video mode (run 398: it
keeps 1280x720). The SDK and its shaders must be in place to build at all:
`third_party/FidelityFX-SDK` (the bootstrap) and `python tools/build_fsr3_shaders.py`.
**Frame generation, lsfg-vk** (9.14, in progress): `Lossless.dll` from the owner's own copy
of Lossless Scaling goes into the app's files folder - the pre-launch settings' row
"Lossless Scaling (Lossless.dll)" picks it and copies it there (checked as a Windows DLL:
"MZ", then "PE" where the header says; the log `Lossless.dll: NAME, N bytes, in place`),
or by hand (`adb push` it to `/sdcard`, then
`adb shell "run-as dev.daorecompandroid.duo sh -c 'cp /sdcard/<name> files/Lossless.dll'"`, and
remove the `/sdcard` copy) - never into the repository. `files/frame-generation` holding
`lossless` (lsfg-vk's quality mode, its flow at full size) or `lossless-performance` (its
performance mode, the flow at half size) turns it on from the next start (the options
panel's Frame generation row writes it; `off` or no file is off; 3x and 4x were dropped
by the owner). The Thor's pad, reporting its face buttons by label (BUG-074), logs `pad
N reports its face buttons by label: A and B, X and Y traded` as it attaches. The log: `Frame
generation: Lossless Scaling's (lsfg-vk), quality mode - made at the first guest output`,
then `the generator is up in N ms - WxH, ..., 16-bit shaders` (about 2 s, once),
`frame pacing: frame generation's pairs - the real frame held for half a title frame after
the generated one`, `Frame generation: the pairs' thread is up, nice -8`; the presenter's
`guest frame N refreshed (...), with a generated frame`; every 300 guest outputs `Frame
generation: G of the last 300 guest outputs with a generated frame, H taken from the image
before the UI, ...; GPU X ms a guest output with a generation (n timed), Y ms taken alone`
- the copies, the UI passes and lsfg-vk's passes in the game's own command stream, timed
with two timestamps (6.8 ms at the camp in quality mode, 1.2 in performance mode). With
FSR 3.1 off the upscaler logs `FSR 3.1: off - the image before the UI captured for frame
generation` and captures it every 3D frame (its last-pass learning as with FSR on);
`files/frame-generation-ui` holding `off` (a test switch) makes the frames from the guest
output as shown, the UI in them (it ghosts: run 442); the frame rate's line becomes
`frame rate: 60 on the screen - the game at 30, a frame generated after each` and
`GPU: frame rate 60, the game's own 30` (Frame rate is what the screen shows; the game
runs at half of it); every 300 pairs `the pairs' thread - the generated frame's paint and present X ms,
the real one's Y ms` (the real one waits for the first to be latched, about a GPU frame:
BUG-073). The frame pacing summary (every 5 s) gains `frame generation's pairs, asked N ms
apart, readiness R ms, L late: generated to real: ...` - the gap asked between a pair's
frames, the timeline's present-to-panel readiness, and the gaps the panel's own times
show; its `off panel` line counts every frame shown (about 300 in 5 s at the camp). The
swapchain is FIFO with seven images while frame generation is set (`presentation mode
2`). No `Lossless.dll` in the files folder: `Frame generation: no Lossless.dll ... - off`.
`files/frames-in-flight` (a test switch) holding 2 or 1 lets the GPU be fewer frames
behind than Xenia's 3, `alternate` 3, 2 and 1 in turn, 15 s each (`frames in flight: now
N`) - run 467: 2 no different from 3, 1 halves the game.
`files/frame-generation-timing` (a test switch) holding `off` presents the pairs with no
desired times, `alternate` timed and untimed in turn every 10 s (run 441: untimed, the
compositor drops nearly every generated frame). Measure at the camp as FSR 3.1 is, with
`bash tools/measure_ostagar.sh <run>`; runs 427-441 added the camera turn to the route's
presses (`0000 135 45 0 0 0 0 14000 0`).
**Frame generation, FSR 3.1's** (9.14): `files/frame-generation` holding `fsr3` (the
options panel's FSR 3.1 2x) with FSR 3.1 on (`files/fsr3` Quality, Balanced or
Performance; with it off, `Frame generation: FSR 3.1's needs FSR 3.1 on - off`). The log:
`Frame generation: FSR 3.1's 2x - from the upscaled scene before the UI, ...`, `FSR 3.1:
on (performance) - the game renders at 960x540, with its frame generation`, `FSR 3.1:
frame generation made - 1920x1080 from 960x540, optical flow 120x68 (on the render size's
image), 40.6 MB of GPU memory` (once a size), `Frame generation: FSR 3.1's frames put
together at WxH`; every 300 guest outputs `Frame generation: G of the last 300 guest
outputs with FSR 3.1's frame, N without one (a cut, no 3D scene), ...; GPU 0.86 ms
putting each together` (the UI pass and the blit); with the upscaler's GPU times every 5
s `FSR 3.1: frame generation - X GPU ms a frame for its optical flow and interpolation
(n timed); F frames generated so far` and `FSR 3.1: frame generation's passes, GPU ms a
frame - Opticalflow_Luma ..., INTERPOLATION ..., INPAINTING ...` (run 449 at the camp,
Performance: 9.5 ms, the game at 25.9 fps). `files/fsr3-flow` holding `display` (a test
switch) runs the optical flow on the upscaled scene, as the SDK's FSR 3 does (run 448:
14.5 ms). The presenting, pacing and frame rate lines are Lossless 2x's.
`files/frame-generation-dump` (a test switch) writes a generated frame and the two it lies
between once the generator has been up the seconds the file holds (120 without a number):
`lsfg-generated.raw`, `lsfg-source-before.raw`, `lsfg-source-after.raw` in the files folder
(raw RGBA8 at the guest output's size; `adb exec-out run-as dev.daorecompandroid.duo cat
files/<name>`). These are the generator's sources - the scene before the UI, so without the
HUD or the corner vignette (BUG-113 was misread from them once). Step 1's `files/lsfg-test`
is gone. `files/frame-generation-fp16` holding `off` runs lsfg-vk's shaders in 32-bit
arithmetic (a test switch, BUG-112).
**HDR10** (9.2): `files/hdr` holding `on` (the options panel writes it; offered only
where `Display.HdrCapabilities` lists HDR10), `files/hdr-white` the paper white in nits
(150-300; 200 without it). The log: `GPU: the display takes HDR10, 420 nits at its peak`,
`GPU: colour HDR10 - ...`, `GPU: HDR10 - the swapchain's buffers tagged BT.2020 PQ` and
`GPU: the final pass in HDR10 - white at N nits ...` each time the last pass's pipeline
is made (twice in a run that switches it on and changes the paper white - BUG-062).
Whether the display shows it as HDR: `adb shell dumpsys SurfaceFlinger` - the
`SurfaceView[dev.daorecompandroid.duo...]` layer's `dataspace=BT2020_PQ`, its composition
`Device/Device`, and the Qualcomm composer's `Dynamice_range HDR` (SDR otherwise). A
screenshot is Android's tone-mapped copy of an HDR layer: darker, and not what the panel
shows.
**The options panel** (10.1): `adb shell input keycombination KEYCODE_BUTTON_SELECT
KEYCODE_BUTTON_START` opens it (the log: `options: Select + Start`, `options panel open:
the game holds`, `guest suspended`); `adb shell input keyevent KEYCODE_DPAD_DOWN` (UP,
LEFT, RIGHT) moves and changes, `KEYCODE_BUTTON_B` closes (`options: <file> is now
<value>` for each change). adb's keys come from the virtual keyboard (device -1), which is
no pad of the game's, so they never reach the game; to press **as the Thor's own
controller** - the path a player's presses take, Start and Select passed on to the game
included - write into its input node, which the shell may: find it with `adb shell
getevent -pl` ("Odin Controller", `/dev/input/event9` so far), then `adb shell "sendevent
$NODE 1 315 1; sendevent $NODE 0 0 0; sleep 0.1; sendevent $NODE 1 315 0; sendevent $NODE
0 0 0"` taps Start (codes: A 304, B 305, X 307, Y 308, Select 314, Start 315, D-pad keys
544-547 up/down/left/right; the hat is `3 17 1` down, `3 17 -1` up, `3 16` across). Run
396 checked it so: Start alone opens the game's menu, Select + Start the panel only.
**Texture filtering** (9.6): `files/anisotropic` holds 2, 4, 8 or `off`; without it 16x
anisotropic (the log: `GPU: texture filtering ...`). The options panel writes it, as it
writes `files/glyphs`; both are read as the game starts.
**Button prompts** (Phase 7): `files/glyphs` holds `nintendo` or `playstation` (none:
Xbox), read as the title loads - the log says `glyphs: <set> prompts, N descriptions
written to ...` and then `override: atl_..._dat.xml from ...` as the title reads them;
the dialog writes the same file. `files/override/` is the tree laid over D:\ - any file
there replaces the disc's of that name, and `packages/core/patch/` there replaces files
inside the archives by name. On the computer, `python tools/check_glyphs.py` draws every
redirected image of both sets from the disc's data (`work/glyphs/check_<set>.png`).
**A launch from the icon opens the settings first** (session 37, the owner's) - and
`am start` without a `boot` extra alike: the options panel with Launch Dragon Age, Disc
image (.iso) and Title update at its top (the log: `options: the pre-launch settings
open`); `--ez boot true` (every capture and measuring script) starts the game at once,
`--ez boot false` keeps the app at the bring-up report alone (`tools/appbringup/run.sh`
passes it). The icon's own intent: `adb shell monkey -p dev.daorecompandroid.duo -c
android.intent.category.LAUNCHER 1`; then `adb shell input keyevent KEYCODE_DPAD_DOWN`,
`KEYCODE_DPAD_LEFT` and `KEYCODE_BUTTON_START` (launch) drive the panel - the log
`options: Launch Dragon Age`, `launch: Launch Dragon Age chosen in the settings`, then
the title's load (`glyphs: ...`, `skip_intro_logos: ...`, `applied the title update
...`) and `bootGuest() = true` (run 453). A or a tap on a file's row opens the document
picker, in portrait on the main screen; a plain `adb shell input keyevent` goes to the
focused display, which can be the second screen's launcher - `adb shell input -d 0
keyevent KEYCODE_BACK` cancels the picker. Without the game data the rows say Choose... and Launch
waits; an interrupted extraction resumes on its own from an image in app storage or one
chosen before, its progress on the image's row. The scripted launches (`--ez boot true`)
without the game data extract or ask for the disc first, as before.

**Where the GPU's time goes** (3.8): a traced frame also times each render pass with
timestamp queries - before and after each pass, read back once the frame's submission
completes - as `FT gpu pass N: T ms, WxH, D draws from draw F (VS .. PS ..), G ms before
it` and an `FT gpu frame:` total, also appended to `files/gpu-timing.txt` (pull it: logcat
drops lines after a trace's burst). `files/hack-split-pass` holding `n` ends a traced
pass every n draws, so each piece is timed; `n:first:last` splits only draws first..last
(counted from 0 in the frame) - `1:880:930` times each of those draws. Splitting adds a
little (an empty pass costs 0.1-0.6 ms). Experiments (markers): `hack-implicit-lod` (2D
fetches with implicit LOD plus bias, as the translator does for cube maps),
`hack-no-snap` (no texel snapping), `hack-simple-fetch` (those two and every texture
taken as unsigned), `hack-no-integer-scale` (no post-fetch integer-scale conversion, the
16-bit rounding of normalized results included - inexact, to measure it),
`hack-no-mul-zero` (the multiplication rule's form, by the file's first word: empty or
any other word - IEEE multiplication, no rule (inexact); `nmin` - upstream's NMin test;
`fmin`; `parity` - `(|a| == 0) || (|b| == 0)` for shaders with an even ucode hash and
`fmin` for odd, two forms measured in one run; `vs` or `ps` - IEEE in that stage only;
without the file the exact `(|a| == 0) || (|b| == 0)`), `hack-no-transfers` (the
render-target transfer draws skipped - the picture is wrong wherever the game reads what
they would have copied), `hack-tiny-scissor` holding `first:last` (the 8x8 scissor
for those draws of the traced frame only), and `hack-no-resolve-direct` (every resolve
through the render target's dump to the EDRAM buffer, as before run 295). Session 33 (runs 240-255) used them to find a third of the King's Camp's main
pass in the fetch emulation (PLAN 3.8). The exact fixes are shader modification bits; a
pixel shader whose textures qualify for all three shows `E0` as the top byte of the
modification in its `files/shaders` names (`shader_<ucode>_<modification>.vk.bin.frag`).
**What writes the textures** (BUG-052): `GPU: resolve to <address>+<length>, dest info
<RB_COPY_DEST_INFO> pitch <RB_COPY_DEST_PITCH> (distinct N)` names each distinct resolve
destination, the first 256 of a boot, traced or not (the pitch register's low half is
the pitch, its high half the height); `GPU: memory export to <address>+<length> by VS
<hash> PS <hash> (distinct N)` does the same for memory export ranges; and in a traced
frame `FT bind tf<n> key @<address> mips @<address> fmt <format> <w>x<h> pitch <p>
tiled <t> dim <d>` says which texture each binding is. Look a texture's address up in
the resolve and export lines to find what writes it. **What is drawn before a resolve:**
`files/hack-resolve-dump` holding a destination pitch (hex, as those lines print it -
`009200A0` for the HUD portraits) dumps every resolve with that pitch in any frame, not
only the traced one (`FT resolve dump N: ... (K non-zero bytes, pitch P)`), and logs the
draws since the previous one, up to the last 64: `GPU: drawn before the resolve to ...`
with each draw's shaders, vertex count, render target, depth state and six textures,
then its vertex shader constants (read / zero vectors / non-finite / a checksum) and its
pixel shader constants (the first eight listed), then `draw <n> VS zero: <ranges>` - which
vertex constants were zero vectors - and after the draws `GPU: constants loaded before the
resolve to ...: VS|PS c<a>..c<b> from <address> before draw <n>` for each
`LOAD_ALU_CONSTANT` since the previous such resolve. With the marker set, the first 400
draws of the skinned head's vertex shader (`0CA594B641A10C7E`) also log `GPU: head draw
<k> (draw <n>, <i> indices): <z> of 256 VS constants zero: <ranges>` (\"as before\" when
the set has not changed) - when the face's bones are posed. The first such draw into the
portraits' 160-pitch target with its face constants (c186 on) set also searches every
resident guest page for c186-c189 as guest memory holds them (`BUG-052 watch: N matches
... libwarden.so at <base>`, then each match) and write-watches up to four matching pages
below the physical views (the title's heap): each first write by a new writer logs
`BUG-052 watch: write to <address> by host pc <pc>, tid <t>, guest lr <lr> (in sub_X);
returns ...` - the host pc minus the library base, through the NDK's `llvm-symbolizer`
on `app/build/intermediates/cxx/.../libwarden.so`, names the recompiled function. The
search (`warden::DaoFindAndWatch` in `warden_memory.cc`) can serve other values. With the
same marker (BUG-052, session 33): each portrait head draw reaching the GPU interrupts
every guest thread (`[name] sampled here:` with the call chain and, new, the last 48
functions it `entered`); each kick is scanned - its indirect buffers - for the head's draw
initiator, and the first four that carry it log the kicking thread's chain and trace (the
trace ring is 32,768 entries a thread now; a fault report still prints the last 512); and
recompiled guest functions are wrapped from our code (then `PPC_FUNC(tu6_sub_X)` calling
`__imp__tu6_sub_X`, now `WARDEN_OVERRIDE` - no recompile): the Direct3D draw builders log a call with 10,788
indices during a load with its chain (`BUG-052 draw probe:`), the mesh setup's Direct3D
calls their arguments (`BUG-052 state probe:`), and every Direct3D function in
0x82D8C000-0x82DB1000 (`warden_d3d_probe.cc`, generated from the recompiled names;
retired in PLAN 12.2) logged its arguments while the GUI's per-frame 3D render ran
(`BUG-052 d3d:`, the first 600).
`files/hack-block-render-load` holds the render thread's Direct3D calls while an area
loads (an experiment: the load waits for it). Also with the marker: `BUG-052 palette:`
(each skinning-palette record's size when it changes, from `sub_826183A0`), `BUG-052 high
constants:` (vertex-constant uploads covering c186-c204 or c209-c232), `BUG-052 face-slot
call` (any wrapped Direct3D call with 186, 744 or 0x42E8 in r4 or r5 - mostly
coincidences), `BUG-052 d3d:` now only for the update thread's model routines in combat,
and the kick scan reports the kicks carrying a portrait (the head's draw with the
160-pitch target, `0x028000A0`) with the kicking thread's name and chain. `python tools/recent_draws.py
build/run<N>.log [--constants] [--pitch 160]` decodes them. A trace armed by a mode
change cannot reach the frame the mode changes in - which is where an area load draws the
portraits - so this is how they were seen (BUG-052).
**What the GPU is doing:** every 5 s a `GPU:` line gives the ring's write pointer and
kicks, swap packets, vblanks, the watched writes served, how far paint requests got,
the draws per recent swap (submitted, issued, failed, predicated away - DAORecomp's
counters, which the Vulkan backend now feeds), the read pointer written back and the
title's Direct3D device counters. On that schedule the presenter's guest output -
the last frame the GPU produced, before the panel - is written to the app's `files/`
as `guest-output-N.png` with a count of lit pixels; pull one with
`adb exec-out run-as dev.daorecompandroid.duo cat files/guest-output-2.png > out.png`. One
whole frame is traced as `FT` lines (every draw's shaders, targets and textures, each
resolve and what it wrote, the swap) - the frame after swap 600 by default; a file
`files/trace-swap` holding a number picks the swap, and holding `busy` traces the
first frame after one with 60 or more draws (`DAO_GPU_FRAME_TRACE`, read in
`warden_gpu.cc`'s `Start`). `presenter: guest frame N` says how the
presenter paints and whether its surface can be painted to. The Java status overlay
hides itself once the title boots.

**Looking at guest memory:** `files/probe.txt` in the app's files directory, one
`address length` pair per line in hex, is read at every status tick. An address below
`0x20000000` is guest physical (a texture, a movie plane) and is summarised as
`GPU: probe ADDR+LEN: mean, byte histogram, first bytes, changed/unchanged, write-watch
faults`; any other address is guest virtual (the module's globals, a heap object). A
range of 256 bytes or fewer is printed in full as big-endian words (`probe ADDR+LEN:`
then eight words a line) - the way BUG-043's empty request pool was read off the
device without a rebuild. Uploads into the physical ranges are also logged as `FT
upload` lines in every frame (`DAO_LOG_UPLOADS`). Push the file with a Windows path
when `MSYS2_ARG_CONV_EXCL` is set (`adb push "$(cygpath -w probe.txt)" /sdcard/ &&
adb shell run-as dev.daorecompandroid.duo cp /sdcard/probe.txt files/`). **Looking at what the GPU resolved:** uncomment
`SetReadbackResolveMode("full")` in `warden_gpu.cc` and the resolve's result is copied
back to guest memory; the capture then logs `GPU: front buffer in guest memory` (pixels
non-zero) and dumps the raw bytes to `files/front-buffer-2.raw`. It costs most of the
frame rate (~10 Hz), so it is off by default. **Skipping the logo movies:** a file named
`skip_intro_logos` in the files directory (`adb shell run-as dev.daorecompandroid.duo touch
files/skip_intro_logos`) hides the three logo movies, as DAORecomp's option does.

**The traced frame (`FT` lines), fully:** besides each draw, resolve and swap, the frame
after swap 600 logs each pixel and vertex shader's microcode once, the draw's float
constants (pixel shader c0-c3 and c250-c255, vertex shader c0-c7) and the pixel
shader constants as uploaded, the system constants, the host viewport the guest
registers came to and the Vulkan viewport and scissor as set, each vertex buffer's
first words, each fetch constant's binding (image view or the null image), texture
loads and uploads with their non-zero byte counts (the first 64 whether traced or not),
the pipeline description, the render pass key and the framebuffer entered, the EDRAM
ownership transfers, the render target dump and the resolve's rectangle and dispatch.
**Experiment markers** in the app's files directory: `hack-rt-readback` copies the
first dumped colour render target image to a host buffer in the traced frame and logs
a histogram (`FT RT readback`) and writes `files/rt-readback.raw` - ground truth for
whether draws reach the image (it asserts in `SubmitBarriers` in a frame with EDRAM
transfers - BUG-045 - so not there); `hack-no-sparse` binds the shared memory buffer
plainly instead of sparsely; `hack-resolve-readback` turns resolve readback on (as the
commented `SetReadbackResolveMode("full")` would), so the memory probe reads what each
resolve wrote; `hack-no-cull` draws both faces of every triangle; `hack-no-depth-test`
leaves the depth test off; `hack-depth-always` keeps it on with the comparison always
passing. The last three are pipeline-state edits recorded in `tools/assemble_runtime.py`;
none of them lit the BUG-045 scene. **BUG-045's instruments** (markers too; where
the file holds shader hashes they are hex as the `FT` lines print them, comma
separated, up to 8): `hack-vs-probe` - those vertex shaders store the guest position
of vertices 0..4095 into a spare range of the shared memory (guest physical
`0x1FF80000`, above what the allocator hands out) in up to 8 draws of the traced frame,
read back and logged per draw as the screen extent, row and column histograms and the
first vertices (`FT VS probe`) - compare with `FT draw VS on the CPU`, the same shader
run by Xenia's interpreter; `hack-skip-vs` - the draws of those vertex shaders are
dropped; `hack-resolve-dump` - every resolve of the traced frame is copied out and
written to `files/resolve-<n>-<address>.raw`, which `python tools/resolve_to_png.py
<file> [width]` turns into a PNG (a 32-bit colour resolve reads A R G B byte by byte;
a depth/stencil resolve shows its bytes); `hack-ps-paramgen` - those pixel shaders'
colour 0 becomes their raw `gl_FragCoord` (`|x|/2048`, `|y|/1024`, `fract(|x|)`), or
magenta without one; `hack-ps-red` - that pixel shader's colour becomes red;
`hack-no-float-controls` - the file names the float-control modes dropped from every
shader (`n` NaN preservation, `d` denormal flush, `r` round to nearest even; empty
drops all three); `hack-no-a0`, `hack-no-nmin`, `hack-msaa-2x-as-4x` - a0-relative
constants read absolutely, NMin/NMax/NClamp made FMin/FMax/FClamp, the 2x MSAA targets
made 4x; `hack-rect-in-vs` - rectangle lists expanded in the vertex shader instead of
the geometry shader, which the SPIR-V translator does not implement: every
rectangle-list draw fails with it; `hack-tiny-scissor` clamps every draw's scissor to
8x8 (pixels against vertices: the frame rate that is left is geometry);
`hack-no-sample-mask` leaves `gl_SampleMask` and the alpha-to-coverage path out of
every pixel shader. The 3D scene renders as one tile without MSAA by default (the
title's tiling function, hooked in `warden_game.cc`); `files/scene-msaa` keeps the
console's two 2x MSAA tiles. **The frame-rate benchmark:** `bash tools/route.sh
ostagar`, then `bash tools/measure_ostagar.sh <run> [--simpleperf]` - the swaps per
second at the King's Camp, the GPU's busy percentage and clock, or simpleperf
reports per thread with off-CPU time. And
`files/trace-mode` with `<mode> [swaps [nth]]` traces the frame `swaps` swaps after the
game enters that mode for the nth time (11 is character generation, 0 exploring;
`warden_game.cc` lists them; on the `ostagar` route the second mode 0 is Ostagar)
instead of a fixed swap number. **Shader dumps:** a `files/dump-shaders` marker writes every
translated shader's microcode and SPIR-V to `files/shaders/`; the NDK's `spirv-dis`
(`ndk/<version>/shader-tools/windows-x86_64/`) reads the `.vk.bin.*` files. **The shader
build** (`tools/build_xenia_shaders.py`) targets Vulkan 1.1 on purpose: the Adreno 740
driver ignores the SPIR-V 1.6 workgroup-size form a Vulkan 1.3 target produces.
**The Khronos validation layer:** `libVkLayer_khronos_validation.so` (arm64-v8a, from the
Vulkan-ValidationLayers release's Android binaries) in `app/src/main/jniLibs/arm64-v8a/`
- git-ignored, never committed - ships in the debug APK; `/data/local/debug/vulkan`
needs root. `python tools/bootstrap.py --validation-layer` fetches it (its other steps
are skipped when done). Enable it for the app with `adb shell settings put global
enable_gpu_debug_layers 1`, `settings put global gpu_debug_app dev.daorecompandroid.duo`,
`settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation`, and create the
`files/hack-vk-validation` marker so Xenia asks for the layer and `VK_EXT_debug_utils`
and writes the messages to its log (`Vulkan Error (...)`, `Vulkan Warning (...)`, each
with its ID). The layer's own settings are system properties, `debug.vvl.<setting>`:
`setprop debug.vvl.validate_sync true` adds synchronization validation (hazards as
`SYNC-HAZARD-...`), `debug.vvl.validate_best_practices` and
`debug.vvl.validate_best_practices_arm` the performance advisories (tile-based ones
included). Afterwards: `settings delete global` the three settings, the `setprop`s back
to `false`, the marker removed. The layer costs a third of the frame rate, which shifts a
route's timed presses: `chargen`'s one X lands outside the intro's skippable moment -
press X every ten seconds instead (run 327). Runs 323-327 were clean (PLAN 3.9): the one
warning, `WARNING-Swapchain-PreTransform`, is harmless on the Thor, whose compositor
rotates the layer on the display hardware (`dumpsys SurfaceFlinger`: `composition:
Device/Device ... transform: 90`).

**GFXReconstruct (PLAN 3.10): capturing frames and replaying them.** `bash tools/gfxr.sh
setup` once: GFXReconstruct's Android release (SHA-256 checked) into `build/gfxr`, its
layer stripped and copied into the app's own data directory - not loaded from the replay
app, which Android 11's package visibility hides from ours ("Debug layer app ... not
installed", then `ErrorLayerNotPresent`) - and the replay app installed. Then `bash
tools/gfxr.sh arm <name> [<frames>]` before a boot: frames by number (`1800-1801`; frames
are presents, about 1800 by the title), or, with none, one frame at `bash tools/gfxr.sh
trigger` - for a place a route reaches, say the King's Camp. `pull <name>` copies the
capture to `/sdcard/Download/<name>.gfxr`, `replay <name> [args]` replays it on the Thor
with a screenshot of every frame into `build/`, and the extra args go to the replay
(`--loop-frame 1 --loop-count 50`, `--pbi-all` to print every call, `--validate`); `off`
removes the settings. The layer forces FIFO presentation while it is on, and the default
page-guard memory tracking works beside the runtime's write-watch handler. A replay is
the frame as the game drew it (run 330's camp), but its timing is not the game's: per-pass
GPU time is `files/gpu-timing.txt`. Start each replay after a force-stop of the replay
app (the script does): a start sent to a replay still exiting is lost.

**GPU memory (PLAN 3.7).** The `GPU memory:` line (every 5 s when the total has moved by
16 MB) gives every live `vkAllocateMemory` by its caller - an offset into `libwarden.so`
and a memory type - and the driver's own count for the process. Name the callers with
`llvm-addr2line -f -C -e app/build/intermediates/cxx/Debug/<hash>/obj/arm64-v8a/libwarden.so
0x<offset - 4>` (the NDK's, under `toolchains/llvm/prebuilt/windows-x86_64/bin`). From
outside: `adb shell cat /sys/class/kgsl/kgsl/proc/<pid>/kernel` (bytes), and `dumpsys
meminfo dev.daorecompandroid.duo`, whose `GL mtrack` does not see Vulkan memory on this driver.

**Do not call `xe::FlushLog()` after a log line in a guest import.** It sleeps 10 ms
before flushing, so a thread that logs more runs slower - with a flush per line, the
Update thread's package mount took 2.3 s and the race in BUG-034 depended on how much
each thread had logged. The fault handler drains the logger once before its report,
which is what the per-line flushes were for.

### A release build (PLAN 11.3)

```bash
python tools/game_check.py                 # on the owner's PC, first
./gradlew :app:assembleRelease             # signed with ~/.android/dao-release.jks
python tools/release.py                    # proves it; keeps it in build/releases/<version>/
```

`release.py` fails on: no signature or the debug key's; any file of the game's types
(the pre-commit rule's and .gitignore's); Lossless.dll by name or by its bytes; the
owner's disc image and title update file names; any path of the build machine (a
drive's Users or Repos folder - CMakeLists.txt maps them to `ndk`, `src` and `home`). It keeps the APK, the unstripped
libwarden.so and `build-id.txt` (the build id a tombstone from that release names). Build
a release from a committed tree (and put `<!-- manual-update -->` in the notes of one that must
not install from the launcher, PLAN 13.1): About this app shows the commit, "+changes" when there
were uncommitted ones. Since PLAN 12.6 the release is the one that is published
(DECISIONS D14): it holds no game code, so `release.py` also fails one that has
`libdaogame.so`, a symbol of the game's translated functions in any library
(`sub_XXXXXXXX`, by `llvm-nm`), or a game file in `assets/gamecode/toolchain.zip` - and
one that lacks what the player's device makes the game's code with (`libllvm.so`,
`libxenonrecomp.so`, the toolchain and the recipe). The Khronos validation layer is left
out of release builds.

### The first run on a clean install (PLAN 11.4, 12.7)

PLAN 12.7 did it with the published APK itself (`prod.daorecompandroid.duo`, beside the
owner's debug build): the disc images chosen from Download, disc 2's Title Update 6 taken,
the game's code made on the device, then Launch. The launcher logs `launch: making the
game's code (Title Update N)` under `Warden` as it starts the making, and the making
`made <library> from Title Update N` (or `making the game's code failed`, with the
command's last lines) under `WardenGameCode`; with no run-as, logcat and the running
`libllvm.so` processes (`adb shell ps -A`) are the only view of it.


The Thor's own install is never removed, so a clean install is a second app:
`./gradlew :app:assembleRelease -Pwarden.clean` then `python tools/release.py --clean`
and `adb install build/releases/<version>-clean/*.apk` - `prod.daorecompandroid.duo.clean`, "DAO
clean test", its own storage and saves. A release build has no run-as, so the test files
(`inject-pad`, `route`) do not work, and the game ignores Android's injected keys (a
virtual device): press the Thor's own controller through its input device instead
(the shell is in the input group):

```bash
# press.sh <code> [hold]: BTN_START 315, Xbox A (the Thor's BTN_EAST) 305, B 304,
# BTN_SELECT 314, the D-pad 544-547 (up, down, left, right)
d=/dev/input/event9    # "Odin Controller" - adb shell getevent -p to check
sendevent $d 1 $1 1; sendevent $d 0 0 0; sleep ${2:-0.12}; sendevent $d 1 $1 0; sendevent $d 0 0 0
```

Push it to /data/local/tmp and run it with `adb shell sh`. Select + Start together
(both keys down, one sync, both up) opens the options panel. Never run `getevent`
without a count from a script: it waits for events and does not return. Remove the copy
after with `adb uninstall prod.daorecompandroid.duo.clean` - the package named in full, never the
owner's.

### Symbolicating native crashes

```bash
$ANDROID_NDK_HOME/ndk-stack -sym build/android/obj/local/arm64-v8a -dump crash.log
```

Keep unstripped `.so` files from every build that goes on the device — a stripped
backtrace from a 128,000-function recompiled binary is useless.

---

## 4. Performance measurement

Baseline is established in **task 6.7** and everything in Phase 9 is measured against
it. Measure, don't infer.

**The baseline run (6.7, runs 277-278).** With the `ostagar` route on the device, two
commands side by side - the boot as a short command of its own, the sampler in a second
shell - then the report:

```bash
bash tools/boot_capture.sh <run> 660     # about 11 minutes at the King's Camp
bash tools/sample_power.sh <run> 11      # every 10 s: GPU load and clock, temperatures, power
python tools/frame_report.py <run>       # frame times, 1% lows, power, battery hours
```

The frame times come from the `frames:` line the GPU status reporter writes every 5 s
(`warden_frame_times.cc`): the presents (`display:` in runs 277-278) - when the
presenter queued each frame, in 120 Hz refreshes - and the title's swaps (VdSwap), in
60 Hz vblanks, each with mean, percentiles, worst, 1% low and the histogram. A present
is not the refresh the frame reached the panel at; that takes the presentation engine's
own times (`VK_GOOGLE_display_timing`, 4.7): the `pacing:` line after each `frames:`
line - per pacing mode, the panel's intervals in its own measured refresh (8.445 ms, not
the 8.333 Android reports), present-to-panel and the title's swap-to-panel latency, the
presenter's wait for a swapchain image, how many refreshes early each frame was finished,
frames never shown, and for the paced modes the frames late for their slots (and of
them those held by their desired time though finished), the frames early, and the
timeline's moves and new starts (and of those, how many because it ran ahead of the
frames). Pacing is on by default; `files/frame-pacing` holding `off` turns it off,
`alternate` gives a minute of each mode in one run, and `phases` a minute each of four
desired times - 4, 3, 2 and 1 eighths of a refresh after the slot, `phase4` to `phase1`
(the default is 2) - which `frame_report.py` splits per mode (run 298 named them
`paced`, `paced3`, `paced2`, `paced1`). The `GPU:` warning `ring write pointer ...
without a hooked store` counts the ring poller's kicks: none in runs 298-301 (BUG-047);
one now is a real unhooked store, worth a look.

**The save thumbnail (BUG-049):** `files/hack-memexport-readback` copies what shaders
write to memory back to guest memory, as `hack-resolve-readback` does for resolves
(slow, both); `NtWriteFile` lines name the guest buffer written from (`from A1E13000`
for `scrnshot.bin`). `files/hack-resolve-dump` takes a second word, a guest address
(`010001C0 00DC3000`): with each resolve dumped for its pitch, the shared memory from
that address is dumped beside it (`resolve-NN-<dest>-source-<address>.raw`, 1280x736x4
bytes), the texture at that address logs its loads and watch firings (`FT tex @...`,
the first 16 then one a second), and resolves to it log their source (likewise gated);
a third word is an experiment - `base-only` (that texture's base level alone) or
`reload` (with `hack-log-bindings`: marked written by the GPU before every second draw
of the pixel shader). The draws logged before a dumped resolve now also say where they
land (`lands at viewport ... scissor ...`), stream 0's first 20 words and the first
texture's whole fetch constant. `files/hack-log-bindings` holding a pixel shader hash
logs that shader's texture bindings - image or null image, key, and the texture's state
(outdated, scaled resolve). Run 306's two saves: the first as it is, the second
reloaded. Session 34 added an event ring of the watched address's textures (each load,
watch firing and creation, with the texture object), printed at the logged pixel
shader's draw - and found the cause elsewhere (BUG-049): read a two-save run with care,
the second save's pipeline is warm. **Pipelines** are created when first drawn with;
`files/hack-async-pipelines` brings back upstream's background compilation, which draws
a new pipeline with a placeholder pixel shader meanwhile. **The pipeline storage**
(10.3) is `files/pipeline-storage-v1/`: delete it for a first-session run;
`files/hack-no-pipeline-storage` turns it off; its pipelines are created when first drawn
with, hits in the driver's cache (BUG-060), and `files/hack-precreate-pipelines` creates
them all at startup instead, as upstream does; `files/hack-pipeline-stats` logs the
driver's statistics for each new pipeline (`pipeline #N (driver ... MB, of it not
allocated by Xenia ... MB)`: instructions, registers, scratch memory per stage); "Loaded N shaders from storage" and
"Saved N bytes of VkPipelineCache data" in the log say what it holds. **One capture at
a time:** a
`boot_capture.sh` still running stops the app when it ends, whatever run the app is
(runs 295, 308).

**The app's end (4.9).** `files/hack-gpu-loss` reports the host GPU lost at 60 s through
the graphics system's own path: the "has to close" dialog, then OK ends the process (run
286). Back: `adb shell input keyevent 4` once the title runs brings up "Quit Dragon Age:
Origins?"; Quit ends the process (run 289). `uiautomator dump` cannot read the game's
window ("null root node"), so tap by position from a screenshot (`adb exec-out screencap
-p`): on the 1920x1080 panel the dialogs' right-hand button (OK, Quit) is at about
(1446, 660). Check with `adb shell pidof dev.daorecompandroid.duo` (nothing once ended) and boot
again to see a clean start (run 290). The power: the Thor on the computer's USB
port takes 3.9 W, less than the game draws, so the battery discharges while plugged in,
and its discharge plus the input is the draw; the report gives it by the battery's
current and by its charge counter's slope, which can lag (run 278: 8.4 W against 10.2;
runs 279-281 agree at about 10.3 W).

**Suspend (4.9, BUG-056, BUG-057).** `bash tools/suspend_check.sh <run>`, beside a capture
already booting (`boot_capture.sh <run> 150 --install` in the background), goes home and
back, presses START, turns the screen off and on, and goes home and back three times
quickly, then says PASS or FAIL. By hand: `adb shell input keyevent KEYCODE_HOME` (or
`KEYCODE_POWER`, then `KEYCODE_WAKEUP` - the Thor has no lock screen), then `adb shell am
start -n dev.daorecompandroid.duo/.GameActivity`, which brings the running game to the front
(`singleTask`: "its current task has been brought to the front"). The log says `guest
suspended (#N)`, each thread's `held for the suspension` in the first two, and `guest
resumed (#N) after S s; T threads were held; its clock stopped at X s` - the next
suspension's reading less X is the play between. The window's focus and any ANR are in
the events buffer (`adb logcat -d -b events | grep input_focus`), which a plain `logcat
-d` leaves out; an ANR's thread dump is in `adb shell dumpsys dropbox --print
data_app_anr`. The process's threads while away: `adb shell top -H -b -n 2 -d 5 -p <pid>`
- 10-13% of one core is normal (BUG-058); a title thread there is not.

| Metric | How | Why |
|---|---|---|
| Framerate | `frames:` lines (`tools/frame_report.py`); `GPU:` swaps per second | The headline number |
| **Frametime distribution** | `frames:` histogram in refreshes | **More important than average fps.** A 60 fps average with regular 100 ms spikes feels worse than a steady 40. |
| **1% lows** | `frames:` | What you actually feel |
| Frame pacing | The `pacing:` line's panel histogram: the share off the main bucket (4.2% is the floor for 30 fps on the 118.4 Hz panel) | A 120 Hz panel makes pacing errors obvious |
| CPU per-cluster load | `/proc/stat`, simpleperf (`measure_ostagar.sh --simpleperf`) | Confirms main thread is on the X3 (4.8) |
| GPU load / frequency | kgsl sysfs (`sample_power.sh`) | Headroom for Phase 9 features |
| Temperature | The `GPU:` line's `thermal:` part every 5 s (the game's own reads: hottest GPU sensor, big cores, board, GPU clock, thermal level), or `/sys/class/thermal` (`sample_power.sh`) | Drives 10.2 |
| Battery drain | `/sys/class/power_supply` (`sample_power.sh`) | 10.6 |
| Memory | `dumpsys meminfo` | 16 GB is plenty, but leaks still matter |

**The GPU profile (PLAN 10.9, runs 494-501).** `bash tools/gpu_profile.sh <run> [swap]`
traces a frame at the King's Camp (`files/trace-swap`, 4000 on the ostagar route is half a
minute after arrival) and writes its GPU time per render pass to
`build/run<run>-gpu-timing.txt`; the switches set beforehand are removed at its end:
`files/hack-split-pass` (`2:850:2100` - draws 850-2100 two to a pass; the timing table
holds 1,024 passes, after which every draw piles into the last), `files/hack-pipeline-stats`
(the driver's statistics, a log line per stage: `pipeline #N PS <hash> Fragment Shader
...`; FSR 3.1's and frame generation's own compute pipelines too, `VS` the SDK's pass
number - the upscaler's accumulate is 6 - and `PS` its permutation), `files/hack-no-mul-zero ieee`, `files/dump-shaders` (`files/shaders/`, read with the
NDK's `shader-tools/windows-x86_64/spirv-dis.exe`). FSR 3.1's and Lossless's own GPU time
are in the log's `FSR 3.1: GPU ms a frame ...` and `Frame generation: ... GPU N ms a guest
output` lines.

**Frame generation compared** (PLAN 10.9, runs 506-508): one boot per choice -
`files/frame-generation` holding `lossless`, `lossless-balanced`, `lossless-performance` or
`fsr3` (the generator is made at the start), `bash tools/gpu_profile.sh <run>`, then the
owner's own value put back; the log's `Frame generation: ... GPU N ms a guest output with
a generation` and the `GPU swaps` lines (the game's frames a second are the 5-s difference
over 5).

**Shader math compared** (PLAN 10.9, runs 510-512): `files/shader-math` holding `fast` (the
log says `GPU: shader math fast`) or absent (Exact), one boot each on the ostagar route,
`bash tools/gpu_profile.sh <run>` for the frame rate; for the picture, a screenshot 25 s
after arrival (`adb exec-out screencap -p`) of each setting **twice** - the camp's light
shafts differ from boot to boot, so a Fast-Exact difference means something only beyond
the Exact-Exact one. The owner's own value put back after (absent).

**Test under thermal load, not from cold.** A handheld's cold-start numbers are
meaningless — run 20 minutes first, then measure. This is the single most common way
handheld performance claims turn out to be wrong.

---

## 5. Correctness testing for the feature work

### Glyph system (Phase 7)

Switching glyph sets must change **every** prompt and **nothing else**. The audit
checklist in [`GLYPHS.md`](GLYPHS.md) §5 lists every surface to check. Specific traps:

- **Text prompts as well as bitmap prompts.** A half-converted UI showing `B` on icons
  while text still says "press A" is the most likely failure.
- **The controller layout screen** (`load_controller.gfx`) is the most glyph-dense screen
  in the game and the obvious place a mistake shows.
- **Button functions must not change** (D3). Verify the bottom button still confirms in
  Nintendo mode.

### Second screen (Phase 8)

**As built (session 37).** The deck opens as the game boots (Options -> Second screen,
on by default where there is a second display): the log says `deck: display 4
(Screen-2), DeckActivity in its own process`, then (tag `WardenDeck`) `deck: on display
4, 1240x1025, in its own process`, `deck: connected to the game`, `deck: the game's
display focused again` (BUG-078). Its screen: `adb exec-out "screencap -d
4630946482288158084 -p" > deck.png` (the physical display id, from `dumpsys
SurfaceFlinger --display-id`; `-d 4` gives an empty file). A tab: `adb shell input -d 4
tap X Y` (the rail across the top, about 40 px down). Focus: `dumpsys window | grep
mTopFocusedDisplayId` must say 0 with the deck up - the controller's keys go to the
focused display. The deck's process: `adb shell pidof dev.daorecompandroid.duo:deck`; the
isolation test kills it (`adb shell "run-as dev.daorecompandroid.duo kill -9 \$(pidof
dev.daorecompandroid.duo:deck)"`) - run 458 in the Korcari Wilds' fight (`bash tools/route.sh
wilds`; wait for `game: mode is now 1 (combat)` exactly - `mode is now 1` also matches
the movie's 10). `files/deck-trace` (a test switch) logs every token path the UI
registers (`deck: scene S observes PATH as ID`) and every value change (`deck: PATH =
VALUE`, bounded) and adds the deck's Data tab with everything the mirror holds;
The deck is a Presentation in the game's process (8.1's Route B, the default since BUG-116);
`files/deck-route` holding `activity` makes it DeckActivity in its own process (Route A) - and
puts the game in AYN's second-screen volume (`adb shell getprop sys.audio.uids` lists its uid).
**The deck's cost:** `deck: in 30 s, reads X ms (Y ms/s); N snapshots ... positions read N
times, M changed`, every 30 s while it pulls - the game thread's time in the deck's reads and
snapshots. **The game's volume** (BUG-116): AudioFlinger's power log of the primary output
(`dumpsys media.audio_flinger`, the AudioOut_D thread's dB rows) shows what reaches the
hardware; `settings get system secondary_screen_volume_level` is AYN's second-screen level. With `files/deck-trace` the mirror also dumps the
data model's tree to the log (`deck: tree PATH = TEXT (N members)`) after ten seconds of
exploring, twice at most - the roots are listed in `warden_deck.cc`. **The deck by adb:**
`adb shell input -d 4 tap x y` (coordinates as `adb exec-out screencap -p -d <its physical
id>` draws them; `dumpsys display` has the id). Never `adb shell input keyevent` without a
display while the deck is up: it is a key for the top display, BUG-106's way in, and the
game ignores it anyway. `files/inject-pad` drives the game live. **Anything that pauses the
game while it explores autosaves, if Autosave when put away is on** - run 602's test of
another app on the second screen opened it on the main one, and the Auto-save slot took the
test's changes (put back from a backup): before testing writes on a player's save, back up
`files/content/00000001`, open a menu first (`not exploring ... none`), and end with
force-stop. `dumpsys input | grep -m1 FocusedDisplayId` is the display that has the keys now
(the `dumpsys window` line of that name can be the last ANR's). **A background
`boot_capture.sh` ends in a force stop:** one whose tool was stopped can live on and fire at
the end of its settle time, in whoever's game is running then (session 42) - kill it and
its `sleep` (`ps -ef`) before handing the device over. A "crash" with no tombstone: `adb shell
dumpsys activity exit-info <package>` says `FORCE STOP` for a stop, and from which pid.
**The output's level:** `dumpsys media.audio_flinger`, the thread's `Signal power history`
(BUG-109): oldest line first, so read the last lines (the first are half a minute old, which
cost run 612 a wrong reading), and a silent output adds no lines. The deck's own reads
run only while it pulls; `files/deck-reads-staged` (a test switch) switches their
patterns on one at a time, 15 s apart (`deck: read pattern N on`), to find one that
changes what the game does (run 462: none). The tabs at the camp: `bash tools/route.sh
ostagar`, then taps across the rail (seven tabs across 1240 px) and the deck's screen
after each (runs 461, 463). The game's own map screen for comparison: add `0020 134
0.15` (Back opens the Character Record) and `0000 136.5 0.2 255 0` and `0000 138 0.2 255
0` (LT twice, to the map) to the route's presses (run 471). The camp's draws depend on where the camera faces: the route
with the camera-turn presses about 749 a frame, without them about 2,130.

- **Isolation test (8.7):** kill `DeckActivity` mid-combat and confirm the game is
  completely unaffected. The deck must never be able to stall or crash the render loop.
- Verify effective orientation on the real device — reported rotation differs between
  base and override display info.
- Confirm data accuracy by comparing the deck against the game's own HUD and journal
  side by side.
- Check deck colours in **Display-P3**, not sRGB, or the brass tones will be wrong.
- **The party's faces (8.9):** boot `bash tools/route.sh morrigan` with the deck on and
  watch the log for `Deck: portrait render at ...` (one per face, with how much of it
  the head's alpha covers) and `Deck: N portrait(s) drawn, placed ...` (how the burst was
  placed: by the slots renamed, every named slot, or by address), then compare the Party
  tab's rings with the HUD's portraits (`screencap -d` for each display). Run 548: Jaden,
  Morrigan, Alistair, each in their own ring.
- **The Performance tab/overlay option (8.10):** `files/perf` holds `on` or `off` and is
  read twice a second by the overlay and every pull by the deck, so writing it with
  `run-as` checks the switch without the panel. With `files/deck` off the overlay shows
  on the main screen from the boot (runs 547, 549); with the deck on, the Perf tab
  (run 546). Its lines must match the log's `Frame generation:` and `FSR 3.1: on`.
  The boot script stops the app at its end, so for screenshots start the game with
  `am start` and capture while it runs.

### HDR and FSR (Phase 9)

- **Rendering resolution and field of view (9.16, 9.17):** `files/resolution` holds
  960x540, 1280x720, 1360x768, 1600x900, 1920x1080 or 2560x1440 (the last three the
  device at half the size with Xenia's 2x draw resolution scale - the log's `GPU: rendering
  at` line; `native-1600x900` and `native-1920x1080` draw wrongly, for the EDRAM
  investigation only), read as the game starts; `files/hack-edram-map` holding N logs the
  next N changes of render targets (`EDRAM map:` lines - bases, pitches, formats, the
  ranges the cache gives them) and the resolves meanwhile; the log's `render
  size:` line names the device's size and `scene:` the tiles. `files/hack-scene-tiles`
  holding 1 or 2 changes the big sizes' scene tiles within a second. `files/fov` and
  `files/fov-conversation` hold degrees (80-120); the log's `camera: field of view`
  lines show them taken at the first camera update. Measure at the King's Camp with the
  ostagar route, FSR 3.1 and frame generation off.
- **The launcher's test launch** (session 40) ran the ostagar route without adb; it was
  removed before release (PLAN 11.6). Use `bash tools/route.sh ostagar` and a boot.
- **Motion blur, depth of field, photo mode (9.18-9.20):** `files/motion-blur` (off, low,
  high) and `files/dof` (on, off) need FSR 3.1; their cost shows in the log's `FSR 3.1: GPU
  ms a frame` line as the composite's (0.72 ms without either at FSR 3.1 Performance).
  To turn the camera or talk without a pad, put `files/touch-controls` on and press the
  on-screen controls with `input swipe x y x y 150` (A at 1699,713, B at 1814,583, ≡ at
  67,86; the right stick drags from 1340,880). Photo mode: the panel's Photo mode row or
  L3+R3; photos land in `/sdcard/Download/Dragon Age Origins/` (pull with
  `MSYS2_ARG_CONV_EXCL='*'` from Git Bash).

- HDR10 requires a real PQ surface. Watch for the documented Adreno 740 issue where the
  swapchain silently comes back as 10-bit sRGB instead — verify the actual colour space
  that was granted, don't trust the request.
- FSR 3.1 needs reconstructed motion vectors. Test specifically for temporal artefacts
  on fast camera movement and on the Scaleform UI layer, which has no motion vectors at
  all and should be excluded from temporal upscaling.

### A conversation's start (BUG-092)

- `bash tools/route.sh wilds`, then keep its first six presses (`head -6 files/inject-pad`):
  the Korcari Wilds in explore mode with the debug party, about 110 s after the boot
  (`am start ... --ez boot true`). An A there walks back to Ostagar's camp (a load);
  Daveth stands in front with a conversation marker, and an A talks to him.
- Read the 5 s `frames:` lines (the title's and the panel's intervals) and the lines in
  between: `frame pacing:` mode switches (none should come at a conversation's start
  with frame generation on), `frame times: a title frame of ... ms` (over 50 ms),
  `Pipeline created in ... ms` (over 3 ms), `VkPipelineCache data unchanged`.

### The update check (PLAN 13.1)

- **The install itself without the published app (BUG-114, run 681):** DAO Debug updating
  itself in place, from a local feed. Test-only edits in the build clone, never committed:
  `AppUpdate.read`'s `published` true, `connect` letting `http://127.0.0.1` through, and
  `android:usesCleartextTraffic="true"` on the manifest's application. Build it twice
  (`-Pwarden.version=` a lower and a higher version), serve the higher APK with `python -m
  http.server 8765 --bind 127.0.0.1` and `adb reverse tcp:8765 tcp:8765`, and put a feed in
  `files/update-feed` whose asset's `browser_download_url` is `http://127.0.0.1:8765/<apk>` with
  its size and `sha256:` digest. `adb shell appops set dev.daorecompandroid.duo
  REQUEST_INSTALL_PACKAGES default|allow` gives Android's first-time dialog or skips it. After:
  revert the edits, reinstall the normal debug build, remove the feed, the reverse and the server,
  and set the permission back to `default`. Never with the published app.

- A real update: `gradlew :app:assembleRelease -Pwarden.version=<an older version>`, install it
  (it is a separate app from the debug build, with no game data), open its launcher, and the
  newest release on GitHub is offered. Remove it after. Its first install asks to allow installs
  from the app.
- The other states: a debug build reads `files/update-feed`, if there, in place of GitHub - the
  API's JSON (`tag_name`, `draft`, `body`, `html_url`, `assets` with `name`, `size`, `digest`,
  `browser_download_url`). The log says `update check: ...`. Remove the file after.
- `adb install` needs a Windows path here (`C:/...`): `/c/...` fails with `failed to stat` under
  `MSYS2_ARG_CONV_EXCL='*'`, and a script that goes on tests the old build. Stop on anything but
  `Success`.

### Debugging Mode's report (PLAN 11.1)

- `files/debug-mode` holding `on` (or the option), then any start: the log's head reads
  "Debugging Mode: ..." (the header, then the GPU line) and the file is
  files/debug/log-current.txt. A route that loads a save (`bash tools/route.sh load`)
  shows "the save being loaded (...) kept for the report".
- In the game: `adb shell input keycombination KEYCODE_BUTTON_SELECT KEYCODE_BUTTON_START`
  opens the panel; KEYCODE_DPAD_UP twice from the first option reaches Exit and export
  save/logs (the list wraps past Quit); KEYCODE_BUTTON_A runs it. The zip is in
  /sdcard/Download/Dragon Age Origins/. Remove it, files/debug and the option after.

### Moving the Thor to a new package id (session 41)

A new application id is a new app with empty storage. What moved the owner's install
from the former package id to `dev.daorecompandroid.duo`, both debug builds, the new one
installed beside the old:

```bash
# private files: a copy on the computer first, then app to app on the device
adb exec-out run-as OLD tar cf - -C /data/user/0/OLD files > build/migrate/old-files.tar
adb shell "run-as OLD tar cf - -C /data/user/0/OLD files | run-as NEW tar xf - -C /data/user/0/NEW"
adb shell "run-as NEW sed -i 's#/Android/data/OLD/#/Android/data/NEW/#g' /data/user/0/NEW/files/mods-enabled.txt"
# external files: start NEW once (it makes its folder), stop it, then rename into it
adb shell 'cd /sdcard/Android/data && mv OLD/files/gamedata OLD/files/TU_* OLD/files/mods NEW/files/'
```

Check the new app (launcher, a save loaded) before the old one is uninstalled - and only
with the owner's word.

### Saves: the put-away autosave, names, thumbnails (session 41)

- **Put away:** in explore with Autosave when put away on, press Home (the controller's
  own key: `sendevent /dev/input/event9 1 102 1/0`) - the log: `autosave on pause:
  DoAutoSave called`, `<name> committed`, `autosave on pause: written; the guest holds`
  within a few seconds. `adb shell input keyevent KEYCODE_SLEEP` for the lock.
- **Names:** pause, Save Game, New Save, Save (the box's confirm) - the app's "Name this
  save" dialog; `adb shell input text "Some%sname"`, OK at (1446, 427) on the Thor. The
  log: `save name: ... renamed`. Load Game lists `Save (N) - Some name`.
- **Thumbnails:** a save's `scrnshot.bin` (`run-as ... cat`) is mostly non-zero; its
  `.x` is the CRC-32, big-endian.
- To reach a save screen out of combat on the morrigan route: `files/route-call` holding
  `!killallhostiles`.

### The character limit (session 41)

The game keeps saves for a limited number of characters: with seven on the device (four
went through; the exact limit is not measured), New Game says "Character limit reached"
and every route that starts a new game stops at the main menu. Count them with `run-as dev.daorecompandroid.duo ls
files/content/00000001` (one `0x<hash>_...` prefix per character). The test characters'
saves can be moved aside - `files/test-saves-moved/` holds session 40's three tour
characters; the owner's are never moved.

### The deck's faces and Home (BUG-096, BUG-097)

- Faces: boot `bash tools/route.sh morrigan`; the log says `Deck: portrait made:
  portrait_...` for each, then `Deck: portrait render at <address> ... - portrait_...`;
  the rows match the HUD's.
- Home: press the Thor's own Home key (`sendevent` on its controller, /dev/input/event9,
  key 102 - see 11.4's notes) - both screens home, `deck: put away with the game`; back
  in the game, `deck: back with the game` and the faces in their rings.

### Saves out and in (PLAN 11.2)

- The launcher's Saves row (tap it, or the row under Lossless.dll): Export one save
  writes the .zip to /sdcard/Download/Dragon Age Origins/; Import saves from a .zip
  through the picker (Download -> Dragon Age Origins). A save already there asks Keep
  both / Replace / Skip. To see it in the game: boot with only START injected (`0010 45
  0.15`), then D-pad down twice and A for Load Game, X (0x4000 with Xbox positions) to
  switch character. Write `files/inject-pad` through a temporary name and `mv`: a file
  read while still being written holds no presses.

### The second device's priority (PLAN 9.21)

- `files/hack-priority-probe` holding a number of seconds: that long after the GPU starts
  (250 for the King's Camp by the ostagar route, 30 for the title), the probe logs the
  device's extensions, makes a second device at realtime, high and medium global
  priority (realtime is refused) and times a paint-sized job on each ("priority probe:"
  lines). Remove the file after.

### PC mods and the converter (Phase 10)

- **The layers (10.12):** a mod folder holding a copy of a game file (a patch .tlk, an
  .xds) pushed to `<app storage>/mods/`, turned on in the launcher's Mods page; the log
  then says `override: <file> from .../mods/<mod>/...` when the game reads it.
- **The converter (10.13):** `python tools/make_test_texture_mod.py build/test-mod` makes
  a PC-style mod (magenta .dds for Duncan's body). Push it with `adb push` file by file
  (a folder push can fail on the shared storage's `secure_mkdirs`), open Mods, choose
  the "[PC version]" mod, Convert for this version: the report says 3 files written.
  Then the ostagar route to the King's Camp: Duncan, next to the hero, is magenta.
  Remove the mod and `mods/.originals/` after.
- **Add-ins (10.19):** a mod with a .dazip (in a .zip, .7z or .rar, or alone) converts to
  a package in `files/content/00000002/mod-<id>/` (the report names it). The ostagar
  route loads it (hide-dlc keeps `mod-` packages); the log shows the game opening
  `PRC000n:\<id>\...`. For an item-giving add-in, look in the inventory (the Back button,
  then LT/RT to the Equipment tab; injected presses: Back 0020, LT `0000 0.1 0.2 255 0`).
  With the owner's Nintendo prompts the face buttons are swapped: 0x2000 acts as A,
  0x1000 as B, 0x8000 as X. The radial menu is LT held: one `files/inject-pad` file can
  hold several lines, each its own press with its own delay, e.g. LT for 9 s, the stick
  left (Potions), 0x1000 (select), the stick up-left, 0x1000. The game's own debug
  scripts run through `files/route-call` (`!zz_givearmor` dresses the hero in armour with
  a helm).

---

## 6. Regression protection

With no CI for a project of this shape, the discipline has to be manual:

1. **Commit a screenshot at each milestone.** Visual regressions in a renderer are
   otherwise very easy to miss.
2. **Keep the x86-64 host build working.** It is the reference for ARM64 divergence.
3. **Keep frame captures** from known-good states to diff against.
4. **Record the exact toolchain and upstream commits** in `STATUS.md` §4 so output is
   reproducible.
5. **Re-run the save/load round trip** (6.5) after any change touching game state — this
   is where corruption would show, and corruption found late is expensive.

### The game's code in development (PLAN 12.2)

The APK never holds the game's code: Gradle builds `libdaogame.so` with the app and
leaves it out. After installing, `python tools/push_gamecode.py` copies the built one to
`files/gamecode/libdaogame.so` (read-only; only when its SHA-256 differs from the
device's). The boot log's `game code: ... loaded (ABI ...)` line says it was taken; a
library made for another runtime is refused with the reason ("made for another version
of the app", or the overrides that differ), and the boot report says the same. After a
change to the runtime's overrides, `python tools/gen_gamecode_recipe.py` (configuring
stops until it is run), build, and push again.

### The game's code made by the app (PLAN 12.5)

A build with the device toolchain staged (`python tools/build_device_toolchain.py`, then
`python tools/stage_gamecode_toolchain.py`, before Gradle) makes the game's code itself:
remove `files/gamecode` (`adb shell run-as dev.daorecompandroid.duo rm -rf
files/gamecode`), open the app, and choose **Build** (PLAN 12.8: the launcher starts
`GameCodeService` only then; it is offered once the disc is extracted and a title update is
in place, with no extraction or disc 2 install going on, and the code not already made for
them). To try the row without losing
the made code, set `files/gamecode/manifest.json` aside: Build says "Build again", and the
code counts as made again when it is put back. Follow it in `files/gamecode/state.txt`
(phase, done, total, words; touched every 10 s), the logs in `files/gamecode/work/log/`
(each compile's, XenonRecomp's, the link's - deleted when it is made), and logcat's
`WardenGameCode`. Made: `manifest.json` says `"made": "device"`. Run 595: 7 min 30 s.

### The game's code compiled on the device (PLAN 12.1)

`python tools/gamecode_probe.py [--jobs N]` pushes the device toolchain
(`tools/build_device_toolchain.py`), the NDK's headers, SIMDe and the 211 generated files
to `/data/local/tmp/gc` on the Thor, compiles every file there with the guest's own flags
(`toybox time -v` around each, `xargs -P` for the jobs) and reports the wall time, each
file's time and the largest memory one compile reached; the generated files and objects
are deleted after (`--keep` leaves them). From Git Bash, adb paths need
`MSYS2_ARG_CONV_EXCL='*'` (the script runs adb itself, so it needs nothing). First
result (session 41): 8 min 42 s at 6 jobs, `ppc_recomp.17` 521 s of it.

The device's XenonRecomp is checked the same way by hand: push it with the disc's
`default.xex`, the title update's `default.xexp`, `XenonUtils/ppc_context.h` and
DAORecomp's TOML (its `out_directory_path` pointed at a folder that exists), run
`./xenonrecomp config/DAO_tu6.toml ppc_context.h tu6`, and compare `sha256sum` of its
output with `recomp/generated/ppc_tu6` - identical on the Thor (8.0 s).
