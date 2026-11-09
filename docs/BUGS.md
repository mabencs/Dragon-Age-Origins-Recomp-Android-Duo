# Bug tracker

Every defect this project has run into, including ones in upstream code and in the
device's drivers. Each entry says **what it actually causes**, because several of these
fail in ways that give no useful diagnostic at all.

Update this file whenever a bug is found or fixed. Keep IDs stable — they are referenced
from commit messages, `STATUS.md` and the design docs.

**Status values**

| | |
|---|---|
| `OPEN` | Not addressed. Still bites. |
| `FIXED` | Corrected at the source (for upstream code, in our graft edits). |
| `WORKAROUND` | Upstream is still wrong; our tooling avoids or compensates for it. |
| `WONTFIX` | Understood and deliberately left alone. Reasoning recorded. |
| `WATCH` | Not understood yet, not currently hurting us. |

---

## Index

| ID | Component | Severity | Status | Summary |
|---|---|---|---|---|
| [BUG-001](#bug-001) | DAORecomp shader | high | **FIXED** | `dao_resolve_direct.xesli` uses HLSL `saturate()`, so it cannot compile as GLSL |
| [BUG-002](#bug-002) | Xenia | high | **FIXED** | `vulkan_api.h` include order makes `vulkan.hpp` fail to compile |
| [BUG-003](#bug-003) | DAORecomp | high | **FIXED** | `mutex.cc` `reinterpret_cast` between integer types is ill-formed on Android |
| [BUG-004](#bug-004) | XenonRecomp | high | **WORKAROUND** | Aborts with `0xC0000409` and *no output at all* on a path mistake |
| [BUG-005](#bug-005) | XenonRecomp | medium | **OPEN** | Integer divide emitted with no zero guard; crashes on x86 hosts |
| [BUG-006](#bug-006) | XenonRecomp | cosmetic | **OPEN** | ARM64 FPSCR rounding table comment mislabels two entries |
| [BUG-007](#bug-007) | simde / toolchain | high | **WORKAROUND** | `__builtin_roundevenf` becomes an unresolvable libcall without SSE4.1 |
| [BUG-008](#bug-008) | git | medium | **WORKAROUND** | `--recurse-submodules=no` makes later `submodule update` a silent no-op |
| [BUG-009](#bug-009) | Adreno driver | medium | **OPEN** | HDR swapchains come back 10-bit sRGB instead of PQ |
| [BUG-010](#bug-010) | our difftest | medium | **FIXED** | Host FP rounding not normalised, producing a false divergence |
| [BUG-011](#bug-011) | our difftest | medium | **FIXED** | Selection filter missed macro spellings, admitting unsafe functions |
| [BUG-012](#bug-012) | our tooling | low | **FIXED** | Error classifier broke on Windows drive letters |
| [BUG-013](#bug-013) | our tooling | medium | **FIXED** | Assembler did not copy `.inc` files |
| [BUG-014](#bug-014) | AYN Thor | low | **WATCH** | `persist.sys.force_sw_gles=1` set on the device |
| [BUG-015](#bug-015) | NDK | low | **WORKAROUND** | `spirv-opt` has no `--canonicalize-ids` |
| [BUG-016](#bug-016) | DAORecomp | high | **FIXED** | `graphics_system.cc` uses Win32 unguarded in two places |
| [BUG-017](#bug-017) | Xenia | high | **FIXED** | `window_android.cc` passes 3 args to a 4-arg function |
| [BUG-018](#bug-018) | Xenia | high | **FIXED** | `threading_posix.cc` needs glibc robust mutexes; bionic has none |
| [BUG-019](#bug-019) | Xenia | high | **FIXED** | `SetAndroidPreApi26Name` called from a const method but not const |
| [BUG-020](#bug-020) | Xenia | high | **FIXED** | `filesystem_android.cc` redefines `SetAttributes` as a stub, colliding with the working one |
| [BUG-021](#bug-021) | our tooling | medium | **FIXED** | Object files named by loop index, so stale objects were linked twice |
| [BUG-022](#bug-022) | our tooling | medium | **FIXED** | Link-error check ignored `unable to find library` |
| [BUG-023](#bug-023) | Xenia | high | **FIXED** | Instance never requests `VK_EXT_swapchain_colorspace`, hiding every non-sRGB colour space |
| [BUG-024](#bug-024) | our app | high | **FIXED** | `getInsetsController()` before `setContentView` throws, crashing on launch |
| [BUG-025](#bug-025) | Adreno driver | medium | **WORKED AROUND** | No `HDR10_ST2084` swapchain colour space, though Android reports HDR10 support - the 10-bit swapchain's buffers are tagged BT.2020 PQ directly (PLAN 9.2) |
| [BUG-026](#bug-026) | our app | high | **FIXED** | `setBackgroundColor` on the `SurfaceView` drew over the surface, hiding every presented frame |
| [BUG-027](#bug-027) | Xenia | low | **WORKAROUND** | `windowed_app_context.h` tells subclasses to call a private overload |
| [BUG-028](#bug-028) | our tooling | medium | **OPEN** | `appbringup/run.sh` carries on after a failed install and reports on the old build |
| [BUG-029](#bug-029) | our app | high | **FIXED** | Game data needed a folder grant, which Android refuses for `Download` |
| [BUG-030](#bug-030) | our runtime | high | **FIXED** | Import stubs return 0, which status-returning callers read as success |
| [BUG-031](#bug-031) | our runtime | low | **FIXED** | Every attached guest thread is given the same stack |
| [BUG-032](#bug-032) | our runtime | medium | **WORKAROUND** | The `RtlRaiseException` stub returns, so the title runs on past a raise |
| [BUG-033](#bug-033) | Xenia | low | **WONTFIX** | Xenia's `NtOpenFile` takes five arguments; the console's takes six |
| [BUG-034](#bug-034) | our runtime / title | high | **WATCH** | Startup can skip mounting the packages; the first shader then has no data |
| [BUG-035](#bug-035) | our runtime | medium | **FIXED** | The network thread spun through two Winsock stubs at five million calls a second |
| [BUG-036](#bug-036) | our runtime | high | **FIXED** | After startup the Update thread spins in `Sleep(0)` on a flag nothing sets; the GPU interrupt never fires |
| [BUG-037](#bug-037) | our build | high | **FIXED** | The guest function trace was never compiled into the title: a shadowed header the quoted include never reached |
| [BUG-038](#bug-038) | our runtime | high | **FIXED** | With the GPU up, the Render thread presents 50,000 frames a second without kicking the ring, then calls a null pointer |
| [BUG-039](#bug-039) | our runtime / renderer | high | **FIXED** | The title renders and presents at 60 Hz, and every frame is black |
| [BUG-040](#bug-040) | our runtime (title update) | low | **FIXED** | The EA logo movie plays yellow-green: the Bink shader is given the key art as its luma plane (BUG-046) |
| [BUG-041](#bug-041) | our runtime / diagnostics | low | **FIXED** | The guest thread report crashed on a bad function-name pointer |
| [BUG-042](#bug-042) | title / our runtime (GPU) | medium | **WATCH** | Intermittently the title stops kicking the ring buffer and the swaps freeze |
| [BUG-043](#bug-043) | our runtime (physical memory) | high | **FIXED** | After sign-in the Update thread waits forever for a FMOD non-blocking load slot: the request pool was never allocated |
| [BUG-044](#bug-044) | our runtime (write watch) | high | **FIXED** | Two threads writing one GPU-watched page at once: the second fault is "not watched" and kills the process |
| [BUG-045](#bug-045) | Adreno driver | high | **FIXED** | The 3D scene is lost behind the HUD and menus (no character in character creation): the driver reads gl_FragCoord as zero in shaders declaring DenormFlushToZero |
| [BUG-046](#bug-046) | XenonRecomp (XEX patcher) | critical | **FIXED** | The title update applied on the device differs from the host's: an overlapping memcpy smears 54 KB of read-only data on ARM64 |
| [BUG-047](#bug-047) | our runtime / GPU (ring kicks) | medium | **WATCH** | Rarely a command buffer holds a malformed packet and the debug build's assertion kills the process - twice an indirect buffer, once the primary ring right after the ring poller's kick (run 297) |
| [BUG-048](#bug-048) | our runtime (import stubs) | medium | **FIXED** | Every save's time was garbage: the void `RtlTimeToTimeFields` stub left the time fields unwritten, and `RtlTimeFieldsToTime`'s refused every date |
| [BUG-049](#bug-049) | our runtime / GPU (pipeline cache) | low | **FIXED** | Every save's thumbnail was blank: its quad, drawn once at the save, drew the pipeline cache's placeholder pixel shader |
| [BUG-050](#bug-050) | our runtime (kernel waits) | high | **FIXED** | `KeWaitForMultipleObjects` read its timeout from the stack instead of r9, so the title's infinite waits polled; its XAudio callback skipped 11% of its frames |
| [BUG-051](#bug-051) | DAORecomp (recompiler config) | critical | **FIXED** | Entering the Korcari Wilds crashed: a jump table missing from the recompiler's config left two of a function's cases with no code |
| [BUG-052](#bug-052) | our runtime / GPU (pipeline cache) | medium | **FIXED** | The HUD's party portraits showed their coloured discs but no faces: drawn once at an area load, they drew the pipeline cache's placeholder pixel shader |
| [BUG-053](#bug-053) | our app (input) | medium | **FIXED** | Android's Back never reached the activity: the virtual keyboard's device id matched an empty pad slot |
| [BUG-054](#bug-054) | our app (frame pacing) | medium | **FIXED** | Frame pacing aimed each frame a refresh early by the panel's own times, so a frame a refresh late went uncounted; its period correction wound up; catch-up frames after a stall left the latency high |
| [BUG-055](#bug-055) | upstream (shader storage) / our runtime | medium | **FIXED** | Xenia's shader and pipeline storage kept nothing between sessions on Android: emptied at every start, never flushed, the driver's cache never saved |
| [BUG-056](#bug-056) | our app (lifecycle) | high | **FIXED** | Coming back to the app could hang its main thread for good in the wake pipe's read: a frozen picture and "not responding", or a black screen after the power button |
| [BUG-057](#bug-057) | our app (lifecycle) | high | **FIXED** | The game ran on in the background: put away, only its sound stopped |
| [BUG-058](#bug-058) | upstream (Xenia) | low | **FIXED** | Xenia's timer queue and log writer spin-wait: about 10% of one core each. Now they nap while the game is held or not yet started (the launcher); in play they spin as before |
| [BUG-059](#bug-059) | upstream (Xenia) / our runtime | low | **FIXED** | With no Vulkan instance the boot went on, and crashed in the command processor's constructor |
| [BUG-079](#bug-079) | our runtime | medium | **FIXED** | The guest output captures, a debugging aid, ran in every session: a 4 MB PNG of the screen every minute of play, never removed (about 240 MB an hour in the app's files directory), each with a GPU readback and a PNG encode |
| [BUG-119](#bug-119) | our runtime and app / the title's render thread | high | **FIXED** | Put away (the lid, the lock), the game kept a core busy: the render thread spun on a flag a held thread sets in half the lid-closes in play (93% of a core); the XMA decoder, the deck and the touch check polled on. Held by signal, the polls stopped: 1.4-1.7% of a core |
| [BUG-118](#bug-118) | our tools (`push_gamecode.py`) | low | **FIXED** | A pushed game code's manifest was never written: the launcher asked for Build again after every push |
| [BUG-117](#bug-117) | our runtime / the title's waits | medium | **FIXED** | High temperatures (issue #2): the title's waits spin - the main thread's Sleep(0) hand-off and the render thread's GPU fence wait - and the guest trace costs every function's entry. Options -> Heat guard's rests sleep instead, no frame rate lost: the hottest big core 9.5-25 C cooler (the guest trace only in Debugging Mode) |
| [BUG-116](#bug-116) | the device's firmware (AYN Thor) / our app | high | **FIXED** | The game's volume did not follow Android's: AYN gives apps on the second screen their own volume, and the deck (an activity there) put the game there. Held volume-up past the top raised it further; with the deck off, the media volume was applied twice. The deck is a window of the game's now |
| [BUG-115](#bug-115) | our app (the deck's Map tab, PLAN 8.4) | medium | **FIXED** | The deck's map was upside down against the game's map screen in every area whose North is 1 or 3 (the King's Camp): it was turned anticlockwise, the game turns clockwise |
| [BUG-114](#bug-114) | our app (the update, PLAN 13.1) | high | **FIXED** (0.1.5-alpha) | The launcher's update failed after Android's Install: the launcher, back in front, abandoned the session Android was installing - "the install was cancelled". In 0.1.2 to 0.1.4; mostly fails, sometimes wins |
| [BUG-113](#bug-113) | our runtime (FSR 3.1's composite, frame generation's UI paste) | medium | **FIXED** | With FSR 3.1 the upper corners of the world juddered: the game's corner vignette, drawn after the capture, counted as UI, so the corners showed the jittered render-size frame |
| [BUG-112](#bug-112) | our runtime (FSR 3.1) / a replaceable driver, dropped | medium | **FIXED** (the fallback); the driver work dropped | With FSR 3.1 chosen on a device where its upscaler cannot be made, the jitter stayed on: the whole world juddered under a still HUD. Found with Mesa's Turnip, which the owner then dropped |
| [BUG-111](#bug-111) | our app (the update, PLAN 13.1) | medium | **FIXED** | Before it shipped: cancelling Android's first install dialog left the launcher's row on Installing for good, ignoring taps |
| [BUG-110](#bug-110) | our app (the deck, PLAN 8.1) | medium | **FIXED** | Back from sleep, the deck could be missing: the game came back before the second screen, the one try found no display on, and only a new start brought it back |
| [BUG-109](#bug-109) | the device's firmware (AYN Thor) / our app | high | **FIXED** (BUG-116) | The game's sound did not follow Android's media volume: audible with the media volume at 0. The app now applies the volume itself, on AYN devices |
| [BUG-108](#bug-108) | our runtime (frame generation, PLAN 9.14) | high | **FIXED** | With an upscaler and Lossless frame generation, a conversation whose start had ten frames without a 3D scene froze the game for nearly 4 s: the generator made again at the small size and again at the large |
| [BUG-107](#bug-107) | our app (the deck, PLAN 8.5) | medium | **FIXED** | The deck's Quests tab never showed a completed quest: they are in a list of their own, Journal.CompletedPlots, which it did not read - and it listed subplots as quests of their own, in no group |
| [BUG-106](#bug-106) | our app (the deck, PLAN 8.1) | high | **FIXED** | With the second screen left as the top display, the first key waited on the deck's window, which takes none: "isn't responding", again after each Wait, and the deck gone with Close app - which a pad's button can press |
| [BUG-105](#bug-105) | our app (the deck, PLAN 8.5) | medium | **FIXED** | Every codex entry on the deck said its text was not read yet: the entry's Descriptions are a list of pages, read as one text; long pages and line ends would have been lost as well |
| [BUG-104](#bug-104) | our test tools (`tools/route.sh`) | high | **FIXED** | A test route's files left in the owner's app after run 598 - New Game went straight to a debug start, and the DLC were hidden from the main menu, at every start until removed; the launcher and the log now say when test files are in place |
| [BUG-103](#bug-103) | our runtime (the game's code as a library, PLAN 12.2) | high | **FIXED** | With Title Update 6's game code (disc 2's) the game hung at startup: every thread's entry and callback was looked up in the indirect-call table at Title Update 7's place - XenonRecomp's PPC_LOOKUP_FUNC baked this build's image size into the runtime |
| [BUG-102](#bug-102) | our runtime (saves, warden_xam.cc) | high | **FIXED** | A save cut off mid-write (the app swiped away during an autosave) was left half-written in its own folder - corrupted; saves are now written aside and put in place whole |
| [BUG-101](#bug-101) | our runtime (GPU resolves) | low | **FIXED** | Every save's thumbnail blank again: the thumbnail's resolve never reached guest memory (readback off; BUG-049's whole thumbnail had come with the test marker on) |
| [BUG-100](#bug-100) | our runtime (frame generation, FSR 3.1) | high | **FIXED** | The autosave asked for as the app is put away finished only when it came back: with the window gone FSR 3.1 stopped upscaling, the frame changed size, and frame generation made its generator again - 8.7 s on the GPU thread, the game stopped behind it |
| [BUG-099](#bug-099) | our runtime (the sound output, PLAN 9.12) | medium | **FIXED** (an option) | The sound very low at the handheld's full volume: the mixdown keeps combat's headroom, so the rest of the game peaks near -20 dBFS; Options -> Volume boost raises it with a limiter |
| [BUG-098](#bug-098) | our app (the deck) | low | **FIXED** | A deck opened with the faces already drawn showed initials: the faces came at its first pull, before the deck was made again with the game's art, which lost them |
| [BUG-097](#bug-097) | our app (the deck, PLAN 8) | medium | **FIXED** | Home sent the main screen home but left the deck on the second; a second press, for the second screen, reached the home app already showing (the home app), which can bring the game back on the main screen |
| [BUG-096](#bug-096) | our runtime (the deck's faces, PLAN 8.9) | medium | **FIXED** | The deck's faces on the wrong rows (the hero's row with Gorim's face): each portrait render was placed by the burst's order and the resolve's address, a guess; now by the portrait's own name |
| [BUG-095](#bug-095) | our app (the manifest) | low | **FIXED** | The deck's task affinity was the package id written out (`<package>.deck`), so a second install of the app (11.4's clean copy) would have shared its second-screen task with the owner's |
| [BUG-094](#bug-094) | our build (licences) | high | **FIXED** | UnRAR's library, built in for .rar mods (10.19), carries a licence restriction GPL-3.0 does not allow, so a build of the app could not have been shared at all; .rar is now read by libarchive (BSD) |
| [BUG-093](#bug-093) | upstream (XenonRecomp) | medium | **FIXED** | Six instructions in the title were left as traps by the recompiler (`blrl`, `vpkd3d128` and `vupkd3d128` with Xenos half floats): reaching one would kill the game with no log line; none was reached in testing |
| [BUG-092](#bug-092) | our runtime (frame generation's pacing, PLAN 9.14) | high | **FIXED** | Each conversation's start stuttered: its first frame has no 3D scene, so no generated frame, and the pacing switched out of frame generation's pairs for it and back - a 60-100 ms frame and seconds of uneven frames |
| [BUG-091](#bug-091) | our runtime (10.3's pipeline cache save) | medium | **FIXED** | Every 30 s while a session met pipelines new to it, the driver's whole pipeline cache (33 MB) was copied out and written to storage on the GPU command thread - a stall, landing on conversations' starts among others - though the data had not changed |
| [BUG-090](#bug-090) | our app (the test launch, PLAN 10.19) | medium | **FIXED** | The test launch's files/hide-dlc hid every package from the game, the mods' add-ins too, so a test of an add-in had nothing loaded; the launcher's disc-2 count counted mod packages as DLC |
| [BUG-089](#bug-089) | our app (the Mods page, PLAN 10.19) | medium | **FIXED** | The Mods page's file picker greyed out a .rar download: its list of file types did not hold the one Android gave the file |
| [BUG-088](#bug-088) | upstream (DAORecomp's mod converter) | high | **FIXED** | A generic GFF field holding a string points at the string itself; upstream's converter read and wrote one pointer too many, so a PC add-in's module (.cif) with a string variable could not be converted |
| [BUG-087](#bug-087) | our app (the deck's quickbar, PLAN 8.8) | medium | **FIXED** | A tap on the deck's quickbar went through with the game's pause menu open: the guard read the game mode from the HUD's own copy, which does not see menus - now the engine's mode |
| [BUG-086](#bug-086) | the game's own (the arrival view) | - | **NOT OURS** | Awakening's Smuggler's Cove, jumped to through its debug menu, is black on arrival but for the player and white shapes - DAORecomp's Windows build shows the very same from the same save (session 40): the player arrives at the tunnel's mouth facing out of the area, where nothing is modelled |
| [BUG-085](#bug-085) | the game's own (a debug jump's state) | - | **NOT OURS** | The Arl of Denerim's estate, jumped to through the debug menu, renders black but for the player and a row of doors - DAORecomp's Windows build shows the very same with the same jump (session 40): the player lands outside the estate's level. On the way, the game's occlusion-query culling was turned off as DAORecomp does (fewer draws, the camp 37.6 -> 38.9 fps uncapped) |
| [BUG-084](#bug-084) | our runtime (the logger) | high | **FIXED** | The game died in a native crash in the log's ring buffer during an area load - three times in one night: a log line is formatted into a 64 KiB thread buffer with `fmt::format_to_n`, whose result is the line's whole length, not what fit, and the logger copied that many bytes out of the buffer - past its end when the port's sync report grew beyond 64 KiB during a load. The length is clamped now, and the port's reports are logged in pieces. |
| [BUG-083](#bug-083) | our tools (the routes) | medium | **FIXED** | With downloadable content installed (Awakening), New Game first asks for the campaign, and the scripted routes' presses landed a step early - stuck in the prelude's debug conversation |
| [BUG-082](#bug-082) | our app (the deck, PLAN 8.5) | low | **FIXED** | The deck's Quests tab showed the journal's markup (`<emp>` tags) and its bookkeeping fields (`Completed: 0`, `PlotGroup: pre_plot_group`) - the owner found it |
| [BUG-081](#bug-081) | our runtime (frame generation, PLAN 9.14) | high | **FIXED** | Closing the game's Select menu froze play for 4-5 s with Lossless 2x: its generator was made again twice (about 2 s each) for a one-frame change of size - the owner found it |
| [BUG-080](#bug-080) | our app (the deck, PLAN 8.4) | low | **FIXED** | The deck's Items tab showed the plot items without their icons: its atlas reader kept only the cells named `ico_`, not the 120 `plt_ico_` ones |
| [BUG-078](#bug-078) | the app (the deck, PLAN 8.1) | high | **FIXED** | Launching the deck onto the second screen made it the focused display: the controller's keys would have gone there, where no window takes them - found before it shipped (run 454) |
| [BUG-077](#bug-077) | Xenia (DAORecomp's subset) | low | **FIXED** | The pipeline storage truncated and then written without a seek between: an update stream needs one after reading (not seen failing here; upstream's fix taken) |
| [BUG-076](#bug-076) | the game (TU6) / DAORecomp | high | **FIXED** | Loading an area with input held could crash the render thread: an effect made while its resource streams in has a null parameter, and its bind read null+48 (DAORecomp's fix, ported) |
| [BUG-075](#bug-075) | Ours (FSR 3.1) / the presenter | medium | **FIXED** | With FSR 3.1 on, the presenter still ran the Upscaling setting's CAS sharpening over FSR 3.1's already sharpened frames (FSR 1 and CAS chosen): both on at once |
| [BUG-074](#bug-074) | Ours (input) / the Thor's pad | high | **FIXED** | The Thor's pad reports its face buttons by their printed (Nintendo) letters: with the Nintendo prompts the bottom button, drawn as the game's confirm, cancelled, and the right one confirmed |
| [BUG-073](#bug-073) | Ours (frame generation) / Android | high | **FIXED** | In FIFO, Android's buffer queue takes a pair's second frame only once the first is latched (about one GPU frame): presented from the guest output thread, it held the title at 22 fps with the GPU 80% busy |
| [BUG-072](#bug-072) | Ours (frame generation) | high | **FIXED** | The pairs' readiness counted frames held by the compositor's phase as late: it wound up to 101 ms, every swapchain image was held and the title waited for one (19 fps) |
| [BUG-071](#bug-071) | Xenia (upstream) / ours | high | **FIXED** | Xenia's MAILBOX swapchain replaced the queued frame, so one frame of each pair never reached the panel; with FIFO Xenia paints from the UI thread, which showed the real frames only |
| [BUG-070](#bug-070) | Ours (frame generation) | high | **FIXED** | Generated frames presented as due at once made Android's buffer queue drop every real frame held for its time: the panel showed generated frames only |
| [BUG-069](#bug-069) | Ours (frame generation) | high | **FIXED** | DAORecomp's frame pacer, timed by the CPU, does not fit a GPU-bound game on one queue: both paints ran back to back after the title's frame, and the pacer judged the panel behind and paused generation for 5-60 s at a time |
| [BUG-068](#bug-068) | Ours (tools) | low | **FIXED** | `game_check.py` stopped with a UnicodeEncodeError on a window title holding an emoji, before its verdict: no build could start |
| [BUG-067](#bug-067) | Ours (FSR 3.1) | high | **FIXED** | FSR 3.1 ran without its depth and motion: the three shared resources the caller must pass with each dispatch were never passed - any camera motion smeared, motion vectors had no effect |
| [BUG-066](#bug-066) | Ours (FSR 3.1) | medium | **FIXED** | FSR 3.1 jittered the post-processing's half-size multisampled passes and could take its depth input from them; the depth copied twice a frame |
| [BUG-065](#bug-065) | Ours (FSR 3.1) | high | **FIXED** | Making FSR 3.1's context or images again ended Xenia's open submission in the middle of the swap: the process died (SIGTRAP) at the first render size or precision change |
| [BUG-064](#bug-064) | FidelityFX SDK | high | **WORKED AROUND** | The Vulkan backend chooses shader paths (64-lane subgroups, 16-bit arithmetic, synchronization2) by the extensions the GPU has, not the features the device enabled: invalid Vulkan on Xenia's device |
| [BUG-063](#bug-063) | FidelityFX SDK | medium | **WORKED AROUND** | FSR 3.1's context does not fit its opaque block where `wchar_t` is 4 bytes (Android): the SDK does not compile |
| [BUG-062](#bug-062) | Xenia (upstream) | medium | **FIXED** | The presenter made its last pass's pipeline again at every paint, after waiting for the previous paint to finish on the GPU: frames at the King's Camp 3, 4 or 5 refreshes apart |
| [BUG-061](#bug-061) | our runtime | medium | **FIXED** | Rewriting the UI's atlas descriptions while the game ran (a live switch of the button prompts) crashed its next load |
| [BUG-060](#bug-060) | our runtime / Adreno driver | medium | **FIXED** | The pipeline storage created every stored pipeline at startup, and a pipeline whose shaders spill holds about 24 MB of the driver's memory: 1.1 GB at each start, growing with every area visited |

Open: **6**. Worked around: **9**. Watched: **4**. Fixed: **64**. Won't fix: **1**. (84 entries; counted from the table above, which is the record - recount when a status changes.)

Three of these (BUG-017, BUG-018, BUG-019) are the same underlying problem: **Xenia's Android path has rotted because nobody builds it.** Each is a small mechanical error that any compile of that target would have caught.

---

## BUG-001
**DAORecomp shader uses HLSL `saturate()` — cannot compile as GLSL**
`xenia/gpu/shaders/dao_resolve_direct.xesli:52` · severity high · **FIXED**

Xenia's shaders are written in `xesl`, which abstracts over shading languages;
`saturate_xe` becomes `saturate()` in HLSL and `clamp(x, 0, 1)` in GLSL. Every other
Xenia shader uses the abstraction. DAORecomp's own resolve shader calls raw
`saturate()`, which exists only in HLSL.

**What it causes:** `dao_resolve_direct.cs.xesl` and `dao_resolve_direct_scaled.cs.xesl`
fail to compile to SPIR-V with `'saturate' : no matching overload`. Two of the game's
resolve paths would have no Vulkan implementation. Invisible in DAORecomp's own build
because it is Direct3D-only.

**Fix:** graft edit in `tools/assemble_runtime.py` — `saturate` → `saturate_xe`.
Shader count went 119/121 → 121/121.

---

## BUG-002
**Xenia's `vulkan_api.h` include order breaks `vulkan.hpp`**
`xenia/ui/vulkan/vulkan_api.h` · severity high · **FIXED**

`vulkan_api.h` includes `vulkan_to_string.hpp` without including `vulkan.hpp` first.
That header sets its own include guard and *then* includes `vulkan.hpp`. Later
`vulkan.hpp` tries to include `vulkan_to_string.hpp` for its own `to_string()` calls
and is skipped, because the guard is already set.

**What it causes:** every translation unit that reaches `vulkan_api.h` fails with
`use of undeclared identifier 'to_string'` at `vulkan.hpp:6789` (inside
`ErrorCategoryImpl::message`). That was 22 of the 30 Vulkan translation units.

**Fix:** graft edit — include `vulkan.hpp` before `vulkan_to_string.hpp`, which is
Vulkan-Hpp's documented order.

---

## BUG-003
**`reinterpret_cast` between integer types is ill-formed on Android**
`xenia/base/mutex.cc:94` · severity high · **FIXED** *(Phase 3.1c)*

```cpp
inline uint64_t xe_current_thread_id() {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pthread_self()));
}
```

`reinterpret_cast` may convert pointer↔integer, but **not** between two different
integer types. On glibc `pthread_t` is `unsigned long`, which *is* `uintptr_t` on LP64,
so the cast is a no-op conversion to the same type and compiles. Android's bionic
defines `pthread_t` as `long` — signed — so it is a different type and the cast is
ill-formed.

This is DAORecomp's own modified `mutex.cc`, not Canary's.

**What it causes:** `mutex.cc` does not compile for `aarch64-linux-android`. Since
mutexes are core infrastructure, nothing links without it.

**Fix:** graft edit — `static_cast<uint64_t>(pthread_self())`. A `static_cast` is
correct and portable here because the value is only ever compared for equality to
detect a re-entrant owner.

---

## BUG-004
**XenonRecomp aborts with `0xC0000409` and no diagnostic whatsoever**
`XenonRecomp/recompiler.cpp` · severity high · **WORKAROUND**

Two separate path mistakes produce the same silent abort:

1. `out_directory_path` must be **relative to the config file's directory**. Every TOML
   path is resolved that way — `recompiler.cpp:2856` builds
   `"{configDir}{outDirectoryPath}/{file}"` — so an absolute path silently becomes
   `config/C:/...`.
2. **The output directory must already exist.** XenonRecomp does not create it.

Either way the resulting failure is an uncaught C++ exception, which on Windows
fastfails as `STATUS_STACK_BUFFER_OVERRUN` (`0xC0000409`).

**What it causes:** the process dies having written **nothing** — not one line of log,
no error message, no partial output. Both of upstream's own shipped configs reproduce
it if `ppc/` and `ppc_tu6/` are absent, which they are in a fresh checkout. Diagnosing
it from scratch took bisecting the config section by section.

**Workaround:** `tools/recompile.py` creates the output directory, rewrites
`out_directory_path` as a relative path, and recognises `0xC0000409` to print what to
check. Upstream is unchanged.

---

## BUG-005
**Integer divide emitted with no zero guard**
`XenonRecomp` generated code · severity medium · **OPEN**

The recompiler emits PowerPC `divwu`/`divw` as a raw C division:

```c
// divwu r10,r4,r9
ctx.r10.u32 = ctx.r4.u32 / ctx.r9.u32;
// twllei r9,0          <- the original compiler's own check, AFTER the divide
```

That ordering is correct for the target hardware: on PowerPC, dividing by zero produces
an undefined result but **does not trap**, so the original compiler could safely divide
first and check afterwards.

**What it causes:** architecture-dependent behaviour on a zero divisor.

| | divide by zero | matches Xbox 360? |
|---|---|---|
| ARM64 `udiv` | returns 0 silently | **yes** |
| x86-64 `div` | raises #DE → SIGFPE | no |

Measured in the Phase 2 differential test: **16 of 6,635 functions crashed on the
x86-64 host and none on the handheld.** So the ARM64 target is unaffected — but the
x86-64 build is our *reference renderer* for Phase 3, and it carries a latent crash
class the real target does not. A divide-by-zero crash on the host is not necessarily
a port bug.

**Why still open:** fixing it would mean guarding every division, which costs
performance on the target that is already correct. The honest fix is host-only. Noted
rather than patched. Contrast with `cntlzw`, which upstream *does* guard correctly
(`x == 0 ? 32 : __builtin_clz(x)`).

---

## BUG-006
**ARM64 FPSCR rounding table comment mislabels two entries**
`XenonUtils/ppc_context.h` · severity cosmetic · **OPEN**

```c
// Nearest, Zero, -Infinity, -Infinity
static constexpr size_t GuestToHost[] = { 0 << RoundShift, 3 << RoundShift,
                                          1 << RoundShift, 2 << RoundShift };
```

The mapping itself is **correct** — PPC 0/1/2/3 (nearest, toward zero, +inf, −inf) maps
to ARM FPCR RMode 0/3/1/2 (RN, RZ, RP, RM). The comment should read
"Nearest, Zero, +Infinity, −Infinity".

**What it causes:** nothing at runtime. Recorded because anyone auditing ARM64
floating-point behaviour will read that comment, conclude the table is wrong, and lose
time proving it isn't — which is exactly what happened here.

---

## BUG-007
**`__builtin_roundevenf` becomes an unresolvable libcall without SSE4.1**
simde + MSVC CRT · severity high · **WORKAROUND**

simde reaches rounding through `__builtin_roundevenf`. Clang lowers that to a native
instruction when one exists — `ROUNDPS` with SSE4.1 on x86, `FRINTN` on ARM64 — and
otherwise emits a call to the C23 function `roundevenf`, which the MSVC CRT does not
provide.

**What it causes:** linking the recompiled game for x86-64 fails with
`undefined symbol: roundevenf`, referenced from one translation unit, with no hint that
the cause is a missing instruction-set flag. ARM64 is unaffected.

**Workaround:** the host build carries `/arch:AVX`. This is correctness, not
optimisation. It also explains why DAORecomp requires an AVX CPU (2011+) and why
XenonRecomp's own tests build with `-march=sandybridge`.

---

## BUG-008
**`--recurse-submodules=no` makes later `submodule update` a silent no-op**
git · severity medium · **WORKAROUND**

Cloning with `--recurse-submodules=no` writes `submodule.active = no` into that clone's
config. Afterwards `git submodule update --init --recursive` **does nothing**: no
output, exit status 0, nothing fetched, and `git submodule status` still shows every
submodule unregistered with a `-` prefix. `git submodule init` is equally silent.

**What it causes:** the submodule directories exist but are empty, so the first symptom
is an unrelated-looking build failure — in our case CMake reporting
`thirdparty/fmt does not contain a CMakeLists.txt`. Nothing points at the clone flag.

**Workaround:** `git config --local --unset submodule.active` before initialising.
Documented in `docs/TOOLCHAIN.md` and `THIRD_PARTY.md`.

---

## BUG-009
**Adreno HDR swapchains come back 10-bit sRGB instead of PQ**
Qualcomm Adreno driver · severity medium · **OPEN** *(affects Phase 9.2)*

A documented Adreno 740 issue: requesting an HDR swapchain yields
`VK_FORMAT_A2B10G10R10_UNORM_PACK32` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` rather
than a true PQ `VK_COLOR_SPACE_HDR10_ST2084_EXT` surface.

**What it causes:** HDR output silently renders as washed-out SDR — the application
believes it has an HDR surface and tone-maps for one. Not detectable from the request
succeeding.

**Mitigation when we reach 9.2:** verify the colour space actually *granted*, never
trust the request. Both required extensions are present on the device
(`VK_EXT_swapchain_colorspace`, `VK_EXT_hdr_metadata`), so this is a driver behaviour
rather than a capability gap. Decision D8 already defaults HDR off for the separate
reason that the panel peaks at 420 nits.

---

## BUG-010
**Differential test did not normalise host FP rounding mode**
`tools/gen_difftest.py` · severity medium · **FIXED**

`seed()` reset flush-to-zero but not the rounding mode. A case that executed `mtfsf`
earlier in the run left the host floating-point control register changed, and a later
case using `mffs` (move from FPSCR) then observed it.

**What it causes:** one function (`tu6_sub_82AA9AF0`) reported a byte-different context
between architectures and looked like a genuine ARM64 logic bug. Running it in
isolation produced identical state on both, which is what pointed at the harness.

**Fix:** `seed()` now also calls `storeFromGuest(PPC_ROUND_NEAREST)`. Genuine suspects
went from 1 to 0 across 6,635 functions.

---

## BUG-011
**Differential test selection filter missed macro spellings**
`tools/gen_difftest.py` · severity medium · **FIXED**

The filter excluded functions by matching `PPCQueryTimebase` and `ctx.fn[`, but the
generated code reaches those through **macros**: `PPC_QUERY_TIMEBASE()` and
`PPC_CALL_INDIRECT_FUNC`.

**What it causes:** functions that read the timebase or make indirect calls were
admitted into a test that assumes determinism and no control transfer. The timebase
case aborted loudly — `FATAL: difftest reached import` — which is exactly what the
import stubs exist for; without them it would have produced quietly meaningless
results.

**Fix:** filter matches the macro spellings, and also excludes
`__builtin_unreachable`, whose behaviour if reached is undefined and could differ by
platform for reasons that are not a port bug.

---

## BUG-012
**Error classifier broke on Windows drive letters**
`tools/compile_runtime.py` · severity low · **FIXED**

The diagnostic regex used `^(?P<file>[^:]+):` to match `file:line:col: error:`, which
cannot match `C:\path\file.cc:94:32:` because the drive letter contains a colon.

**What it causes:** every compile failure was classified as `[other]` with the message
truncated to `In file included from ...`, so 28 failing files appeared to have 3
mysterious causes instead of 1 actionable one.

**Fix:** `(?:[A-Za-z]:)?` prefix in the pattern.

---

## BUG-013
**Assembler did not copy `.inc` files**
`tools/assemble_runtime.py` · severity medium · **FIXED**

The graft copied only `.cc` and `.h`. Xenia's Vulkan loader keeps its entry-point
tables in 14 `.inc` files under `xenia/ui/vulkan/functions/`, which it x-macros over.

**What it causes:** all 28 remaining translation units failed with
`'xenia/ui/vulkan/functions/instance_1_0.inc' file not found`.

**Fix:** `SRC_EXT = (".cc", ".h", ".inc")`.

---

## BUG-014
**`persist.sys.force_sw_gles=1` set on the device**
AYN Thor system property · severity low · **WATCH**

The Thor ships with this property set, which on the face of it forces software OpenGL
ES. The device plainly has working hardware acceleration, so it appears to be a vendor
leftover.

**What it causes:** nothing observed. The Vulkan capability probe runs correctly against
the hardware driver and reports Adreno 740 / Vulkan 1.3.128.

**Why watching:** if a GLES fallback path is ever added, this is the first thing to
check.

---

## BUG-015
**NDK `spirv-opt` has no `--canonicalize-ids`**
Android NDK shader tools · severity low · **WORKAROUND**

Xenia's own `compile_shader_spirv.py` passes `--canonicalize-ids`, which the NDK's
build of `spirv-opt` does not recognise.

**What it causes:** every shader fails with
`Unknown flag '--canonicalize-ids'` — 0 of 121 compiled on the first attempt.

**Workaround:** `tools/build_xenia_shaders.py` omits it. The flag only renumbers result
ids for reproducible output, so the generated SPIR-V is unaffected.

---

## BUG-016
**`graphics_system.cc` uses Win32 unguarded, in two places**
`xenia/gpu/graphics_system.cc:16` and `:110` · severity high · **FIXED**

Two separate unguarded Windows dependencies in a file that is otherwise portable, both
added by DAORecomp:

1. `#include "xenia/base/platform_win.h"` at the top, while the only Windows code in
   the file — the Optimus / PowerXpress `dllexport`s — is correctly wrapped in
   `#if XE_PLATFORM_WIN32`.
2. `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)` in the frame
   limiter thread, raw Win32 with no guard at all.

**What it causes:** `graphics_system.cc` does not compile for Android — and it is core
GPU infrastructure, so nothing links without it. The second site is also *masked* by
the first: fixing only the include reveals the priority call, so this looks like two
bugs in succession rather than one.

**Fix:** graft edits guarding both. The priority call is guarded rather than switched
to `xe::threading`'s portable `set_priority`, deliberately: this is a bare
`std::thread`, so `Thread::GetCurrentThread()` returns null for it and the portable
call would silently do nothing — including on Windows, where the existing code works.
A `TODO(dao)` records that Android still needs `setpriority()` or an Android-specific
equivalent.

---

## BUG-017
**`window_android.cc` passes three arguments to a four-argument function**
`xenia/ui/window_android.cc:58` · severity high · **FIXED**

`Window::OnActualSizeUpdate` takes `(width, height, WindowResizeAction cause_action,
WindowDestructionReceiver&)`. The Android implementation omits `cause_action`. Every
other platform passes `WindowResizeAction::kManual`.

**What it causes:** `window_android.cc` does not compile. The signature gained
`cause_action` at some point and the Android file was never updated, because nobody
builds that target.

**Fix:** graft edit passing `WindowResizeAction::kManual`, matching `window_win.cc` and
`window_gtk.cc`.

---

## BUG-018
**`threading_posix.cc` requires glibc robust mutexes; bionic has none**
`xenia/base/threading_posix.cc:282, :301, :378` · severity high · **FIXED**

`PosixConditionBase` reinitialises its mutex as a *robust* mutex and then handles
`EOWNERDEAD` on lock to recover from a dead owner. It uses
`pthread_mutexattr_setrobust`, `PTHREAD_MUTEX_ROBUST` and `pthread_mutex_consistent`.

Android's bionic implements **none** of that API — grepping its `pthread.h` for
"robust" returns nothing at all.

**What it causes:** `threading_posix.cc` does not compile for Android. As with
`mutex.cc`, this is core threading infrastructure, so nothing links.

**Fix:** graft edit adding inert definitions when `PTHREAD_MUTEX_ROBUST` is undefined.
This is correct rather than a papering-over: without robustness a mutex lock *cannot*
return `EOWNERDEAD`, so the recovery branches guarded by it are unreachable rather than
broken. The alternative — deleting them — would diverge further from upstream for no
behavioural gain.

---

## BUG-019
**`SetAndroidPreApi26Name` called from a const method but not const**
`xenia/base/threading_posix.cc:801, :807, :1110` · severity high · **FIXED**

`set_name(...) const` calls `SetAndroidPreApi26Name(name)`, which is non-const.

**What it causes:** `threading_posix.cc` does not compile for Android — and it only
appears *after* BUG-018 is fixed, since that one stopped the compile earlier in the
same file.

**Fix:** graft edits marking the method `const` and the buffer it writes `mutable`.
The mutex beside that buffer, `android_pre_api_26_name_mutex_`, was **already**
declared `mutable`, which says plainly that the author intended exactly this and missed
the line below it.

---

## BUG-020
**`filesystem_android.cc` redefines `SetAttributes` as a stub**
`xenia/base/filesystem_android.cc:277` · severity high · **FIXED**

`filesystem_android.cc` adds Android-specific helpers — `AndroidInitialize`,
`IsAndroidContentUri`, `OpenAndroidContentFileDescriptor` — *and* defines
`SetAttributes` as `return false;`. `filesystem_posix.cc` already defines it properly,
with `stat` and `chmod`.

Both files are required on Android: the POSIX one provides `to_path`,
`GetExecutablePath`, `GetUserFolder`, `Seek`, `TruncateStdioFile` and
`CreateEmptyFile`.

**What it causes:** a duplicate symbol, so the Android target **cannot link at all**.
And the quieter half: had the stub been the one that won, setting a file read-only
would have silently done nothing, since Android supports `chmod` on regular files
perfectly well.

**Fix:** graft edit removing the Android stub, keeping the POSIX implementation.

---

## BUG-021
**Object files named by loop index, so stale objects were linked twice**
`tools/compile_runtime.py` · severity medium · **FIXED**

Objects were written as `{index:04d}_{stem}.o`. Compiling a second time with a
different `--scope` assigned different indices to the same sources, so new objects
landed beside the old ones instead of replacing them.

**What it causes:** the object directory held 331 files for 112 sources, and the link
failed with `duplicate symbol` for every translation unit present twice — including a
duplicate `main`. The errors pointed at upstream code and looked like a graft problem.

**Fix:** derive the object name from the source's path, so re-runs overwrite in place.

---

## BUG-022
**Link-error check ignored `unable to find library`**
`tools/compile_runtime.py` / link step · severity medium · **FIXED**

The link check counted `undefined symbol` and `duplicate symbol` lines only.

**What it causes:** a link that failed with
`ld.lld: error: unable to find library -lvulkan` was reported as **0 undefined, 0
duplicates** — i.e. as a success — while no binary was produced. The underlying
mistake was passing `-static` on Android, where libvulkan exists only as a shared
library; but the tooling hid it.

**Fix:** the check greps for `unable to find` as well, and `tools/vkdevice/run.sh`
treats a non-zero compiler exit as fatal rather than inferring success from the
absence of known error strings.

---

## BUG-023
**Xenia's Vulkan instance never requests `VK_EXT_swapchain_colorspace`**
`xenia/ui/vulkan/vulkan_instance.cc` · severity high · **FIXED** (graft edit)

`VulkanInstance::Create` builds an explicit `requested_extensions` map:
`VK_KHR_surface`, the platform surface extension, `VK_KHR_get_physical_device_properties2`,
`VK_EXT_debug_utils`, `VK_KHR_portability_enumeration`. `VK_EXT_swapchain_colorspace`
is not among them, and nothing else adds it.

**What it causes:** without that extension enabled, the Vulkan specification permits
`vkGetPhysicalDeviceSurfaceFormatsKHR` to report only
`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`. So a Display-P3 panel is indistinguishable from an
sRGB one, and **decision D8's default — wide-gamut Display-P3 SDR — looks impossible on
the hardware.** Measured on the Thor before the fix: 6 surface formats, every one
`SRGB_NONLINEAR`, no P3 and no HDR colour space at all. The driver advertises the
extension (`device/vulkan-capabilities.txt`, recorded in Phase 0), so nothing was
missing but the request.

This is the worst kind of gap to leave unfixed, because the symptom is a plausible
wrong answer rather than an error. Had it not been checked against the Phase 0
capability dump, "this panel cannot do P3 under Vulkan" would have gone into the docs
as a measured fact.

**Fix:** two asserted edits in `tools/assemble_runtime.py` — declare
`ext_EXT_swapchain_colorspace` in `vulkan_instance.h` and request it inside the
`with_surface` block in `vulkan_instance.cc`. After the fix: **12 surface formats**,
including `A2B10G10R10_UNORM_PACK32` + `DISPLAY_P3_NONLINEAR`, which is exactly D8's
default. Enabling the extension only makes colour spaces *enumerable*; behaviour
changes only when a swapchain asks for one.

---

## BUG-024
**`getInsetsController()` before `setContentView` crashes on launch**
`app/src/main/java/dev/daorecompandroid/duo/GameActivity.java` · severity high · **FIXED**

`goFullscreen()` was called at the top of `onCreate`, before `setContentView`.

**What it causes:** an immediate `NullPointerException` and the activity never starts:
`PhoneWindow.getInsetsController()` reads the decor view, which does not exist until a
content view has been set, so it dereferences null inside the framework rather than
returning null. The app installed and launched to a crash dialog.

**Fix:** `goFullscreen()` moved after `setContentView`, and it now asks for
`getDecorView()` first — which forces the decor view to exist — then tolerates a null
controller rather than assuming one. Found on the device on the first launch, which is
the argument for running on hardware early.

---

## BUG-025
**Adreno offers no `HDR10_ST2084` swapchain colour space**
AYN Thor / Adreno 740 driver `0x802A4035` · severity medium · **WORKED AROUND** 2026-10-27 (session 35)

With `VK_EXT_swapchain_colorspace` enabled (see BUG-023), the Thor's display-0 surface
reports 12 formats. `VK_COLOR_SPACE_HDR10_ST2084_EXT` is **not one of them**, even
though Android's own `Display.HdrCapabilities` for the same display reports
`HDR10 HLG` with a desired maximum luminance of 420 nits.

**What it causes:** HDR10 output through a Vulkan swapchain is not available on this
device. The platform and the Vulkan driver disagree about what the display can do, and
the Vulkan side is the one that matters for a Vulkan renderer. What *is* offered for
HDR is `R16G16B16A16_SFLOAT` with `EXTENDED_SRGB_LINEAR` / `EXTENDED_SRGB_NONLINEAR`,
i.e. scRGB — a different submission model from PQ, not a drop-in substitute.

This corroborates [BUG-009](#bug-009) from an independent direction and sharpens it:
the problem is not that a PQ swapchain comes back mislabelled as sRGB, it is that **PQ
is never offered**. BUG-009 was recorded from documentation; this is measured on the
device.

**Consequence for D8:** the default (wide-gamut Display-P3 SDR, 10-bit) is confirmed
reachable and is the right default. The HDR10 opt-in cannot be built on `HDR10_ST2084`
here. If HDR is pursued it has to go via scRGB, and 420 nits peak limits what there is
to gain. Re-probe after driver updates — `tools/appbringup/run.sh` reports it.

**Worked around (session 35, PLAN 9.2, run 397).** The colour space only decides the
dataspace Android's Vulkan puts on the window's buffers as the swapchain is made. So the
HDR10 swapchain is the 10-bit sRGB one, and right after it is made its window's buffers
are tagged BT.2020 PQ directly (`ANativeWindow_setBuffersDataSpace`); the presenter's
last pass encodes PQ itself. SurfaceFlinger then lists the layer as `dataspace=BT2020_PQ`,
composed by the display hardware (`RGBA_1010102`, Device), and the Qualcomm composer's
dynamic range goes from SDR to HDR - and back when the setting is off. The scRGB route
was not needed.

---

## BUG-026
**`setBackgroundColor` on the SurfaceView hid every presented frame**
`app/src/main/java/dev/daorecompandroid/duo/GameSurfaceView.java` · severity high · **FIXED**

The constructor called `setBackgroundColor(0xFF14110F)`, meaning "show the game's dark
grey until the first frame arrives".

**What it causes:** nothing the renderer presents is ever visible. A `SurfaceView` that
is not Z-ordered on top puts its surface on a **separate compositor layer below the
window**, and punches a transparent hole in the window to let it show through. A View
background is drawn into the window layer — straight over that hole — so it covers the
surface permanently, not just until the first frame.

The reason this is worth an entry rather than a quiet fix is that **every log said it
was working.** Xenia reported `Created 1920x1080 swapchain with format 37, color space
0, presentation mode 1`, the paint connection reported 1920x1080, a frame was painted,
and there were no errors anywhere. The renderer *was* presenting correctly — into a
layer nothing could see. The only way to tell was to look at the panel.

**Fix:** no background on the SurfaceView. The activity's `windowBackground` already
covers the gap before the first frame, because it is in the window layer where a
background belongs.

**What it changed about the process:** `tools/appbringup/run.sh` now samples pixels off
the device with `tools/appbringup/sample_screen.py` and prints a verdict, so "a frame
was presented" and "something is visible" are checked as the separate claims they are.

---

## BUG-027
**`windowed_app_context.h` directs subclasses to a private overload**
`xenia/ui/windowed_app_context.h` · severity low · **WORKAROUND**

The comment on `QuitFromUIThread` says to "invoke
`ExecutePendingFunctionsFromUIThread(true)` in the destructor instead". That overload is
declared under `private:`; only the no-argument public one is reachable from a
subclass.

**What it causes:** a platform implementation that follows the comment fails to compile
with `'ExecutePendingFunctionsFromUIThread' is a private member`. Harmless once
understood, but the comment is specifically advice to implementors of the exact thing
it cannot be used for.

**Workaround:** `app/src/main/cpp/warden_app_context.cc` calls the public no-argument
overload in its destructor, which is also what Xenia's own `windowed_app_context_win.cc`
and `windowed_app_context_android.cc` do. Safe there: it may call
`NotifyUILoopOfPendingFunctions`, which is still the derived override with its pipe
intact, because the base destructor has not run yet.

Not fixed upstream-side via a graft edit, because the correct fix is either to make the
overload protected or to change the comment, and neither is needed to build.

---

## BUG-028
**`appbringup/run.sh` carries on after a failed install**
`tools/appbringup/run.sh:82` · severity medium · **OPEN**

The install step prints the first lines of `adb install`'s output and moves on without
checking whether it succeeded. The script then force-stops, launches and captures
whatever build of the app is already on the device.

**What it causes:** a report with a fresh capture timestamp, a correct APK size and
`no crashes, no native fatals` — describing a build that is not the one just compiled.
Found 2026-10-23 on the first build from a second workstation: its newly generated
debug key did not match the installed app, the install failed with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`, and the report that followed came from the
previous machine's APK. The only sign was one `Failure [...]` line in the console
output, which the report file itself does not contain.

**Fix (not applied):** require `Success` from `adb install` and exit non-zero
otherwise. The signing mismatch that exposed it is now prevented by the shared key in
[`SIGNING.md`](SIGNING.md).

---

## BUG-029
**Game data needed a folder grant, which Android refuses for `Download`**
`app/src/main/java/dev/daorecompandroid/duo/GameData.java` · severity high · **FIXED**

The app found the disc image by asking for a folder (`ACTION_OPEN_DOCUMENT_TREE`) and
scanning it. Since Android 11 the system picker will not grant the root of `Download`,
the root of internal storage or `Android/data`: the "Use this folder" button is
replaced by "To protect your privacy, choose another folder".

**What it causes:** on a fresh install, with the disc image and title update in
`Download` - where the handheld's files actually land - the app could not get at either
of them, and there was no other way in short of `adb push`ing the 7.3 GiB image into app
storage. It surfaced on 2026-10-23, when a signing-key mismatch forced a reinstall and
the picker was shown for the first time since the image moved to `Download`. How the
original install came by its grant is not recorded.

**Fix:** the user picks the files instead (`ACTION_OPEN_DOCUMENT`, several at once,
opening on `Download`), which Android does not restrict. Each file is identified by its
contents: a disc image by its XEX2 header, as before; a title update by its STFS header
(content type `000B0000`, title `454108C0`). The image keeps a persisted read grant, so
an interrupted extraction resumes without asking again; the 3.5 MB title update is
copied into app storage, where `FindTitleUpdate` already looks. A pick that misses
something asks again for what is still missing, and a cancel stops asking until the next
launch.

---

## BUG-030
**Import stubs return 0, which status-returning callers read as success**
`build/gen/guest_import_stubs.cc` (from `tools/gen_import_stubs.py`) · severity high · **FIXED**

Every unimplemented import is a weak stub that logs, counts, and leaves `r3` at zero,
on the reasoning that a stub cannot know what 0 means to its caller. For the large
share of imports that return an `NTSTATUS` - every `Nt*`, most `Rtl*`, `Ex*` and
`Xam*` - 0 is `STATUS_SUCCESS`, and none of the stubs writes its output parameters.

**What it causes:** the caller proceeds as if the call worked and uses outputs nobody
wrote. It has already caused one crash: `MmQueryStatistics` "succeeded" with an all-zero
structure, the allocator sized itself to nothing, and `operator new` returned null
(STATUS.md §4, Phase 5.1e). And it caused the second: `NtQueryDirectoryFile` "succeeds"
without writing its buffer, the title's `FindFirstFile` copies `FileNameLength` bytes of
name out of it - a length that is whatever was on the stack - and the copy smears the
stack upward through ~300 MB of guest memory into the title's own code. Confirmed by
making the code read-only and catching the first write (Phase 5.1f).

**Fix (2026-10-23):** every stub returns its import's failure value -
`STATUS_NOT_IMPLEMENTED` (`0xC0000002`) for an `NTSTATUS`, `ERROR_FUNCTION_FAILED`
(`0x65B`) for a Win32 error, `E_NOTIMPL` (`0x80004001`) for an `HRESULT`, 0 for a
pointer or value. The family is read from Xenia's kernel by the generator, not
guessed from the name; `python tools/gen_import_stubs.py --list` shows each one and the
line it came from. With it the title stopped crashing and took its own error paths
(STATUS.md §4, Phase 5.1g).

**Still true of the 11 imports Xenia does not implement** - mostly `NetDll_*` sockets -
which fall back to "Nt* returns an NTSTATUS, anything else a value". For a socket call
0 is success, so those stubs still claim to work. None is reached yet; give them
`SOCKET_ERROR` if one is.

## BUG-031
**Every attached guest thread is given the same stack**
`app/src/main/cpp/warden_guest.cc` (`AttachCurrentThreadToGuest`) · severity low · **FIXED**

*Fixed 2026-10-23 with 5.1h: every thread but the boot thread gets a stack of its own
from the guest heap (`AllocateStack`), sized as `ExCreateThread` asked, 256 KB by
default. The boot thread keeps the XEX header's stack at its fixed address.*

Each thread that attaches gets `r1 = 0x70100000`. The title's main thread does, and so
would the two GPU threads that call `xe::dao_host::AttachGuestThread`
(`command_processor.cc`, `graphics_system.cc`).

**What it causes:** nothing yet - only the main guest thread attaches in practice so
far, and no guest code runs on another thread. Once `DispatchInterrupt` delivers guest
callbacks on a GPU thread, two threads would push frames onto one stack and corrupt
each other's. Each thread needs its own stack range; the stack walk already reads the
top per thread, so it will not need changing.

## BUG-032
**The `RtlRaiseException` stub returns, so the title runs on past a raise**
`build/gen/guest_import_stubs.cc` · severity medium · **WORKAROUND**

`RtlRaiseException` is `void` in Xenia, so its stub returns nothing - and returns. The
real one never returns: it dispatches the exception to a handler and unwinds to it.
The same goes for any import that does not come back, such as `HalReturnToFirmware`.

**What it causes:** after a raise, the title carries on from the instruction after the
call, on a path its compiler never expected to be taken - a `throw` that falls through.
Nothing crashes, which makes the imports reached afterwards easy to take at face value.

*Recorded first as having happened on 2026-10-23, after `NtOpenFile` failed. It had not:
once implemented, the record said that raise was `0x406D1388` - the title naming its
thread `Eclipse::StartupThread` - which a title raises on purpose and continues from.
The conclusion came from the order of the import log, not from the record, and the
record was one line away.*

**Workaround (2026-10-23):** `RtlRaiseException` is implemented in
`warden_imports_kernel.cc`. A thread name (`0x406D1388`) is logged and returned from,
as Xenia does. Anything else - every C++ `throw` (`0xE06D7363`) included, whose thrown
type is decoded from its `ThrowInfo` - is logged with the guest call chain, and the
guest thread stops: a raise is the end of the run, and the log says why. The real fix
is dispatching to the title's handler, which needs the guest's unwind data.

## BUG-033
**Xenia's `NtOpenFile` takes five arguments; the console's takes six**
`third_party/xenia-canary/src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc:114` · severity low · **WONTFIX**

Xenia declares `NtOpenFile_entry(handle_out, desired_access, object_attributes,
io_status_block, open_options)`. The title's own call (`sub_82AB8170`, its
`FindFirstFile`) sets six: `r7 = 3`, which is share access (read and write), and
`r8 = 0x4021`, the open options (`FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
FILE_OPEN_FOR_BACKUP_INTENT`) - the same order as Windows' `NtOpenFile`.

**What it causes:** in Xenia, the share access is read as the open options. For this
call, `3` happens to include `FILE_DIRECTORY_FILE`, so it does the right thing by
accident; another share mode would not.

**Why won't fix:** it is upstream's, and this port does not run Xenia's kernel - its
`NtOpenFile` (`warden_fs.cc`) reads `r7` and `r8` as the title passes them. Recorded
because this project uses Xenia's kernel as its reference, and copying its signature
would have copied the mistake. Read the title's call site before trusting a signature.

## BUG-034
**Startup can skip mounting the packages; the first shader then has no data**
the title's startup, under this runtime · severity high · **WATCH** (2026-10-31: not seen
again in about 450 boots since 2026-10-24 - runs to 479 - since the per-line log flushes
went; watched rather than open)

Seen 2026-10-23. One build crashed on every boot in the renderer's first shader:
`sub_8269FA68` calling a null vtable slot, the shader object's data pointer (recovered
from the saved registers in the fault report) pointing at a buffer of zeros. The
shaders are `xenonreload.vsh.fxo` / `.psh.fxo` inside `packages\core\data\shaders.erf`,
and that build's file log showed **no package opened at all** before the request - the
title had walked `packages\core\data` and listed every archive, then never opened one.
The very next build, identical but for the guest function trace (one thread-local store
per guest call, so slower), opened and read twenty archives and the shader loaded.

**What it causes:** a crash that comes and goes with timing, in a function that is
only the victim.

**What is known** (2026-10-24, from four boots with the calling thread named on every
file log line): the mount is one synchronous pass on `Eclipse::Update`. Right after it
releases the Render thread's semaphore for the splash frame, Update walks
`audio\sound`, `audio\vo\en-us` (reading `corefacialanimations.erf`) and the
voice-over directory, then lists `packages\core\data` and opens every archive in
listing order, recursing into `abilities`, `movies` and `talktables`; then it probes the
PC paths (`E:\DAGame\...`, `WRITE_PATCH:\`, `UPDATE:\`) and only then starts releasing
FileIO's semaphore. Nothing is posted to another thread and no event gates it, so
"skipped" would mean the Update thread itself took a different branch after listing -
which nothing in its inputs explains.

The likelier reading is that the mount was **outrun, not skipped**. Every logged guest
call used to end in `xe::FlushLog()`, which sleeps 10 ms, so the listing alone took
~2 s in a build that logged each entry (0.07 s now, without the per-line flush), while
the renderer's thread was paced by how many stubs *it* logged. A shader requested from
another thread during that window finds no `shaders.erf` mounted and gets zeros - the
exact crash. The crashing build's report did not name its thread; the "no package
opened" log is equally what a process that died mid-listing leaves behind. The `Ke*`
waits on the title's own events were adopted (and so working) in both builds.

**How to catch it:** the fault report now opens with the faulting guest thread's name
and prints the last 512 guest functions that thread entered (`WARDEN_GUEST_TRACE`). If
the thread is not `Eclipse::Update`, this is the race above, and the fix is on the
title's side of the timing: find what the renderer waits on before its first shader
(an event Update sets after the mount, most likely) and why it did not wait. If it is
Update, read its path between the `packages\core\data` listing and `sub_826950D0`; the
mount is the step missing from it.

---

## BUG-035
**The network thread spun through two Winsock stubs at five million calls a second**
our runtime · severity medium · **FIXED** 2026-10-24

The title's network worker (`sub_826E8368`, started last in startup) gathers its
sockets' events behind a wake-up event and calls `WSAWaitForMultipleEvents` on them
with no timeout. The stub returned 0 - `WSA_WAIT_EVENT_0`, "the first event fired" - so
the thread reset the event and waited again, forever, at 24 million guest calls per
five seconds on a performance core. The `guest threads` report (warden_trace.cc) is
what showed it; logcat's per-thread CPU time would have too.

**Fix:** Winsock events are real dispatcher events (`warden_threads.cc`), so the wait
blocks; the generated stubs' `net` family returns `SOCKET_ERROR` for every socket call
rather than 0; `warden_net.cc` answers the XNet and startup queries as a console with no
cable. The thread now parks in its wait. **No online functionality is wanted in this
app**, so "no cable" is the design, not a stopgap.

---

## BUG-036
**After startup the Update thread spins in `Sleep(0)` on a flag nothing sets**
our runtime · severity high · **FIXED**

Seen 2026-10-24, once every thread was accounted for. With the packages mounted, the
movies opened and the sign-in state queried, `Eclipse::Update` enters `sub_826CF7D0`
(a TU6 function; the return-address chain under `KeDelayExecutionThread` names it),
builds its sign-in screen (`LoginButton`, `ReConnect`, `OnlineFeatures` - UTF-16 GUI
element names - go through `sub_82711C70` just before), then loops
`while (byte at 0x832127E5 == 0) Sleep(0)` at a million guest calls a second. The Render
thread waits for the next frame, which Update never posts; everything else is parked.

**What is known:** the image holds 1 in that byte; `sub_826CB4A8` (a constructor called
from the same function) clears it. No recompiled function stores to it with an immediate
offset, so the setter goes through a pointer. The D3D worker (`guest-101C`, start
`sub_82DA6600`) times out every 30 ms on an event nothing signals, and the graphics
interrupt callback the title registered (`VdSetGraphicsInterruptCallback`, `sub_82DA12C8`)
has never been called: this runtime raises no vblank and no swap interrupt. On the
console the GPU raises both, the callback (source 1 = swap done, source 0 = vblank)
clears pending-swap bits and signals the D3D worker, and presentation proceeds. The
splash `VdSwap` is the one frame the title ever presented here, and it never completed.

**The suspect, in order:** a vblank at 60 Hz and a swap-complete on each `VdSwap`,
delivered to the callback from a guest-attached host thread as Xenia's vsync worker
does (`GraphicsSystem::MarkVblank` → `DispatchInterruptCallback(0, 2)`; the command
processor's swap → `DispatchInterruptCallback(1, cpu)`). If the flag is not set by that
path, the next candidates are a notification through `XamNotifyCreateListener` (a stub,
returns 0) and the sign-in UI's completion.

**2026-10-24, later:** the interrupt is delivered (`warden_video.cc`: a `gpu-interrupt`
guest thread at 60 Hz, swap-complete before vblank, the GPU register window preset
with Xenia's answers so the callback sees the vblank bit; the spinlock imports are
real). The callback ran, the title's one `VdSwap` was reported complete - and nothing
changed: the D3D worker still times out every 30 ms, Update still spins. So the flag
is not set from the interrupt path alone. The trace could not say what the loop does
because it was never compiled in (BUG-037); with that fixed, the next boot's thread
report shows the loop body's functions, and reading them is the next step. Note the
loop sits right after the title's sign-in screen is built: a wait for the player
(a "press start" or sign-in prompt, through `XamInputGetState`, still a stub) is now
as likely as a wait for the GPU.

**Resolved in effect, 2026-10-24, later:** with the real command processor executing
the ring and writing the read pointer back (`warden_gpu.cc`), the Render thread gets
past its fence wait, frames flow, and the Update thread leaves this spin for game
code. The flag was set by the frame pipeline after all. What happens next is BUG-038.
Left open until a boot shows the title past the first frames without this spin.

**Closed 2026-10-25:** every boot since session 30 has gone past it - the title screen,
the main menu, character creation, the Fade and Ostagar - and the spin has not been
seen again.

---

## BUG-037
**The guest function trace was never compiled into the title**
our build · severity high · **FIXED** 2026-10-24

`WARDEN_GUEST_TRACE` (2026-10-23) put a shadow `ppc_context.h` first on the guest
objects' include path, redefining `PPC_FUNC_PROLOGUE` to record `__func__`. The
generated sources include that header with quotes, and the recompile step leaves a copy
of it beside them - which a quoted include finds before any `-I` directory. The shadow
was never read; `llvm-nm` on a guest object showed no reference to `warden_guest_trace`.
Every trace printed since held only the kernel imports that record themselves, which is
why a thread "parked" in a wait showed nothing but the wait, and why the Update thread's
spin showed `KeDelayExecutionThread*128` with no function around it.

**What it caused:** a diagnostic believed to exist for a day, relied on in BUG-034's
plan, and the "first 512 functions" claims in the docs, none of it real.

**Fix:** the prologue header is force-included (`-include`) into every guest source. It
includes the generated folder's own `ppc_config.h` and `ppc_context.h` (so the sources'
later include of them is a no-op under `#pragma once`) and redefines the macro after.
Verified by `llvm-nm` on a guest object listing `U warden_guest_trace`. A full guest
recompile.

**Lesson:** a diagnostic is not in place until it has shown something it could only
know by working. The function names in a trace were that test, and nobody looked for
them.

---

## BUG-038
**With the GPU up, the Render thread presents 50,000 frames a second without kicking the ring, then calls a null pointer**
our runtime · severity high · **FIXED** 2026-10-24 (session 28)

**Three causes, found in order, each hiding the next:**

1. **The null call was an unpatched import variable.** The 9-instruction thunk
   `sub_831E2328` reads `[[0x820007FC]]` and calls `+24` off it. `0x820007FC` is the
   import record for `KeDebugMonitorData`; the console's loader overwrites each
   variable record with the variable's address, and nothing here did, so it still held
   its raw ordinal word `0x00010059` and the title followed that into low memory. All
   twelve variable imports (`ExThreadObjectType`, `KeDebugMonitorData`,
   `KeTimeStampBundle`, `XboxHardwareInfo`, `XboxKrnlVersion`,
   `XexExecutableModuleHandle`, `ExLoadedCommandLine`, `KeCertMonitorData`, the four
   `Vd*` globals) are now given homes with Xenia's values and their records patched
   (`warden_kernel_variables.cc`, PLAN 5.1d). `KeTimeStampBundle` is kept current by a
   thread at 1 kHz - the title's tick count.
2. **The present storm was `VdIsHSIOTrainingSucceeded` answering 0.** That stub told
   Direct3D the GPU link had failed to train; from the second frame on, its
   submission path (`sub_82DA1BA0`) skipped the ring kick for a recovery hook it never
   had, so the read pointer never moved and `Present` never throttled. Xenia and
   DAORecomp both answer 1. With it, the ring is kicked ~600 times in 5 s.
3. **Then the Update thread died with `SEGV_ACCERR` filling freshly allocated
   memory.** The fault was a write-watch fault the handler declined to serve: it
   took the memory-state mutex with `try_lock`, and the GPU thread held it while
   protecting a range for the texture cache - the normal case, not an error. The
   handler now waits for another thread's hold and refuses only its own
   (`warden_memory.cc`, `StateLock`). ~50,000 watched writes are served per 5 s.

**Result:** the title runs at 60 frames a second with ~52 draws a frame, one resolve
into the front buffer and a swap, presents through Xenia's Vulkan presenter, loads the
single-player module (`modules\single player\...`) about 30 s in, and does not crash.
The frames are black (BUG-039).

What follows is the investigation as it was written while open.

Seen 2026-10-24, the first boots with Xenia's Vulkan graphics system driving the
title's Direct3D (`warden_gpu.cc`). What goes right first: `VdInitializeRingBuffer`
(ring at physical `10002000`, 32 KB), `VdEnableRingBufferRPtrWriteBack` (`1EA846DC`),
four write-pointer kicks (`0x16`, `0x19`, `0x1C`, `0x1F`), the command processor
executes them, runs the title's `INTERRUPT` packet (its swap handler
`sub_82D9E0F8` on the `GPU Commands` thread), writes the read pointer back (once the
interrupt callback could clear the right per-core bit, see the PCR note in
`RESUMING.md`), and the title's main loop starts: the Update thread leaves its sign-in
spin (BUG-036's) for game code, the Render thread presents.

**Then:** `VdSwap` is called ~50,000 times a second (283,709 swap packets in 5 s; the
D3D device's present counter `+16560` matches, its flip counter `+16568` stays at 1,
the swap queue `+16700/+16704` stays 0/0), the ring is never kicked again (still four
kicks, write pointer `0x1F`), and after ~5 s the Render thread calls a null function
pointer: `sub_82DABE98` from `sub_82DAC178`, the device at `BEA99880`, r3 = 0x44.
Before the per-thread TLS existed the Update thread died first, in `sub_82679C28`,
calling a method off a TLS pointer; that one is fixed.

**Where to look:** `sub_82D9E780` (Present) waits while `+16560 - +16568 >= 15` using
`sub_82D8E218` as its step; with the counters above the wait should hold and does not
- so either the step returns 0 (its "GPU hung" path, `sub_82DA3F28`, with a 5000 ms
timeout read off `[kthread + 88]`, which is now a real KTHREAD and zero) or the
comparison is not what it looks like. The kick is `sub_82DA1BA0`'s fenced store, the
only MMIO write-pointer store in the title; find why it is not reached after the first
frame. The null call: read `sub_82DABE98` for where r11 came from - a device method
slot, a resource, an unpatched import variable (PLAN 5.1d) are the candidates. The
`GPU:` status line every 5 s and `GPU: WAIT_REG_MEM stalled` are the instruments.

---

## BUG-039
**The title renders and presents at 60 Hz, and every frame is black**
our runtime / renderer · severity high · **FIXED** (2026-10-24, session 29)

**Two causes, both Adreno 740 driver behaviour on valid-looking input, both found
by instrumenting Xenia's Vulkan backend frame by frame:**

1. **The shader build targeted Vulkan 1.3** (`tools/build_xenia_shaders.py`), so
   glslang declared every precompiled compute shader's workgroup size as SPIR-V 1.6's
   `OpExecutionModeId LocalSizeId`, which this driver ignores. The resolve copy wrote
   one pixel row per workgroup row and texture loads were partial. Now Vulkan 1.1.
2. **The translated pixel shaders declared `gl_SampleMask` without writing it.**
   Xenia's SPIR-V translator declares the output for alpha to coverage but writes it
   only on the alpha-test path, which a shader with early fragment tests skips. An
   unwritten sample mask output is undefined coverage: desktop drivers keep the
   primitive's coverage, this driver discards every fragment, so no draw with a pixel
   shader ever reached a render target. The translator now writes full coverage at
   the start of every fragment shader that declares it (`spirv_shader_translator.cc`,
   an asserted edit in `tools/assemble_runtime.py`). The Khronos validation layer,
   run on the device, reported nothing: both are valid Vulkan that the driver answers
   differently from desktop ones.

**First light:** the main menu's key art renders and is presented (`guest-output-4.png`,
921,600 of 921,600 pixels lit), with a colour cast that is BUG-040.

**The record of the investigation, kept because its instruments remain:**

Seen 2026-10-24 once BUG-038 was fixed. Every instrument says frames flow: the
command processor executes ~52 draws per swap, 51 issued to the host GPU, none failed
(`GPU:` status line, from DAORecomp's per-swap counters now fed by the Vulkan backend);
each frame ends with one resolve of `RT0 k_8_8_8_8` into the front buffer
(`FT resolve wrote 1000E000+398000`) and a swap; the presenter refreshes its guest
output at 60 Hz in paint mode 2 (painting from the guest output thread) with the
surface connection paintable. But the presenter's own guest output, captured to PNG
(`files/guest-output-N.png`), has 0 of 921,600 pixels lit, and the panel is black.

**The traced frame** (`DAO_GPU_FRAME_TRACE=600`, the `FT` lines): 24 single-point
draws with no pixel shader and no colour target, a depth-only rectangle, then one
textured quad into `RT0` sampling a 1280x720 `k_8` (8-bit, one channel) texture at
`1E8C8000`, a depth-only rectangle, 24 more points, the resolve and the swap. One
channel, 8 bits, full screen: that is a Bink movie's luma plane - the intro movies
(`dragon_age_ea_logo.bik`, `edge_logo_withaudio.bik`) are opened at this point.

**Suspects, in order:** the movie planes are zero because the movie is not being
decoded (no `.bik` read appears in the log, though the read log's budget may simply
be spent); the movie waits on the audio clock, and `XAudioRegisterRenderDriverClient`
fails, so FMOD and Bink have no audio callback; or the Vulkan backend's draw or
resolve produces black on Adreno. The `GPU: guest output capture` lines and PNGs, the
`FT` trace and the per-swap draw counts are the instruments.

**Progress (2026-10-24, session 29).** The first two suspects are cleared, and the
black is placed:

- **The movies load.** Each `.bik` is read whole after its 56-byte header and one
  more header read: `dragon_age_ea_logo.bik` 23.5 MB, `edge_logo_withaudio.bik`
  3.5 MB, `dragon_age_main.bik` 15.6 MB, one `NtReadFile` each. The per-file read
  log had shown only the first four reads of a file; it now shows eight.
- **The audio clock runs.** `XAudioRegisterRenderDriverClient` is implemented
  (`warden_audio.cc`): the title's callback is called every 256 samples at 48 kHz and
  it submits frames with real content (peak 0.49). It had unregistered its client
  43 ms after registering because `XMACreateContext` was a stub; the XMA context pool
  (`warden_xma.cc`, DAORecomp's silent decoder) keeps it registered - 65 contexts
  allocated, decode passes running.
- **The planes hold data.** `files/probe.txt` (see `TESTING.md`) summarises guest
  physical ranges every 5 s: the three Bink planes (`1E75D000`, `1E83F000`, `1E884000`)
  have byte means of 137-141 with under 0.3 % zero bytes.
- **The shader and its constants are right.** The traced draw's pixel shader
  (microcode now in the `FT` trace) is Bink's YUV->RGB: three `tfetch2D` and the BT.601
  coefficients in c253-c255 (1.164, 1.596, -0.391, -0.813, 2.018, -0.871); alpha is
  `max(c0.x)` = 1.
- **The resolve writes opaque black.** With resolve readback on
  (`SetReadbackResolveMode("full")` in `warden_gpu.cc`, commented out when not needed
  - it drops the frame rate to ~10 Hz) the front buffer at `1000E000`, read back from
  the GPU to guest memory and dumped (`files/front-buffer-2.raw`), holds exactly one
  non-zero value, `000000FF`, in 115,200 pixels (the first 90 rows' worth) and zeros
  elsewhere. Alpha 1 says the quad rasterised and the shader ran; RGB 0 says the
  texture fetches returned zero. Why only 90 rows is open (the resolve wrote
  `0x398000` bytes).
- **It is not the movie.** In a frame before the movies (swap 200) the same
  full-screen draw samples a 1280x720 DXT1 texture (`fmt 18`) and a 32-bit 640x360
  one, and the output is black the same way. Re-encoding the movies would change
  nothing: Bink's decoder is compiled into the title and runs as ordinary guest code.

**Where that leaves it:** the data is in guest memory and the GPU samples zeros, so
the suspect is the path between them - Xenia's shared memory and texture cache over
our `xe::Memory`: whether a texture upload ever reads the planes' bytes, whether it
re-uploads when Bink writes a new frame (the write watches must fire on the view the
title writes through), or the texture load shaders on this device. Next: in the traced
frame, log what each texture upload reads (address, length, a checksum), and count the
write-watch faults landing in the probed ranges between probe ticks.

`skip_intro_logos` (DAORecomp's option) is implemented: a file of that name in the
app's `files/` hides the three logo movies and the title goes straight to
`dragon_age_main.bik` behind the menu - drawn the same way, so the menu is black too.

**Progress (2026-10-24, session 29, later).** One cause found and fixed, and the black
placed exactly:

- **FIXED: every compute shader ran with the wrong workgroup size.** The shader build
  (`tools/build_xenia_shaders.py`) targeted Vulkan 1.3, so glslang emitted SPIR-V 1.6
  with the workgroup size as `OpExecutionModeId LocalSizeId`. The Adreno 740 driver
  (Qualcomm build of 2023-12-27, Vulkan 1.3.128) ignores that form: the 1280x720
  resolve copy, dispatched as 20x90 groups of 64x8 pixels, wrote one pixel row per
  group row - the 1280x90 band - and the texture load shaders converted fragments. The
  build now targets Vulkan 1.1 (SPIR-V 1.3, plain `LocalSize`); 121 shaders
  recompiled; the resolve now covers the whole frame.
- **The draw's fragments never reach the render target image.** With the resolve
  fixed, the front buffer is uniformly `000000FF`. A chain of experiments placed it:
  the pixel shader's float constants reach the GPU correctly (logged at upload);
  forcing every pixel shader's output to opaque red changed nothing; forcing every
  vertex shader to emit a clip-space-covering strip changed the result only through the
  title's depth-rectangle "clears"; reading the colour render target image back
  directly (`DAO_HACK_RT_READBACK`, the `files/hack-rt-readback` marker) shows it
  holds only what the EDRAM-to-image transfer loaded - the clear colour - never the
  quad. The host-side state of the quad draw is all ordinary and valid: the pass is
  begun on the right framebuffer (1280x2048 colour image, UNORM view), the pipeline
  has triangle-strip topology, no culling, no blend, write mask F, no depth or stencil,
  the viewport is Xenia's 8192x8192 with the guest's pixel coordinates mapped through
  `ndc_scale`, the scissor is 1280x720, the draw is `vkCmdDraw(4)`, the vertex buffer
  holds the four screen corners, the vertex shader passes them through. Sparse binding
  of the shared memory buffer is not the cause (tried off: `files/hack-no-sparse`).
- **What the title does with EDRAM** (the shape of the frame): a 4x MSAA depth target
  aliased over the colour buffer's EDRAM tiles is drawn as a rectangle to clear the
  colour buffer (the console's fast clear); Xenia transfers the tiles between the depth
  image and the colour image with its transfer draws, which work (the clear colour,
  and zero under the vertex-shader experiment, arrive in the colour image).

**Where that leaves it:** a plain Vulkan draw that is valid by inspection produces no
fragments on this driver. The next instrument is the Khronos validation layer on the
device: `libVkLayer_khronos_validation.so` from the Vulkan-ValidationLayers Android
binaries, placed where the loader finds it for a debuggable app, enabled with the
`enable_gpu_debug_layers` / `gpu_debug_app` / `gpu_debug_layers` global settings. It
needs a download and an install the automated session could not perform. Candidates
it would confirm or rule out: a render pass / framebuffer / pipeline incompatibility
the driver answers by dropping the draw; the 8192-pixel viewport; depth clamp with no
depth attachment; image layout at draw time. Failing that, the same draw could be
reproduced in a small standalone Vulkan test on the device.

---

## BUG-040
**The EA logo movie plays yellow-green: the Bink shader is given the key art as its luma plane**
our runtime (title update) · severity low · **FIXED**

**Fixed by BUG-046 (2026-10-25).** With the title update applied correctly the EA logo
plays as it should - the red blood-splatter dragon on white (run 131). The wrong texture
binding below was a consequence of the corrupted read-only data, not a renderer
behaviour. The record as it was:

Seen 2026-10-24 the moment BUG-039 was fixed, and read at first as a colour cast on
the menu: in the presenter's capture the blue channel was 0 at every sampled pixel
while red and green carried a recognisable image. Running further showed what it is.
The frame is the EA logo phase of the intro: the title draws it through its Bink
YUV-to-RGB pixel shader, and the traced frame has the shader's `tf0` (the Y plane)
bound to the 1280x720 DXT1 **key art** texture with the U and V fetches bound to the
null image - so the key art's luminance goes through the chroma-less YUV matrix and
comes out green-yellow. The Edge logo movie that follows plays normally, and the title
screen after sign-in renders the key art in its true colours (run 89's capture: the
red dragon). Not a renderer colour bug: the renderer draws exactly what the fetch
constants say. Why the title binds the key art there - a Bink texture never created
because the movie's audio or XMA side answered oddly, or the EA logo's "key art plus
glow" being how this phase is meant to look while the movie decodes - is not known
and is not hurting anything; `files/skip_intro_logos` skips the phase.

---

## BUG-041
**The guest thread report crashed on a bad function-name pointer**
our runtime / diagnostics · severity low · **FIXED**

Seen once, 2026-10-24, 40 s into the first lit run: `SIGSEGV` in `StripPrefix` called
from `ReportLoop` (`warden_trace.cc`), fault address `0x50000000000` - a ring entry
that was not a string pointer. The rings were thread-local objects: a guest thread
that exits takes its ring with it while the report still holds the pointer it
registered. **Fix:** each ring is heap-allocated by its holder and never freed
(`Ring* ring = new Ring;`), so it outlives its thread; the holder records the pthread
for the sampler.

---

## BUG-042
**Intermittently the title stops kicking the ring buffer and the swaps freeze**
title / our runtime (GPU) · severity medium · **WATCH**

Seen twice on 2026-10-24 before first light (runs 86-88): the swap count froze (at
1976 in one run) with every thread alive and the Render thread still running its
loop. The title's own ring write cursor in its Direct3D device (`+11012`) was ahead of
the last write pointer the hook had kicked (`0xF8B` against `0xEED`), yet no store to
the write-pointer register had been seen - as if a store had bypassed the hook, or
the title had stopped storing. **Safety net:** `WritePointerPoller` (`warden_gpu.cc`)
reads the big-endian write-pointer word in the register window at 1 kHz and kicks the
command processor when it changes without a hook, counting `unhooked kicks` in the
`GPU:` status line. It has counted none in the runs since, and the stall has not
recurred; kept as WATCH until it does or the mechanism is understood.

---

## BUG-043
**After sign-in the Update thread waits forever for a FMOD non-blocking load slot: the request pool was never allocated**
our runtime (physical memory) · severity high · **FIXED**

Seen 2026-10-24 in every run past first light (runs 89-92). About a second after the
title screen appears with a signed-in user, the swaps stop (63 to 78 in a run) and
nothing moves again. The sampler (`files/sample-threads` = `Update`) places
`Eclipse::Update` in `sub_82ACFFE8` at `82AD00E8`: unlock, `Sleep(10)`, lock, look
again - FMOD's enqueue of a non-blocking load request, waiting for a free request
node. `DragonAge::Render` waits on the frame semaphore (`F8000028`) that Update
releases each frame, so the screen freezes with it.

The mechanism, from the recompiled FMOD (TU7 image addresses):

- `sub_82AD0A90` initialises the non-blocking loader once (flag `0x83255A24`): two
  locks, a worker callback (`sub_82AD2050`, registered on a FMOD worker thread by
  `sub_82B04728`; `sub_82B04620` creates that thread with a semaphore - thread 1040,
  `F8000154`, in these runs), then a pool of request nodes: the count `N` from the
  title's FMOD advanced settings (`eventqueuesize`, struct offset +52; the image's
  default at `0x8321E0C8` is 32), `N x 44` bytes from FMOD's allocator
  (`sub_82AF26B0` -> `sub_82AF22A8`), linked into the free list at `0x8321E0F8`.
- `sub_82ACFFE8` takes a node from the free list, fills it and moves it to the pending
  list at `0x8321E0CC`; its callers (`sub_82AD1148`, `sub_82AD1488`, `sub_82AD1C18`)
  then signal the worker (`sub_82B04158` -> `NtReleaseSemaphore`), whose callback
  processes the pending list and returns the nodes.
- The memory probe (`files/probe.txt`: `8321E0C8 40` and `83255A08 1C`) at the stall
  shows `N = 0x2000` (8192 - the title's own setting), the pool pointer at
  `0x83255A0C` **zero**, and both lists empty. The 360,448-byte allocation failed:
  `sub_82AD0A90` returned 43 (`FMOD_ERR_MEMORY`), but it had already set its once-flag,
  so every later call succeeds with no nodes, and the first enqueue waits for a node
  that will never exist. No signal was ever sent - the sync report has no `release` of
  `F8000154` or `F8000100` - which is how the first enqueue was known to be the one
  that blocked. (The stack walker's "saved r30 = 8321E0F8 -> [8321E0F8 8321E0F8 ...]"
  in the sample showed the empty free list before the probe confirmed it.)

**Why FMOD's allocation fails is the open question.** `sub_82AF22A8` takes memory from
a user callback (+312 of the FMOD system object) if one is set, else from FMOD's own
fixed pool (+8 set; `sub_82AF2038` finds a block) or the default heap (`sub_82B0FEA0`);
on failure it calls FMOD's debug callback (+32 of the global at `0x8321E18C`) with a
formatted message and returns 0. During FMOD's initialisation the title allocates one
1,674,990-byte block (`NtAllocateVirtualMemory`, right after the three XMA contexts),
which looks like the FMOD pool; a 360 KB request failing against it means the pool is
nearly full or was sized from something we answer. Suspects, in order:

1. The pool's size: does the title derive it from `MmQueryStatistics`' available
   pages? We report what the physical heap has left after the title's own 214 MB and
   18 MB heaps (5,375 pages in run 92); a console would report more.
2. What consumed it: the three `glo_fly_*_pre.fsb` banks (237 KB each) loaded whole,
   sample data decoded to PCM because something about our XMA answers made FMOD choose
   software decoding, or the 320-context XMA pool itself.
3. The FMOD system object at `0x83255A08 -> BDB92D00`: +4 pool base, +8 pool flag,
   +292 peak and +296 current usage, +312 user allocator - probe it at the stall
   (`BDB92D00 140`), and log `sub_82AF22A8`'s failure path (size, caller's file and
   line in `r5`/`r6`) from a `PPC_FUNC` override or a trace hook, to see which
   allocation failed first and how much the pool holds.

A runtime that answered `FMOD_ERR_MEMORY` honestly would not have saved the title here:
the once-flag is FMOD's, and the title ignores the error. The fix is upstream of the
allocation - give FMOD the memory it had on the console.

**Cause and fix (2026-10-25).** A hook on FMOD's allocator (`warden_fmod.cc`, a strong
`tu6_sub_82AF22A8` over the recompiled weak one) showed FMOD running on a *user
allocator*, `sub_8254B3E8` - the title's own engine heap, heap id 0 - and the first
failures were 32 KB file buffers after 3,396 allocations totalling 2.08 MB, then a
292-byte request, then the pool. The heap behind them was the title's **last** heap,
which it sizes from `MmQueryStatistics`: it allocates its big heaps first
(`0xD600000`, `0x1200000`, `0x200000` bytes), asks what is left, and takes that less
6 MB. Our physical allocator handed out only the top half of the console's 512 MB
(`0x10000000..0x1FF00000`, "clear of whatever the title maps low"), so after 250 MB
it reported 5,375 pages - 21 MB - and the title's last heap was 15 MB, shared with FMOD
and everything else at the title screen. Nothing maps physical memory low (the GPU is
only ever given addresses from this allocator), so `kPhysicalBase` is now `0x00100000`
(`warden_guest_heap.cc`; page 0 stays unserved so an address of 0 is caught). The
title then sees 70,655 pages, makes a 270 MB heap (`0x10DFF000`), FMOD never fails, and
the title screen, the storage prompt and the menu follow. The allocator hook stays:
`FMOD memory:` lines describe the memory system on first use, every failure with
FMOD's source file and line, and allocations of 32 KB or more.

---

## BUG-044
**Two threads writing one GPU-watched page at once: the second fault is "not watched" and kills the process**
our runtime (write watch) · severity high · **FIXED**

Seen 2026-10-25 (run 98) the moment the title started loading the character stage
after "New Game": `SIGSEGV` `SEGV_ACCERR` in recompiled code (`sub_825A2598`, on
`Eclipse::WorkerThread0`) at guest `A14B8080` - a write to physical page `014B8000`,
which the GPU's write watch had made read-only. The fault handler
(`Memory::HandleAccessViolation`) took the state lock, found the page *not* watched,
treated that as a real access violation and chained to the default handler. It was not
real: a second thread had faulted on the same page a moment earlier and unprotected
it, which is exactly what the first thread's pending store needed. The race existed
from the start, but the fix for BUG-043 put the title's 250 MB of heaps in the half of
physical memory the GPU reads from, so watched pages are now everywhere the title
writes, and with 370,000 watched writes served in a run the window was hit within
seconds. (`warden_memory_host_write`, the file reader's unwatch, opens the same window
from the host side.)

**Fix:** a fault on a page in the physical range that is not watched is retried - the
page's protection is set to read-write again in case it is stale, a per-page retry
count (cleared when the page is watched again) catches a page that keeps faulting
while unwatched (over 16 retries: the real violation it then is), and the retries are
counted in the `GPU:` status line (`watched writes served`, `races retried`).

---

## BUG-045
**In the game world the HUD renders but the 3D scene behind it is black, with ~580 draws a frame issued and none failing**
Adreno driver (with Xenia's SPIR-V float controls) · severity high · **FIXED**

**Fixed (2026-10-25, runs 135-167).** Seen again in character creation: the menu, the
backdrop's clouds and floor, and no character. The cause is the Adreno 740 driver: a
fragment shader whose SPIR-V declares the `DenormFlushToZero 32` execution mode reads
`gl_FragCoord` as zero. Xenia's translator adds that mode, with
`SignedZeroInfNanPreserve` and `RoundingModeRTE`, to every shader when the device
reports support. In this title's frame one pass depends on the pixel position: after
the two predicated tiles are resolved, the title redraws the scene into EDRAM for its
post effects with a full-screen rectangle whose pixel shader (`A91D0274EC946F8C`, five
instructions) fetches the resolved colour (`tf0`, `00DC3000`) and depth (`tf1`,
`0051D000`) unnormalized at its own position, taken from PsParamGen. With the position
zero every pixel took texel (0,0): the scene became one flat dark colour, and what
showed was the fog and cloud overlays the post chain draws afterwards. **Fix:** the
translator does not declare `DenormFlushToZero` on Qualcomm GPUs (vendor `0x5143`) -
`spirv_shader_translator.cc`, recorded in `tools/assemble_runtime.py`; the driver's
default denormal handling applies, as on a device without the feature. Verified in run
167 with no experiment on: the character, the wall and the pillars render, lit and
textured.

How it was found, in order (the instruments are in `docs/TESTING.md`):
- The character's three meshes are drawn - a shadow-map depth pass and a colour pass
  each, in both tiles - and their state is sound. Experiments that changed nothing on
  screen ruled out tiling and MSAA (`scene-single-tile`, `hack-msaa-2x-as-4x`), depth
  and culling (`hack-no-depth-test`, `hack-depth-always`, `hack-no-cull`), a0-relative
  constants (`hack-no-a0`) and the NaN-aware min/max (`hack-no-nmin`); the stencil
  test is always-pass (the character writes 0x0F) and the literal constants are right.
- `hack-vs-probe` read the vertex shaders' positions back from the GPU: they match the
  CPU interpreter's and put the meshes where a standing character belongs (head
  x 357-427, y 37-154; body x 291-524, y 129-698).
- `hack-skip-vs` on every draw after the character showed no more of it: the character
  never reached the final image.
- `hack-resolve-dump` showed the character fully drawn in the first tile's resolve, and
  the next resolve of the same texture - the restore pass's output - flat.
- `hack-ps-paramgen` made the restore pass show its own position: zero everywhere; the
  raw `gl_FragCoord` too, and in a plain triangle-strip pass as well, while a constant
  colour came through.
- `hack-no-float-controls`: with all three modes dropped the positions and the
  character were right; with round-to-nearest-even dropped alone, still black; with the
  denormal flush dropped alone, fixed.

The `arena` world recorded below was very likely the same failure (the same restore
texture, sampled once a frame). **Run 168 (played by hand):** the first real area -
the Mage origin's Fade - renders with its HUD, about 420 draws a frame and none
failing.

**Re-read after BUG-046 (2026-10-25).** The world seen here was not the start of the
game: with the read-only data corrupted, the single-player module's script name read
as "rn_troop_e", its load and start events went to no script, character creation and
the intro were never started, and the engine fell back to `default_start_area` (the
`arena` test environment) with a default player. With the update applied correctly
"New Game" plays the intro cutscene as on the console. Whether the black 3D scene
happens in a real area is open until the game gets to one - the corruption also hit
about 54 KB of other read-only data, which may have included what this frame needed.
The record as it was:

Seen 2026-10-25 (run 99), the first time the title got past "New Game": it loads the
default start area (`al_default_start_area.rim`, the `arena` environment) and its
HUD comes up - mini-map, quick bar with X/Y/B glyphs, a health bar, a character named
Jaden - over a scene that is black-brown everywhere (mean pixel value 19, 900,000 of
921,600 pixels lit but dark). The `GPU:` line says 584 draws submitted and 569 issued
per frame with 0 failed and 0 predicated away, the frame rate is 23 Hz, and there are
no shader-translation or Vulkan errors. So the scene is drawn and comes out black: a
lighting or post-processing pass producing nothing, a render target whose contents
never reach the final composite, or another Adreno 740 behaviour on valid Vulkan of
the kind BUG-039 was (two of those, both in valid shaders). Different from BUG-039 in
that the UI's own draws are fine. First steps: trace a frame of this phase
(`files/trace-swap` = a swap number past the load, ~3900 in run 99) and read the
colour render target back (`files/hack-rt-readback`) to see whether the 3D draws
write anything but black; compare against DAORecomp's D3D12 path for the HDR scene
target format and the EDRAM transfers between MSAA depth and colour targets. Also
open: whether this is the character creator at all - the HUD suggests the title went
straight into the world; `inject-pad` held A for a second on "New Game".

**Run 100 (same day), trace at swap 3900 with the readback:** the first colour render
target dumped in that frame (`RT base 1024`, `k_8_8_8_8`, 1280x720) read back as a
uniform `1E 11 0A FF` - the dark brown of the screen - at every sampled pixel: the
draws into it leave the clear colour. Then the readback hack itself asserted
(`SIGTRAP` in `VulkanCommandProcessor::SubmitBarriers` from
`DumpRenderTargets`, a barrier recorded where the dump does not allow one) and the
process died, so the trace has only its first 213 draws: 143 of one pixel shader
(`3E9A8E47DD3E00D5`, triangle lists, no blending) in a render pass with three colour
attachments at MSAA 1 - the shape of a G-buffer pass - and two 1024x1024 `fmt 20`
textures. The readback is not safe in a frame with EDRAM transfers; trace without it
next and read the whole frame, then decide where to look.

**Run 101, the whole frame (246 draws, 14 resolves), traced without the readback:**
152 draws in a three-attachment MSAA-1 render pass and 68 in a three-attachment
non-MSAA one - geometry (`prim 4`, 1,000-2,000 vertices, depth write on, no blending,
`RT0 k_8_8_8_8 base 1024`) with textures bound on `tf0`-`tf3`, `tf7` and a volume
texture on `tf10`, drawn with the scissor at 1280x512 (the title's predicated tiling,
handled by the host render target path); then RT0 resolved to `@00DC3000`,
`@01043000`, `@0115B000`, `@01CE2000` (a 640x360 half-resolution buffer, sampled back
once) and at last to the front buffer `@0010E000`. Nothing fails, nothing is missing
in form. What is odd: the resolved buffers at `00DC3000` and `0115B000` are sampled
back only once each and `01043000` never, for a frame whose final image should be
composed from them. Where to look next: the pass that samples `00DC3000` (its pixel
shader, and what it writes), the UI draws (`PS A46EA964F2A2DE30`, blend `07060706`,
29 of them) which do come out right, and whether the draws between the last resolve
and the swap write to the same EDRAM base the UI does. The shader dumps
(`cvars::dump_shaders`) of the 3D pixel shaders are the next thing to read, with the
BUG-039 record in mind: every output a pixel shader declares and does not write is a
candidate for this driver to take as something else.

---

## BUG-046
**The title update applied on the device differs from the host's: an overlapping memcpy smears 54 KB of read-only data on ARM64**
XenonRecomp (XEX patcher) · severity critical · **FIXED**

Found 2026-10-25 because the user noticed that "New Game" went straight to a gameplay
HUD, with neither the intro cutscene nor character creation - and asked whether the
game was loading out of order rather than rendering badly. It was.

**What it looked like.** Hooks on the title's own code (`warden_game.cc`) showed the
module's load and start events (types 20 and 19) dispatched to a script named
"rn_troop_e", the title's own log printing "roop_b - _troop_a" where its code says
"EVENT_TYPE_MODULE_LOAD - Loading singleplayer...", and the module start finding no
character generation to run. The string literal at `0x820591BC` held, on the device,
the bytes the reference image (`tools/check_xex.py --build ppc_tu6 --patch auto`) has
0x1048 bytes lower. A dump of the guest image at load (`files/dump-image`) against the
reference found 17 ranges, 54,401 bytes, all in the read-only data the title update
patches, already wrong before the title ran a single instruction.

**Cause.** XenonRecomp's `xex_patcher.cpp` applies a delta patch's "move" records with
`std::memcpy(&dst[new], &dst[old], length)` inside one buffer, and the header delta the
same way. When a block moves by less than its own length the ranges overlap, and
`memcpy` on overlapping ranges is undefined. The x64 host's C runtime happens to copy
it correctly - which is how `recomp/generated/ppc_tu6` was made, from a correct image -
while bionic's ARM64 `memcpy` copies forward and smears the block: Title Update 7
moves about 27 KB of string data up by 0x1048, and the device got the start of it
repeated. Both the runtime and the host check call the same patcher, the input
`default.xexp` was byte-identical (`tools/check_stfs.py`), and the match check passed
(it looks at code, which is not moved) - so nothing said so.

**Fix.** `tools/gen_xex_patcher.py`, which generates the Android copy of the patcher,
now replaces both copies with `memmove` (two more asserted edits). The image at load
now differs from the reference only in the 12 import-variable slots the loader fills
on purpose. "New Game" plays `game_intro.cut` and `game_introduction.bik`; the EA logo
renders correctly (BUG-040). The host check still uses the upstream file, which works
there by the accident above; worth reporting upstream.

**To check an image again:** `adb shell run-as dev.daorecompandroid.duo touch files/dump-image`,
boot, pull `files/image-at-load.bin` and compare it with `build/xex-check/image.bin`
word by word (TESTING.md).

## BUG-047
**Rarely a command buffer holds a malformed packet, and the debug build's assertion kills the process**
our runtime / GPU · severity medium · **WATCH**

Seen twice in about 70 boots (runs 105 and 171), both times at the title screen or
earlier: `ExecutePacketType0 overflow (read count 00002B88, packet count 00003610)`,
then `**** INDIRECT RINGBUFFER: Failed to execute packet.` and the `assert_always()` in
`ExecuteIndirectBuffer` (`pm4_command_processor_implement.h`) traps the GPU command
thread (SIGTRAP; the tombstone's abort message is only the last log line). A type-0
packet claiming more registers than the indirect buffer holds means the GPU read the
buffer while it held something else - most likely the title still writing it, or our
guest memory view of it stale. Not chased yet: the next occurrence should log the
buffer's address and first words, and whether the CPU wrote to its pages after the
kick (the write-watch counters). A release build would drop the rest of that buffer
and carry on; the debug build stops, which is the right default while it is rare.

**Run 297 (session 33): the primary ring, right after the poller's kick.** A minute in, on
a load: `GPU: ring write pointer 000004B6 in the register window without a hooked store
(last hooked 000004B3); kicked (16 so far)`, then in the same millisecond
`ExecutePacketType0 overflow (read count 000013D8, packet count 000035D0)` and `****
PRIMARY RINGBUFFER: Failed to execute packet.` - the packet at word 1FBF of the 2000-word
ring, the first of the 1,270 words that kick sent the command processor into.

- **A race in our ring poller (fixed).** The hooked store (`PPC_MM_STORE_U32`) writes the
  register window's word before it calls the hook, and `WritePointerPoller` (1 kHz, the
  BUG-042 safety net) could read the word in between and kick with it - from its own
  thread, ahead of the hook's release that makes the packets the title wrote visible to
  the command processor. That was nearly every "unhooked" kick there was: some 0.4 a
  second at the King's Camp in every run since at least 250 (256 or more in an
  eleven-minute run); of those logged, 397 three words past the last hooked value, 94
  equal to it and three behind it - which sends the command processor round the whole
  ring again. The poller now kicks only a value still unhooked a poll later (the hook has
  delivered it by then) and never one behind the hooked value, and reads the word with an
  acquire fence: **no unhooked kick in runs 298-301**.
- **Why ARM64 shows it.** XenonRecomp compiles PowerPC's `eieio`, `lwsync` and `sync` to
  nothing. On x86 other cores see a thread's stores in order; on ARM64 they may see the
  window's word before the packets written ahead of it, so a kick from another thread can
  send the command processor into words not yet there.
- **For the next one:** a packet the command processor cannot execute now logs, before
  the trap, the 32 words around it (the packet marked), the stretch being executed, the
  last 16 kicks (value, hooked or polled, the kicking thread, how long ago), the read
  pointer written back and the title's own ring index (`warden_gpu_packet_failed`, three
  recorded edits in `pm4_command_processor_implement.h`). Still WATCH: the race fits the
  primary ring's case, not necessarily the indirect buffers' (runs 105 and 171).

---

## BUG-048
**Every save's time was garbage: two time-conversion stubs**
our runtime (import stubs) · severity medium · **FIXED** 2026-10-25 (session 32)

Seen in the first save and load runs (202-203). The title writes the time a save was
made into its display name - `Jaden_Dragon Age_20261025191739` is the character, the
save's name and the time as YYYYMMDDhhmmss in UTC, from `RtlTimeToTimeFields` - and
parses it back with `RtlTimeFieldsToTime` whenever it lists the saves, to sort the lists
newest first and to pick the save **Resume** loads.

**What it caused:** both were generated stubs. `RtlTimeToTimeFields` is `void`, so its
stub returned with the caller's `TIME_FIELDS` unwritten, and the title formatted
whatever the stack held: the run-199 save and the first saves of run 202 are named
`Jaden_Dragon Age_00000000006553565535` - year, month, day and hour 0, minute and second
0xFFFF - and the autosave `Jaden_Auto-save_00000016411637286553565535`.
`RtlTimeFieldsToTime`'s stub returned 0, FALSE, for every date (the log after the fix:
`RtlTimeFieldsToTime(0000-00-00 00:65:53.000) -> FALSE, not a date`), so no save had a
usable time and the lists fell back to some other order. Nothing crashed and every save
still loaded, which is why only the display names showed it. The same class as BUG-030,
a stub that is not neutral - for a `void` import, where returning a failure is not
something the stub can do.

**Fix:** both implemented in `warden_imports_kernel.cc` - calendar arithmetic on the
console's FILETIME, as Xenia's `xboxkrnl_rtl.cc` does it - and checked on the host
against Python's `datetime` for 20,011 values, both directions and the weekday. A save
written since is named `Jaden_Dragon Age_20261025191739` and sorts first in the list
(run 203). The saves written before keep their broken names (the title rewrites
`<name>.name` only when it writes that save again) and sort after the dated ones;
harmless.

---

## BUG-049
**Every save's thumbnail is blank: `scrnshot.bin` is all zeros**
our runtime / GPU (pipeline cache) · severity low · **FIXED** 2026-10-27 (session 34)

Seen 2026-10-25 (runs 202-203). The Save Game and Load Game lists show an empty frame
where the save's screenshot belongs. Every `scrnshot.bin` the title has written - four
saves, from two workstations' builds - is 458,752 bytes of zeros, byte for byte the
same. The list previews do read it (`NtReadFile(save:\Saves\scrnshot.bin, 458752
bytes)` and its `.x` checksum), so the empty frame is the file, faithfully shown.

**What it causes:** no thumbnails in the save lists. Nothing else - the saves load.

**The likely cause, not yet confirmed:** the title takes the thumbnail through a GPU
resolve into guest memory and copies that into the save, but this runtime never copies
resolve results back to guest memory: Xenia's `readback_resolve` cvar is `none` here, and
`full` costs most of the frame rate (BUG-039's record). The CPU then reads zeros where
the GPU wrote the picture.

**Run 204, one save under `hack-resolve-readback` (readback `full`): partly.** Save (3)'s
`scrnshot.bin` has 3,269 non-zero bytes of 458,752 (0.71%), where every save without the
marker has none - so the thumbnail does come from the GPU, and readback is part of the
cause. But not all of it. The file is 8 slices of 57,344 bytes, and the data is only in
the last 4 KB of each slice. That is the shape of a 448x256 32-bit texture tiled in 32x32
tiles of 4 KB, 14 tiles to a row of tiles, with only the rightmost column of tiles
present. The words there repeat (`FE 00 8D 02`), which does not look like a picture of
the arena. So either the readback covers only part of what the title wrote, or the rest
is written another way the CPU cannot see - **memory export** from a shader is the
candidate: Xenia reads it back only with `readback_memexport` (off, no marker here yet;
"a huge performance impact").

**Next:** trace the frame the thumbnail is taken in (`files/trace-mode 3 0 1` for the
frame the pause menu opens, or mode 9 for the save) to see which resolves and which
memory exports write that range; or add a `hack-memexport-readback` marker
(`SaveGPUSetting(GPUSetting::ReadbackMemexport, 1)` in `warden_gpu.cc`) and repeat run
204's save with both. The fix should then read back that one range only, not every
resolve or export.

**Runs 293-294 (session 33): the GPU's own thumbnail is empty.**

- **Where it comes from.** The save writes `scrnshot.bin` from guest `A1E13000`
  (`NtWriteFile` logs the source buffer now), which the resolve `01E13000+70000` (pitch
  `010001C0`: 448x256, `k_8_8_8_8`) fills 77 ms before - at the save itself, not when the
  pause menu opens. Before it (`hack-resolve-dump` holding `010001C0`): the save menu's
  UI, then, into a 448x256 target at EDRAM base 0 (pitch 480), a fill of the whole target,
  a depth pass on a half-size surface, and a quad (vertex shader `78963937A7788928`, pixel
  shader `8D5822CC49BEC996`) sampling the 1280x720 tiled 8888 texture at `00DC3000` - a
  copy of the scene, resolved when the menu opened (as `00DC3000+280000`, then
  `+398000`).
- **The GPU's result is the file's.** The resolve dumped on the GPU side, before any
  readback, holds 3,270 non-zero bytes: the same rightmost 32-pixel column. So it is not
  the readback that loses the rest - nor memory export (`hack-memexport-readback`, new:
  no change) - nor this port's one-tile scene rendering (`scene-msaa`, the console's two
  tiles: no change). No draw or pipeline failed.
- **What is left:** the quad's coverage (its viewport, scissor or window offset), or the
  scene copy it samples. **Next:** the draw log extended with each draw's scissor and
  viewport and the resolve's source rectangle, and an address filter for
  `hack-resolve-dump` to dump the resolve to `00DC3000`. When the GPU gets it right, the
  CPU still sees it only with that one 448 KB resolve read back - cheap, once per save.
- Two test saves came of it on the Thor: "Save (5)" and "Save (6)", both in the Korcari
  Wilds; nothing was overwritten.

**Runs 302-308 (session 33): the texture cache's copy of the scene is stale.**

- **The draw and the resolve are right.** The draws before the thumbnail's resolve now
  log where they land (host viewport and scissor, the guest's window offset, scissors,
  viewport and VTE), stream 0's first vertices and the first texture's whole fetch
  constant; the dumped resolves log their source rectangle. The quad (draw into the
  480-pitch target at EDRAM base 0) lands at (0,0) 436x256, a full-screen quad of
  (+-1, +-1) with texture coordinates 0..1; the resolve copies (0,0)-(440,256) of the same
  target to the 448-wide thumbnail. The pixel shader only samples the scene copy and
  writes it (`mad` of the coordinates by c0 = (0.958, 1, 0.021, 0), `tfetch2D`, `max
  oC0`). The non-zero strip was the 4 pixels the resolve copies and the quad does not
  cover: the fill.
- **The scene copy is there.** At the thumbnail's resolve the 1280x720 copy of the scene
  at `00DC3000` - resolved every frame from EDRAM base 1024 - holds 3,686,369 non-zero
  bytes of 3,768,320 (a second dump, `hack-resolve-dump`'s address word). Its fetch
  constant (`8A024802 00DC3086 0059E4FF 00A80C14 00000003 00000200`): k_8_8_8_8, tiled,
  one level (no mips), linear, clamped, a plain BGRA swizzle - so not the mips
  (`base-only`, run 305: no change).
- **The texture cache samples a stale copy.** The binding log for the thumbnail's
  pixel shader (`hack-log-bindings`) shows the texture at `00DC3000` bound as up to date
  (base not outdated) - and the quad writes zero. With the range marked written by the
  GPU just before the draw (`reload`, every second save; run 306), the cache loads it
  again and the thumbnail comes out whole: **449,734 non-zero bytes, the party in the
  Korcari Wilds**. So a resolve's write to `00DC3000` did not outdate the texture the
  thumbnail samples, at least once between its last load and the save. At the title
  screen the same texture is outdated by every resolve and reloaded every frame (run
  307), as it should be; how it is left stale by the save - which loads, which watch
  fires, in explore, the menu and the save's own load mode - is the next run's
  question: run 308 was to log that timeline (through an event gate: the first 16,
  then one a second) but never reached the save.
- Test saves from these runs on the Thor: "Save (7)" to "Save (12)", all in the Korcari
  Wilds. Run 303 crashed in the new dump code mid-save (fixed); the empty save package
  it left was removed. The device is clean again: no route, no marker.

**Runs 309-313 (session 34): the cause - the pipeline cache's placeholder pixel shader.**

- **The texture cache was not at fault.** A ring of the last 4,096 events of the
  textures at `00DC3000` - each load and watch firing, with its texture object - printed
  at the thumbnail draw (run 310): the texture the quad samples is the one the game
  reloads three times a frame, outdated by the frame's last resolve and loaded again
  4 ms before the quad.
- **The real pattern.** Barriers around the load and the draw (runs 311-312) seemed to
  help the second save of each run - but the experiment fired one draw late, after the
  thumbnail: the second save of a run came out whole on its own (runs 306, 311, 312), the
  first never. The "stale texture" reading above was wrong: the forced reload of run 306
  "worked" because it was the second save.
- **The cause.** Xenia's pipeline cache with `async_shader_compilation` (upstream's
  default, on) draws a new pipeline with a placeholder pixel shader while the real one
  compiles on a background thread. The thumbnail's quad is a pipeline used once, at the
  save: it drew the placeholder (nothing of the scene; the strip at the right is the
  fill). By the second save the real pipeline was ready.
- **Fix (run 313):** pipelines created when first drawn with - `async_shader_compilation`
  off by default (`warden_gpu.cc`; `files/hack-async-pipelines` brings it back). The
  first save's thumbnail is whole (449,733 non-zero bytes) - and the HUD's portraits have
  their faces: BUG-052 had the same cause.
- **The cost (run 314, the King's Camp, eleven minutes):** 29.9 fps and 6.4% off the
  cadence as in run 301, no fault; the camp's first load 4.4 s instead of 1.7, and two
  hitches in its first seconds (0.48 s, 0.57 s) while new pipelines compiled, none after.
  Each pipeline costs once a session; Xenia's shader and pipeline storage, never set up in
  this runtime, would create those seen before at startup (PLAN 10.3).
- Test saves from runs 309-313: "Save (13)" to "Save (19)". Run 308 never reached the
  save: run 307's capture, still running, force-stopped its app (run 295's
  "force-stop from outside" was the same) - one capture at a time.

---

## BUG-050
**`KeWaitForMultipleObjects` read its timeout from the stack instead of r9**
`app/src/main/cpp/warden_threads.cc` · our runtime (kernel waits) · severity high · **FIXED** 2026-10-25 (session 32)

`KeWaitForMultipleObjects(count, objects, type, reason, mode, alertable, timeout,
wait_blocks)` has eight arguments, so all eight arrive in r3-r10 and the timeout pointer
is the seventh, r9. The implementation read it from the stack at r1 + 0x54 - the ninth
argument's slot - on the stated belief that "the seventh argument is the first past the
registers". Whatever the caller's frame held there became the timeout pointer.

**What it caused:** every call from the title waited with a garbage timeout. Found in its
XAudio render callback (`sub_8303BCC0`), which runs each 5.333 ms frame as `KeSetEvent`
(start the mixer), `KeWaitForMultipleObjects(2 objects, any, timeout NULL)` - wait for
the mix - and submits the frame only on `STATUS_WAIT_1`. The word at r1 + 0x54 in that
frame is the second object's address, so the event's own dispatcher header was read as
an absolute time long past: the infinite wait became a poll, and whenever the mixer was
not already done the callback returned without a frame. 11% of callbacks submitted
nothing (89,111 callbacks, 79,141 frames in run 203) - silence once there was an
output, and the title's mixer racing a clock that did not wait for it. Nothing crashed;
the title tolerates a missed frame.

**Fix:** the timeout is `ctx.r9.u32`. Every callback has submitted its frame since
(run 207: 14,704 of 14,704), and the boot is otherwise unchanged. The other waits were
checked and read theirs from the right registers (`KeWaitForSingleObject` r7,
`NtWaitForSingleObjectEx` r6, `NtWaitForMultipleObjectsEx` r8,
`KeDelayExecutionThread` r5). The lesson is RESUMING.md's own: arguments one to eight
are r3-r10, and the ninth is the first on the stack, at r1 + 0x54.

## BUG-051
**Entering the Korcari Wilds crashed: a jump table missing from the recompiler's config left two of a function's cases with no code**
`tools/recompile.py` · DAORecomp (recompiler config: `DAO_tu6.toml`, `DAO_tu6_switch_tables.toml`) · severity critical · **FIXED** 2026-10-25 (session 32)

**What it looked like.** Run 217, the first run of the `wilds` route: the prelude debug
menu's "Start in the Korcari Wilds" loaded `al_pre200ar_korcari_wilds.rim`, and eight
seconds later the `Eclipse::Update` thread called host address 0 ("non-guest fault:
host 0x0" - the session's first fault). The guest state named the call: r12 =
`0x82D7D1F0`, r11 = 6, lr `0x82D82454` - the return into `sub_82D821E8` from its call
to `sub_82D7D148`.

**Cause.** `sub_82D7D148` compares two floats (f1, f2) with constants, sets r11 to a
case number from 0 to 6, and ends in a jump table: r12 = `0x82D7D1F0` (`lis`/`addi`),
`rlwinm r0,r11,2,0,29`, `lwzx r0,r12,r0`, `mtctr r0`, and the `bctr` at `0x82D7D1EC`,
the seven words right after it. The comparisons bound the index, not a `cmplwi` - and
DAORecomp's `scripts/find_switches.py`, which made the 635 tables in
`DAO_tu6_switch_tables.toml`, starts from that bound check, so it skipped this one.
With no table XenonRecomp ended the function at the `bctr` and emitted it as an
indirect call through the function table. The table's words were decoded as a function
of their own (`sub_82D7D1F0`), cases 1-5 became functions by accident (`sub_82D7D218`
to `sub_82D7D238`), and cases 0 and 6 (`0x82D7D20C`, `0x82D7D244`), inside those, had
no entry: case 6 called null. A branch past the cut end (`ERROR 82D7D248` in the
generated code) was the other sign.

**Fix.** `tools/recompile.py` corrects the copy of DAORecomp's TOML it hands
XenonRecomp (the files in `third_party` stay as fetched): its `ADDITIONS` table gives
`sub_82D7D148` its real size, 0x108 (to the next function, `0x82D7D250`), and its
`[[switch]]` - the `bctr` address, r11 and the seven case labels - in a copy of the
switch-table file. Each addition is asserted: one upstream has since made itself, or a
TOML of another shape, stops the recompile. The function now holds a C `switch`, the
image check still passes (843 of 843 groups), and run 218 loaded the Korcari Wilds into
explore mode and played on for over two minutes with no fault. `python
tools/find_missed_switches.py` scans the generated code for the same shape (a constant
base, a scaled index, `lwzx`, `mtctr`, a `bctr` emitted as a call): none is left in
`ppc_tu6` (636 switches, DAORecomp's 635 and this one), and it does find this one in
the code from before the fix. DAORecomp's own build uses the same configuration and has
the same gap: PLAN 3.3b's reference crashed entering the Korcari Wilds the same way
("Indirect call to unrecompiled address 82D7D244", session 35) until
`tools/reference_build.py` gave it the same correction.

**After pulling the fix,** a machine set up before must run `python tools/recompile.py
tu`: the bootstrap skips step 5 whenever generated code exists (`RESUMING.md` §3.4).

**Upstream since (2026-10-30, session 37):** DAORecomp found the same and fixed it in its
own config (7da0e07, "the Aeducan Thaig crash", issue #5) with the very same bounds and
table. With its pin moved to `eb2360a`, `ADDITIONS` is empty again, and the recompiled
code came out byte-identical to the corrected one.

## BUG-052
**The HUD's party portraits show their coloured discs but no faces**
our runtime / GPU (render to texture: each face is a GPU resolve) · severity medium · **OPEN** 2026-10-25 (session 32, noted by the owner) · **parked** 2026-10-26 by the owner, after run 275 · **FIXED** 2026-10-27 (session 34, with BUG-049)

**What it looks like.** The four portraits on the left of the HUD show only their
coloured backgrounds: the player's a flat dark red or black; the second's (Alistair's -
it is the one selected while he is controlled) a blue radial gradient, purple while he
used Shield Bash; the third's brown; the fourth's gold. The fourth's face appeared
during the first fight in the Korcari Wilds and stayed (runs 219-220); no other face has
shown. Seen at the King's Camp (run 216, the player alone in the party) and in the Wilds
(runs 218-223). **The faces can render:** in the Tower of Ishal the player's portrait
was a black disc after the jump there, and showed his face after the death screen's
Load Last Save had reloaded the level (run 221) - what differs between the two loads
is not known.

**Cause: not investigated.** The discs are the Scaleform HUD's (`portraits.gfx` in
`guiexport.erf`); the faces are not among that archive's textures (it holds the portrait
movie and the party picker's atlases, no portrait images), so the title most likely
draws them at run time - each party member's head rendered to a texture. That would put
this in the same family as BUG-049, the save thumbnail, which is also a render the title
copies out of a frame. **Runs 225-227 (session 32):**

- **Not a missing readback** (run 225): with `hack-resolve-readback` (every resolve
  copied back to guest memory) the portraits stayed bare discs - the CPU does not read
  them.
- **The faces are GPU renders** (run 227, with new instruments - the texture key on each
  traced binding, and every distinct resolve destination and memory export range of a
  boot, `docs/TESTING.md`). Each portrait is a 146x146 `k_8_8_8_8` texture written by a
  resolve (pitch 160, 0x19000 bytes): one at the first load (`0276D000`, the start area,
  the player alone) and three at the Korcari Wilds' load within 5 ms (`034FE000`,
  `03F07000`, `04013000`), plus a 160x160 one (`018C2000`) as the debug conversation
  began. In the traced HUD frame the fourth portrait's draw samples `04013000` (with the
  HUD's DXT5 atlases), yet it shows its gold disc. No shader exported to memory in that
  boot at all.
- So either the head render into the portrait target comes out empty, or the HUD's
  texture never gets the resolved data. *(Runs 228-239 settled it: the render. Below.)*
  The step then planned: trace the frame of those resolves with `hack-resolve-dump` -
`files/trace-mode` `9 <swaps> 2` on the `wilds` route's first six presses (the Wilds load
is the second mode 9; the resolves came 0.45 s after it began, so a few swaps in) - and
turn the dumps into PNGs (`tools/resolve_to_png.py`): a face in the dump puts the fault
in the texture cache, which then misses the resolved data; an empty dump puts it in the
portrait render's own draws.


**Runs 228-239 (session 33): the face's bones are unset when the portraits are drawn.**

- **What the portrait renders hold** (run 228, `hack-resolve-dump` given the portraits'
  destination pitch `009200A0`, which dumps those resolves in any frame): the shoulders
  and armour - one with a hood - and no head or hair; alpha 0 where the head belongs, so
  no head pixels were written at all. The player's, in the start area, is empty.
- **What is drawn into them** (runs 234-239, the last 64 draws logged at each such
  resolve - `docs/TESTING.md`): clears, then the head (vertex shader `0CA594B641A10C7E`,
  10,788 indices), three smaller facial meshes, the armour (three draws) and the hair
  (a depth pass and three more) - all of it, into the 160-pitch target, with the depth
  test as in the scene. The head's six textures are the same in every render and hold
  their data when uploaded (run 235: `02405000` 15,871 of 16,384 bytes non-zero); no
  resolve or memory export writes them. Its pixel shader's 49 constants match a working
  render's (the same 10 zero, the same lights).
- **A working render:** in the first fight the game redraws the fourth portrait, and that
  one has a face (run 236: 49,969 non-zero bytes against 17,581 at the load).
- **The difference is the vertex constants.** The skinned meshes read all 256: at the
  load 75-76 of them are zero vectors, in the fight's redraw 32-33 - for the armour too,
  which does not use the extra ones. About 43 constants, some fourteen 3x4 bone matrices,
  are unset at the load: the face's bones, by every sign - the head and the hair vanish,
  the body renders, and the failed render shows faint eyelid lines where vertices weighted
  to other bones remain. The game draws the portraits during the area load, before the
  face is posed; later redraws find it posed. Faces in the scene and in conversations
  render and animate. Why the order differs from the console's is not known.
- *(Runs 257-258 found the indices and that the palette is CPU-written. Below.)*

**Runs 257-258 (session 33): which constants, where from, and not the jump.**

- **The zero constants by index** (run 257, the `wilds` route, new log lines - each
  recorded draw's zero vertex constants as ranges, and every head draw's): the three
  portraits drawn at the Korcari Wilds' load read c18-c29, c186-c204 and c209-c252 as
  zero vectors; the fourth portrait's redraw in the fight - the one with a face - reads
  only c18-c29 and c233-c252 as zero. So the face's bones are **c186-c204 and c209-c232**
  of the portrait's palette (43 constants), zero at the load; c18-c29 are zero in both
  and are not the face. The scene's own head draws after the load have c186-c252 zero
  and c18-c29 set, with faces that render - the scene's palette is laid out differently.
- **They are not loaded from memory by the GPU:** the only `LOAD_ALU_CONSTANT` packets
  before the portrait renders fill c244-c255 (the literal constants). The palette is
  written into the command buffer by the CPU as it records the draw, so at the load the
  array it is copied from holds zeros.
- **Not the debug jump:** after a save load (run 258, the `load` route) the four
  portraits are bare discs too, drawn 0.3 s before explore mode with c186-c252 zero -
  the bug is in normal play. `face.erf` is read by the FileIO thread two seconds before
  that render, so the face's data is loaded; what fills the face's part of the palette
  has not run, or has run and produced zeros (a recompilation fault in its vector math
  would look the same).
- **Run 259: the face's matrices exist only in GPU memory.** At the fight's working
  redraw, a search of every resident guest page for the face's first constants found
  four copies, each through the three physical views (`0D75B810`, `0D766070`,
  `0D77E6E0`, `0D825190` physical) - the command buffer's constant data for the
  portrait's draws - and none in the title's heap, not even the first row alone. So the
  face's part of the palette is computed straight into the command buffer as the
  portrait is recorded (the 360's D3D lets a title write constants there directly),
  from bone data kept in another form; a write watch on a CPU-side copy is not
  possible. (The instrument stays: `warden_memory.cc`'s search and write watch,
  `BUG-052 watch:` lines.)
- **A lead for a workaround:** the fourth portrait's redraw in the fight came 60 ms
  after an `EVENT_TYPE_ABILITY_CAST_IMPACT` - a status change on that member re-renders
  its portrait, and by then the face is posed. Calling that refresh after an area load
  would hide the bug.
- *(Runs 260-267 found the code and the cause. Below.)*

**Runs 260-267 (session 33): the palette stops short of the face's bones.**

- **Who records the portraits.** At each area load the **update thread**
  (`Eclipse::Update`) draws them, from the GUI's update: `sub_82677060` ->
  `sub_82670210` -> `sub_82670040` -> `sub_82685330` (the GUI's per-frame 3D render; it
  runs every frame and draws a portrait when one needs drawing) -> `sub_82689390` ->
  `sub_82686040` -> `sub_82599490` -> `sub_825FD6B8` -> `sub_82606C50` -> the mesh draw
  `sub_82698558`, which draws the head through the Direct3D function `sub_82D9DB20`
  (device, 4 = a triangle list, 0, 0, 10,788 indices). Found by wrapping the
  recompiled Direct3D functions in our own code (`PPC_FUNC` overrides, no recompile -
  `warden_game.cc`, `warden_d3d_probe.cc`), after a sample of every guest thread when
  the GPU reached a portrait (run 260) and a scan of each kick's indirect buffers for
  the head's draw (run 261) showed only the update thread presenting the loading screen
  afterwards.
- **Not a race.** The render thread uses the Direct3D device during loads too (runs
  263-264), but the zeros are the same in every run and on every load path, and holding
  the render thread during a load only stretched the load - the update thread waits for
  it (run 265, `hack-block-render-load`).
- **The cause** (run 266, every Direct3D call inside the model render logged): the
  vertex constants go up through `sub_82D941F8` (device, start register, data, count),
  called from `sub_826183A0` with count = min(bytes / 16, limit) from a constant record
  (+8 the data, +18 the start register, +20 the limit, +32 the bytes). Before the load's
  portrait head draw the skinning palette goes up as **c33 + 103 vectors** - 34 bones.
  The head's vertices reach bones up to about 66 (palette slots to c232): **the face's
  bones are not yet part of the character's skeleton palette when the load renders the
  portraits.** A later redraw - after an ability's impact - finds them merged.
- *(Runs 268-275 refined this. Below.)*

**Runs 268-275 (session 33): no upload fills the face's slots; the working redraw
comes from the render thread's side.**

- **The palette records** (run 268, `sub_826183A0` wrapped): the skinning palettes are
  shader-parameter records with a limit of 201 vectors (67 bones - room for the face)
  that alternate between 103 and 4 vectors per draw, sharing one scratch buffer; none
  reaches beyond 154 vectors (c32-c185) in a load, the scene or a conversation.
- **Nothing uploads c186-c204 or c209-c232** through the vertex-constant setter
  `sub_82D941F8` in any mode or thread (runs 269-270) - yet the fight's working redraw
  has them set (run 270, the GPU's view, as in run 257); no GPU constant load fills
  them either (only c252-c255). c205-c208 are the scene's own constants (0.4 and 2.6,
  every frame).
- **The working redraw is the render thread's** (runs 273-275): the update thread draws
  the portraits at the loads (and records no head draw during the fight); the kicks
  carrying a portrait's head draw - the head's initiator and the 160-pitch target in the
  same indirect buffers - come from `Eclipse::Update` at the loads and from
  `DragonAge::Render`, mid-way through its own mesh draws, in the fight. Both threads
  use the one Direct3D device during play, so what one leaves in the device's constants
  the other's draws can inherit.
- **Next:** how the fight's portrait gets c186-c232 - the device's shadow of those
  constants watched for writes (the first try armed the wrong offset), or the GPU-side
  values of c186-c232 at the redraw compared with the render thread's constant uploads;
  and whether the console's load draws a scene frame before the GUI draws the
  portraits, which this port does not.
- **Parked** (2026-10-26, the owner's decision): the work moved on to the rest of the
  plan. The instruments stay in the tree, their logging switched on only by the
  investigation's marker (`hack-resolve-dump` holding a pitch); without it the 542
  Direct3D wrappers cost a thread-local read and a few compares per call. The next
  steps are the two above.

**FIXED (2026-10-27, session 34): the cause was BUG-049's.** The portraits are drawn
once at an area load into their render targets, with pipelines first used there, and
each drew Xenia's placeholder pixel shader while its real pipeline compiled in the
background; the redraw in the first fight used the compiled one - the "one face after a
redraw". With pipelines created when first drawn with (run 313, `docs/BUGS.md` BUG-049)
every portrait has its face at the load. The reading above - the face's bones not yet
posed when the load draws the portraits - was at best part of it: with the real
pipelines the faces render at the load. The instruments stay, behind their marker.

## BUG-053
**Android's Back never reached the activity: the virtual keyboard was taken for a pad in an empty slot**
`app/src/main/java/dev/daorecompandroid/duo/ControllerManager.java` · our app (input, 4.6) · severity medium · **FIXED** 2026-10-26 (session 33)

**What it looked like.** Run 287: Back injected with `adb shell input keyevent
KEYCODE_BACK` while the title ran did nothing - no quit question, no line from the
activity - though `dumpsys window` showed the game focused on the main display (run 288).

**Cause.** `ControllerManager` marks an empty pad slot with the device id -1, and
`slotOf(deviceId)` compared ids without leaving that out. -1 is also
`KeyCharacterMap.VIRTUAL_KEYBOARD`: the device of the system's own Back - the gesture
and the navigation bar - and of every key `adb shell input` injects. Those keys matched
the first empty slot, went to the guest as presses on a pad that is not there, and never
reached `onBackPressed`. There since the pad mapping (4.6).

**What it causes.** The system's Back was dead while the game ran - the player could
leave only with Home - and keys injected for tests went to a phantom pad.

**Fix.** `slotOf` answers -1 for any negative device id. Run 289: Back brings up the
quit question, and Quit ends the process (4.9).

---

## BUG-054
**Frame pacing aimed each frame a refresh early by the panel's own times, so a frame a refresh late went uncounted; its period correction wound up; catch-up frames after a stall left the latency high**
`app/src/main/cpp/warden_frame_pacing.cc` · our app (frame pacing, 4.7) · severity medium · **FIXED** 2026-10-26 (session 33)

**What it looked like.** Run 296, the first with the resolve straight from the render
target: the title's own frames came almost perfectly even (1.5% off two vblanks, from
21%), yet on the panel 9.9% of frames were off the four-refresh cadence against run 285's
6.8% - 2.9% shown five refreshes after the one before and three before the next, in
windows where the pacer counted no frame late. Its period correction climbed to +0.1 ms a
frame, its offset sat at its floor, and its timeline started again 19 times in eleven
minutes.

**Cause.** Three faults, found one after another (runs 296-300).

- **The aim.** Each frame's desired present time was half a refresh before its slot, on
  the reading that the compositor shows a frame at the first refresh after that time. On
  the Thor a frame reaches the panel a refresh before that, by the panel's own times
  (`actualPresentTime`; run 297: every frame), so a frame shown at its slot was really a
  refresh late - five refreshes, then three - but "late" meant a refresh after the slot,
  two after where the frames went: the pacer neither counted it nor added slack for it.
  With the title's frames uneven (run 285) the slack it kept for them hid most of this;
  with them even and ready earlier, it showed.
- **The windup.** The period's correction took the offset's pushes (half a refresh later
  for a late frame) but not its fast pull-ins (for frames waiting two refreshes or more),
  so it only climbed: the timeline ran slower than the title, the frames ever further
  ahead of their slots, the offset pulled to its floor, and past that the timeline
  started again. Cutting the pull-ins down to an on-time frame's step instead wound it up
  the same way (run 300: +0.13 ms a frame, seven new timelines in five minutes).
- **The catch-up.** After a stall (a load, a long frame) the title's next frames come a
  vblank apart, and each moved the timeline a whole period ahead: with two periods
  allowed before a new timeline, three stalls left the frames 72 ms from present to panel
  for good, the offset pinned at its floor and no frame late to start the correction (run
  299, against 38 ms in run 298).

**What it causes.** On the panel, about 3% of frames a refresh late after the direct
resolve (run 296: twice run 285's share), each a visible hitch; a restarted timeline every
half-minute or so; or twice the latency after a few loads.

**Fix.** The desired time a quarter of a refresh after the slot, so the slot is the
refresh the frame is shown at by the panel's times (runs 298-301: no frame early); a late
frame that was finished a refresh before it was taken - held by its desired time - no
longer pushes the offset; every step of the offset goes into the period once it has found
its edge, the pull-ins in full; and a new timeline once the old one is a period ahead of
the frames. Run 298 compared four desired times (4, 3, 2 and 1 eighths of a refresh after
the slot, a minute each): no frame early in any, 6.2-6.9% off the cadence in all, where
4.1% is the floor for the title's 33.43 ms. Run 301 (the default, eleven minutes): 6.4% off the cadence, 38 ms from present to panel (p50), no frame early, the period's correction within 0.025 ms a frame either way, two new timelines once at the camp.
New counters in the `pacing:` line: late (and of them held by the desired time), early,
and timelines started because the slots ran ahead.

---

## BUG-055
**Xenia's shader and pipeline storage kept nothing between sessions on Android**
`shader_storage.h`, `vulkan_pipeline_cache.cc` · upstream (shader storage) / our runtime · severity medium · **FIXED** 2026-10-27 (session 34)

**What it looked like.** With the storage set up (PLAN 10.3, `warden_gpu.cc`), the second
boot logged "Loaded 0 shaders from storage" and "0 pipeline descriptions" (run 316), and
the files never grew past whole 4 KB blocks.

**Cause.** Three faults, all in the path from the storage to the disk.

- **Emptied at every start.** The storage files are opened `a+b` and the header read at
  once. glibc and MSVC start an `a+` stream at the beginning; Android's bionic, from BSD
  stdio, starts it at the end - the read found nothing, the storage was taken for
  invalid and truncated to a fresh header.
- **Never flushed.** The writer thread flushes when the pipeline cache's `EndSubmission`
  asks, and this command processor calls that only for a pipeline not yet created - with
  pipelines created when first drawn with (BUG-049), never. Only stdio's own 4 KB blocks
  reached the files, and the rest died with the process.
- **The driver's cache never saved.** The `VkPipelineCache` data is written at
  `ShutdownShaderStorage`; this app's process is killed, never shut down.

**What it causes.** Every session compiled every pipeline afresh - with pipelines created
when first drawn with, an area's first load slower and its first seconds hitching every
time (run 314).

**Fix.** The two reads seek to the start; `DaoFlushStorage` (a new pipeline-cache entry,
called at every submission's end) asks the writer to flush, and saves the driver's cache
every 30 s while the pipeline count grows, through a temporary file renamed into place.
Run 317 (an empty storage) wrote 105 shaders, 106 pipeline descriptions and 2.3 MB of the
driver's cache; run 318 loaded them all at startup and created the pipelines in the
background: the King's Camp's load no longer stalls (its worst frame 112 ms against 924
and 3,474 in run 314), startup 0.4 s slower. The storage is
`files/pipeline-storage-v1/` (its version is `kPipelineStorageVersion` in
`warden_gpu.cc`: bump it when the shader translation's modification bits change meaning);
`files/hack-no-pipeline-storage` turns it off.

---

## BUG-056
**Coming back to the app could hang its main thread for good: a frozen picture and "not responding", or a black screen after the power button**
`warden_app_context.cc` · our app (lifecycle) · severity high · **FIXED** 2026-10-27 (session 35)

**What it looked like.** The owner's report (2026-10-27): switching to another app, or to
the home screen, and back left the picture frozen with the sound playing and the pad
dead, and a few seconds later "Warden's Port isn't responding" (two that morning, 09:41
and 09:44); locking with the power button and waking left the screen black with the
sound playing, and no dialog. Not every time: the lid closed and opened at 09:52 came
back whole, the power button 13 s later did not.

**Cause.** The app context's wake pipe (`warden_app_context.cc`, PLAN 4.2a) was made with
`pipe()`, so blocking, and `OnWake` drained it with `while (read(...) > 0)` - which on an
empty blocking pipe never returns. The first function posted to the main thread from
another thread parked the main thread in that `read` for good: both ANRs' traces (`adb
shell dumpsys dropbox --print data_app_anr`) show it there, under
`warden::AppContext::OnWake`. Only one thread ever posts: Xenia's presenter asks for a
paint from the guest output thread only between a surface's (re)connection and its first
paint from the main thread (`kUIThreadOnRequest`), so a return hung only when a guest
frame finished in that moment - about one in three at 30 fps. The paint counters said it:
"5 requested, 4 posted" after the power button, and nothing posted after that. With the
main thread parked the window never drew again: Android left the task's snapshot on the
screen (the frozen picture) or nothing (black, once the screen came back on), never gave
the window input focus, and at the first key called the app not responding
("Application does not have a focused window"); behind it the game ran on, sound and all
(BUG-057).

**What it causes.** Any return to the app could leave it dead, to be closed - and
everything since the last save lost.

**Fix.** The pipe is non-blocking at both ends (`pipe2(O_NONBLOCK | O_CLOEXEC)`), as the
writer's own `EAGAIN` comment had assumed all along; the GPU status line's paints now
count the requests that crossed from another thread. Runs 319-322: fourteen returns - home,
the power button, three in quick succession, at the title, during the intro movie and in
play - and after each the window took input focus within 0.6 s, every paint request was
posted, three of them through the pipe from the guest output thread, and no ANR. **The
owner confirmed it on the Thor, by hand, the same day.**

---

## BUG-057
**The game ran on in the background: put away, only its sound stopped**
`warden_suspend.cc` · our app (lifecycle) · severity high · **FIXED** 2026-10-27 (session 35)

**What it looked like.** Pausing the activity paused the sound output and nothing else
(4.9): the title ran on unseen. In the owner's runs the guest went on refreshing frames
and the audio worker threw its frames away ("931 dropped") while the screen was off.

**What it causes.** A game put away mid-fight fights on without the player; a
conversation goes on unheard; the GPU draws frames nobody sees, and the handheld runs warm
in a bag. A handheld game has to stop where it is put away.

**Fix.** The guest is suspended from `onPause` to `onResume` (`warden_suspend.cc`). Its
threads hold at the entry and the exit of every wait and sleep and at each yield, the
XAudio worker before its callback, the kernel's clock thread and the 5 s reports with
them - never inside one of the runtime's locks, so the main thread cannot find one taken.
The timebase (`PPCQueryTimebase`) and the kernel's time stamp bundle stand still
meanwhile, so the title sees no time pass; the wall clock runs on. The console's hardware
runs on - the GPU finishes what it was given, and the vblank still reaches the title's
interrupt handler - because Direct3D's render thread polls the GPU's progress with no
wait in its loop (`sub_82D8E218`): with the interrupts held it could spin a core in the
bag. The ring poller slows to 20 Hz. Runs 319-321: nine to twelve threads held at each
suspension, and while away no title thread made a call in the 5 s reports - only the
vblank handler; the title's clock stopped and went on from where it stopped (run 320:
7.615 s of play between two suspensions read 7.618 s on it, the 20.7 s away not at all);
after a return, the scene as it was put away (run 320), the intro movie going on from its
frame (run 321) and the pause menu at START (runs 320-321). The process while away: 10-13%
of one core, nearly all of it two of Xenia's threads that spin-wait whatever happens
(BUG-058). Not covered: Android can still end the process in the background when memory
runs short - the game's state is in memory only. The owner decided against a foreground
service (STATUS Q12); PLAN 9.13 investigates an autosave as the game is put away. **The owner confirmed it on the Thor, by hand, the same day.**

---

## BUG-058
**Two of Xenia's threads spin-wait: about 11% of one core, playing or put away**
`threading_timer_queue.cc`, `logging.cc` · upstream (Xenia) · severity low · **FIXED** session 46 for the game put away and the launcher (found 2026-10-27, session 35)

**What it looked like.** With the game suspended (BUG-057) and all of the title held, the
process still used 11-12% of one core (`top -H`, runs 321-322): 5.6-5.8% in a thread
named after the app - Xenia's timer queue dispatcher (`xe::threading::TimerQueue`; its
25-character name is past bionic's 15, so `set_name` fails and it keeps the main
thread's) - and 5.0% in the "Logging Writer", both found in `nanosleep`, with nothing to
dispatch or write.

**Cause.** Both wait with disruptorplus's `spin_wait_strategy`: busy spins, then yields,
then short sleeps, so they wake hundreds of times a second whether anything arrives or
not. Upstream tried the blocking strategy for the timer queue and went back (the comment
in `threading_timer_queue.cc`: first it would not build, then it broke initialisation
under Proton).

**What it causes.** About a tenth of one core all the time, in play too - and, put away
with the screen on (another app in front), all the CPU the game still takes. With the
screen off Android suspends the device, as this app holds no wake lock, so it should cost
nothing there (not measured: adb over Wi-Fi keeps the device awake).

**Fix considered, not applied.** `dp::blocking_wait_strategy` in both, as recorded edits: its
timed wait already has the argument order upstream had to fix; each publish then takes a
mutex and notifies. The log writer is on every thread's path, so a mistake would hang the
whole app: it wants a run of its own.

**Fixed for the idle host** (session 46, BUG-119; `tools/assemble_runtime.py`, four edits):
both wait with `WardenIdleWaitStrategy`, disruptorplus's spin wait except that while
`warden_host_idle()` says so - no title started yet (the launcher: the timer queue's thread
starts as the library loads, the log writer with the runtime), or the title suspended (put
away, the lid, the lock, the options panel) - a wait sleeps up to 100 ms at a time instead of
yielding and sleeping 1 ms, never past its own timeout, so a timer still fires on time. In
play the wait is the spin it was. The published 0.2.1's launcher in the background: 7.7% of
a core, all of it these two; this build's, 0.0%. Locked at the title screen they went from
10.5% and 10.1% to 0.1% and 0.0%. The guess above that the screen off costs nothing missed
that the device still wakes by itself, and these ran each time.

---

## BUG-059
**With no Vulkan instance the boot went on, and crashed in the command processor's constructor**
`vulkan_graphics_system.cc`, `warden_runtime.cc` · upstream (Xenia) / our runtime · severity low · **FIXED** 2026-10-27 (session 35)

**What it looked like.** Run 328, the first GFXReconstruct capture: the GPU debug layer
setting named a layer the loader could not load (Android 11's package visibility hid the
replay app it lives in), `vkCreateInstance` failed with `ErrorLayerNotPresent`, and the
app died at the boot with a SIGSEGV at address 0x10 in `VulkanCommandProcessor`'s
constructor.

**Cause.** `VulkanGraphicsSystem::Setup` creates its Vulkan provider and goes on to
`GraphicsSystem::Setup` whether or not it got one, which creates the command processor,
whose constructor reads through the null provider. Upstream's emulator checks a backend's
provider before choosing it; this app sets the graphics system up directly. And the boot
itself went on when the GPU failed - a choice from bring-up, when a title running blind
was worth having.

**What it causes.** Any failure to create a Vulkan instance - a broken driver, a
misnamed debug layer - crashed the app at the boot instead of saying what was wrong.

**Fix.** A recorded edit: no provider, and `Setup` fails (`X_STATUS_UNSUCCESSFUL`). The boot
then does not start the title (`Runtime::BootGuest`), and the report on the screen names
the Vulkan failure. Run 331, with a layer that does not exist named: `startup() = false`,
`bootGuest() = false`, "boot: the GPU did not start, so the title is not started", no
fault; run 332 boots as before. 167 recorded edits.

---

## BUG-060
**The pipeline storage created every stored pipeline at startup, and a pipeline whose shaders spill holds about 24 MB of the driver's memory**
`vulkan_pipeline_cache.cc` (10.3's storage) · our runtime / the Adreno driver · severity medium · **FIXED** 2026-10-27 (session 35)

**What it looked like.** PLAN 3.7's measurements (runs 333-337). The Adreno driver counts
1.8 GB of GPU memory against the game at the King's Camp
(`/sys/class/kgsl/kgsl/proc/<pid>/kernel`); the app's own accounting of every
`vkAllocateMemory` (`warden_gpu_memory.cc`) finds 614 MB - the texture cache 224, the
shared memory's sparse chunks 160, render targets 154, upload pages 52. The other 1.18 GB
appeared at the graphics system's start: with the pipeline storage off it was 40 MB at
the title, growing as pipelines were created (411 MB by the camp); the bring-up device
costs 5 MB, and the sparse shared memory hides nothing (the plain 512 MB buffer moves
both counts alike, run 336).

**Cause.** Two things together. **A pipeline whose shaders spill holds about 24 MB of the
driver's memory**: the driver's own statistics per pipeline
(`VK_KHR_pipeline_executable_properties`, `files/hack-pipeline-stats`, runs 338-340) show
44 of 181 pipelines with any scratch memory - 6 to 8 bytes a shader instance in most,
536 in the biggest pixel shader - at 22-27 MB each, 1.04 GB together, and the other 137 at
about 1 MB. The scratch is the compiler spilling under register pressure (vertex shaders
of up to 3,200 instructions and 39 registers, pixel shaders up to 6,645 and 48): every
register index in their SPIR-V is a constant, so it is not the register file's array
(run 341's dumps); the driver reserves the scratch for every wave the GPU can run. And
**the storage created all its pipelines at startup** (PLAN 10.3) - 182 after a day's
tests, from every area visited before, a number that grows with play.

**What it caused.** 1.1 GB of GPU memory at each start for pipelines the area did not
use, growing with every area ever visited - a whole game's would have asked for
gigabytes; and a bigger process is the likelier one for Android to end in the background
(STATUS Q12). Found on the way: each pipeline created at its first use was written to
the storage again (288 descriptions for 182 pipelines after run 342).

**Fix.** Two recorded edits. The stored pipelines are no longer created at startup: each
is created when first drawn with, a hit in the driver's `VkPipelineCache` that the storage
still loads - no compile; `files/hack-precreate-pipelines` brings upstream's way back. And
a stored pipeline is not written again at its first use. The King's Camp (runs 342-343,
same route, same build): the driver's count 1,027 MB against 1,799, the title 386 against
1,511 - and the frames around the camp's load the same (worst 263 ms against 252, the
camp's own arrival hitch). Two boots in a row keep 288 descriptions (runs 344-345). Left:
a long session still keeps every pipeline it used - 24 MB for each that spills; spilling
less would need smaller translated shaders.

## BUG-061
**Rewriting the UI's atlas descriptions while the game ran crashed its next load**
`warden_glyphs_apply.cc` · our runtime · severity medium · **FIXED** 2026-10-27 (session 35)

**What it looked like.** Run 350, PLAN 7.7's live switch of the button prompts: the game
started with the PlayStation set; at the main menu the settings changed it to Nintendo -
the six atlas descriptions rewritten in the override tree, 0.1 s - and Resume loaded the
latest save. At that load the title read the descriptions again (14:09:19.4-19.6), and
0.3 s later its render thread called through a null function pointer inside Scaleform
(`sub_830BE448`: "an INDIRECT CALL to guest 00000000 whose table slot is empty"); the
app died. The same load with the set fixed from the start ran clean in both sets (runs
346-347, 351).

**Cause, as far as it was followed.** The title lists its patch folder once, at its start,
and reads the atlas descriptions again at every load. The sets move some images from a
screen's own description to the shared one (the dialogue pop-up's prompts drawn from the
PS3 cells, character creation's A from the B coin), so a rewrite changes which
description holds which image, and UI objects built from the first layout outlive it.
Not traced further: the live switch was dropped instead.

**What it caused.** A crash at the first load after changing the prompts - everything
since the last save lost. Found in the change's own test; never in a build the owner
used.

**Fix.** The prompts are applied only as the title starts (`Runtime::LoadTitle`); the
settings write the choice for the next start and offer to quit now (run 349). PLAN 7.7's
live switching is not met: a change shows from the next start.

---

## BUG-062
**The presenter made its last pass's pipeline again at every paint**
`xenia/ui/vulkan/vulkan_presenter.cc` · Xenia (upstream) · severity medium · **FIXED** 2026-10-27 (session 35)

**What it looked like.** Found while giving the pass that draws into the swapchain an
HDR10 variant (PLAN 9.2). The presenter keeps that pass's pipeline, per effect, with the
swapchain format it was made for, and makes it again when the swapchain's format
differs - but nothing ever records that format, so it always differed. At every paint
the presenter waited for the previous paint's submission to complete on the GPU,
destroyed the pipeline and made a new one, on the command processor's own thread (the
guest output thread presents directly). Upstream Xenia Canary has the same code;
DAORecomp's Direct3D 12 presenter is built differently.

**What it caused.** A wait for the GPU and a pipeline creation in every frame, on the
thread that feeds the GPU, and frames presented unevenly: at the King's Camp at 60 fps
(GPU-bound at about 31), frames 3, 4 or 5 refreshes apart - present intervals p95 37.1
ms, the 1% low 26.2 fps (run 394).

**Fix.** A graft edit records the format - and the HDR10 state - the pipeline was made
for. The same scene with the same settings (run 397): every frame 4 refreshes apart, p95
33.2 ms, the 1% low 29.7 fps, 31.1 fps against 30.6. The HDR10 pass's pipeline was made
twice in the run - switched on, and the paper white changed - where it would have been
made at every paint.

---

## BUG-063
**FSR 3.1's context does not fit its opaque block on Android**
FidelityFX SDK v1.1.4, `ffx_fsr3upscaler.cpp` · severity medium · **WORKED AROUND** 2026-10-27 (session 35)

**What it looked like.** The SDK's FSR 3.1 upscaler, built for Android (PLAN 9.4), fails
to compile: `static_assert(sizeof(FfxFsr3UpscalerContext) >=
sizeof(FfxFsr3UpscalerContext_Private))`. The public context is an opaque block of
`FFX_SDK_DEFAULT_CONTEXT_SIZE` (128 Ki `uint32_t`, 512 KB) that the private state must
fit; that state holds wide-character names (resource and pipeline names), and `wchar_t`
is 4 bytes on Android and Linux against Windows' 2.

**What it caused.** No FSR 3.1 build at all. (`-fshort-wchar` would have made the sizes
match but broken every C library wide-character call the SDK makes.)

**Worked around.** `tools/build_fsr3_shaders.py` writes a copy of the SDK's
`FidelityFX/host/ffx_types.h` with the size doubled into `build/fsr3-shaders`, which is
first on the include path for the SDK's sources and the app's alike, so both agree on the
size.

---

## BUG-064
**The SDK's Vulkan backend uses features the device never enabled**
FidelityFX SDK v1.1.4, `ffx_vk.cpp` (`GetDeviceCapabilitiesVK`) · severity high · **WORKED AROUND** 2026-10-27 (session 35)

**What it looked like.** Read while bringing FSR 3.1 up (PLAN 9.4): the backend decides
its shader paths - 64-lane subgroups (`VK_EXT_subgroup_size_control`), 16-bit arithmetic
(`VK_KHR_shader_float16_int8`), synchronization2's barriers - from
`vkEnumerateDeviceExtensionProperties` and `vkGetPhysicalDeviceFeatures2`: what the GPU
has, not what the logical device was created with. An application that made its own
device with fewer features gets those paths anyway.

**What it would have caused.** On Xenia's device, which enables none of those features:
pipelines that require a subgroup size the device was not created to allow, 16-bit
shaders on a device without `shaderFloat16`, `vkCmdPipelineBarrier2` without
synchronization2 - invalid use, undefined on the Adreno (a crash or wrong results).

**Worked around.** The SDK's call is renamed into the app by the compat header
(`#define vkEnumerateDeviceExtensionProperties
warden_ffx_vkEnumerateDeviceExtensionProperties`) and answered in `warden_fsr3.cc`:
no extensions, so the 32-bit paths with plain barriers (run 399's context, runs 400-401's
frames). PLAN 9.4's next step answers `VK_KHR_shader_float16_int8` once the graft
enables its features, for the 16-bit paths.
**Since session 36** the graft enables `shaderInt16` and Vulkan 1.2's `shaderFloat16`
where supported, and the answer is always one extension - `VK_KHR_shader_float16_int8`
for the 16-bit permutations, or a name the SDK does not know for the 32-bit ones - so its
count never changes: the SDK sizes its scratch memory by the count when its interface is
made and fills the list again as each context is made, at the end of that memory (runs
402-403).

---

## BUG-065
**FSR 3.1's upscaler ended Xenia's open submission in the middle of the swap**
Ours, `warden_upscale.cc` (`DestroyContext`, `ReleaseImage`) · severity high · **FIXED**
2026-10-30 (session 36)

**What it looked like.** Run 402: the first time the upscaler's context was made again
(16-bit and 32-bit alternating, `files/fsr3-half` holding `alternate`), the process died
at once - SIGTRAP on the `GPU Commands` thread, the native backtrace
`WardenUpscaler::Impl::OnSwap` <- `VulkanCommandProcessor::IssueSwap`, no guest fault.

**Cause.** Before letting go of the context or one of its images, the upscaler waited for
the GPU with `CheckSubmissionCompletionAndDeviceLoss(GetCurrentSubmission())`. Asked for
the submission still being recorded, Xenia ends it first (`EndSubmission(false)`) - here
in the middle of `IssueSwap`, whose remaining commands then went into no open
submission, and an assertion stopped the process. Runs 400-401 never reached it: the
context and the images were only ever made, never replaced.

**What it would have caused in play.** The same death at any render size change: the
images made again (Balanced and Performance, PLAN 9.4's next step), or the context
(the panel's size).

**Fixed.** `AwaitSubmitted` waits for what was submitted before the frame
(`GetCurrentSubmission() - 1`) and never ends the open submission: nothing recorded in
it has used what is let go at those points (the dispatch and the composite come after;
a size change is at the swap). Run 403: the context made again 14 times in a run, 16-bit
and 32-bit in turn, no fault.

---

## BUG-066
**FSR 3.1 jittered the post-processing's half-size passes and took its depth from them**
Ours, `warden_upscale.cc` (`OnDraw`, `OnResolve`) · severity medium · **FIXED**
2026-10-30 (session 36)

**What it looked like.** Run 406's summary of a frame's draws at the King's Camp
(`files/fsr3-trace`): after the scene's resolve, the post-processing draws a few
depth-tested passes 640 wide with 2x MSAA, at EDRAM 0 and 1024 - and they were taken for
"the game's own 2x MSAA scene tiles" (depth-tested, multisampled, at least half the
screen wide: the rule for `files/scene-msaa`). And three render target copies a frame
where two were needed.

**What it caused.** Those passes were jittered by FSR's sub-pixel offset every frame - a
shimmer in whatever they draw - and each left the scene's depth "pending", so the next
resolve copied FSR's depth input from a half-size multisampled depth buffer instead of the
scene's. Separately, the depth was copied at the first resolve after the scene's draws,
which is the depth prepass's own, before the shadow maps - once too often a frame. With
zero motion vectors FSR leans little on depth; with motion vectors (PLAN 9.4's next step)
the wrong depth would have misjudged disocclusion.

**Fixed.** Once a frame has drawn its scene single-sampled (one tile, the default),
multisampled draws are never the scene; and the depth is taken once the scene is drawn,
at its colour's resolve, or before a resolve that clears it. Run 407: only the depth
prepass and the scene's colour draws jittered (816 and 1,122 draws in the traced frame),
two copies a frame, the camp 24.2 fps against 23.7-23.9.

---

## BUG-067
**FSR 3.1 ran without its depth and motion: the shared resources were never passed**
Ours, `warden_upscale.cc` (the dispatch description) · severity high · **FIXED**
2026-10-30 (session 36)

**What it looked like.** With the camera's motion vectors built (PLAN 9.4 step 4, runs
410-416), a turning camera smeared just as without them - brushstroke streaks in the
foliage and on the walls - and a still camp fed a deliberately wrong 20 px of motion
looked exactly like one fed none (run 417). FSR 3.1's debug view
(`files/fsr3-debug`) showed its dilated motion vectors flat grey (zero) and its depth
black, though the motion pass's own probe read real depths (0.91-0.999) and 3-16 px of
motion at the same moments (run 415).

**Cause.** FSR 3.1's upscaler writes the dilated depth, the dilated motion vectors and
the depth reprojected into the frame before into three **shared resources** that the
caller makes - `ffxFsr3UpscalerGetSharedResourceDescriptions` describes them - and passes
with every dispatch (`dilatedDepth`, `dilatedMotionVectors`,
`reconstructedPrevNearestDepth` in `FfxFsr3UpscalerDispatchDescription`), so that frame
generation can share them. The port (session 35) never set them, and the SDK does not
check: its passes wrote to and read from null resources, so everything after the inputs'
preparation saw zero depth and zero motion - no reprojection, no disocclusion, history
taken from the same place whatever moved.

**What it caused.** Since FSR 3.1 was first built (runs 399-409): a still scene upscaled
well (the history lines up anyway), but any camera motion smeared; the motion vectors
could not help; and FSR's cost was understated - 7.8 ms with its depth work skipped.

**Fixed.** The upscaler makes the three at the render size (R32 float, R16G16 float, R32
uint - FSR's descriptions) and passes them in the unordered-access state, which the SDK
returns them to. Run 418's debug view: the motion field smooth across the screen with the
standing character apart by parallax, depth in metres, disocclusion along silhouettes,
the shading-change mask from 17.7 to 1.1 (mean); run 420: a turn without streaks. FSR
3.1 at Quality now 9.2 ms (prepare inputs 0.38 -> 1.74 ms, doing its reprojection).

**Also found:** the Adreno 740 has no linear filtering for `R32_SFLOAT`, `R32G32_SFLOAT`
or `R16_UNORM` (`adb shell cmd gpu vkjson`), and FSR samples its R32 float depths
through a linear sampler in two places - `SampleInputDepth` (never called) and
`SampleDilatedDepth` (only the debug view's depth panel): undefined there, harmless to
the upscale.

---

## BUG-068
**The game check stopped on an emoji in a window title**
Ours, `tools/game_check.py` · severity low · **FIXED** 2026-10-30 (session 36)

**What it looked like.** Before a build: a traceback, `UnicodeEncodeError: 'charmap' codec
can't encode character '👀'`, and no verdict line - so the build that waits for
`verdict: no game` did not start.

**Cause.** The check prints every visible window's title, and Python's console output on
Windows uses the console's code page (cp1252), which has no emoji; a browser tab's title
had one.

**What it caused.** Only a refused build - the safe way round: no verdict, no build.

**Fixed.** Standard output replaces what the code page cannot hold instead of raising.

---

## BUG-069
**DAORecomp's frame pacer does not fit a GPU-bound game on one queue**
Ours (frame generation, PLAN 9.14) · severity high · **FIXED** 2026-10-30 (session 37)

**What it looked like.** Run 427, Lossless 2x on, DAORecomp's `ui/dao_frame_pacer.cc` ported
to Android (a condition variable for its Win32 timer and event): 120 frames a second on
the panel at the title's 60 fps logos, then `the display can't show twice the frame rate,
showing only real frames for now` - and at the King's Camp generated frames were shown only
in short bursts between pauses of 5, 10, 20, 40 s.

**Cause.** Upstream's pacer paints a guest output's generated frame when the guest output
thread hands it over and its real frame half a frame later, by the CPU's clock. On a PC the
GPU keeps up and a paint runs when it is submitted. Here the title is GPU-bound, the
command processor runs up to three frames ahead of the GPU, and everything is on queue 0
in order: the real frame's paint, submitted half a frame later, still ran right after the
generated one, behind the title's frame - no spacing at all - and the pacer thread, held up
in its paints behind the frames in flight, had not started the real frame's paint by the
time the next guest output came, which upstream counts as "behind".

**What it caused.** Generation paused most of the time at the camp; when it ran, the two
frames of a pair on adjacent refreshes.

**Fixed.** `app/src/main/cpp/dao_frame_pacer_android.cc` keeps upstream's thread (BUG-073
says why it is needed) but paints both frames at once and leaves the spacing to the
compositor: each present carries a desired time on one timeline
(`warden_frame_pacing.cc`, VK_GOOGLE_display_timing), the real frame half the title's frame
interval after the generated one.

---

## BUG-070
**Generated frames due at once made the compositor drop every real frame**
Ours (frame generation, PLAN 9.14) · severity high · **FIXED** 2026-10-30 (session 37)

**What it looked like.** Run 428: about 20 frames a second reached the panel for 44
presented, and no real frame ever came back in the panel's times (`swap to panel ms none`).

**Cause.** Each pair's generated frame was presented with its own present time as its
desired time (shown as soon as it is finished), the real frame with a time half a frame
later. Android's buffer queue, when the frame behind the front one is also due, drops the
front one (BufferQueueConsumer::acquireBuffer) - and the next pair's generated frame, "due
now", was always due before the held real frame was shown.

**What it caused.** The panel showed the generated frames only.

**Fixed.** Every present of a pair gets a target on one timeline, each later than the one
before: the generated frame half a title frame after the previous real one, never before
it can be ready nor more than half a frame after that; the real frame half a frame after
its generated one. (Run 441 alternated timed and untimed pairs every 10 s: untimed, the
generated frame of nearly every pair is dropped - 26-32 frames a second on the panel;
timed, 60-67.)

---

## BUG-071
**One frame of each pair lost to the swapchain's mode, then to the paint mode**
Xenia (upstream choices) / ours · severity high · **FIXED** 2026-10-30 (session 37)

**What it looked like.** Run 429: still about half the presents on the panel. Run 430,
after the switch to FIFO: one present a title frame - the presenter log said `paint mode 1`.

**Cause.** Xenia's Vulkan presenter prefers a MAILBOX swapchain (`presentation mode 1`),
where a newer present replaces the one still queued: with both frames of a pair queued
before the panel takes either, the second replaced the first. With FIFO, Xenia moves the
painting to the UI thread (`GetDesiredPaintModeFromUIThread`: implicit vsync must not make
the guest output thread wait), and the UI thread paints the newest guest output's real
frame only.

**What it caused.** Half the frame generation lost (MAILBOX), or all of it (FIFO).

**Fixed.** Three graft edits while frame generation presents
(`warden_frame_generation_presents()`, the setting and `Lossless.dll`): FIFO, not MAILBOX;
four more swapchain images; and the guest output side paints (the pairs' thread, BUG-073)
rather than the UI thread.

---

## BUG-072
**The pairs' readiness counted the compositor's phase as lateness**
Ours (frame generation, PLAN 9.14) · severity high · **FIXED** 2026-10-30 (session 37)

**What it looked like.** Run 431: the pairs reached the panel, 25 ms apart as asked, but
`readiness 101.3 ms`, 127 ms from present to panel, the presenter waiting 12-20 ms for a
swapchain image (p50) and the title at 19 fps.

**Cause.** The timeline's readiness - how long after its present a frame can be on the
panel - grew by half a refresh for every late frame. A frame finished long before and
shown a refresh after its target because of the compositor's phase is not late for want
of readiness (the paced mode's "held" frames, run 296), but it was counted so; the
readiness only ever grew, frames were held further and further ahead, every image was held,
and the title waited for one.

**What it caused.** About 130 ms of latency and the title held to 19 fps.

**Fixed.** As the paced mode's offset: a frame late but finished a refresh or more before
it was taken does not push; one that waited two refreshes or more pulls the readiness in
by a sixteenth of the excess at once (run 432: 35-43 ms).

---

## BUG-073
**A pair's second present waits for the first to be latched**
Ours (frame generation, PLAN 9.14) / Android's buffer queue · severity high · **FIXED**
2026-10-30 (session 37)

**What it looked like.** Runs 432-438: the title at 22 fps with the GPU 80% busy at 550
MHz, where it makes 38-40 without frame generation; the real frame's paint and present on
the guest output thread 33-45 ms on average, the generated one's 0.4 ms. simpleperf (run
438): the guest output thread in `vkQueuePresentKHR` -> `android::Surface::queueBuffer`.

**Cause.** In FIFO, Android's buffer queue takes a second frame only once the first has
been latched, which needs the GPU to have finished it - the title's whole frame and the
generation. So the second present of each pair waited about one GPU frame, on the thread
that feeds the GPU; the GPU ran dry and clocked down. (First taken for the presenter's own
waits: its three paint submissions in flight and three guest output descriptors, for six
images in turn; both raised - eight submissions, six descriptors - which is right for two
paints a guest output but changed nothing measured.)

**What it caused.** The title at 22 fps instead of about 32 with Lossless 2x.

**Fixed.** Upstream's reason for its pacer thread holds: the guest output thread only hands
each guest output with a generated frame over, and a thread of the presenter's paints and
presents the pair and takes the queue's waits (`dao_frame_pacer_android.cc`). Run 439: the
title at 31.8 fps, the GPU 99% busy at 680 MHz, about 60 frames a second on the panel.

---

## BUG-074
**The Thor's pad reports its face buttons by label: the Nintendo prompts' buttons did the opposite**
Ours (input) / the Thor's built-in pad · severity high · **FIXED** 2026-10-30 (session 37)

**What it looked like.** The owner, in play with Button prompts on Nintendo: the Skills page
said "Undo (A)" and "(B) Learn", and pressing B - the bottom button - undid, while A, on
the right, learned.

**Cause.** The input table (warden_input.cc) and the prompts (warden_glyphs.cc) take
Android's BUTTON_A for the bottom face button, as Android's convention and its generic
layout have it, and the Nintendo prompts draw B there. The Thor's pad ("Odin Controller",
vendor 0x2020 product 0x0111) has Nintendo's letters printed on its buttons, and with AYN's
controller setting at its default (`persist.sys.gamepad.type` 0) its firmware reports them
by those letters: the bottom button (B) as BUTTON_B. Its key layout file maps the codes
straight through, so the swap is the firmware's. warden_input.cc foresaw such a pad: "a
per-device quirk keyed on the descriptor".

**What it caused.** Every confirm and cancel - and X and Y - the other way round from the
prompts, in the game and in the options panel.

**Fixed.** `ControllerManager.positional`: for the Thor's pad while it reports by label, A
and B trade, and X and Y, before anything reads the key (GameActivity.dispatchKeyEvent) -
the game and the options panel both get positions, the bottom button the game's A, as the
prompts draw it. The property is read with `getprop` when the pad attaches; another value
is taken for AYN's other mode, by position (not yet seen). The log: `pad N reports its face
buttons by label`.

---

## BUG-075
**The presenter sharpened FSR 3.1's frames again**
Ours (FSR 3.1) / Xenia's presenter · severity medium · **FIXED** 2026-10-30 (session 37)

**What it looked like.** The owner noticed Upscaling (Bilinear, FXAA, FSR 1, CAS) stayed
selectable with FSR 3.1 on, and asked whether both were active.

**Cause.** With FSR 3.1 the guest output is already the panel's 1920x1080, and Xenia's
presenter, given a frame at its output size with Upscaling on FSR 1 or CAS, still runs
CAS's sharpening over it unless the frame says it is sharpened - DAORecomp's flag for its
upscalers' frames, which the graft passed as false. (Bilinear and FXAA do nothing at that
size: FXAA is the dithered bilinear pass, skipped for an 8-bit frame at its own size.)

**What it caused.** FSR 3.1's sharpened frame sharpened a second time, at the cost of a
CAS pass, whenever Upscaling was FSR 1 or CAS.

**Fixed.** The swap passes FSR 3.1's frames as sharpened (`WardenUpscaler::FrameUpscaled`),
so the presenter runs none of its own effects over them; frames FSR 3.1 does not upscale -
menus, loading - keep the Upscaling choice. The options panel greys Upscaling out while FSR
3.1 is chosen, its line saying so.

---

## BUG-076
**An effect with a parameter not yet loaded crashed the render thread (DAORecomp's Ostagar load crash)**
The game (Title Update 6) / found and fixed by DAORecomp · severity high · **FIXED** 2026-10-30
(session 37, ported)

**What it looks like** (DAORecomp's report, 48fd991 and 5c56514): loading the Ostagar save
after Awakening's with buttons held crashed the render thread about half the time - a read
at guest 0x30 in tu6_sub_82698458. Not seen on the Thor yet.

**Cause.** An effect made while its resource still streams in takes its parameter
descriptor from the resource's parameter list, still empty, and stores the cached value -
null the first time; the effect's bind (tu6_sub_82698458) passes each descriptor's field
+48 to the per-parameter bind, and reads null+48 on every draw.

**Fixed.** DAORecomp's hook, ported into `warden_game.cc`: the bind's loop with the null
entries unbound, as the effect's unbind clears every slot (r5-r8 0). The log, once:
`game: effect X has a parameter that was not loaded when it was made - unbound`.

---

## BUG-077
**The pipeline storage written after reading without a seek**
Xenia (DAORecomp's vendored subset) · severity low · **FIXED** 2026-10-30 (session 37, upstream)

**What it looked like** on Windows (DAORecomp's issue #7, e013da4): `Failed to write pipeline
description to storage`, the cache never growing past the first session, the HUD's party
portraits broken until redrawn. Not seen in the Thor's logs: no such message.

**Cause.** `TruncateStdioFile` seeked only when it shrank the file; C's update streams need a
seek (or a flush) between reading and writing, and the storage reads its descriptions, then
truncates and writes. Bionic's stdio is the same family as the one that failed.

**Fixed.** DAORecomp's fix, with its pin moved to eb2360a: the truncation always seeks, and
a pipeline storage too large to be real is reset. This port's own 10.3 seeks (the storage
read from its start, bionic's "a+" starting at the end) stay as they are.

---

## BUG-078
**The deck's launch took the controller's focus to the second screen**
The app (PLAN 8.1) · severity high · **FIXED** 2026-10-30 (session 37, before it shipped)

**What it looked like** (run 454): with the deck up, `dumpsys window` said
`mTopFocusedDisplayId=4` and `dumpsys input` `FocusedDisplayId: 4` - the lower screen,
whose only window (the deck's) cannot take focus. Key events go to the focused display:
the Thor's own controller would have gone dead in the game, and a key waiting there for a
focused window can end in an ANR. The scripted routes inject their presses natively, so
they never showed it.

**Cause.** Starting an activity on a display moves that display to the top - focus with
it - whatever the activity's window can take.

**Fixed.** The deck, once connected to the game (so the game's process is known to be
alive), brings the game's activity back to the front on the default display
(`DeckActivity.giveFocusBack`): the main screen focused again, the game held for 0.0 s,
once. A tap on the deck, its window never focusable (`FLAG_NOT_FOCUSABLE`), leaves the
focus with the game (run 455; run 456: display 0 focused at the title, at the camp, and
after a tap). The Presentation route (`files/deck-route`) launches no activity and never
moved it.

## BUG-079
**The guest output captures ran in every session, one a minute without end**
Our runtime (`warden_gpu.cc`) · severity medium · **FIXED** 2026-10-31 (session 38)

**What it looked like:** the app's files directory held a `guest-output-N.png` for every
minute of the longest session so far - about 4 MB each (run 475's fifteen minutes left 25
of them, about 90 MB); two hours of play would leave about 480 MB, and they were never
removed. Each is a GPU readback of the guest output and a 1920x1080 PNG encode on the
status thread.

**Cause.** The status loop's capture schedule, a bring-up aid (10, 20, 40, 60, 80, 100, 120
and 150 s, then every minute - the boot's screens to read off when the panel showed
nothing), was never limited to test runs; each minute's capture has a name of its own (the
5-second tick), so nothing overwrote them.

**Fixed.** The first eight stay - the boot and the menus, the same eight files at every
start - and the captures stop there unless `files/capture-every` asks for them
throughout; at the start, captures past the eighth left by earlier sessions are removed
(`GPU: N guest output captures from earlier sessions removed (M MB)`).

## BUG-080
**The deck showed the plot items without their icons**
Our app (`DeckArt.java`, PLAN 8.4) · severity low · **FIXED** 2026-10-31 (session 38)

**What it looked like** (runs 468, 480): the Items tab drew every item with its icon but
the Runic Worry Token, a plot item, with none; session 37 put it down as an icon on
neither shared page.

**Cause.** The deck's reader of the shared DXT1 atlas kept only the cells whose names
begin `ico_`; the plot items' 120 begin `plt_ico_` (the token's `plt_ico_commem_coin`,
which the deck named in run 480 once it logged the icons it could not find).

**Fixed.** Both prefixes kept (run 481: the token with its gold coin). An icon in neither
atlas is now looked for as a texture of its own in `gui.erf`, the engine's next place for
an `img://` name, and a name found nowhere is logged once (`deck: no icon for ...`).

## BUG-081
**Closing the game's Select menu froze play for seconds (Lossless 2x)**
Our runtime (`warden_frame_generation.cc`, PLAN 9.14) · severity high · **FIXED** 2026-10-31
(session 38)

**What it looked like** (the owner; runs 482-483): with Frame generation -> Lossless 2x,
closing the in-game menu (Back: the map, the journal, the inventory) held the picture for
seconds before play went on - "a solid 2 second freeze". In run 482, three closes at the
King's Camp, each a present interval of 2.06-2.61 s and then another of about 2 s: 27-43
of the game's frames in the 5 s after a close against about 107. The deck was not it: the
third close came after its process was killed, and froze the same.

**Cause.** lsfg-vk's generator is made for one size of frame (about 2 s: its shaders'
pipelines), and was made again whenever a guest output came at another size. The first
frame after the menu is not upscaled - FSR 3.1 has no image before the UI yet - so it came
at the render size (960x540): the generator was made for it (2 s), and then again for the
next frame, upscaled to 1920x1080 (2 s more). Opening the menu changed no size.

**Fixed.** The generator is made again only once a new size has held for 10 guest outputs
in a row; until then the real frames alone, the generator kept
(`Frame generation: the guest output is WxH now, not ... - the generator made again` when
it is). Run 483: three closes, no generator made, the worst frame 298-320 ms - the game's
own first frame back (it opens its explore HUD again, "GUI open GameModeExplore").

## BUG-082
**The deck's Quests tab showed the journal's markup and bookkeeping**
Our app (`DeckView.java`, PLAN 8.5) · severity low · **FIXED** 2026-10-31 (session 38)

**What it looked like** (the owner): the quests' texts with their `<emp>` tags as written,
and lines like `Completed: 0` and `PlotGroup: pre_plot_group` under each quest.

**Cause.** The tab printed every field of a journal entry as "field: value", and the
texts as they come from the talk table, markup and all.

**Fixed.** The deck's text goes through `DeckView.rich`: `<emp>...</emp>` in bold, any
other tag dropped, line breaks kept; a text that opens with its emphasis (a quest's
objective) has it on a line of its own, as the journal does. Each quest shows its name and
its text only - the open ones first, then the completed, dimmed and marked so (run 486,
the Korcari Wilds: "Tainted Blood", its objective in bold over its description).

## BUG-083
**With DLC installed, the scripted New Game routes went astray**
Our tools (`tools/route.sh`, `warden_xam.cc`) · severity medium · **FIXED** 2026-10-31
(session 38)

**What it looked like** (run 494): the ostagar route reached the test area and stopped in
the prelude's debug conversation; the King's Camp never came, and a traced frame caught a
menu.

**Cause.** With Awakening installed, New Game first shows the campaign chooser (Origins,
Awakening): the route's A for New Game landed there, and each press after it one step
early.

**Fixed.** A route that starts a new game (its `.new-game` file) puts `files/hide-dlc`
on the device, and while it is there the content enumerator lists no downloadable content
(`XamContentCreateEnumerator: downloadable content hidden (files/hide-dlc)`); `route.sh
off` and the next route take it away, so the owner's play sees the DLC. Run 495: the route
at the camp again.

---

## BUG-084
**A native crash in the log's ring buffer during an area load**
Our runtime (the logger, `xe::Logger`) · severity high · **FIXED** 2026-11-01 (session
40; found 2026-11-01, session 38, the areas' tour; seen three times that night)

**What it looked like:** during the tour, stray presses on the world map started a travel
from Denerim's Alienage to Flemeth's hut; the loading screen stayed, and the process was
gone - the Thor's launcher behind it. `build/areas/crash-flemeth-travel.log` holds the
log: `signal 11 (SIGSEGV), code 2 (SEGV_ACCERR), fault addr 0x713a400000` (a page
boundary) in `memcpy_opt` from `xe::RingBuffer::Write` from `xe::Logger::AppendLine` from
`AppendLogLineFormat_Impl` from the port's `ReportLoop()` (a thread the tombstone names
"GPU Commands"), while `al_pre05al_flemeth.rim` and the `ost101d` environment packages
were being read, with thousands of `NtReadFile` lines a second in the log.

**What it causes:** the game ends without a word; the next start reports the native crash
(10.7). Seen three times in one night, each during an area load - three of about thirty
loads in the tour, so the owner's own loads are at risk too.

**Seen again (the same night, read in session 39):** twice more within an hour, at
01:22:41 and 01:27:51, as the debug menu's jumps loaded Orzammar's Noble's Quarter and
Slums (`build/areas/tour.log`: `failed still-loading`; the tour rebooted and the second
try loaded each). The exit reports' tombstones (`files/exits/20261101-0122*`, `-0127*`,
decoded from their protobuf) show the same frames each time - `memcpy_opt` from
`xe::RingBuffer::Write` from `xe::Logger::AppendLine` from `AppendLogLineFormat_Impl`
from `ReportLoop()` - but **the fault address 0x1 both times** (`SEGV_ACCERR`): the
memcpy's destination was address 1, so the ring's write pointer or its buffer was null
(or torn by a concurrent writer), not a write past the ring's end. The first crash's
page-boundary address fits a torn pointer as well. The flood of file-IO logging during
a load is the likely trigger: the logger's ring is written by every thread and read by
its writer thread, and a race on the write pointer is the thing to look for.

**Cause (session 40).** Not the ring's write pointer: the first crash's registers say
it. The `memcpy` was copying from `x1 = 0x713a3fffb0` with `0xa288` bytes still to go
and faulted at `0x713a400000`, the page after the report thread's 64 KiB
`thread_log_buffer_` - the *source* overran, not the destination. A line is formatted
into that thread-local buffer by `AppendLogLineFormat_Impl` (`xenia/base/logging.h`)
with `fmt::format_to_n`, whose `result.size` is the length the whole line would have
had, not what fit; `AppendLine` then copied `result.size` bytes out of the 64 KiB buffer.
The crashing frame is the template's `std::string` instantiation, so the line was one of
`ReportLoop()`'s single-string reports: `signals and blocking waits since the last
report`, which lists every wait and signal site with its count (`RecentSyncReport`) and,
during an area load with its thousands of reads and completions, grows past 64 KiB. The
page past the buffer differs run to run (a guard page here, a torn mapping there), which
is why the fault addresses varied.

**Fix.** Two edits to the logger recorded in `tools/assemble_runtime.py` (BUG-084): the
length appended is never more than the thread buffer holds, in the format path and in
`LoggerBatch::submit` alike - a line longer than 64 KiB is cut short, the process lives.
And the port's two reports (`warden_trace.cc`) are logged in pieces of at most 16 KiB,
split at a line, so none is cut.

---

## BUG-085
**The Arl of Denerim's estate renders black**
The game's own state at the debug jump · **NOT OURS** (session 40: the reference renders the same)
(found 2026-11-01, session 38's tour; read in session 39)

**What it looked like:** the tour's jump through `zz_den_debug` ("Jump to area" -> "Jump
to Arl of Denerim's Estate" -> "Outside of the estate"; the last area file read
`al_den510ar_arl_estate.rim`) landed in explore mode in a black scene: the player drawn
and lit, the HUD, and when the camera turns a row of a few lit props (stones or crates),
nothing else - at six times the brightness still nothing, so no geometry is drawn rather
than a dark lighting state. 80-87 fps uncapped against 40-68 everywhere else, the GPU at
99% all the same. Screenshots: `build/areas/den-howe-estate-*.png`,
`build/areas/sheet-dark-check.png`.

**What it causes:** if the estate renders like this in play, the Landsmeet's "Rescue the
Queen" quest is unplayable on the Thor. If only the debug jump does (the exterior before
its plot sets the area up, or a night variant with its lights scripted), nothing.

**To do first:** the same jump in DAORecomp's Windows build (`python
tools/dao_reference.py run ...`, the game's console or the same debug script); if the
reference draws the estate, it is ours - then a frame trace there (`files/trace-swap`)
for the draws that are missing or culled, and the area's render targets.

**Session 40 - what it is not.** The draws are not failing: the status lines count about
143 draws submitted a frame at the estate against about 400 where areas render, none
failed or skipped - the engine does not submit the level's geometry. Not a missing file
(every environment chunk near the player, `den005d_0..18`, opens and reads; the one
failure, a driveless `den005d.bin`, every area makes). Not the occlusion-query culling
(DAORecomp turns it off - Xenia's ZPD counts are unreliable - and the port now does too,
`warden_game.cc`: 174 draws, doors and crates now floating in the black, the level still
missing). Not the waypoint: the debug jump uses `den510wp_from_arl_exterior`, which the
area has. The player cannot move there (no walkmesh underfoot) and the minimap shows them
at the map's left edge. The area's environment is `den005d` (its `.are`); the City Elf's
estate exterior uses `den012d` and draws. Left: the reference build with the same jump
(its patched executable, `DAO_RUN_SCRIPT`, is built; installing it was refused by the
permission classifier - the owner's call), or a traced frame of the estate.

**Resolved (session 40): not ours.** With the owner's permission the patched reference
build was installed (`DAO_RUN_SCRIPT`, the original kept as `DAORecomp.exe.orig`) and
given the same jump from the owner's camp save: `DAO_RUN_SCRIPT=zz_den_debug@10 python
tools/dao_reference.py run howe2 150 --input "20 START; 38 A; 56 DOWN 0.2; 58 A 0.2;
61 DOWN 0.2; 63 A 0.2; ..." --capture 2`. DAORecomp's Windows build shows exactly the
Thor's picture - black, the row of doors floating, the player at the minimap's left edge
(`build/reference/howe2/`). The debug jump places the player outside the estate's level;
in play the estate is entered through its doors from the market, at other waypoints.
**On the way, the occlusion-query culling:** DAORecomp turns the game's occlusion-query
culling off (its `gpu/d3d_queries.cpp`: Xenia's ZPD counts are unreliable, objects
vanish for a frame at 60 fps); the port now does the same (`warden_game.cc`, the engine's
own switch at `0x832E2404` after `sub_8266F388`; `files/occlusion-queries` keeps them).
Measured at the King's Camp uncapped: 38.9 fps and 1,280 draws a frame without the
queries, 37.6 and 2,095 with them (the query boxes are draws of their own; runs 542-543).

---

## BUG-118
**A pushed game code's manifest never written**
Our tools (`tools/push_gamecode.py`) · severity low (development only) · **FIXED** session 44

**What it looks like:** after `python tools/push_gamecode.py` the launcher says "Ready to build"
and Build again, though the library is the computer's own for this APK.

**Cause:** the manifest (`files/gamecode/manifest.json`, the ABI the launcher compares) was written
with `run-as <pkg> sh -c 'echo <json> > ...'`, the JSON quoted by `shlex.quote` inside the single
quotes: adb's shell, run-as and sh -c split it, so the echo printed to the screen and the file kept
the last device build's ABI. And when the library was already there the script returned before
the manifest at all.

**Fixed:** the manifest is written to a temporary file and pushed like the library, whether or not
the library had to be (the script prints `manifest: ABI <hash>`).

## BUG-119
**Put away, the game kept a core busy: the render thread spun in half the lid-closes in play, and the app polled on**
Our runtime (`warden_suspend.cc`, `warden_xma.cc`) and app (`Deck.java`, `TouchControls.java`);
the title's render thread · severity high · **FIXED** session 46 (the owner's ask, 2026-11-09)

**What it looks like:** battery drain with the lid closed or the screen locked and the game open.
Measured on the Thor over USB (`tools/put_away_cpu.py`; over USB the device never suspends - the
USB controller holds a wakeup source - so these are the costs whenever it is awake: each time it
wakes by itself with the lid shut, and all the time with the game behind another app):

| DAO Debug, the Thor | Before | After |
|---|---|---|
| The launcher, no game started (the published 0.2.1's, in the background) | 7.7% of one core | 0.0% |
| The title screen, locked | 28.9% | 1.4% |
| In play, locked (lock-ups the render thread spun in) | 93.5-93.9% (4 of 7) | 1.5-1.7% (the spin held by signal in 5 of 6) |
| In play, locked (the others) | 1.1-1.3% | 1.5-1.7% |
| The options panel open in play | - | 4.0% |
| The title screen, playing (unchanged, as meant) | 47.8% | 48.2% |

The in-play "before" is the session's first build, with the polls below already gone: the
original code had those on top.

**Causes**, by size:
- **The render thread's spin (the title's).** `DragonAge::Render` empties its job queue in
  `sub_82682BF0`, then loops - no call in the loop - on a "batch done" byte the main thread
  sets. A suspension holds the main thread at its next wait; caught mid-frame, the byte is never
  set, and the render thread ran a whole core until the game came back (simpleperf: 99.7% of its
  time in that function). A suspension point never reaches it, as it makes no call.
- **Xenia's two spin-waits** (BUG-058): about 10% of a core each with nothing to do, more locked
  than playing.
- **The XMA decoder** (`warden_xma.cc`; its thread inherits the name `Eclipse::Update`): woke every
  2 ms to scan its contexts, 2.6%.
- **The companion deck** (a Presentation): its pull went on every 100 ms while hidden - a snapshot
  from the game, and the party tab's text rebuilt with regular expressions on the main thread
  (2.8% main thread, 1.7% `deck-pull`).
- **The on-screen controller's option check**, twice a second on the main thread.

None of them held a wake lock or kept a sound stream open (`dumpsys power`: none of the app's;
`dumpsys media.audio_flinger`: every output in standby), so a Thor on battery still suspends -
they cost while it is awake, and a spinning core makes each waking longer and hotter.

**Fixed:**
- **Held by signal** (`warden_suspend.cc`): the title's threads register as they start
  (`RegisterTitleThread`, by kernel thread id). A watcher thread (`warden-hold`), a second into
  each suspension and every 2 s after, signals (`SIGRTMIN + 6`, by `tgkill`) any of them that used
  over 60% of a core in half a second. The handler parks the thread on a futex until the resume -
  only if the signal finds it in the title's translated code (`libdaogame.so`'s executable
  segments), which takes none of the runtime's locks; elsewhere it returns, and the thread holds
  at its next suspension point. The log: `suspension #N: [DragonAge::Rend] still running (98% of
  a core) - held by signal`. Six lock-ups in play, then walking on: the game played on as before.
- Xenia's spin-waits nap while the host is idle (BUG-058).
- The XMA decoder waits 100 ms at a time while the guest is suspended (a kick still wakes it).
- The deck's pull looks twice a second while put away, and pulls and draws nothing.
- The on-screen controller stops its checks from `onPause` to `onResume`.

The game code's ABI is unchanged (no override was added): no Build again.

**Not measured:** the battery itself with the lid shut. That wants the cable out: the charge
counter before and after half an hour, with and without the game open.

## BUG-117
**High temperatures during play (issue #2)**
Our runtime (`warden_threads.cc`, `warden_trace.cc`) and the title's own waits · severity medium ·
**FIXED** sessions 43-44 (the owner's choices)

**What it looks like** (issue #2, 2026-11-03: 65-70 C; the owner: about 80 C): the Thor hot in play.
The game's own reading at the King's Camp (DAO Debug: 120 fps, FSR 3.1 off, Lossless 2x, 1360x768,
FXAA): big cores 94-95 C, the GPU 87-90 C at its 680 MHz ceiling, the board 64-71 C.

**What uses the CPU** (run 697, per thread from /proc, simpleperf as the app): the game process
3.2-3.3 cores, two threads at about 99% each:
- **DragonAge::Render** - the title's Direct3D waits for the GPU by spinning. sub_82DA16F0, its
  BlockOnFence, loops until the read pointer the command processor writes back passes the fence,
  calling sub_82D8E218 each time round: db16cyc delays at low priority (cctpl), which gave the
  Xenon's core to its sibling hardware thread, and a 5000-tick check. Translated, the delays are
  nothing: a big core flat out whenever the game waits on the GPU, most frames.
- **Eclipse::Update** - the main thread's hand-off to the render thread (sub_825596E0) sets a flag
  and polls, with a zero-timeout event wait and Sleep(0) (KeDelayExecutionThread, 0 ticks), until
  it is answered; Sleep(0) was sched_yield, which returns at once when the core is free.
- **The guest trace** (warden_guest_trace, every translated function's prologue): 22% of the two
  threads' cycles, 55% of the render thread's with the dynamic linker's thread-local lookup
  (tlsdesc_resolver_dynamic) - the library is opened at run time, so each entry's ring is found
  the slow way.

**Fixed - Options -> Heat guard** (session 44, the owner, 2026-11-04: each way of running cooler
the player's own, at its cost): a section of three options, all live. The two rests are **on by
default** (the owner, after the measurements below: no frame rate lost; a file holding `off` turns
one off), the frame-rate guard off.
- **Rest the CPU in the game's waits** (`files/heat-cpu-rest`): a Sleep(0) or NtYieldExecution
  within 200 us of the last, from the eighth in a row, naps 30 us instead of yielding
  (`YieldOrNap`); a lone one between pieces of work still yields. The answer is seen up to a nap
  later: 41.8 game frames over 50 ms a Korcari Wilds run against 37.4 (runs 704-717), so it says
  it can add a little stutter.
- **Rest the CPU while the GPU draws** (`files/heat-gpu-wait`): an override of sub_82D8E218 that,
  when the pass says go on waiting, sleeps on a futex the command processor bumps where it writes
  the read pointer back (command_processor.cc, after `store_and_swap` of read_ptr_writeback_ptr_),
  at most 2 ms; the loop then checks the fence again. An override, so the game code's ABI changes
  (74aa2ee2adfcb1e2 against 1961314d8a1bb6ae): every player's Build again once, about eight
  minutes.
- **Lower the frame rate when hot** (`files/thermal-guard`, PLAN 10.2's guard) now steps at the GPU
  75 C or a big core 88 C, lifting at 62 / 72: at 90 C on the GPU only, it never acted with the
  fan running. Run 731 (120, frame generation off, the rests off): 120 -> 60 -> 45 -> 30 in 90 s,
  each on a big core over 88 C - but held at 30 the big cores stayed at 84.3 C (the hottest 94.6),
  the spinning waits filling the idle time, so it never lifted. Its text says so: best with the
  rests.
- The guest trace is recorded only in Debugging Mode (`WARDEN_DEBUG_MODE`) or with
  `files/guest-trace` (always, not an option): otherwise the entry returns before the ring. Its
  readers are the fault log and the thread reports only.

**Measured** at the King's Camp, 12 minutes each and the last four read (heat levels out slowly),
the owner's fan at a fixed 50% over 45 C, DAO Debug at 1360x768, FXAA, FSR 3.1 off. The device's
power from its USB-PD input with the battery full:

| Runs | Settings | Game fps | 1% low | Big cores (hottest) | GPU | Board | Power | CPU |
|---|---|---|---|---|---|---|---|---|
| 720 / 719 | Frame rate 30, Lossless 2x: nothing / CPU rest | 15.1 / 15.1 | 13.6 / 14.2 | 71.1 (82.4) / 53.8 (57.7) | 61.3 / 50.3 | 50.1 / 42.9 | (charging) | the main thread 105% / 23% |
| 721 / 722 / 723 | 120, Lossless 2x: nothing / CPU rest / both | 29.5 / 29.7 / 30.0 | 23.8 / 26.5 / 26.7 | 84.2 (94.5) / 83.6 (93.9) / 73.0 (78.4) | 78.3 / 78.1 / 70.2 | 60.7 / 60.7 / 55.4 | (charging) / 9.77 / 9.07 W | the render thread 98.5% / - / 24.9% |
| 728 / 729 / 730 | 120, frame generation off: nothing / CPU rest / both | 40.3 / 40.5 / 40.5 | 25.2 / 31.8 / 34.9 | 85.8 (94.9) / 84.3 (94.5) / 80.5 (85.4) | 80.5 / 79.9 / 76.0 | 62.4 / 62.2 / 59.7 | 10.42 / 10.61 / 10.00 W | 3.05 / 2.63 / 1.87 cores |
| 738 / 739 | 30, frame generation off: nothing / both | 30.0 / 30.0 | 22.5 / 27.5 | 84.2 (94.6) / 69.6 (73.6) | 75.0 / 65.5 | 59.8 / 53.5 | 10.49 / 7.78 W | 2.81 / 1.57 cores |

Stutter (game frames over 50 ms, the log's `frame times: a title frame of`): at the camp, the last
four minutes, 11 -> 0 at 30 (runs 738/739) and 2 -> 0 at 120 (728/730). The Korcari Wilds route
at 30, frame generation off (runs 740-743, nothing / both / nothing / both, about 130 s of walking
and the wolves' fight each): 34 / 33 / 35 / 39 frames over 50 ms, none over 100, the worst 93 / 87
/ 89 / 99 ms; 30.1 fps throughout, the 1% low 20.6 / 20.6 / 19.7 / 21.0 - the same within the
runs' spread. **The log stops at 200 lines a session:** at a game rate of 15 (Frame rate 30 with
Lossless 2x) every frame is over 50 ms and the 200 are gone on the title screen, so runs 719-720
and 724-727 counted nothing - their "no frame over 50 ms" meant nothing. Check the count stays
under 200 before reading one.

So neither rest costs frame rate - the 1% lows rise with them - and together they take the hottest
big core down 9.5 C uncapped, 16 C with Lossless 2x and 25 C at Frame rate 30 (the CPU rest alone there). The CPU rest alone does most where the frame rate is capped (the main
thread otherwise spins the idle time away); the GPU rest where the GPU is the limit. With Lossless
2x locking the game to 30 at the camp (each frame two of the title's 60 Hz vblanks), the GPU has
idle time and the render thread's wait is long, so the GPU rest gains most there.

**Why a frame-rate cap alone barely cools** (the owner: Frame rate 30 did not help much): the
spinning waits fill the idle time a cap makes - at 30 without the rest, the main thread 105% of a
core and the prime core at 3187 MHz (1843 with it).

## BUG-116
**The game's volume not fully following Android's**
The device's firmware (AYN Thor) and our app (`GameActivity.reportMediaVolume`, BUG-109's
workaround) · severity high · **FIXED** session 43 (the owner chose the fix)

**What it looks like** (the owner, 2026-11-03): with Android's media volume at its top, holding
volume-up makes the game louder still - most of all after putting the game away, muting, and
coming back at a partial volume.

**Cause - AYN's second-screen volume.** The Thor's firmware gives apps on the second screen a
volume of their own. AYN's Game Assistant (`com.ro.gameassistant.service.MultiDisplayVolumeControl`)
lists the uid of every app with a task on the second display in the property `sys.audio.uids`
and calls `setStreamVolume` to have it applied; AYN's `libaudioflinger.so` then gives those
uids' sound the second screen's level (`Settings.System secondary_screen_volume_level`) in
place of the media volume. AYN's `PhoneWindowManager` also broadcasts each volume key
(`action.volume.key.event`) and skips its own handling while `persist.sys.volume.skip` is 1.
The deck is an activity on the second screen, so the game's uid is listed (run 689:
`sys.audio.uids` 10164, DAO Debug's) - that is BUG-109's cause: the media volume does not reach
the game's track because the second screen's level is applied to it instead.

Measured (AudioFlinger's power log of the primary output, before the hardware):
- **Listed, media 15:** second screen 15 -41.6 dB, 8 -54.1 dB (Android's curve for 8 is -13),
  0 -49.3 dB, 15 again -41.3 dB. The app's own gain (BUG-109) follows the media volume in every
  step, including a resume (run 689's steps), and is 1.0 at the top.
- **The owner's case:** muting with the volume key while the game is away (not listed) drags the
  second screen's level down to 0 with the media volume; raising the media volume after does not
  raise it again (media 3, second screen 0). Back in the game, the game's sound has the media
  volume (the app's gain) and the second screen's lower level too; held volume-up at the top
  raises the second screen's level - the "louder than the top".
- **The deck off (not listed):** Android applies the media volume and the app applies it again:
  15 to 8 drops the game 27.4 dB, not 13.
- After a resume by `am start` the deck did not come back (the launcher's own second-screen
  activity in front), so the game was not listed: the same double volume until it returns.

**Fixed** (the owner's choice of two - the other, the app's gain chosen by whether the game is
listed, could not undo a second-screen level of 0): the deck is a Presentation, a window of the
game's activity on the second display (PLAN 8.1's Route B, made the default; `files/deck-route`
holding `activity` gives Route A back, as a test switch). Game Assistant lists apps with a
*task* on that display, so the game is never listed, and Android applies the media volume to it
as to any app; the app's own gain (BUG-109) is kept only for Route A. The Presentation now loads
the game's typefaces and HUD art as DeckActivity does, and is made afresh when its display went
(a wake). Run 691: `sys.audio.uids` empty throughout and no app gain; media 15 to 8 drops the
game 14.2 dB (Android's curve, once); the owner's case - away, held down to 0 (the second screen's
level dragged to 0 with it), up three, back - the game at media 3, then normal at 15; volume-up
held 4 s at the top: no louder (-42.5 dB against -40.4); the deck back after Home and after sleep
and wake. Frame times as with Route A (runs 688, 691: about 370 frames in 5 s, p95 27 ms). The
cost: the deck runs in the game's process - a fault in it closes the deck (its updates are
caught), but no longer in a process of its own (PLAN 8.7's reason for Route A).

## BUG-115
**The deck's map upside down in some areas**
Our app (`DeckView.MapView`, PLAN 8.4) · severity medium · **FIXED** session 43

**What it looked like** (the owner, 2026-11-03, the published app at the King's Camp,
`ost000t`): the deck's map was the game's map screen turned half round - the bridge to the
other half of the ruins on the wrong side.

**Cause.** The game's map screen (`map.gfx`) sets the map's `_rotation` to 0, 90, 180 or 270
for `Area.North` 0 to 3, and Flash turns clockwise. The deck turned the map by the same quarter
turns anticlockwise: the same for North 0 and 2, half a turn wrong for 1 and 3. Run 471, which
checked the King's Camp (North 3), compared an east-west road and where Jaden stood - both of
which a half turn keeps.

**Fixed.** The deck turns clockwise. Measured on the screens (normalised cross-correlation of
edge images over the eight turns and mirrors): the owner's screenshots, the deck against the
game's map screen, matched only turned 180 degrees (0.71, the rest 0.29-0.38); after the fix,
run 684 at the King's Camp matches the game's map screen unturned (0.77, the rest 0.29-0.36).

**Six more areas (the owner asked), run 685,** by motion: the hero walked forward for a second,
the HUD minimap's slide (minimap.gfx turns the map by North as map.gfx does, and shows it
explored or not) against the deck's player icon. The angle between the two: the City Elf
alienage (North 3) -2 degrees, the City Elf home (3) -5, the arl's estate exterior (1) +7, the
Tower of Ishal's level 3 (1) +2, Denerim's market (2) 0, Orzammar's commons (0) -2 - a map still
half a turn out would show 180, a quarter turn 90. The walks were short (about 5 minimap
pixels), so each angle is good to about 10 degrees. North from the areas' own files: the 360's
`.are` is GFF 3.2 little-endian (`ARE V3.2`), North a top-level field - 312 of 352 areas 0, 10
are 1, 14 are 2, 16 are 3; the 26 odd ones were upside down. Not usable for this: the game's
map screen right after a jump (it opens on the last tab used, and a fresh area is mostly
unexplored) and matching the minimap's picture (the camera cone covers it; it chose a wrong
turn on the King's Camp, whose answer was known).

## BUG-114
**The launcher's update failed after Android's Install**
Our app (`AppUpdate.java`, PLAN 13.1) · severity high · **FIXED** session 43, in 0.1.5-alpha

**What it looked like** (the owner, 2026-11-03, 0.1.3 to 0.1.4 on the published app): the
download verified, Android's dialog asked, the owner tapped Install - and the row said the update
failed, the install cancelled. Three times running. (0.1.2 to 0.1.3, the same code, had worked.)

**Cause.** BUG-111's fix. Android's first dialog (allow installs from this app) says nothing to
the session when cancelled, so the launcher, back in front with the dialog shown, abandoned the
session. But it is also back in front the moment the player taps Install, while Android installs:
the log has `Integrity check passed for vmdl...tmp` and, 30 ms later, our `INSTALL_FAILED_ABORTED:
Session was abandoned`. Whether Android's process kill or our resume came first decided it.

**Fixed.** Back in front, nothing is abandoned: if no answer has come after 30 s and the
session's progress shows no work of Android's (the APK written in is 0.8 of it; a cancelled
dialog leaves 0.8), the row offers Try again, and only that tap gives the old session up. The
installer's answers are taken only for the current session (Try again's abandon of the last one
answered during the new download). Checked with DAO Debug updating itself in place from a local
feed (a test build: the published package's check and https bypassed, never committed), run 681:
the first-time dialog cancelled - Try again after 30 s; allowed, Update tapped - `back from
Android's installer - waiting for its answer`, Android's `installPackageLI` 0.8 s later, the app
0.1.4 to 0.1.5.

**Players on 0.1.2 to 0.1.4** still have the old code: their launcher's update can fail this way
(trying again may win the race), or they install the APK from the releases page by hand, over the
app, keeping everything.

## BUG-113
**With FSR 3.1 the upper corners of the world juddered**
Our runtime (`warden_upscale_composite.cs.glsl`, `warden_framegen_ui.cs.glsl`) · severity
medium · **FIXED** session 43

**What it looked like** (the owner, 2026-11-03): at the King's Camp, the camera still, the
scene in the upper left and upper right corners shook; the HUD and the centre did not. With
frame generation on or off alike.

**Cause.** The game darkens the corners of the frame after the image FSR 3.1 is given (a
vignette, drawn with the UI). The composite finds the UI by comparing the final frame with
that image, and took any difference over 2/255 as UI - so in the corners it pasted the final
frame, which is the render-size frame moved by FSR 3.1's jitter: up to a render pixel each
frame, with nothing to take it out. `files/fsr3-show-ui` painted both corners red (run 672).
Frame generation's paste of the real frame's UI onto the generated frames (PLAN 9.14) found the
same "UI", so there the corners stepped at the game's rate as well. Measured on a 6 s recording
of the still camp (the mean change per frame of the outer 40-px bands, 0-255): FSR 3.1 on, the
left bands 0.4-0.7, the right 0.8-1.4, the top 1.0-1.1, the centre 0.07 (runs 668 and 670,
frame generation on and off); FSR 3.1 off, every band 0.01-0.15 (run 669).

**Fixed.** A pixel where the final frame is the captured scene times one grey factor (within
2.5/255 in each channel, the scene not near black) is a shade, not UI: the composite puts the
factor on the upscaled scene, and frame generation's paste puts it on the generated scene
(`Over`). Anything else that differs is UI as before. Run 673: the corners blue (shade) in
`fsr3-show-ui`'s view, which now paints a shade blue, the HUD still red. The same recording
after the fix (runs 674 and 676, frame generation on and off): every outer band 0.01-0.19, the
centre 0.11-0.16 - the same as with FSR 3.1 off (run 675).

Two things tried first did nothing, because the real frames the dumps hold are the generator's
sources, without the vignette or the HUD: lsfg-vk's samplers clamped to the edge instead of a
border colour, and the generator's sources padded by 96 px (both reverted).

## BUG-112
**With FSR 3.1 chosen but its upscaler not made, the whole world juddered**
Our runtime (`warden_upscale.cc`) · severity medium · **FIXED** (the fallback) session 43;
the driver loading it was found with dropped

**What it looked like** (the owner, 2026-11-03): with Mesa's Turnip loaded as the game's Vulkan
driver (a feature built this session and then dropped - DECISIONS D16), the world shook
everywhere under a still HUD.

**Cause.** FSR 3.1's SDK failed to make its Vulkan backend on that driver (its memory type
lookup), and the upscaler was never made - but the jitter the game's projection had been given
stayed on, moving the scene by up to a render pixel each frame with no upscaler to take it out.

**Fixed.** `Unavailable()`: any failure to make the backend or the upscaler turns the jitter
off as well (`FSR 3.1: unavailable - the scene is drawn without its jitter, not upscaled`).
Kept, as it applies to any device where the SDK fails. With it came the SDK's failing Vulkan
calls logged by name (`FfxLogged`), the device capabilities the SDK chooses its passes by, and
the GPU status line's new counts (the host's waits for the GPU, the passes and barriers, the
submits and presents with their time - `docs/TESTING.md`), from the frame-rate investigation
that followed: Turnip ran the game about 16 ms a frame slower than the system driver at any
resolution, with no host waits and the GPU 78% busy, which is its serializing of Xenia's ~57
render passes and ~115 barriers a frame; dropping the buffer barriers gained 2.3 ms, no more.
The owner dropped the custom-driver feature on that.

## BUG-111
**The update row stuck on Installing after a cancel**
Our app (`AppUpdate.java`, PLAN 13.1) · severity medium · **FIXED** session 42, before release

**What it looked like** (run 615): Cancel on Android's "not allowed to install unknown apps"
dialog, and the row stayed on `Installing…`, taps doing nothing.

**Cause.** That dialog tells the session nothing when cancelled, so no status ever came.

**Fixed.** With the dialog shown and the launcher back in front, `AppUpdate.resumed` abandons the
session and offers Try again. Run 617: `INSTALL_FAILED_ABORTED`, the row on Try again, and a
second download went on to install.

## BUG-110
**Back from sleep, the second screen showed the launcher and not the deck**
Our app (`Deck.java`, PLAN 8.1) · severity medium · **FIXED** session 42

**What it looked like** (the owner): resuming a locked game, the lower screen held the home
app. Home and back did not bring the deck back.

**Cause.** `GameActivity.onStart` runs before the second display is on: a lid wake logged
`deck: back with the game` and, a millisecond later, `deck: no second display`, as
`findDisplay` asks for STATE_ON. That was the only try. `Deck.hide` also set `hidden` only when
it found a deck to put away, so with none there `show` did nothing: Home and back did not help,
nor after the deck's process died (BUG-106's end).

**Fixed.** `Deck.show` waits for the display with a `DisplayListener` when the deck did not open
(and looks once more after registering it); `hide` always marks the deck as wanted back.

The first fix asked `findDisplay` a second time before deciding to wait: the display could come
on between the two, and the deck neither opened nor waited (found by the first of three test
rounds). Run 616: 20 of 20 sleep and wake rounds at gaps from 0.3 to 6 s, with `input keyevent
KEYCODE_SLEEP` and `KEYCODE_WAKEUP`, the deck having to wait in 17; the deck's process killed,
back with Home and back.

## BUG-109
**The game's sound did not follow Android's media volume**
The device's firmware (AYN Thor), worked around in our app (`GameActivity`,
`warden_audio.cc`) · severity high · **WORKED AROUND** session 42; the cause found and fixed
as BUG-116 (session 43): AYN applies the second screen's own volume to any app with an activity
on the second screen, and DeckActivity put the game there. The deck is a Presentation now and
the workaround below is kept only for that route's test switch.

**What it looked like** (the owner): tapping the volume buttons moved Android's slider but
not the game; at media volume 0 the game could still be heard. Holding a button seemed to
change it (not reproduced).

**Cause, as far as it is known.** Android does its part: the volume events reach
STREAM_MUSIC, the policy has this track in the Music group, and `dumpsys media.audio_flinger`
shows stream 3 at -inf, -51, -42, -37, -30 for index 0 to 4. But the track's final gain (`G
db`) stays at -2.6 at every index, against the -inf of tracks that are muted. It is not the
usage: `AAUDIO_USAGE_MEDIA` behaves the same (run 612). What on the Thor detaches it is not
known. The candidates: `com.odin.settings` owns an Equalizer on session 0 of this output
that declares it implements volume control, and this firmware's AudioFlinger logs moving an
effect from the app's session to session 0 as the stream opens (`admin: createEffect sessionId
0: sessionId_old <the app's>`); `com.odin.gameassistant` and `com.odin.settings` also call
setStreamVolume themselves. Other apps on the Thor were not compared.

**Worked around.** On devices whose manufacturer is AYN, `GameActivity` reads the media volume
(`VOLUME_CHANGED_ACTION`, the mute broadcast, a resume, an output change) and gives the native
side Android's own curve for that output (`getStreamVolumeDb`), applied after the boost and
ramped across a frame (`SetMediaVolume`). Not over Bluetooth, where the headset takes the
volume. Elsewhere nothing changes, as Android's own volume would be applied on top.

**Checked** (run 613, by the output's Signal power history): index 15 to 8 is 13.0 dB three
times over, one gain and not two; tapped and held keys move the gain step by step (0.0076,
0.0146, 0.0316 ...); at index 0 the power log stops, the output silent. Not checked: wired,
USB and Bluetooth outputs.

**A conversation's start froze the game for nearly 4 s**
Our runtime (`warden_frame_generation.cc`, PLAN 9.14) · severity high · **FIXED** session 42

**What it looked like** (the owner's report, session 42): "the game hitches a lot when
starting conversations", in the Diamond Quarter at Orzammar, with FSR 3.1 and Lossless 2x.

**Cause.** The frames without a 3D scene are not upscaled, so they reach frame generation at
the render size (1120x630), not the panel's. BUG-081 made the generator again once a new size
had held for 10 outputs; BUG-092's first frame was one such frame. Some conversation starts
draw no scene for ten or eleven outputs, while the game streams the area's parts around the
camera (`orz300d_2_4_0.rim` ...). Run 607: the generator made at 1120x630 (1,786 ms), and the
scene's return at 1920x1080 made it again (2,113 ms) - the game held behind both, as a title
frame each. The conversation in the owner's report had nine outputs, one short, so its log
shows a mild start: the pacing's switch only (`kPacingGapOutputs` was 8). A menu open for ten
outputs met the same rule (BUG-081).

**Fixed.** The generator is never made again for a smaller size: those frames are shown real,
and the pacing keeps its pairs for up to 30 outputs. Run 608, the same conversation from the
same save: no rebuild and no pacing switch, 11 outputs without a scene in 193 ms, the panel's
longest gap 33.8 ms against 2,135.7. A size that grows still makes it again once, after the
first load, and that is the loading screen's cost.

## BUG-107
**The deck's Quests tab showed no completed quests, and subplots as quests**
Our app (`DeckView.java`, `warden_deck.cc`, PLAN 8.5) · severity medium · **FIXED** session 42

**What it looked like:** a quest finished left the deck for good, and a quest's subplots
stood beside it in one flat list.

**Cause.** The tab read `Journal.PlotList` and took its `Completed` field for the
journal's Completed Quests page. journal.gfx keeps that page in a list of its own,
`Journal.CompletedPlots`; `Completed` on a current plot marks a subplot done. Its
QuestGroup lists only the plots with no `ParentPlot`, and folds a subplot's text into its
parent's (AddSubplotDescriptions).

**Fixed.** PLAN 8.11: the tab has the journal's two pages, its groups and its subplots.

## BUG-106
**The deck "isn't responding", and gone with Close app**
Our app (`DeckActivity`, PLAN 8.1) · severity high · **FIXED** session 42

**What it looked like** (the owner, session 42): the second screen's deck vanished, the
game playing on. Before it, Android's "isn't responding" dialog on that screen.

**Cause.** BUG-078 again, by another door. The deck's window never takes focus
(`FLAG_NOT_FOCUSABLE`, so that a tap leaves the pad with the game), and the fix there gave
the top display back once, as the deck connected. Anything later that leaves the second
screen as the top display - on the Thor its dual-screen assistant, seen opening and
closing before the first dialog; in run 602 another app finishing there - leaves a focused
application with no focused window. The next key is held for it, and after 5 s Android
calls the deck not responding (`Input dispatching timed out (Application does not have a
focused window)`). Wait only starts the same 5 s again, as the key is still held. The
dialog takes the focus, so a pad button can press its first choice, Close app:
`dumpsys activity exit-info` has the deck's end as `reason=6 (ANR)`, `user request after
error`. Here the held key was an `adb shell input keyevent` of ours; a pad's press does
the same. (`adb shell input keyevent` has no place in a test while the deck is up.)

**Fixed.** `DeckActivity.onTopResumedActivityChanged`: each time the deck comes to the top
it asks the game's process whether the game is in front (`DeckService.GAME_IN_FRONT`) and,
if it is, brings it forward as BUG-078's fix does. Never over Home.

**Checked** (run 602): Settings opened on the second screen and closed with Back -
`deck: this display came to the top - back to the game`, `FocusedDisplayId: 0`, the same
key sent, no ANR and no dialog. With the game at Home, the same steps left it there.

## BUG-105
**Every codex entry on the deck: "the entry's text is not read yet"**
Our app (`warden_deck.cc`, `DeckView.java`, PLAN 8.5) · severity medium · **FIXED** session 42

**What it looked like** (the owner): that line under every entry, read in the journal or
not.

**Cause.** `Journal.CodexGroups.G.CodexList.E.Descriptions` is no text but a list:
journal.gfx's UpdateDescription takes `.Descriptions.N` in order, cuts each one's
`<summary>...</summary>`, and joins them with a blank line. The deck read the list's own
node, always empty. Reading the entry in the game had nothing to do with it.

Reading the pages alone would have met three more limits. `EngineString` refused anything
over 4,096 characters; it turned line ends into spaces, the snapshot's lines being its
separators, and so the paragraphs into one; and all the entries' text in the snapshot
(335 entries, about 1.5 kB each, on top of its 100 kB, twice over as UTF-16 in the parcel)
would pass Binder's limit late in the game.

**Fixed.** PLAN 8.11: the deck reads the pages of the entries it has open, the data model's
texts up to 64 kB with their line ends (U+E002 in the snapshot, `DeckSource.LINE_END`).
Run 602: Orzammar Politics word for word as the journal has it, paragraphs included.
Run 603: The Korcari Wilds, unread, on a new character that had never opened the journal -
an entry's pages are there once it is unlocked.

## BUG-104
**A test route's files stayed in the owner's app: New Game a debug start, the DLC hidden**
Our test tools (`tools/route.sh`) · severity high · **FIXED** session 41

`tools/route.sh <name>` puts a route in the app's files - `route` (what the game's
script calls do), `inject-pad` (timed presses) and, for most routes, `hide-dlc` (the game
sees no downloadable content, so New Game asks no campaign) - and leaves them there for
every start, by design, until `bash tools/route.sh off`. After run 598 (the ostagar route,
2026-11-02 16:29) that was not run.

**What it causes:** the owner's own game, from then on - New Game "loads directly into the
debug start instead of character creation" (the route's `call zz_pre_debug 0`) and "can't
see" the DLC from disc 2 in the main menu (`hide-dlc`), though all 11 were installed. The
injected presses ran at each start too. Nothing on the screen said why.

**Fixed:** the three files removed (`route.sh off`). And the launcher now says it: with any
test file in the app's files (`route`, `route-call`, `inject-pad`, `hide-dlc`, `dlc-only`,
`hack-*`) Launch's line reads "Test files in place: ..." and the header "Test files in
place", and each start logs `TEST FILES IN PLACE: ...` at warning level, so a bug report
shows it. A route left behind is still possible; it is no longer silent.

---

## BUG-103
**Title Update 6's game code hung at startup: the runtime looked its calls up at TU7's place**
Our runtime (the split, PLAN 12.2; found by 12.3) · severity high · **FIXED** session 41

The game's code is a library of its own since 12.2, made from the player's title update -
Title Update 6 (disc 2's) or 7. TU6's image is 0x13F0000 bytes, TU7's 0x1400000, and the
indirect-call table sits right after the image. 12.2 moved the runtime's own uses of the
layout (`warden_guest.cc`, `warden_guest_registers.cc`) to the loaded library's values, but
not XenonRecomp's `PPC_LOOKUP_FUNC` (ppc_context.h), which computes the table's address from
`ppc_config.h`'s constants - this build's, TU7's - and which six runtime files use: a guest
thread's entry (`warden_guest.cc`), the XAudio render callback, the graphics and GPU
interrupt callbacks, the deck's and the routes' calls by address.

**What it causes:** with game code made from Title Update 6, every guest thread the title
starts "entry 82AB8B30 is not a translated function" and returns at once; the startup thread
waits for them forever - no GPU work, no frame, the launcher's "Launch" leading to a black
screen (run 597). Any player with disc 2 and no TU7 file would have met it.

**Fixed:** `warden_game_library.h` redefines `PPC_LOOKUP_FUNC` with the loaded library's
layout, and is force-included into every C++ file of the runtime (CMakeLists.txt), so no
runtime file can use the compile-time one. The game code itself is unchanged (it uses its own
`ppc_config.h`, which is right for it). Run 598: Title Update 6's code to the King's Camp.

---

## BUG-102
**A save cut off mid-write was left corrupted**
Our runtime (`warden_xam.cc`; PLAN 9.13) · severity high · **FIXED** session 41

**What it looked like:** the owner: "the autosaves generated when i close out the app
(Pressing home, waiting 5 seconds and then swiping the game away from open apps) are
corrupted." **Cause:** a save was written straight into its own folder - the one before
moved to `.backup` first - so a process ended mid-write (BUG-100 kept the autosave
unfinished for seconds, and the swipe ended it) left a half-written save where the game
looks. **Fixed:** new content is written into `files/content/<type>/.writing/<name>`,
mounted as the save's root, and committed at `XamContentClose` - the one before moved to
`.backup`, the new folder renamed into place, its display name written. A write cut off
leaves the save before untouched (a stale `.writing` folder, removed when that name is
next written). Checked: Home, 5 s, the app killed - the autosave committed 1.5 s after
Home, its three files' CRC-32s matching their `.x`.

---

## BUG-101
**Every save's thumbnail blank again**
Our runtime (`tools/assemble_runtime.py`, the Vulkan command processor) · severity low ·
**FIXED** session 41

**What it looked like:** the owner: "The thumbnail images for saves are missing." Every
`scrnshot.bin` 458,752 zero bytes again. **Cause:** BUG-049's fix (session 34) made the
thumbnail's quad draw its real shader, but its whole thumbnail came with the readback
marker on: this runtime never copies resolves back to guest memory (`readback_resolve`
stays "none" - every resolve read back costs most of the frame rate, BUG-039), so the
title copied zeros into the save. **Fixed:** that one resolve - 448x256 k_8_8_8_8,
destination pitch `010001C0`, 0x70000 bytes - read back in full (Xenia's own readback,
a wait of a few milliseconds once a save). Checked: the autosave's and manual saves'
thumbnails in Load Game (449,806 non-zero bytes).

---

## BUG-100
**The autosave as the app is put away finished only on return**
Our runtime (`warden_upscale.cc`, `warden_frame_generation.cc`; PLAN 9.13, 9.14) ·
severity high · **FIXED** session 41

**What it looked like:** the owner: "The autosaves that are supposed to generate when i
put the app away (Going to home, closing the lid, locking the device) seem to finish
generating when the device resumes, not before fully suspending the app." **Cause**
(Home in the Wilds, the owner's FSR 3.1 and Lossless frame generation): the window
destroyed, FSR 3.1 found no panel to upscale to ("the panel is no bigger" - its size
read as nothing), so the guest output came out at the render size, 1120x630; frame
generation took it for a new size and made its generator again - 8.7 s on the GPU
thread, the game's frames stopped behind it. The autosave, which needs frames (its
thumbnail), was "not written within 3 s", the guest held, and the save finished on
return. **Fixed:** with no window, FSR 3.1 keeps the last panel's size, and frame
generation neither generates nor makes its generator again; the hold's limit 6 s.
Checked: Home - the autosave committed 1.8 s after, then the hold; the device put to
sleep - 2.5 s.

---

## BUG-099
**The sound very low at full volume**
Our runtime (`warden_audio_mix.cc`, `warden_audio.cc`; PLAN 9.12) · severity medium ·
**FIXED** session 41, as an option

**What it looked like:** the owner: "Audio Volume is very low even with maxed device
volume" (the speaker at 15 of 15). **Cause:** the mixdown scales the six channels by
1 / 2.414 so that combat's peaks never clip; the rest of the game then sits well under
full scale - exploring Orzammar's peaks near 0.1 at the mixdown's input, quiet areas
0.02 - which the Thor's small speakers make faint. **Fixed:** Options -> Volume boost
(+6, +12, +18 dB) with a lookahead limiter; off by default, the owner's Thor at +12 dB
(PLAN 9.12).

---

## BUG-098
**A deck opened again showed initials, not faces**
Our app (`DeckActivity.java`) · severity low · **FIXED** session 41

**What it causes:** with BUG-097's fix the deck is opened again when the game returns,
with the faces already drawn: it fetches them at its first pull - and a moment later
makes its view again with the game's fonts and art (PLAN 8.4), which dropped them; their
generations already seen, they were never fetched again. The rings held initials.
**Fixed:** the deck keeps the faces it was given and gives them to the view made again.

---

## BUG-097
**Home left the deck on the second screen**
Our app (`GameActivity.java`, `Deck.java`; PLAN 8) · severity medium · **FIXED** session 41

**What it looked like:** the owner: "pressing the home button on the thor twice only
returns to home on the second screen, the primary screen stays locked to the game."
**Cause:** the deck was opened once, as the game started, and nothing put it away: Home
sent the main screen home and left the deck. A second press, meant for the second
screen, reached the Thor's home app already showing - which put its own page on the
second screen and, at times, launched the game again on the main one (the log: START
GameActivity from the home app's uid). And coming back to the game, the deck stayed under
the home app's page. **Fixed:** the deck follows the game - its task finished when the game's
activity stops, opened again when it starts. One Home press now shows home on both
screens, and the deck returns with the game.

---

## BUG-096
**The deck's faces on the wrong rows**
Our runtime (`warden_deck.cc`, `warden_game.cc`; PLAN 8.9) · severity medium · **FIXED**
session 41

**What it looked like:** the owner, at Orzammar with the Dwarf Noble and Gorim: the hero's row
showed Gorim's face and Gorim's a woman's. **Cause:** a portrait render arrives as a
resolve with only an address; the deck guessed its slot - by which slots' portrait names
changed, by the burst's order, by the address a slot had. The game redraws a face several
times to one address, and joins and redraws mix, so the guesses went wrong (the log: a
lone new face "placed every named slot" into slot 0). **Fixed:** the game names each
portrait as it makes it (its builder `sub_824F5E50`: "portrait_" + tag + a running
count, the HUD's own name), hooked at its call to `sub_825FDDA8`; a new resolve address
is the oldest portrait made and not drawn, and a slot shows the face of the name the HUD
gives it. Found on the way: the routes' new games failed with "Character limit reached"
(docs/TESTING.md) - the save folder held seven characters; three of session 40's tour
characters' saves moved aside.

---

## BUG-095
**The deck's task affinity named the package**
Our app (`app/src/main/AndroidManifest.xml`; PLAN 8, 11.4) · severity low · **FIXED**
session 41

**What it causes:** with a second install of the app beside the owner's - 11.4's clean
copy, `<the former id>.clean` - both apps' second-screen deck activities would have
claimed the task affinity `<package>.deck`, so one's deck could open in the other's
task. **Fixed:** `${applicationId}.deck`, each package its own.

---

## BUG-094
**UnRAR's licence could not go out with the app**
Our build (`app/src/main/cpp/CMakeLists.txt`, `dao_archive_android.cc`; PLAN 10.19,
11.3) · severity high · **FIXED** session 41

**What it causes:** the app is GPL-3.0 as soon as it is shared (lsfg-vk, GPL-3.0, is
compiled in), and GPL-3.0 allows no further restrictions on what is distributed with it.
UnRAR's licence, on the library 10.19 built in to read .rar mods, forbids using its code
"to develop RAR (WinRAR) compatible archiver and to re-create RAR compression algorithm".
A build holding both could not have been given to anyone under either licence - the
owner's plan to share the app in a public repository (2026-11-02) would have been stuck
on it.

**Fixed:** .rar archives (RAR 4 and RAR 5) are read by libarchive v3.8.11's readers
(BSD 2-Clause; `third_party/libarchive`, the bootstrap fetches it), built as a static
library of its read side; UnRAR is gone from the bootstrap, the build and the licences.
Checked on the Thor: the No Helmet Hack .rar imported through the Mods page, its .dazip
byte-identical to what Windows' own tar.exe unpacks (MD5 657a6ddc...).

---

## BUG-093
**Six instructions left as traps by the recompiler**
Upstream (XenonRecomp, `recompiler.cpp`) · severity medium · **FIXED** in our edits
(`tools/patch_xenonrecomp.py`), session 40

**What it looked like:** nothing yet - found by reading the generated code for PLAN 2.4:
six `__builtin_debugtrap()` in recomp/generated/ppc_tu6, in 82AADBE0 (`blrl`), 82D9F030
(`vpkd3d128` type 3, pack 1, shift 3), 82DB4D58 and 82DB4E60 (`vupkd3d128` type 3) and
82EC2F60 and 82EC3080 (`vupkd3d128` type 5). The tour of 118 areas and the routes never
ran one.

**What it causes:** reaching one ends the game at once (signal 5, a tombstone in
`files/exits/`, nothing in the log) - progress since the last save lost, on some rare
path: an effect, a model format or a library routine not yet met.

**Cause:** XenonRecomp translates `vpkd3d128` only for D3DCOLOR and FLOAT16_4,
`vupkd3d128` only for D3DCOLOR and NORMSHORT2, and not `blrl` at all; it warns only of
mnemonics it does not know, so the recompile printed nothing.

**Fixed:** the three translated as Xenia's CPU does them (see PLAN 2.4); the generated
code holds no trap.

---

## BUG-092
**A conversation's start stuttered with frame generation**
Our runtime (`warden_frame_generation.cc`, `warden_frame_pacing.cc`,
`dao_frame_pacer_android.cc`; PLAN 9.14) · severity high · **FIXED** session 40
(runs 575-579)

**What it looked like:** the owner: "whenever i start a conversation there are 2-5
stuttering/freezing on my current settings" (FSR 3.1 Performance, Lossless 2x balanced,
60). Reproduced at Ostagar talking to Daveth (the wilds route, then back to the camp):
in the 5 s windows after the press, title frames of 62 and 97 ms, and the panel's frames
uneven for about ten seconds (a mix of one, three, four and five refreshes apart).

**What it causes:** two to five visible stutters at every conversation's start, and an
uneven picture after them, with frame generation on.

**Cause:** a conversation's first frame draws no 3D scene, so FSR 3.1 does not upscale it
and frame generation has no image to generate from ("1 shown real only"). For that guest
output frame generation told the pacing to stop pacing pairs ("frame pacing: as before
frame generation") and, a frame later, to start again. The lone frame was painted on the
GPU command thread itself, unpaced, ahead of the pairs still queued 40-50 ms in advance,
and the timeline started over.

**Fixed:** a gap of up to 8 guest outputs without a generated frame keeps the pairs'
pacing (`FrameGeneration::Pace`); such an output goes to the groups' thread as a group of
one (role 4: its real frame two steps after the group before's, where its title frame
falls, taking its swap). A longer gap - the menu's other size, BUG-081 - paces as without
frame generation, as before. Run 579: no pacing switch, one disturbed 5 s window - the
game's own rate change, 25 fps at the camp to 35 in the close-up - and the next even.

**Left:** one title frame of about 95 ms remains at the start, the game's own work for
the conversation's first frame (timed: no GPU wait, no file loads, a 5 ms pipeline; its
GUI-opening calls take no time themselves), as frames of 55-87 ms come now and then while
exploring the camp. Frame times over 50 ms are now logged ("frame times: a title frame of
... ms"), and pipelines taking 3 ms or more to create.

---

## BUG-091
**The driver's pipeline cache saved on the GPU command thread**
Our runtime (10.3's edit in `vulkan_pipeline_cache.cc`, `tools/assemble_runtime.py`) ·
severity medium · **FIXED** session 40 (run 576)

**What it looked like:** at a conversation's start, "Saved 33346371 bytes of
VkPipelineCache data, 127 pipelines" - in the same second as the stutter (run 575), and
every 30 s through the session while it met pipelines new to it.

**What it causes:** a stall of the GPU command thread - 33 MB copied out of the driver and
written to storage between two submissions - whenever the save fell due, which first
pipelines (a conversation's faces in close-up) make it do.

**Cause:** 10.3 saved the driver's cache from `DaoFlushStorage`, called at every
submission, whenever the pipeline count had grown and 30 s had passed. Since BUG-060 the
stored pipelines are created at their first draw, as hits in the driver's cache, so the
count grows all session while the cache's data never changes.

**Fixed:** the save runs on a thread of its own and writes only when the data's size or
hash changed (the file's own, read once). Run 576: "VkPipelineCache data unchanged
(33346371 bytes, ...): not written (18-28 ms, on its own thread)".

---

## BUG-090
**The test launch hid the mods' add-ins**
Our app (the launcher's test launch, files/hide-dlc; PLAN 10.19) · severity medium ·
**FIXED** session 40 (run 574)

**What it looked like:** with the No Helmet Hack installed as a package, the King's Camp
test launch logged "downloadable content hidden (files/hide-dlc)" and "content type 2 ...
0 item(s)": the add-in never loaded. The launcher's "Ultimate Edition disc 2" row also
said "12 installed" with one mod package beside the 11 DLC.

**What it causes:** a mod's add-in cannot be tested from the test launch or a route, and
the disc-2 row miscounts.

**Cause:** hide-dlc (BUG-083's fix) hides every package so New Game asks for no campaign;
a mod's add-in extends a campaign and adds no such choice, but was hidden with the rest.
The count took every folder.

**Fixed:** hide-dlc keeps the `mod-<id>` packages (`warden_xam.cc`); the count leaves them
out (`GameData.dlcCount`).

---

## BUG-089
**A .rar could not be picked**
Our app (the Mods page's file picker; PLAN 10.19) · severity medium · **FIXED** session 40
(run 574)

**What it looked like:** in Android's file picker, "No Helmet Hack 1_6 Origins-202.rar"
was greyed out; the .7z beside it could be picked.

**What it causes:** a mod downloaded as a .rar - common on Nexus - cannot be added.

**Cause:** the picker was given a list of file types (zip, 7z and three names for rar);
the file's type as Android reports it was none of them.

**Fixed:** the picker shows every file, and the import tells archives apart by their
first bytes (`Mods.unpack`), saying so when a file is none of them.

---

## BUG-088
**A PC add-in's module could not be converted**
Upstream (DAORecomp's mod converter, mods/convert/gff.cpp) · severity high · **FIXED** in
our edits (`tools/assemble_dao_convert.py`), session 40 (run 574)

**What it looked like:** converting the No Helmet Hack for Awakening: "module/
no_helmet_hack_aw.cif: it isn't a well-formed GFF file". A Python reader of the same
rules stopped at the module's variable 27, PARTY_BANTER_DIALOG_FILE, a string: its
"string" was read at a length of 4294967295.

**What it causes:** any PC add-in's module description (.cif) with a string variable -
most of them - cannot be converted, so the add-in cannot load.

**Cause:** a generic field (type 0xFFFF, a variable's value) is a (type, offset) pair;
for a string the offset points at the string itself - its count, then its characters -
in the PC's files and the 360's alike (the 360's singleplayer.cif holds "NONE" so).
Upstream read the offset as a field slot holding another offset, and wrote it so.

**Fixed:** two recorded edits to gff.cpp: a generic string is read where it is, and
written where the field points. The converted .cif's string variables read as the
360's own.

---

## BUG-087
**A quickbar tap went through in the pause menu**
Our app (the deck's touch quickbar, PLAN 8.8) · severity medium · **FIXED** session 40
(run 545: a tap in the pause menu refused, the same tap in play pressed)

**What it looked like (run 544):** with the pause menu open (mode 3), a tap on the deck's
Shield Cover tile was logged as pressed (`deck: quick slot 1 pressed from the deck
(8000)`) - Y went to the menu instead of being refused.

**What it causes:** a tap outside play presses a button in whatever screen is open - in
a menu, a conversation's reply or a dialog's choice. The quickbar's promise is that a tap
does only what the slot's button does in play.

**Cause:** the guard took the game mode from the state mirror's
`GameModeController.CurrentGameMode`, the HUD's own copy, which the HUD stops updating
while a menu covers it.

**Fixed:** the guard reads the engine's mode, the one the port's mode hook tracks
(`warden::GameMode()` in `warden_game.cc`).

---

## BUG-086
**Awakening's Smuggler's Cove is black on arrival**
The game's own (the arrival view) · **NOT OURS** (session 40: the reference renders the same)
(found 2026-11-01, session 40's tour of the downloadable content)

**What it looked like:** `zz_coa_debug` -> Jump to area -> Smuggler's Cove
(`coa130ar_smugglers_cove`) arrived in explore mode in a black scene: the player drawn,
dimly, a few white jagged shapes at the top and left edges, the HUD; walking forward still
black; the camera turned toward the tunnel, the tunnel drew fully (stone, beams, torches);
turned back, black again. 62 fps standing, 72 after the turn - not as light as BUG-085's
87, so geometry is drawn where the camera looks into the tunnel. Screenshots:
`build/areas/coa-smugglers-cove-*.png`.

**What it causes:** if the cove renders like this in play, part of Awakening's Amaranthine
quests is played blind there.

**Session 40:** the occlusion-culling switch changed nothing here (the same six
screenshots). Read again, the arrival may be the game's own: the player arrives at the
tunnel's mouth facing out of the area, toward the city exit, where nothing is modelled
(the white shapes are the cliff edges against the sky); turned around, the tunnel draws.
Only the reference build with the same jump can tell. **Resolved (session 40): not ours.** The
reference had no downloadable content, and a save records every package installed when
it was made (the Thor's Awakening autosave asked for all eleven), so the port got
`files/dlc-only` (the packages it lists, by folder-name prefix, are the only ones the
game sees; `warden_xam.cc`): a game on the `probe-awakening` route with only Awakening
(`E92188F5`), the cove jumped to, and its arrival autosave copied to the reference (with
Awakening installed there from the Thor, `userdata/content/454108C0/00000002/`, kept for
later comparisons - the owner's call) - DAORecomp's Windows build loads it into the very
same picture: black, the white shapes at the edges, the exit marker
(`build/reference/cove2/`). Its character wears Awakening's armour and bow where the
Thor's is in plain clothes: the reference ran the expansion's start script on loading
the save, the route had it hidden (skip-start). **To do first:** look at the cove's arrival view against the rest of it: a traced frame
looking out (black) and one looking into the tunnel (drawn), the draws and the render
targets of each; with BUG-085, the same question - which draws are missing, or drawn and
then covered. The white shapes look like sky or a light-shaft card drawn over nothing.

---

## Not bugs, but constraints that behave like them

Recorded here because they produce surprising failures and are easy to mistake for
defects.

**`VK_EXT_fragment_shader_interlock` is absent on Adreno 740.** Xenia's accurate EDRAM
emulation path (`kPixelShaderInterlock`) requires it, so `kHostRenderTargets` is forced.
Upstream documents that fallback as *"may be irreparably inaccurate"* for blending and
format precision, which made it the project's top risk (R1b / Q8).

Phase 3.3a measured it instead of assuming: **none of the degradations that warning
describes are active on this hardware.** 0 of 9 render target formats fail to blend,
depth is `D24_UNORM_S8_UINT` matching Xenos exactly, and gamma targets blend as linear
UNORM16. No format substitution happens, which is the mechanism the warning names. R1b
is downgraded to moderate — see [`EDRAM.md`](EDRAM.md). The residual inaccuracy is
structural (tile ownership transfer, resolve semantics, MSAA layout) and needs rendered
frames to assess, which is 3.3b.

Note also that Adreno 740 meets *every other* requirement of the accurate path, so a
driver update exposing the extension would enable it by flipping one cvar. Worth
re-probing after driver updates. Tracked in `docs/PLAN.md` rather than here, because it
is a hardware capability rather than a defect.

**Xenia's Vulkan presenter picks 8-bit sRGB, not the best format on offer.** Measured
in Phase 4.2a: with 12 surface formats available including
`A2B10G10R10_UNORM_PACK32` + `DISPLAY_P3_NONLINEAR`, the presenter chose
`R8G8B8A8_UNORM` + `SRGB_NONLINEAR` (`MAILBOX` for the present mode, which is the right
choice). It has no notion of preferring a wide-gamut or 10-bit swapchain.

**What it causes:** fixing [BUG-023](#bug-023) made wide-gamut Display-P3 *available*
but not *used*. Decision D8's default needs a second, separate change — teaching the
presenter's format selection to prefer 10-bit Display-P3 — which is Phase 9 work and is
now precisely specified rather than vague. **Done in session 35 (PLAN 9.3, runs
370-372):** the presenter asks `warden_surface_format` first - 10-bit sRGB by default,
10-bit Display-P3 for Settings -> Colour -> Vivid. The panel was in Display-P3 all along,
with Android converting the 8-bit sRGB frames into it, so the game's colours were right
before; 10 bits keep the upscaling's in-between values.

**The Thor's "120 Hz" panel refreshes every 8.445 ms - 118.4 Hz.** Found in 4.7 (runs
279-280). SurfaceFlinger's vsync model for the 120 Hz mode settles on 8.45 ms
(`dumpsys SurfaceFlinger`: "For ideal period 8.33ms: period = 8.45ms"), and the panel's
own present times say the same: the intervals between frames on the panel, over the
refreshes they span, average 8.444-8.448 ms in every 5-second window
(`VK_GOOGLE_display_timing`). The display mode still calls itself 120.00 Hz, Choreographer
reports 8.333 ms, and `vkGetRefreshCycleDurationGOOGLE` gives 8.333 or 8.447 ms by turns.

**What it causes:** the title's 30 fps cannot sit on every fourth refresh - four are
33.8 ms, 29.6 fps - so about one frame in eighteen has to follow the one before after
three refreshes (5.5% of frames 25 ms apart instead of 34), however the frames are paced;
at the title screen's 60 fps, one in thirty-six after one refresh. The pacer
(`warden_frame_pacing.cc`) spreads them evenly instead of letting them come as a late
frame and an early one. Only a frame rate matched to the panel would remove them: the
title's emulated vblank at half the panel's real rate, 59.2 Hz instead of 60 - which
slows whatever the title counts in vblanks by 1.4%.

**On the Thor a frame reaches the panel a refresh before the one its desired present time
is nearest** (`VK_GOOGLE_display_timing`, run 297) - as if SurfaceFlinger's expected
present times, against which it holds a frame until its desired time, ran a refresh ahead
of the panel's own (`actualPresentTime`). A pacer reading "the first refresh at or after
the desired time" aims a refresh early (BUG-054); `warden_frame_pacing.cc` asks for a
quarter of a refresh after the refresh it wants. Run 298 found the margin wide: from four
eighths to one after the slot, no frame early and few held late.

**XenonRecomp compiles PowerPC's memory barriers to nothing.** `sync`, `lwsync` and
`eieio` emit no code. On x86, whose stores other cores see in order, that is nearly
harmless; on ARM64 another core may see a thread's later store before its earlier ones.
Where our runtime crossed threads on the title's stores - the ring poller's kick - that
was a race (BUG-047). The title's own handoffs between its threads rely on the same
barriers; if rare corruption turns up elsewhere, emitting `dmb` for `sync` and `lwsync` in
XenonRecomp is the fix (a full recompile; PLAN risk R11).

**Xenia's pipeline hot-swap draws a placeholder.** With `async_shader_compilation`
(upstream's default) a draw whose pipeline is new is made with a placeholder pixel
shader while the real pipeline compiles on a background thread. A frame drawn every
frame shows it for a frame or two; a draw made once - a render to texture at a load or a
save - keeps the placeholder's result for good (BUG-049, BUG-052). This port creates
pipelines when first drawn with (session 34): an area's first load takes longer and its
first seconds can hitch while new pipelines compile (run 314).

**DAORecomp's presentation-side features are Direct3D 12 only.** Found in Phase 4.2
while working out what the frame pacer needed. HDR output, temporal upscaling (FSR 3,
FSR 4, XeSS, DLSS), motion-vector generation and frame generation all live under
`gpu/d3d12/` (the `dao_upscale*` family) and `ui/d3d12/` (`dao_streamline.cc`), with no
Vulkan counterpart. `xenia/dao_temporal_upscaler.h` looks like a shared interface and
is 30 lines holding an enum and a cvar.

What the Vulkan presenter *does* have is **FSR 1** — EASU plus RCAS sharpening — with
SPIR-V bytecode already generated and wired into its guest-output paint path. That part
is free.

**What it causes:** "port the features over from the recompiled project" does not apply
to these. On Vulkan they are new implementations that can be read from the D3D12 code,
not ports of it. Affects features D7 (FSR 3.1) and D8 (HDR10) and is why the Dao pacer
functions in `app/src/main/cpp/dao_frame_pacer_android.cc` can correctly do nothing:
nothing on this backend produces a generated frame. (Until session 37: lsfg-vk's frame
generation, PLAN 9.14, made that file present frames in pairs - BUG-069 to BUG-073.)

**`VK_EXT_conditional_rendering` and `VK_EXT_memory_budget` are absent on Adreno 740.**
Found by the Phase 3.2 device probe. Xenia probes for both and runs without either —
conditional rendering is an optimisation, and memory budget only improves allocation
reporting. Recorded so neither absence is mistaken for a setup error later. Ten of the
thirteen optional extensions Xenia looks for *are* present.

**Fused multiply-add and reciprocal-estimate differences between x86 and ARM.**
Measured and explained in `docs/PROBE-ARM64.md` §3. Both are specified behaviour, and
in both cases ARM64 is closer to the original Xbox 360 than our x86 reference build is.
Not defects in either.

**`DirectX-Headers` is not fetched.** The *host* direction of
`tools/compile_runtime.py` fails on 14 translation units needing
`third_party/DirectX-Headers/include/directx/dxgicommon.h`, because Xenia's Windows
base code includes it. Not a defect and not currently needed: the reference build for
Phase 3 is DAORecomp's own Direct3D installer output, not our tree. Fetch the submodule
if a Windows build of *this* tree is ever wanted.
