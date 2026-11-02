# STATUS

**Living handoff document.** Read this first on any machine. Update it at the end of
every working session, before you commit. If this file is stale, the next session
starts by re-deriving context that was already paid for.

| | |
|---|---|
| **Last updated** | 2026-11-02 (session 42) |
| **Version** | **0.1.1-alpha, "Open Alpha"** (`CHANGELOG.md`), after the first public release, 0.1.0-alpha: the source, and an APK that holds none of the game (`gradle.properties`: `warden.version`, `warden.releaseName`; `docs/DECISIONS.md` D14) |
| **Current phase** | **Phase 12 complete: a published APK, the game's code made on the device from the player's own discs.** Phases 0-4, 7, 8, 10, 11 and 12 complete. Phase 5 reached. Phase 6 reached by script - the hand playthrough to Ostagar is left, the alpha's players its testers. Phase 9 done but for what the owner deferred (9.15, 9.9) and 3.8's Adreno work. §2 has the board. |
| **Overall state** | The whole game runs on the AYN Thor: the main campaign, Awakening and every DLC from the Ultimate Edition's disc 2, and all 118 areas the game's debug menus reach, loaded and profiled (`docs/AREAS.md`). With it: the companion deck on the second screen, button prompts for three controllers, up to 120 fps, FSR 3.1, two kinds of frame generation, HDR10, PC mods with an in-app converter, saves named on the keyboard with thumbnails, export, import and restore, and Debugging Mode's one-file bug reports. [`README.md`](README.md) is the user's manual. |
| **Next action** | **Published** (2026-11-02): https://github.com/mabencs/Dragon-Age-Origins-Recomp-Android-Duo, the 0.1.0-alpha APK on its Releases page. Next: what the alpha's players report (Debugging Mode's reports, `docs/TESTING.md`); the open bugs. `RESUMING.md` §5. |
| **Blockers** | None |

The authoritative task list is [`docs/PLAN.md`](docs/PLAN.md); each session's detail is
§9 below, newest first.

---

## 1. What this project is

**Dragon Age Origins Recomp Android Duo** (Warden's Port until 2026-11-01): a native
Android ARM64 port of **Dragon Age: Origins** (Xbox 360), built by static
recompilation, for the **AYN Thor** handheld and its two screens. Not an emulator - the
game's PowerPC code is translated to C++ and compiled for arm64-v8a. Package ids:
`dev.daorecompandroid.duo` (debug builds) and `prod.daorecompandroid.duo` (release
builds); the Java package is `dev.daorecompandroid.duo`.

Three headline goals beyond "it runs", all met:

1. **Controller glyph remapping** - on-screen button prompts match the controller in
   your hands (Xbox default, Nintendo and PlayStation switchable).
2. **Second-screen companion deck** on the Thor's 1240×1080 lower display, styled to
   match Dragon Age: Origins' own UI.
3. **Feature parity with the PC recomp** where the hardware allows - up to 120 fps,
   HDR10, FSR 3.1, frame generation, better filtering, PC mods.

Free software (GPL-3.0-or-later, `docs/DECISIONS.md` D10) in a public repository, with a
published APK that holds none of the game: the app makes the game's program on the
player's device from their own disc (D14, Phase 12; D11's source-only release before it).
No game content is committed or packaged.

---

## 2. Phase board

| Phase | Name | State | Notes |
|---|---|---|---|
| 0 | Groundwork | **COMPLETE** | Toolchain proven on device; Q1 answered green |
| 1 | Host-side recompilation | **COMPLETE** | 52,857 functions translated; clean, zero warnings |
| 2 | ARM64 bring-up | **COMPLETE** | 208 TUs compile; 6,635 functions differentially tested on-device, 98.70% identical, 0 logic bugs |
| 3 | Vulkan renderer | **COMPLETE** | Xenia's backend renders menus, character creation and the first area (3.1-3.6); the King's Camp at the game's 30 fps cap, the GPU 92% busy (3.8, run 296). Validation-clean, core and synchronization (3.9, runs 323-327); frames captured and replayed on the Thor with GFXReconstruct (3.10, `tools/gfxr.sh`, runs 328-330); memory measured and BUG-060 fixed (3.7: GPU memory at the camp 1.0 GB against 1.8). 3.3b, the frame comparison, and 3.8, Adreno performance, moved to Phase 9 (the owner, 2026-10-27) |
| 4 | Android shell | **COMPLETE** | APK presents on the Thor; 9,450 game files extracted and verified; input 20/20, pinning verified. Sound through AAudio with the XMA decoder (4.5, runs 207-209). Frame pacing on by default (4.7: 11.1% -> 6.8% of frames off the even cadence, run 285; 6.4% with the title's frames even and BUG-054 fixed, run 301; the panel is 118.4 Hz). The app's end (4.9): Back asks and quits, the title's own end and a lost GPU end the process cleanly; device-loss recovery is out of reach in Xenia's design. Suspend and resume (4.9, session 35): put away - home, another app, the power button - the game holds where it was, its clocks stopped, and comes back (BUG-056, BUG-057, runs 319-322) |
| 5 | First light | **Reached** | Title screen, storage prompt and main menu (run 96), Scaleform UI correct, saves written and loaded (5.3, 6.5, runs 199-203). Audio plays (4.5, run 209): the exit criteria are met. 5.6 done (session 41): both screens, in the README |
| 6 | Playable | **Reached by script; the hand playthrough left** | Character creation into the Mage origin's Fade by hand (run 168), checked screen by screen (6.1, run 292); Ostagar by script (6.0, run 175) at 30 fps, the game's cap (6.7, 3.8); the name keyboard (6.1, run 193); saving and loading (6.5, runs 199-203, 215-216); the Korcari Wilds (6.2, run 218) at 28 fps and combat, spells included (6.3, runs 219-223); areas loaded on foot and streamed (6.2, runs 220, 224). The soak (6.6) is dropped. The baseline (6.7, runs 277-278): 29.9 fps at the King's Camp, no throttling over eleven minutes, about 10 W. The voice-over heard by the owner (6.4, 2026-10-27). Left: the hand playthrough to Ostagar - the alpha's players are its testers. **Milestone: character creation → Ostagar** |
| 7 | Glyph system | **COMPLETE** | Xbox, Nintendo and PlayStation prompts (Back -> Settings, from the next start); the PlayStation art the game's own, from its PS3 release; redirects in the UI's atlas descriptions through the title's patch folder and the new override tree (7.3). Gaps: no switching while the game runs (BUG-061); the controller-layout loading screen stays Xbox (runs 346-352) |
| 8 | Second screen | **Done for its exit criteria (session 37)** | The owner asked: the deck on the lower screen in its own process - 8.1 (both routes work; BUG-078 fixed), 8.2, 8.3 (the state mirror: the UI's token observers, and the deck's own reads of the data model), 8.4 (the game's own typefaces and HUD art, made on the device), 8.5's six tabs (Party, Quests, Map, Tactics, Codex, Items), 8.6 (Perf), 8.7 (killed mid-combat, the game unaffected). Left: the map's pan, pinch and world-map toggle. Session 40: 8.8 (the touch quickbar), 8.9 (the party's faces, read back from the GPU), 8.10 (the Performance tab/overlay option: the Perf tab, or an overlay on the main screen without the second screen) |
| 9 | Feature port | **Done but for the owner's deferred items** | 60fps, HDR, FSR, filtering. Opened with 3.3b, the frame comparison against DAORecomp's Windows build (`C:\Games\DAORecomp`, started only through `tools/dao_reference.py`, in the background): nothing renders wrongly on the Thor (session 35). Done in session 35: 9.1 (60 fps as a setting), 9.5 (FSR 1, CAS), 9.6 (16x anisotropic), 9.11 (intro logos), 9.12 (the mixdown, the headphone surround), 9.3 (10-bit, Vivid colour), 9.8 (shadows and post-processing switches), 9.7 (FXAA), 9.13 (an autosave when put away), 9.2 (HDR10, the buffers tagged BT.2020 PQ directly; the owner's ask); 9.4 FSR 3.1 done (session 36: Options -> FSR 3.1, Quality, Balanced, Performance with the camera's motion vectors - 8.35, 7.55 and 6.78 ms, the camp 23.4, 25.9 and 28.8 fps; BUG-067, FSR without depth or motion before); 9.14 frame generation added (FSR 3.1's, lsfg-vk's; the frame rate a target on the screen) - Lossless 2x in play (session 37: about 60 frames a second on the panel at the camp, the title 31.8), FSR 3.1 2x working (the game at 25.8 with FSR 3.1 Performance, 45-62 on the panel); 9.10 dropped (Q2); 3.8's Adreno performance work comes back here too; 9.13 investigates an autosave as the game is put away (the owner, 2026-10-27) Session 40: 9.16 Rendering resolution (960x540, 1280x720, 1360x768; 1080p does not fit the game's EDRAM use) 9.17 field of view, 9.18 motion blur, 9.19 depth of field in conversations and 9.20 photo mode done (upstream's, ported). 9.14 closed by the owner (2026-11-02). Deferred by the owner: 9.15 (LS1) and 9.9; 3.8's Adreno work. |
| 10 | Polish | **COMPLETE** | 10.1 done (session 35, runs 394-396): the options panel - Select + Start during play, every option over the held game, most of them live. 10.2 done (session 38, runs 475-477): no throttling at the heaviest settings; Options -> Heat guard, off by default. 10.4 closed (nothing to build before play beyond 10.3). 10.7 done (run 479): how the last session ended, on the launch screen, with reports in files/exits. 10.3 done (session 34). 10.5 (a 4-hour soak) and 10.6 (battery life) dropped by the owner (2026-10-31). 10.9 done (runs 494-518): GPU headroom - the frame generation's balanced mode, Shader math (Fast an option), FSR 3.1's Lanczos lookup table, the fetch constants decoded on the CPU (the camp 27.2 -> 28 fps), the vertex fetches' binding and endianness known per draw (28 -> 30, the game's cap). 10.17 and 10.18 done (session 40, runs 550-551): the on-screen controller as an option, and the launcher and options panel by touch. 10.12-10.16 done (session 40, runs 569-573): PC mods and their converter, the cheats, the save restore page, language. 10.19 done (run 574): PC add-ins installed as downloadable content, .7z and .rar imports. 9.14 closed by the owner (2026-11-02); BUG-091 and BUG-092 (a conversation's stutter) fixed. 10.20 done (session 41): a manual save named on the keyboard. |
| 11 | Release | **COMPLETE** | 11.1 Debugging Mode (run 584), 11.2 saves out and in (run 585), 11.5 About and licences (run 586), 11.3 the release build - GPL-3.0-or-later, its own key, the APK proven - and 11.4 the first run on a clean install (session 41). 11.6 (session 41): the launcher's test launch removed. Version 0.1.0-alpha, "Open Alpha", the docs brought up to date for a public repository. |
| 12 | A published APK | **COMPLETE** | The game's code made on the device from the player's own discs (the owner, 2026-11-02). 12.1: LLVM 19.1.7 for Android (clang and lld, one 89.8 MB program). 12.2: the game's code is libdaogame.so, out of the APK, loaded at boot (run 594). 12.4: XenonRecomp for Android, its output identical to the computer's. 12.5: the Thor makes it itself in 7 min 30 s (runs 595-596). 12.3: disc 2's Title Update 6 alone is enough (runs 597-598, BUG-103 fixed). 12.6: the release APK (75.6 MB) passes `tools/release.py` - no game code, the device toolchain in it. 12.7: that APK on the Thor beside the owner's, the two disc images from Download, the code made on the device in 7 min 43 s, the game to its main menu (run 599). 12.8: the launcher's Build row - the making starts only when the player chooses it (run 600) |

Detail for every phase lives in [`docs/PLAN.md`](docs/PLAN.md).

---

## 3. Phase 0 checklist — all done

- [x] 0.1 Verify disc images and title updates are the right editions
- [x] 0.2 Capture AYN Thor hardware profile (`device/thor-profile.md`)
- [x] 0.2 Repo scaffolded, `.gitignore` proven to exclude all game content
- [x] 0.2 Asset tooling written and tested (`tools/`)
- [x] 0.3 Host toolchain installed and verified (§7)
- [x] 0.4 Upstream vendored and pinned (`THIRD_PARTY.md`)
- [x] 0.4 **Q1 risk probe run and answered** (`docs/PROBE-Q1.md`)
- [x] 0.5 The git remote wired up, pushed, clone verified
- [x] 0.6 `adb` device loop scripted (`tools/deploy.sh`)

### Proven end to end on the real device

Not assumed — actually executed:

- Cross-compiled a C program with the NDK for `arm64-v8a` / API 33, pushed it to the
  Thor, and ran it. Output confirmed `ELF 64-bit LSB pie executable, ARM aarch64 ...
  for Android 33, built by NDK r28c`.
- That program (`tools/vkprobe/`) loaded `libvulkan.so` on device and reported the
  driver's real capabilities — see §4.
- Captured screenshots of **both** displays simultaneously at the correct resolutions.

---

## 4. Verified facts — do not re-derive these

Established 2026-10-22 by direct inspection and on-device measurement.

### Your game files

Re-run `python tools/verify_assets.py .` to reconfirm at any time.

| File | Verdict |
|---|---|
| `Dragon Age - Origins (World) (En,Es,It).iso` | **Use this.** XGD2, Title `454108C0`, **Media `186A1115`**, XEX v0.0.0.1. Exactly the press the recompiler targets. |
| `TU_..._00000000001O1` | **Use this.** Title Update **#7**, media `186A1115`. |
| `TU_..._00000000000G1` | Keep, don't build with. Title Update **#2**. |
| `Dragon Age - Origins - Awakening (...).iso` | **Archived.** Media `2E6F7560` — the standalone Awakening disc (its own executable, Awakening as loose files). Not needed: disc 2 below holds Awakening. |
| `Dragon Age - Origins - Ultimate Edition (...) (Disc 2) (Additional Content Packs Install Disc).iso` | **Use this for the DLC.** The content installer (title `FFED2000`, media `6FB2B193`): Awakening and the ten other DLC as Live packages, and Title Update 6 (PLAN 10.10). Gitignored like every image (`*.iso`). |

All four are gitignored and verified excluded via `git check-ignore`.

### TU6 vs TU7 — resolved

DAORecomp's config is named `DAO_tu6.toml`, which looked like a problem since we have
TU7. It isn't. From their README:

> *Title Update 7 runs on the same code: its code differs from 6's only in a version
> number.*

So the `tu6` config handles TU7. Also confirmed: at startup the program checks
`title_update/default.xexp` is TU6's or TU7's and **passes over anything else for the
disc version** — which is exactly why TU2 is unusable, matching our verifier's verdict.

Phase 1.2 note: DAORecomp expects the title update as an extracted
`title_update/default.xexp`, not the raw LIVE package.

**Since PLAN 12.3:** the app uses either. Disc 2 carries Title Update 6 (`title_update.bin`,
a PIRS package; its `default.xexp` 2,684,928 bytes, SHA-1 `4141b608...`), so the base disc
and disc 2 are enough; TU7 is optional. The two images differ in size by 64 KB, which the
runtime takes from the game library it loads (BUG-103).

### Awakening — better news than expected

DAORecomp **does** support Awakening, as DLC on the TU build. From their README,
*Awakening*, *Leliana's Song*, *The Darkspawn Chronicles*, *The Golems of Amgarrak* and
*Witch Hunt* all need TU6 or TU7, and DLC packages are extracted to
`userdata/content/454108C0/00000002/` where the game finds them. `config/DAO_tu6.toml`'s
own header says the TU build is *"Needed by Awakening and the DLC campaigns after it."*

**The catch:** it reads Xbox Live packages (LIVE/PIRS format) — the Marketplace
*download*, not the retail *disc*. We have the disc (media `2E6F7560`), whose content
sits in `Addins/DAO_PRC_EP_1/`.

**Session 38 (runs 487-490): it can, as it is** - the add-in folder goes into a DLC
package's folder unchanged (PLAN 10.10); the owner's image is the standalone Awakening
disc, not Ultimate Edition's disc 2 (which holds Live packages and Title Update 6).

So Q5 is no longer "can this ever work" but "can the disc's addin content be repackaged
into what the DLC loader expects". That is a **data-packaging problem, not a second
recompilation**. Materially more promising than when [D2](docs/DECISIONS.md) was taken.
D2 still stands — don't chase it before Phase 6 — but the odds improved.

### Disc internals

- Game partition base offset `0xFD90000` (standard XGD2).
- Content archives are **`ERF V2.1`**, little-endian, 32-byte ASCII names, 44-byte index
  entries, zlib-compressed payloads. Differs from the PC release's ERF V2.0 with UTF-16
  names — PC-oriented DAO tools will not read these. `tools/dao_erf.py` does.
- UI is **Scaleform GFx**. `.gfx` files are `CFX`-signature zlib containers.
- UI bitmaps are atlased with **plain-XML layout descriptors** (`atl_*_dat.xml`) giving
  exact UV rects per cell. 37 atlas groups.

### Glyph system foothold (Phase 7)

`guiexport.erf` → `load_controller.gfx` contains discrete, individually-named
ActionScript MovieClips for every button glyph:

```
AButton_mc   BButton_mc   XButton_mc   YButton_mc
LTrigger_mc  RTrigger_mc  DPad_mc      StartButton_mc  BackButton_mc
Icon_mc      PushButtonImage   StickyButtonImage   ToggleButtonImage
GetButtonText    ButtonIdToString    SetButtonAction
```

Also `X360_ControllerInteraction.GDA`, a `GFF V4.0 / X360 / G2DA` table (19 columns,
76-byte rows) shipped in Awakening's **patchable `packages/core/patch/`** folder. Glyph
prompts are therefore **data-driven, not hardcoded**. See [`docs/GLYPHS.md`](docs/GLYPHS.md).

### Q1 ANSWERED — the Vulkan backend is viable

Full writeup: [`docs/PROBE-Q1.md`](docs/PROBE-Q1.md). Headlines:

- DAORecomp vendors Xenia **in-tree, no submodules**, and includes **only the D3D12
  backend**. There is no `gpu/vulkan/`.
- Upstream Xenia Canary's Vulkan backend has **1:1 file parity** with D3D12 (every
  `d3d12_*` has a `vulkan_*` counterpart), 26,506 lines vs D3D12's 24,804. The SPIR-V
  shader translator is 16,643 lines vs DXBC's 16,593. `ui/vulkan/` supplies the full
  presentation layer including VMA integration.
- **DAORecomp barely forked the frontend: 220 of 256 files byte-identical, 2.4% line
  divergence** (1,895 of 79,960). Divergence is concentrated in the D3D12 presenter,
  `graphics_system.cc`, `imgui_drawer.cc` and a DAO `memory.h` shim — *not* in the GPU
  emulation core (`command_processor.cc` 5.3%, `texture_cache.cc` 5.3%, PM4 2.6%).
- DAORecomp's own runtime is loosely coupled: only **7 of 144** source files touch
  D3D12/DXGI.
- **But** 7,178 lines of DAO-specific code live *inside* `gpu/d3d12/` — the upscaler
  framework, motion-vector generation, HDR output, motion blur, frame pacing, GPU
  timing. These need **reimplementing on Vulkan, not porting**. There is no
  API-agnostic upscaler abstraction to inherit; it has to be built.
- The 126 pre-generated `d3d12_5_1` shader bytecode headers are **build artefacts**.
  Canary ships none; the build generates them. No porting effort.

Re-run with `python tools/probe_divergence.py`.

### Adreno 740 Vulkan capabilities — measured on device

Full report: [`device/vulkan-capabilities.txt`](device/vulkan-capabilities.txt).
Re-run with `./tools/deploy.sh vulkan`.

```
Adreno (TM) 740 — integrated, Vulkan 1.3.128 (loader 1.3.0)
driver 0x802A4035, vendor 0x5143, device 0x43050A01
113 device extensions, 13 instance extensions
memory: 15,255 MB DEVICE_LOCAL + 256 MB DEVICE_LOCAL
```

**Every critical Xenia feature is present:** `geometryShader`, `tessellationShader`,
`sparseBinding`, `sparseResidencyBuffer`, `fragmentStoresAndAtomics`,
`vertexPipelineStoresAndAtomics`, `shaderStorageImageExtendedFormats`,
`shaderStorageImageWriteWithoutFormat`, `independentBlend`, `imageCubeArray`,
`multiViewport`, and — notably — **`textureCompressionBC`**, so the game's DXT1/DXT5
textures load natively with no transcoding.

**HDR is reachable:** both `VK_EXT_swapchain_colorspace` (instance) and
`VK_EXT_hdr_metadata` (device) are present. D8 is implementable; HDR defaults off
because of the 420-nit panel, not a capability gap.

**The one caveat — `VK_EXT_fragment_shader_interlock` is MISSING.** Xenia's render
target cache (its EDRAM emulation) has two paths; the accurate one
(`kPixelShaderInterlock`) needs that extension. It is **opt-in via cvar**, and
`kHostRenderTargets` is the default — so this is **not a blocker**, but that fallback
path is documented upstream as *"may be irreparably inaccurate ... at the mercy of the
host API's fixed-function output-merger"*. This makes EDRAM-via-`kHostRenderTargets`
**the critical accuracy work of Phase 3**, to be validated against the Windows D3D12
build as reference.

Other gaps, none blocking: `VK_EXT_external_memory_host` (shared memory must copy
rather than import host pointers), `VK_EXT_memory_budget` (VMA budget tracking),
`VK_KHR_shader_atomic_int64`, `VK_EXT_swapchain_maintenance1`,
`shaderTessellationAndGeometryPointSize`, `VK_EXT_non_seamless_cube_map`. Also
`maxBoundDescriptorSets` is **7**, lower than typical desktop — watch in Phase 3.4.

### Phase 1 results — the recompilation

Inputs verified against DAORecomp's own known-good hashes, so there is no doubt we are
building from the right binaries:

| input | sha1 | verdict |
|---|---|---|
| `game/default.xex` | `532c0ec164f44bc8784cc0e8f6f67f72c1ed3751` | exact match to `GAME_XEX_SHA1` |
| `title_update/default.xexp` | `dd5b493f4fe2fb4c99e6ef7ed6923ddddeee8291` | exact match to `TU7_XEXP_SHA1` |

Output, from `tools/recompile.py` — **clean, zero warnings or errors in either log**:

| build | config | translation units | generated C++ |
|---|---|---:|---:|
| disc | `DAO.toml` | 202 | 264 MB |
| tu | `DAO_tu6.toml` | 207 | 276 MB |

**52,857 functions**, TU build symbols prefixed `tu6_`. Takes about 5 seconds per build.

The `ppc_tu6` name is upstream's and is kept deliberately even though we feed TU7:
upstream states TU7's code differs from TU6's only by a version number, and renaming
would break runtime sources that reference `tu6_` symbols.

### Phase 2 is largely answered already

The generated code reaches SIMD through **SIMD Everywhere** — every translation unit
includes `<x86/sse.h>` and `<x86/avx.h>`, and simde maps those onto NEON. Whether that
actually works on DAO's code was the central Phase 2 risk. It does:

```
host     clang-cl, x86_64-pc-windows-msvc   208/208 ok   108,026,899 bytes    55s wall
android  NDK aarch64-linux-android33        208/208 ok    85,066,056 bytes   278s wall
```

Both compiled on the first attempt, no source changes. The objects are genuine
`ELF 64-bit LSB relocatable, ARM aarch64`, and the most vector-heavy translation unit
(`ppc_recomp.166.cpp`, 3,417 SSE intrinsic references) disassembles to real NEON:

```
1083 fmul    822 tbl     489 scvtf   479 dup     444 movi
 284 fadd    264 ext     237 fsub    104 sshll    77 fcvtzs
  71 bsl      53 ld1      19 rev32    16 smull
```

`tbl` is NEON's table lookup, which is what simde uses for VMX permute/shuffle, and
`rev32` is the big-endian byte swapping the Xbox 360 needs. This is what a correct
PowerPC VMX128 to NEON translation should look like.

Re-run with `python tools/compile_check.py --target both`.

**What this does not prove:** that the code is *correct* on ARM64, only that it is
accepted and generates sensible instructions. The Phase 2.3 hazard audit (`char`
signedness, `long double` width, denormal and rounding behaviour) is still outstanding,
and those produce subtly wrong results rather than compile errors.

### Phase 2 results — ARM64 is correct, not just compilable

Full write-up: [`docs/PROBE-ARM64.md`](docs/PROBE-ARM64.md). Reproduce with
`python tools/gen_difftest.py && python tools/run_difftest.py`.

6,635 recompiled functions were executed from an identical deterministic state on both
the workstation and the Thor, and the resulting PPC contexts compared:

```
struct layout       identical (sizeof(PPCContext)=2688, alignof=64)
compared            6635
agreeing            6549   (98.70%)
disagreeing           86   (1.30%)
faults              host 16,  android 0
genuine logic bugs     0
```

All 86 divergences are explained, and **in two of the four classes ARM64 is the more
faithful target than x86-64**:

| cause | count | note |
|---|---:|---|
| Fused multiply-add family | 37 | ARM fuses (one rounding); x86 without `-mfma` does two. **The 360's `fmadd` is fused, so ARM matches the original hardware.** |
| Approximate estimates (`vrefp128`, `vrsqrtefp128`) | 17 | Precision is architecture-defined by specification. Unavoidable. |
| Other floating point / FPSCR access | 16 | Mostly downstream of the above. |
| Integer divide by zero | 16 | **ARM's `udiv` returns 0 like the 360; x86's `div` raises #DE.** This is the entire host fault count. |

Hazards that turned out to be non-issues: **`long double`** (zero occurrences in all 207
translation units), **`char` signedness** (one use, `char(128)`, wraps to −128 on both),
**byte order** (explicit `__builtin_bswap*` in the load/store macros), and
**denormals/rounding** (XenonRecomp has a correct ARM64 FPCR implementation).

**Carry into Phase 3:** a divide-by-zero crash on the x86-64 reference build is not
necessarily a bug in the port.

**Still untested:** NaN/infinity/denormal payload propagation (deliberately excluded to
avoid noise), and any function with loops or calls, which needs the real runtime.

### Phase 3.1 results — the Vulkan graft compiles for ARM64

Full write-up: [`docs/GRAFT.md`](docs/GRAFT.md).

```
Vulkan backend scope      30/30 translation units compile for aarch64
Xenia internal shaders   121/121 compile to SPIR-V
whole assembled tree     111/187 translation units
```

The tree is assembled, never committed: DAORecomp's 665-file Xenia subset, plus 72
files from Canary, plus **17 edits**. Build order matters and the assembler says so:

```bash
python tools/assemble_runtime.py        # wipes and rebuilds the tree
python tools/build_xenia_shaders.py     # six backend files need these headers
python tools/compile_runtime.py --target android
```

**The graft turned out far smaller than the first estimate.** The include graph
initially reached 74,219 lines across `xenia/kernel`, `cpu`, `vfs`, `apu` and `hid` —
the whole emulator. Tracing *which* edges caused that found only six, almost all in
`vulkan_command_processor.cc`. With those cut, the graft is **58 files needing no
supporting headers at all**, because every header it reaches that DAORecomp lacks is
itself in the graft set.

DAORecomp had already solved this for D3D12 in a way worth copying: three emulator
includes replaced by **`xenia/dao_host.h`**, a 28-line interface of four functions
(`AttachGuestThread`, `DispatchInterrupt`, `TitleId`, `ShortSleep`). The GPU code needs
nothing else from the emulator.

Two genuine upstream bugs were found and fixed, both of which only appear outside the
configuration their authors build:

- **`vulkan_api.h` include order.** Xenia includes `vulkan_to_string.hpp` first, which
  sets its guard and then includes `vulkan.hpp`; `vulkan.hpp`'s own re-include of
  `vulkan_to_string.hpp` is then skipped and it fails on an undeclared `to_string`.
- **`dao_resolve_direct.xesli` uses HLSL `saturate()`** where every other Xenia shader
  uses the portable `saturate_xe`. Compiles only in a Direct3D-only build.

**No Vulkan SDK needed for the shaders.** The NDK ships `glslc`, `spirv-opt` and
`spirv-dis`, so `tools/build_xenia_shaders.py` is Xenia's own SPIR-V pipeline with
glslc substituted for glslangValidator.

Also worth knowing: Xenia's Vulkan loader already ships an
`instance_khr_android_surface.inc` entry-point list, and `xenia/base/platform.h`
already detects `__ANDROID__` and sets `XE_PLATFORM_ANDROID`. No platform defines are
needed on the command line.

### Phase 5.1g — stubs fail instead of succeeding, and startup stops crashing

`tools/gen_import_stubs.py` now gives each stub its import's **failure** value:
`STATUS_NOT_IMPLEMENTED` for an `NTSTATUS`, `ERROR_FUNCTION_FAILED` for a Win32 error,
`E_NOTIMPL` for an `HRESULT`, 0 for a pointer, handle or value, nothing for `void`.
The family is read from Xenia's kernel rather than from the name - the result type of
each `<Name>_entry`, and for a plain dword what it returns, followed through helpers,
other exports, methods declared as a status, and variables assigned from them:

```
251 imports: value 111, ntstatus 52, void 41, win32 33, hresult 10, pointer 4; 11 not in Xenia, classified by name
```

`python tools/gen_import_stubs.py --list` shows every import's family and the Xenia
line it came from. Two are overridden, with the reason in the script:
`RtlMultiByteToUnicodeN` and `RtlUnicodeToMultiByteN` return an `NTSTATUS`, but Xenia's
versions return a bare 0, which reads as a value. The name alone would have been wrong often: `Xam*` mostly return
Win32 errors, `XGetGameRegion` and `KeSetEvent` return values, `RtlInitAnsiString`
nothing; and the first pass, reading only the export's own body, missed `NtOpenFile`
(returns through `NtCreateFile_entry`), `MmQueryStatistics` (through a helper) and
`NtClose` (through a method).

**Result, identical over three runs: no crash.** The title gets the failures and takes
its own error paths, reaching sixteen imports:

```
NtCreateEvent        STATUS_NOT_IMPLEMENTED   RtlInitAnsiString          (void)
RtlNtStatusToDosError  0                      NtOpenFile                 STATUS_NOT_IMPLEMENTED
NtCreateSemaphore    STATUS_NOT_IMPLEMENTED   RtlRaiseException          (void)
KeQueryPerformanceFrequency  0                ExCreateThread             STATUS_NOT_IMPLEMENTED
XamNotifyCreateListener  0                    ObReferenceObjectByHandle  STATUS_NOT_IMPLEMENTED
XamUserGetSigninInfo E_NOTIMPL                NtReleaseSemaphore         STATUS_NOT_IMPLEMENTED
ExGetXConfigSetting  STATUS_NOT_IMPLEMENTED   NtWaitForSingleObjectEx    STATUS_NOT_IMPLEMENTED
XGetGameRegion       0                        KeDelayExecutionThread     STATUS_NOT_IMPLEMENTED
```

and then **spinning** - the guest thread at 100% of the prime core, because the
`KeDelayExecutionThread` stub returns at once rather than sleeping.

The first failure that matters is **`NtOpenFile`**: the title answers it with
`RtlRaiseException`. That stub is `void` and returns, but the real one never returns -
it unwinds to a handler - so everything after it runs on a path the title never meant
to reach (BUG-032). So the filesystem is next, and the imports after the raise are not
yet a reliable work list.

> **Corrected the same day (Phase 5.2, below):** the raise was not an error. It was
> `0x406D1388`, the MSVC thread-naming convention - the title naming its thread
> `Eclipse::StartupThread` - which a title raises on purpose and continues from. So the
> imports after it *are* the title's normal path, and the list is a work list after
> all. The paragraph above is kept as written, because the log is a record.

### Phase 5.1h and 5.2 — threads, objects and files: the title loads its packages

Done in one long session on 2026-10-23, each step named by the previous one's log.

**Threads and kernel objects** (`warden_threads.cc`, `warden_objects.cc`).
`ExCreateThread` runs a real host thread per guest thread, pinned to the performance
cluster, on a guest stack of its own from the heap (BUG-031 fixed), through the title's
XAPI trampoline (`sub_82AB8B30`: call the start, then `ExTerminateThread`, which exits
the host thread from inside the guest frames). Suspended creation waits for
`NtResumeThread`. Events and semaphores live in the object table with one wait mutex
and one condition variable between them; the waits are `NtWaitForSingleObjectEx`,
`NtWaitForMultipleObjectsEx`, `KeWaitForSingleObject`, `KeWaitForMultipleObjects`.
`KeDelayExecutionThread` sleeps; the title's `Sleep(INFINITE)` arrives as `INT64_MIN`.

Two things the title does that the kernel has to meet it on:

- **It keeps events inside its own structures**, initialised in place, and hands the
  kernel their addresses - `KeWaitForSingleObject(BEA9C580)` before anything created
  BEA9C580. `ObjectAtGuestBody` adopts such an object from its dispatcher header (type
  at +0, signal state at +4, a semaphore's limit at +16), as Xenia's `GetNativeObject`
  does. Before that, the D3D threads spun on an "unknown" event.
- **It calls imports through function pointers** as well as directly: the recompiled
  function table maps each import thunk (`0x831F41AC`) straight to its `__imp__`
  function, so an indirect call to a thunk lands in the implementation. `NtReadFile`
  has no direct call site in the title and is reached that way.

**What the engine starts:** `Eclipse::StartupThread` (the boot thread's name), then
`Eclipse::Update`, `FileIO`, `Streaming`, `Network`, `WorkerThread0/1` and
`DragonAge::Render`, each on its own semaphore; later Direct3D's two threads and two
networking threads. The boot thread parks in `Sleep(INFINITE)` once the others are
away. Startup is driven by the Update thread.

**What stopped the Update thread, in order, each found by the log:**

1. **`ExGetXConfigSetting`** - the stub's refusal cancelled Direct3D device creation:
   `sub_82DA4800` asks for the user video flags and treats a failing status as "no
   display", tears the device down, and the renderer then calls through a device it no
   longer has. Implemented with Xenia's defaults (NTSC-M, English, widescreen, Dolby,
   parental controls off, UTC), sized by the title's own calls.
2. **The video driver** (`warden_video.cc`): `XGetVideoMode` / `VdQueryVideoMode` answer
   1280x720 widescreen 60 Hz; `VdInitializeEngines` returns 1; the interrupt callback
   and command-buffer identifier are remembered for the GPU work to come.
   `MmGetPhysicalAddress` is real - the stub's 0 was what the ring buffer was being
   set up on.
3. **The file reads** (`warden_fs.cc`): `NtCreateFile` (nine arguments, the ninth on the
   stack), `NtReadFile`, `NtReadFileScatter`, `NtQueryInformationFile`,
   `NtSetInformationFile` (position only - a seek), `NtQueryFullAttributesFile`,
   `NtQueryVolumeInformationFile`; writes refused, the disc being read-only. Host files
   open on first read. `RtlNtStatusToDosError` is real too - the stub's 0 turned every
   failure the title converted into `ERROR_SUCCESS`.

**The run that resulted**, this build on the Thor:

```
NtCreateFile("D:\splash.dds")                 -> read in one 460,928-byte call
NtCreateFile("D:\packages\core\data\shaders.erf")   529 reads, 540 KB
NtReadFile(guiexport.erf)   46 reads, 2.5 MB    models.erf   9 reads, 2.1 MB
core_en-us.tlk  1 read, 1.2 MB   materials.erf  477 reads   ... 20 archives in all
NtCreateFile("D:\packages\core\data\movies\edge_logo_withaudio.bik")  -> the intro movie
XamInputGetState, XAudioGetSpeakerConfig, XAudioRegisterRenderDriverClient,
XamUserGetSigninState, XamUserGetXUID, XamUserCreateAchievementEnumerator,
NetDll_WSAWaitForMultipleEvents, NetDll_WSAResetEvent   - all stubs, the title parked
```

Two directory probes look odd and are the title's own: `D:\packages\core\patchULES\`
(TU7's overlay is `patch\`; the suffix may be a region code `XGetGameRegion`'s stub does
not supply) and `D:\ore\data\`. Neither string is in the image; both are built at
runtime, and neither stopped anything.

**And one crash that is timing-dependent (BUG-034).** The build before this one died
every time in the renderer's first shader - `sub_8269FA68` calling a null vtable - with
the shader data pointer, recovered from the saved registers, pointing at zeros. The
shaders are `xenonreload.vsh.fxo` / `.psh.fxo` in `shaders.erf`, and that build had
opened no package at all before the request. The next build differed only by the guest
trace (a store per guest call, so slower) and mounted everything. Thread-named file
logs the next day showed the mount is one synchronous pass on the Update thread, and
that each logged call had been costing 10 ms in `xe::FlushLog()` - which is the timing.
The per-line flushes are gone; the likely reading (another thread's shader request
outrunning the mount) and what to do on a recurrence are in `docs/BUGS.md`.

**Diagnostics added for all of this**, all in the fault report: the saved r26-r31 of
the newest frame with the words each points at; a per-thread ring of the last 512
guest functions entered (`WARDEN_GUEST_TRACE`, a shadow `ppc_context.h` whose prologue
records `__func__`); and budgeted logs of every wait, signal, open and read with the
calling thread's name. `boot_repeat.sh` now compares runs by stubs reached, and that
count varies with thread scheduling - benign, but it no longer proves determinism.

### Phase 5.2 begins — what the title opens, and what actually stops it

`RtlInitAnsiString` is implemented (it fills the caller's `ANSI_STRING` and logs the
first 64 strings), and so is `RtlRaiseException`: a thread name is logged and returned
from, as Xenia does; anything else is logged - for a C++ `throw`, with the thrown type
decoded from its `ThrowInfo` - with the guest call chain, and the guest thread stops,
since nothing can unwind to the title's handler yet (BUG-032). Identical over three
runs:

```
RtlInitAnsiString("D:\packages\core\audio\vo\*")
NtOpenFile - returns STATUS_NOT_IMPLEMENTED
RtlInitAnsiString("D:\NS\*")
RtlRaiseException: thread named "Eclipse::StartupThread" (thread id FFFFFFFF)
ExCreateThread - returns STATUS_NOT_IMPLEMENTED
ObReferenceObjectByHandle, NtReleaseSemaphore, NtWaitForSingleObjectEx,
KeDelayExecutionThread - then the guest thread spins
```

- **`D:\` is the game's root** - `packages\core\audio\vo` is on the disc
  (`docs/ASSETS.md`). The first listing looks like voice-over language discovery. `NS`
  is not on the disc at all, so that listing should fail, and does.
- **The title tolerates both listings failing** and carries on. Its engine is BioWare's
  *Eclipse*.
- **What stops it is threading.** It cannot create its startup thread, and then waits
  for it - on a semaphore the stubs also failed to create - and sleeps in a loop the
  `KeDelayExecutionThread` stub cannot slow.

So two pieces stand between here and the title's next stage: the filesystem, for the
listings and everything after, and guest threads with their events and semaphores.

**Then `D:\` became the extracted disc** (`warden_fs.cc`, with handles in a shared
object table, `warden_objects.cc`, and a real `NtClose`). Identical over three runs:

```
guest D:\ is /storage/emulated/0/Android/data/dev.daorecompandroid.duo/files/gamedata
NtOpenFile("D:\packages\core\audio\vo\", access 00100001, options 00004021) -> 00000000 handle F8000004
NtQueryDirectoryFile(D:\packages\core\audio\vo, "*") -> 00000000 "en-us"
NtQueryDirectoryFile(D:\packages\core\audio\vo, "") -> 80000006       STATUS_NO_MORE_FILES
NtOpenFile("D:\NS\", access 00100001, options 00004021) -> C0000034  STATUS_OBJECT_NAME_NOT_FOUND
```

Both right: the disc's voice-over folder holds exactly `en-us`, and there is no `NS`.
`D:\`, `game:\` and `\Device\Cdrom0\` map to the game data root; components match
without regard to case; opens asking for write access are refused, as a disc refuses
them; attributes are the disc's own (`0x80` files, `0x90` directories, plus `READONLY`
as Xenia adds), measured with `tools/dao_iso.py`.

**`NtOpenFile` takes six arguments, not Xenia's five.** The title's own call
(`sub_82AB8170`, its `FindFirstFile`) sets `r7 = 3` - share access, read and write - and
`r8 = 0x4021` - directory, synchronous, backup intent. Xenia's signature reads the share
access as the open options (BUG-033).

After the listings the title names its thread and fails `ExCreateThread`, as before -
which is now the only thing in the way.

### Phase 5.1f — the code is read-only, and the writer is `FindFirstFile`

The title's code range (`82210000`-`83204000`, whole pages) is `mprotect`ed read-only
just before the entry point is called - as the console's hypervisor keeps it - and the
fault handler's guest-address path now prints registers and the call chain. The first
boot with it stopped at the first write:

```
guest fault: guest address 82210000
  this is a WRITE INTO THE TITLE'S CODE ... the chain below is the writer
  guest lr=82AB8144 r1=700FF480 r3=8220FFFC r4=8220FDD0
    #0  82AB229C in sub_82AB2228  called sub_82AB80D0
    #1  826247CC in sub_826246F8  called sub_82AB2228
```

- `sub_826246F8` is a directory walk in the game's own code. `sub_82AB2228` is the
  title's **`FindFirstFile`** (it calls `RtlInitAnsiString`, opens the directory, and
  converts the first entry); `sub_82AB21D0` beside it is `FindNextFile`.
- `sub_82AB80D0` converts a `FILE_DIRECTORY_INFORMATION` into a `WIN32_FIND_DATA`:
  attributes from `+56`, times from `+8`/`+16`/`+24`, size from `+40`/`+44`, then
  `memcpy(find + 44, entry + 64, [entry + 60])` - the name, `FileNameLength` bytes long.
- The entry is the buffer the **`NtQueryDirectoryFile` stub** returned `STATUS_SUCCESS`
  for and never wrote, so that length is whatever was on the stack. `r3`/`r4` are
  `memcpy`'s running pointers, `0x240` apart - the distance between the two stack
  buffers - and they have climbed from the stack at `0x700Fxxxx` to `0x8220FFFC`: a
  forward copy smearing a 0x240-byte window of stack upward through ~300 MB of guest
  memory, over the image's data, and into its code. That is how `82AB21EC` came to hold
  `700FF760`.

So BUG-030 is confirmed as the cause of the second crash, as it was of the first. (`r5`,
the length itself, is not in the dump; the conclusion rests on the code and the two
pointers.)

### Phase 5.1e — the guest call chain, and the crash was `MmQueryStatistics`

The fault handler now walks the guest stack (`warden_guest_registers.cc`). On the
first boot it printed, every frame confirmed by the `bl` in the frame above it:

```
guest call chain from r1=700FFBE0
  #0  8251A60C in sub_8251A5D8  called sub_8251B240
  #1  82519A40 in sub_825199E8  called sub_8251A5D8
  #2  831EB9D0 in sub_831EB9A8  called sub_825199E8
  #3  82AB8548 in sub_82AB8490  called indirectly (bctrl)
  #4  82AB26B4 in sub_82AB2650  called sub_82AB8490
  #5  (the thread's entry - returns nowhere)
```

So the frame at `r1` is **`sub_8251B240`** - the function the very first tombstone
named. Reading its body after `8251B26C` accounts for every register in the dump:
`bl 0x8251c810` returns **0** (`r30 = r3 = 0`), `r31 = 0` skips the first call, then
`lwz r11,0(r30)` / `lwz r11,24(r11)` / `bctrl` is a virtual call through a **null
object** - `r4 = r29 = 0x20`, exactly as dumped. Its `ctx.lr = 8251B2B4` store is the
one the compiler sank past the faulting load.

The null comes from the singleton getter `sub_8251C810`: it constructs through
`sub_8251C748`, which does `operator new(24)` (`sub_826D00D8`), and **that returned
null** - and `sub_8251C748` then reports success anyway, so the null is cached and
returned. `operator new` reaches the title's allocator (`sub_826619C8`), which on first
use initialises itself in `sub_826613C0`, which calls `sub_82AB4A00` - a
`GlobalMemoryStatus` wrapper around **`MmQueryStatistics`**. The stub wrote nothing and
returned 0, i.e. `STATUS_SUCCESS`, so the allocator sized itself against a console
reporting no memory.

So the retraction of 5.1c was right that `sub_8251C810` is not the faulting function
and that its zero cache slot is normal - but the null object did come out of it. And
the import-table suspect (5.1d) is not the cause.

**The layout was in this checkout after all**: Xenia's `X_MM_QUERY_STATISTICS_RESULT`,
`third_party/xenia-canary/src/xenia/kernel/xboxkrnl/xboxkrnl_memory.h`, 104 bytes -
the length the title writes into `size` before calling. Implemented after Xenia's
values, except that available pages are what this port's physical heap can still hand
out (about 21 MB at that point), not 512 MB less what is used (about 295 MB), which
the heap could not honour.

**Result, identical over three runs:** the allocator works, the title makes a 15 MB
physical allocation sized to what it was told, and startup runs on through ten more
imports - `NtCreateEvent`, `NtCreateSemaphore`, `KeQueryPerformanceFrequency`,
`XamNotifyCreateListener`, `XamUserGetSigninInfo`, `ExGetXConfigSetting`,
`XGetGameRegion`, `RtlInitAnsiString`, `NtOpenFile`, `NtQueryDirectoryFile` - before the
next crash, again a null indirect call (`lr=82AB21F0`).

**And that one is memory corruption.** The walk's first frame is real - `82AB21F0`
follows `bl 0x82ab82f8` in `sub_82AB21D0` - but the word now at `82AB21EC` is
`700FF760`, a guest stack address, and `826248B0` holds `0000001F`. The title's code
has been overwritten since it was loaded. All ten new imports are weak stubs returning
0 - `STATUS_SUCCESS` - without writing their outputs (BUG-030), so the title is
acting on a string, a file handle and directory entries nobody wrote.

### Phase 5.1b / 5.1c — null calls name themselves; one hypothesis disproved

**5.1b is done**, and better than planned. The plan said to fill unmapped table slots
with a trap stub; a decoder is cheaper and tells you more, because the table offset is
**invertible**. The handler now says:

```
non-guest fault: host 0x7555933000, base 0x75d6953000, base-2164391936
  this is an INDIRECT CALL to guest 00000000 whose table slot is empty
  guest lr=8251B26C r1=700FFBE0 r3=00000000 r4=00000020
```

No memory, no per-address thunks, nothing to keep in step with the table. The decoder
re-derives the fault address from the guest address it recovered and only believes
itself if that matches exactly — it runs on unrelated crashes too, and a confident
wrong answer about why something crashed is worse than no answer.

> **Retracted — see the session-24 log entry and `RESUMING.md` §2.1.** Everything below
> about `0x832F13CC` reasons from a function that was never the faulting one.
> `tu6_sub_8251C810` is a lazily-initialised singleton getter: it tests that slot for
> zero, constructs the object and caches it back, so zero is its correct empty state.
> It also contains no indirect branch, so the null call is not in it. Kept because the
> import-table measurement inside it is still valid and is how 5.1d was found.

**5.1c: one hypothesis tested and disproved.** The faulting function calls through
pointers derived from `lis r30,-31953; lwz r28,5068(r30)` — guest `0x832F13CC`. The
obvious candidate was the **XEX import table**, which a real loader patches and this
runtime does not. Checked against the table in the executable's headers:

```
xam.xex          200 imports, addresses 82000600..8312A944
xboxkrnl.exe     314 imports, addresses 82000794..8312AA34
total import slots: 514
is 0x832F13CC an import slot? False
nearest slots: none within 0x4000
```

So that load is **not** an import slot. `0x832F13CC` is ordinary image data, loaded
verbatim from the executable, which the title expects to hold a pointer by the time it
reads it. The remaining explanation is ordering *within* startup — something that
should have written it has not run, or has not got that far.

**The address is `0x832F13CC`** — `lis r30,-31953` loads `0x832F0000` (−31953 is
`0x832F` in 16-bit two's complement), and an earlier note here said `0x833713CC`, from
a botched two's-complement conversion. (A later pass fixing that address rewrote both
halves of this sentence into "X, not X"; a global replace is a poor tool for correcting
a claim that names the wrong value.)

**And the loaded image holds zero there**, in a zeroed region — `0x832F13BC` through
`0x832F13DC` are all zero in the patched image. So this is uninitialised data the title
means to populate at runtime, and it is calling through a function pointer that nothing
has written yet.

Which narrows the cause to **ordering within startup**: `_tu6_xstart` is the CRT entry,
so static initialisers should run as part of it, and the one that fills this has not.
Something earlier is being skipped or failing quietly. That is the thread to pull.

**But the check found a real gap on the way.** Those 514 import slots are patched by
the console's loader and nothing here patches them. XenonRecomp handles imported
*functions* at translation time, through the `__imp__` calls — but imported
**variables** are data, and a title reading one would read whatever the image holds.
That is not this crash, and it is still something nobody has addressed.

### Phase 5.1a — the fault diagnoses itself

A tombstone gives *host* registers, which have to be mapped back to guest ones by hand
through generated code. But the fault handler already installed for the write watch
runs on the **faulting** thread — and for a guest fault that is a guest thread, whose
`PPCContext` is right there in thread-local storage. So it can simply say:

```
non-guest fault: host 0x7558673000, base 0x75d9693000, base-2164391936
  guest lr=8251B26C r1=700FFBE0 r3=00000000 r4=00000020
  guest r11=00000000 r12=8251B26C r29=00000020 r30=00000000 r31=00000000
  guest r3.u64=0000000000000000 r11.u64=0000000000000000 r31.u64=0000000000000000
```

Three kinds of fault are now distinguished: a watched physical page (serviced, the
write retries), a guest address that is not watched, and an address outside the guest
space altogether — reported with its signed distance from the base, because that
distance identifies the arithmetic that produced it.

**What it establishes, which is more than guesswork had:**

- **The stack is fine.** `r1 = 700FFBE0` — the `0x70100000` top less `0x420` of
  frames. So the "bare stack with no backchain" hypothesis is **ruled out**.
- **`lr = 8251B26C` is the return address for the `bl 0x8251c810` at `8251B268`**, so
  the fault is at or below that call. *Later correction:* this was read as "inside
  `tu6_sub_8251C810`", which does not follow — and `ctx.lr` is a store the compiler may
  elide, so it bounds the fault rather than locating it.
- **Every relevant register is zero**, 64-bit halves included. So the bad address is
  *not* a guest register that failed to be truncated to 32 bits, which was the other
  hypothesis.
- **The distance from the base is identical across runs** (`-2164391936` exactly), so
  the faulting expression is `base + a constant`, not `base + a register`.

**Confirmed, not a hypothesis.** The fault address is exactly what the indirect-call
macro produces for a target of zero:

```
PPC_LOOKUP_FUNC(base, 0) with 64-bit arithmetic = 0x7558673000
observed fault address                          = 0x7558673000
EXACT MATCH
```

So the title executes `bctrl` through a function pointer it read as **zero**, inside
`tu6_sub_8251C810`.

The arithmetic is worth writing down, because it is counter-intuitive. The macro is

```c
*(PPCFunc**)(base + PPC_IMAGE_BASE + PPC_IMAGE_SIZE +
             (uint64_t(uint32_t(y) - PPC_CODE_BASE) * 2))
```

and `PPC_CODE_BASE` is `0x82210000`**ull**. So `uint32_t(y) - PPC_CODE_BASE` is
evaluated in **64-bit unsigned** and does *not* wrap at 32 bits: for `y = 0` it is
`0xFFFFFFFF7DDF0000`, doubled to `0xFFFFFFFEFBBE0000`, and `base` plus that lands 2^33
below where a 32-bit model puts it. A first prediction that masked to 32 bits missed by
exactly 2^33 — which is what prompted checking the types rather than accepting a
near-match.

**The practical consequence is worth acting on.** A null indirect call does not fault
at a null address: it faults at `base - 0x81020000`, which looks like a wild pointer and
says nothing. The table is 31 MB of zeroed memory at a fixed offset, so *every* null
call lands in the same uninformative place. Consider having `BuildFunctionTable` fill
unmapped slots with a trap stub that names the guest address instead of leaving them
zero — that would turn this whole investigation into one log line, and it is a few lines
in `warden_guest.cc`.

**Then find where the zero comes from.** `tu6_sub_8251C810` loads via
`lis r30,-31953; lwz r28,5068(r30)` — guest `0x832F13CC`, inside the image — and calls
through pointers derived from similar loads. So the likely answer is a guest structure
the title expects to have been initialised, or the one import still stubbed,
`MmQueryStatistics`.

### Phase 5.1 — eleven imports done, and the crash is not an import problem

Imports hitting stubs: **12 → 1**. Implemented, in the order the title reaches them —
`KeGetCurrentProcessType`, the five `Rtl` critical-section calls,
`XexCheckExecutablePrivilege`, the four `KeTls*`, `MmAllocatePhysicalMemoryEx`. All
fire with plausible arguments:

```
NtAllocateVirtualMemory(00000000, 100000, type 60002000) -> 40000000 100000 status 0
NtAllocateVirtualMemory(40000000,  10000, type 60001000) -> 40000000  10000 status 0
XexCheckExecutablePrivilege(10) -> not granted
MmAllocatePhysicalMemoryEx(size D600000, align 1000) -> B0000000
MmAllocatePhysicalMemoryEx(size 20, align 20)        -> BD600000
MmAllocatePhysicalMemoryEx(size 200000, align 80)    -> BD601000
```

That first physical allocation is **214 MB** — the title sizing its own pools against a
512 MB console, which suggests it is reading its own configuration rather than
stumbling through. Details and the deliberate omissions are in
[`docs/IMPORTS.md`](docs/IMPORTS.md); `MmQueryStatistics` is left as a stub because its
structure layout is not documented anywhere in this checkout, and the critical sections
keep their lock host-side for the same reason.

**And the crash has not moved.** `tu6_sub_8251B240+416`, identically, whether twelve
imports are stubbed or one. So it is not an import returning the wrong thing, and
working further down the list will not fix it — which is a more useful result than
another import would have been.

What it points at is the state the entry point is *entered* with, and none of this has
been checked yet: the stack is a bare `r1 = 0x70100000` with no backchain word written,
`r13` is zero where the Xbox 360 CRT may expect a small-data pointer, `lr` is zero so a
`blr` would branch to guest address 0, and the `PPCContext` is otherwise zeroed where a
real loader hands the entry point more. `RESUMING.md` §2 has the list and the route in:
each translated instruction in `recomp/generated/ppc_tu6/` carries the original as a
comment, so the faulting access can be read without a disassembler.

### Phase 5.0h — guest code runs, and the import log names the work

**The recompiled game executed on the handheld for the first time.** The title's own
entry point, then five frames of translated PowerPC:

```
calling the title entry point 82AB2650 - guest code starts here
_tu6_xstart
__imp__tu6_sub_82AB8490
__imp__tu6_sub_831EB9A8
__imp__tu6_sub_825199E8
tu6_sub_8251A5D8
tu6_sub_8251B240          <- SIGSEGV, SEGV_MAPERR, fault 0x7557c7e000
```

**And the import log did exactly what it was built for.** Fourteen kernel imports
reached, in this order — a recognisable C runtime startup:

   1. `NtAllocateVirtualMemory`      8. `RtlInitializeCriticalSectionAndSpinCount`
   2. `KeGetCurrentProcessType`      9. `KeTlsAlloc`
   3. `RtlInitializeCriticalSection` 10. `KeTlsSetValue`
   4. `HalReturnToFirmware`          11. `KeTlsFree`
   5. `RtlEnterCriticalSection`      12. `RtlTryEnterCriticalSection`
   6. `RtlLeaveCriticalSection`      13. `MmAllocatePhysicalMemoryEx`
   7. `XexCheckExecutablePrivilege`  14. `MmQueryStatistics`

`NtAllocateVirtualMemory` first, and `HalReturnToFirmware` — which is *reboot the
console* — fourth. The title asked for memory, the stub returned 0 because a stub
cannot know what 0 means to its caller, and the title headed for the exit. The crash
six frames later is downstream of that same lie: the fault address `0x7557c7e000` is
*below* the guest base `0x75d801e000`, so it is a bad host pointer rather than
`base + a guest address`.

So this is not a mystery to debug but a list to work through, which is the whole
reason the stubs count their own calls. Fourteen functions, mostly mechanical: a guest
heap allocator, critical sections over pthread mutexes, TLS slots.

**Booting is opt-in**, `am start ... --ez boot true`, and deliberately not the default:
once guest code runs the process crashes, and making that the default would turn every
routine bring-up capture into a crash report and destroy its value as a baseline. The
call happens 1500 ms after the report is on screen and in logcat, so the report
survives. The boot thread is pinned to the prime core and detached — the entry point
does not return on real hardware, so there is nothing to join.

### Phase 5.0g — the indirect-call table, and a callable entry point

```
GUEST BOOT
  indirect-call table            53108 entries at 83400000 (31 MB)
  entry point                    82AB2650 found in the function table
  state                          ready to call
```

**A required step that was not in the plan.** XenonRecomp does not resolve indirect
calls at translation time. It emits

```c
PPC_LOOKUP_FUNC(base, y) = *(PPCFunc**)(base + IMAGE_BASE + IMAGE_SIZE +
                                        (y - CODE_BASE) * 2)
```

so every `bctrl` the title executes reads a host function pointer **out of guest
memory**. That table does not exist until the runtime writes it, and nothing announces
the fact — the first indirect call would simply jump through whatever happened to be in
those bytes. It is 31 MB at `0x83400000`, two bytes of table per byte of guest code.

Zeroed before filling, so an indirect call to an address that was never translated
gives a diagnosable null rather than a jump into stale data. Its bounds come from the
generated header's own `PPC_IMAGE_BASE` / `PPC_IMAGE_SIZE` / `PPC_CODE_BASE` /
`PPC_CODE_SIZE` rather than being written out, so a different recompilation cannot
silently disagree with the runtime about where it goes.

Built **only when the image matches the linked build**. From a mismatched image it
would hold pointers to functions whose addresses mean something else — a worse failure
than not building it.

`xe::dao_host::AttachGuestThread` is real now: a `PPCContext` and a guest stack at
`0x70100000`, the 256 KB the XEX header asks for, idempotent as the header promises. It
remembers the `Memory` the table was built against, because the GPU calls it from its
own threads and has no way to be handed one.

**The entry point is not called yet**, and the report says so rather than implying
otherwise. That is task 5.0h, and it is small: call `0x82AB2650` on a background thread
*after* the report has been logged, so that the import log survives the abort the first
unimplemented import will cause. That log — which imports the title reaches, in order —
is the whole reason for doing it.

### Phase 5.0f — the title update applies, and the image matches

```
image          loaded     decrypted and verified against the build

TITLE IMAGE
  loaded at                      82000000, 20971520 bytes
  entry point                    82AB2650
  stack requested                256 KB
  title update                   .../TU_12K2260_0000004000000.00000000001O1 (STFS)
  mflr r12 at entry              70.5% of 53108 entry points
  verdict                        matches the linked recompilation
```

**70.5%**, against the 1.8% the unpatched base image gave and the ~71% a matching pair
was predicted to give. The generation mismatch that blocked first light is cleared.

The app reads the update straight out of the LIVE package, so the only thing asked of
the user is to drop the file in. It checks for a loose `default.xexp` first (cheaper,
and what a host-side extraction leaves), then any LIVE/PIRS/CON package in the game
data directory, its parent, or app storage.

The patch is **identified, not assumed**: a candidate must be `default.xexp` at exactly
2,758,656 bytes, because Title Update 2 is also in circulation and is not usable — the
title itself passes over anything that is not TU6's or TU7's.

Both halves were verified on the host before anything ran on hardware:

```
tools/check_stfs.py                      26 files, every byte identical to
                                         DAORecomp's scripts/stfs.py
tools/check_xex.py --build ppc_tu6 --patch    843 of 843 groups agree
```

The STFS check compares **contents**, not just sizes, and that was deliberate: an STFS
file's blocks are threaded through hash tables and a fragmented file is read by
following a chain one block at a time, so getting the chain subtly wrong yields a file
of exactly the right length holding some of the wrong blocks — which a size comparison
cannot see.

One thing the first draft of that check got wrong, worth remembering: it **failed** the
repo's other package for not being TU6 or TU7. But TU2 had read perfectly — whether a
package is one this project can use is `verify_assets.py`'s job, and failing the reader
check for it was the wrong signal entirely. It now reports which update a package is
and judges only the reader.

**The patched executable is a different shape from the base one**, which is why the
loader handles both rather than out of caution:

| | Base (disc) | Patched (TU7) |
|---|---|---|
| Entry point | `0x829E8698` | **`0x82AB2650`** |
| Encryption | AES-128-CBC | **none** |
| Compression | basic, 4 blocks | **none, 1 block** |
| Assembled size | 19,759,104 | 20,971,520 |

XenonRecomp's `XexPatcher` is used rather than reimplemented, since it is what produced
`ppc_tu6`. `tools/gen_xex_patcher.py` trims its one Win32-only overload with two
asserted find/replace edits — the same discipline as `assemble_runtime.py`, so an
upstream change fails loudly instead of silently producing something different.

### Phase 5.0e — the image loads, and the app catches that it is the wrong one

The loader (`app/src/main/cpp/warden_xex.cc`) follows XenonRecomp's algorithm
deliberately — that is the code that produced the recompilation, so an image assembled
any other way could disagree with what the translated functions were written against.
Verified on the host with `python tools/check_xex.py`:

```
loaded: base 82000000, 19,759,104 bytes, entry 829E8698, enc 1 comp 1, 4 blocks
instruction groups with 2+ sites: 839
groups where every site agrees:   839
largest group: 'mflr r12' at 36,403 sites
  all encode to 7D8802A6
the image matches the recompiled source
```

**The method matters as much as the result.** A wrong key, a mis-walked header offset
or botched block arithmetic does not produce an error — it produces *noise*, and noise
is indistinguishable from code until something executes it. XenonRecomp quotes each
original instruction as a comment above the translated one, so grouping entry points by
that text and requiring every site in a group to hold identical bytes tests decryption,
key derivation, header offsets and block assembly together, with no opcode table and no
disassembler to write.

It earned its keep at once: a first attempt hand-added the field offsets inside
`Xex2SecurityInfo` and landed on `0x14C` instead of `0x150` — four bytes early, so the
image key was wrong, so the body decrypted to noise. The offsets are now built up field
by field with a `static_assert` on the total.

**Then it said something more useful: the image does not match the linked build.** Not a
loader bug — the same image matches `recomp/generated/ppc` perfectly. The app links
`ppc_tu6`, which XenonRecomp recompiled from the **Title Update 7 patched** image, and
the disc's `default.xex` is the base executable. What gave it away was that the
disagreement was *partial*: 16,043 distinct encodings for `mflr r12` rather than one, or
the 35,274 of a genuinely wrong key. AES-CBC does not decrypt partially, so partial
disagreement means related-but-different images, not broken decryption.

So the app now runs the same check at startup, from data it already has — walk the
53,108-entry function mapping table, read the first word at each entry point, count how
many are `0x7D8802A6`:

```
TITLE IMAGE
  loaded at                      82000000, 19759104 bytes
  entry point                    829E8698
  stack requested                256 KB
  mflr r12 at entry              1.8% of 53108 entry points
  verdict                        DOES NOT match the linked build
```

1.8% against the ~71% a matched pair gives, and the report says what is needed rather
than leaving a bare mismatch to be chased.

**Next (5.0f):** XenonRecomp's `XexPatcher::apply` is bytes-in, bytes-out and
self-contained but for `lzxDecompress`, which is defined in the same file and needs
libmspack's `lzxd.c`. Both compile for Android, so the patch can be applied on-device —
once `title_update/default.xexp` is there, which is the same gap already recorded for
the TU7 patch overlay.

### Phase 5.0d — the recompiled game is in the APK

```
defined symbols                  157,061
recompiled guest functions        52,620
libwarden.so                 160,863,528 bytes
app-debug.apk                199,109,371 bytes
startup() = true in 133 ms
no crashes, no native fatals
```

One Android library now holds 118 runtime translation units, 208 recompiled guest
units, 251 import stubs and 14 app sources. It installs, loads, and still presents.
Startup went from 97 ms to 133 ms, which is the cost of the larger library.

The guest code is compiled with **exactly** what Phase 2 differentially tested — C++17,
`-O2`, warnings off — and deliberately not with the build type's flags. Its behaviour
was validated at those settings by comparing 6,635 executed functions against the x86
reference; shipping something else would throw that away. `-g0` explicitly, because AGP
adds `-g` for debug builds and debug info for 265 MB of generated code runs to
gigabytes.

**Linking it enumerated the kernel boundary, which was the point.** 252 undefined
symbols: 251 kernel / XAM / video-driver / audio imports, plus `PPCQueryTimebase`.
Full write-up in [`docs/IMPORTS.md`](docs/IMPORTS.md).

| Group | Count | | Group | Count |
|---|---|---|---|---|
| `Net*` | **47** | | `Mm*` | 8 |
| `Xam*` | 42 | | `XAudio*` | 6 |
| `Nt*` | 30 | | `Ex*`, `XGet*`, `Ob*` | 4 each |
| `Ke*` | 28 | | `Xe*` | 3 |
| `Vd*` | 20 | | `Dbg*`, `Interlocked*` | 2 each |
| `Rtl*` | 20 | | `XNotify*`, `Hal*` | 1 each |
| `XMA*` | 16 | | other | 13 |

**The largest group is networking, in a game with no multiplayer.** 47 `NetDll_*` and
XNet imports: the engine's network layer linked into the executable whether or not it
is ever reached. That probably makes the biggest group the cheapest, since "there is no
network" is a complete and honest answer for all 47 — but *probably* is doing real work
in that sentence, which is what the import log exists to settle.

**Every stub is weak, and that is the design.** The library links today with nothing
implemented; implementing an import means writing it strongly in
`app/src/main/cpp/warden_imports.cc`, where it wins at link time with no registry to
update and no way for two lists to disagree. It is the same mechanism XenonRecomp
already uses for guest functions.

**Every stub counts its calls and logs the first.** A stub cannot know whether its
caller reads `0` as success, false or null, so it does not pretend to — it leaves `r3`
zero and says so in the log. The report then names every import the title actually
reached, in the order first seen:

```
KERNEL IMPORTS
  declared by the title          251
  reached so far                 0
  nothing has called one yet - no guest code has run
```

Which turns "implement 251 kernel functions" into "implement the ones in the way", in
the order they block progress.

The list itself is derived from `ppc_recomp_shared.h`, where XenonRecomp declares each
import — not scraped from linker output, which would be circular and, since lld caps
its error list and orders it arbitrarily, incomplete. The two agree exactly at 251,
which is a useful cross-check on both.

**Implemented so far:** `PPCQueryTimebase`, as `xe::Clock::QueryGuestTickCount()`, so
the guest and the GPU agree about what time it is. Phase 2's difftest deliberately
aborted in that function, because any guest function reaching the timebase was excluded
from comparison and arriving there meant the filter had a hole. At runtime it is simply
needed.

### Phase 5 groundwork — the renderer links, and guest memory works

**The link boundary, measured not guessed.** lld reports only 20 errors by default, so
the first two link attempts each showed a different arbitrary twenty. With
`--error-limit=0` (now set permanently in the CMake link options) the real list is
**65 undefined symbols**, in two very different piles:

| Count | What | Kind of work |
|---|---|---|
| 57 | glslang `spv::Builder` + typeinfo | a missing library, not porting |
| 4 | `xe::dao_host::*` | host runtime |
| 4 | `xe::Memory::*` | host runtime |

glslang was mechanical: Xenia's own `third_party/CMakeLists.txt` builds every
`SPIRV/*.cpp` except `GlslangToSpv.cpp`, and `compile_runtime.py` now applies the same
rule — so the source of truth stays where the other selection rules live.

**`xenia/memory.h` in the tree is not Xenia's.** DAORecomp replaced Xenia's 2,444-line
`memory.cc` with a 177-line interface, which means implementing it is our job — and its
own doc comment is the specification:

> one 4 GB host reservation where guest address A lives at membase + A, with the three
> physical views (0xA0000000, 0xC0000000, 0xE0000000 at +0x1000) mapped onto one 512 MB
> section

That is also exactly what the recompiled code wants. XenonRecomp emits
`PPC_FUNC(x) void x(PPCContext&, uint8_t* base)` and every guest access is
`base + address`, so the GPU and the guest share one flat base — because this *is* that
base. `app/src/main/cpp/warden_memory.cc`.

**Verified on the device**, which matters because two parts of it could plausibly not
work on Android at all:

```
0xA0000000 -> 0xC0000000         aliased
0xA0000000 -> 0xE0001000         aliased
0xC0000000 -> 0xA0000000         aliased
page reported read-only          yes
watched write completed          yes
invalidation callbacks fired     1
callback reported                00001000 + 1000
page writable again              yes
RESULT    aliasing and write watch both work
```

- **The aliasing is real** — a memfd mapped three times with `MAP_SHARED`, not a copy
  between views. Copying would work until the game wrote a texture through one view and
  the GPU read it through another, and then fail as intermittently stale geometry that
  nobody could reproduce. Tested in both directions so a stale read of the right value
  cannot pass.
- **The write watch works** — mprotect the page read-only, catch the fault, fire the
  invalidation callbacks, unprotect, return and let the instruction retry. This is how
  the renderer finds out that cached guest data changed, and it was the single riskiest
  mechanism in the port.

Three things needed care here that would not on desktop:

1. **SIGBUS as well as SIGSEGV.** A write to a read-only *shared* mapping can raise
   either, and which one is the kernel's business.
2. **Chain to the previous handlers, never replace them.** An unexplained hang inside a
   signal handler is far worse to debug than the segfault it swallowed. If there is
   nothing to chain to, restore the default and re-raise so the crash produces a real
   tombstone.
3. **`try_lock` on the fault path.** Blocking on the state mutex there would deadlock
   the process with no diagnostic if a thread faulted while already holding it.

**`xe::dao_host` is four functions, deliberately not stubbed alike.** `TitleId` returns
`0x454108C0`, which is on the disc. `ShortSleep` uses `clock_nanosleep` with
`TIMER_ABSTIME` — specifically because this process now delivers a signal on every
watched guest write, and a relative sleep restarted by those would run long.
`AttachGuestThread` and `DispatchInterrupt` abort: they give a host thread a guest
identity so it can run guest callbacks, and there is no guest code linked yet.
`DispatchInterrupt` quietly doing nothing would look like a game that renders but never
advances.

**What the renderer now contains:** 118 translation units, an 85 MB native library,
51,249 symbols — `VulkanCommandProcessor`, `VulkanRenderTargetCache`,
`SpirvShaderTranslator`, `PrimitiveProcessor`, the lot. Startup to a presenting Vulkan
device, with the 4 GiB reservation and the fault handler installed, is **97 ms**.

**The one thing everything now waits on** is task 5.0d: linking the recompiled title —
208 generated `.cpp` files, 265 MB, in `recomp/generated/ppc_tu6` (the TU7-compatible
build, see §4 on TU6 vs TU7). Until that is in, the two aborting `dao_host` functions
have nothing to attach to.

### Phase 4.3 / 4.4 — the game data is on the handheld

```
disc:   Dragon Age - Origins (World) (En,Es,It).iso
        9450 files, 6,859,144,771 bytes
device: /storage/emulated/0/Android/data/dev.daorecompandroid.duo/files/gamedata
        9450 files, 6,859,144,771 bytes
  every file present at exactly the size on the disc
extraction verified
```

`python tools/verify_extraction.py` reproduces that. 7m44s on the device, about
14 MB/s through FUSE.

**The size was wrong in the plan.** Not ~20 GB — **6.39 GiB**: `modules/` 4.03 GB in
1,658 files, `packages/` 2.81 GB in 7,789 files, `default.xex`, and two small odds and
ends. `$SystemUpdate/` is skipped, being the console's own update payload rather than
game data. [`docs/ASSETS.md`](docs/ASSETS.md) now carries the measured table.

**The XDVDFS reader is checked, not trusted.** `app/src/main/cpp/warden_xdvdfs.cc` is a
port of `tools/dao_iso.py`, and `python tools/check_xdvdfs.py --all` builds a host
harness from the very same source that ships in the APK and diffs its full recursive
listing — every path, every size, plus the partition base offset — against the Python:

```
Dragon Age - Origins ...iso        9668 lines identical: 9452 files, 215 dirs
Dragon Age - Origins - Awakening   12610 lines identical: 12320 files, 289 dirs
```

That is possible because the reader takes a `ReadAt` callable instead of owning a file
handle: Android hands it a descriptor, the host harness hands it a `FILE*`.

**On SAF being slow.** The common warning is half the story. It is slow only if every
read goes through a `ContentResolver`. `openFileDescriptor` returns a real, seekable
file descriptor, and from there native code uses `pread` like any other file — the
framework is in the way for exactly one call. So the 6.4 GiB extraction reads at disk
speed, not at content-provider speed.

Two routes to the image, direct path checked first because a plain descriptor has no
grant to lose and nothing to re-ask for after a reboot:

| Route | Needs |
|---|---|
| `getExternalFilesDir()` | nothing — app-owned, reachable over adb or MTP |
| SAF file pick | the disc image and the title update picked together (`ACTION_OPEN_DOCUMENT`); the image's grant persisted, the title update copied into app storage |

**Changed 2026-10-23:** the second route was a folder grant
(`ACTION_OPEN_DOCUMENT_TREE`), which Android 11+ refuses for the root of `Download` —
where the files actually are. See BUG-029.

**Resume works, both branches tested:**

```
stamp present    "already extracted", no scan at all
stamp removed    re-scan, 9450 already present, complete in 57 ms
```

Each file is written as `.part` and renamed on completion, so a destination file of
exactly the right size was put there by a write that finished. Without the rename, an
interrupted run leaves a short file at the real name and the resume check cannot tell
"short because interrupted" from "short because that is its size".

**A bug of mine, caught before it shipped.** Cancelling during a file write made
`ExtractFile` return false without setting a state, so the post-loop check still saw
`kExtracting` and wrote the completion stamp — a cancelled extraction recorded as
finished, and the next launch skipping straight past the missing files. Now recorded as
cancelled, and `Runtime::Shutdown` cancels and joins the thread rather than leaving it
writing into a process that is going away.

**Still owed for game data.** TU7 holds `default.xexp` (consumed host-side in Phase 1)
**and 25 data files under `packages/core/patch/`** — GDA tables, `.tlk` strings, a few
plot and item files. Those take precedence over the base ERFs in the engine's own lookup
order, so the device needs them. They are 3.3 MB and `dao_iso.py` already extracts them;
reading the STFS container natively is a bigger job than XDVDFS was (block hash tables
rather than a directory tree), so the overlay is copied in separately for now.

Note the handheld currently holds both the 7.3 GiB image and the 6.4 GiB extraction.
The image can be deleted once extraction is verified.

### Phase 4 — the app runs on the Thor and the renderer presents to it

Everything below is reproducible with one command:

```bash
./tools/appbringup/run.sh          # build, install, launch, capture, sample the screen
```

Full capture in [`device/android-app-bringup.txt`](device/android-app-bringup.txt).

**The renderer is presenting.** Xenia's own `VulkanPresenter`, on the handheld,
through a `SurfaceView`:

```
VulkanPresenter: Created 1920x1080 swapchain with format 37, color space 0,
                 presentation mode 1
paint connection size              1920x1080
swapchain connected                yes
VERDICT                            black - the presented letterbox, surface visible
```

That is `R8G8B8A8_UNORM` / `SRGB_NONLINEAR` / `MAILBOX`. Startup to Vulkan device is
**41 ms warm, 184 ms cold**.

**The presenter was supposed to be blocked and was not.** Xenia's `Presenter` requires
an `xe::ui::Window` (`SetWindowSurfaceFromUIThread` asserts a surface cannot exist
without one), and `window.cc` includes `third_party/imgui/imgui.h`, which appeared to
put all presentation behind Phase 10. But `window.cc` includes ImGui and then never
names `ImGui`, `ImDraw`, `ImVec` or `ImFont` in any of its 758 lines — the includes are
dead, and only broke the build because `third_party/imgui` is an unfetched submodule
here. One graft edit removes them.

What was actually required: `WindowedAppContext` has **two** pure virtuals (a pipe on
the main thread's `ALooper` covers the wake-up) and `Window` has **four**. Both are in
`app/src/main/cpp/`. Xenia's own `AndroidWindow` is not usable — it casts
`app_context()` to `AndroidWindowedAppContext` and asks it for the activity's surface,
so it is bound to Xenia's Java activity rather than ours.

**Colour: one graft edit changed the answer.** Before it, the Thor's surface reported 6
formats, every one `SRGB_NONLINEAR` — which reads as "this Display-P3 panel cannot do
wide gamut under Vulkan". It can. Xenia's instance never requested
`VK_EXT_swapchain_colorspace`, and without it the spec permits the driver to report
sRGB only. The Phase 0 capability dump had already recorded the extension as available,
which is the only reason the wrong answer was caught. After the edit: **12 formats,
including `A2B10G10R10_UNORM_PACK32` + `DISPLAY_P3_NONLINEAR`** — exactly D8's default.
See [BUG-023](docs/BUGS.md).

But available is not used: the presenter still chooses 8-bit sRGB, because its format
selection has no notion of preferring wide gamut. That is now a specific task (9.3)
rather than an assumption.

**HDR10 is not available here.** With the extension enabled,
`VK_COLOR_SPACE_HDR10_ST2084_EXT` is still absent, while Android's
`Display.HdrCapabilities` for the *same display* reports `HDR10 HLG` at 420 nits. PQ is
not mislabelled (which is what BUG-009 described from documentation) — it is simply
never offered. What is offered is `R16G16B16A16_SFLOAT` + `EXTENDED_SRGB_LINEAR`
(scRGB). [BUG-025](docs/BUGS.md), and 9.2 is re-scoped accordingly.

**DAORecomp's presentation features are Direct3D 12 only.** HDR, temporal upscaling
(FSR 3, FSR 4, XeSS, DLSS), motion vectors and frame generation are all under
`gpu/d3d12/` and `ui/d3d12/`, with no Vulkan counterpart;
`xenia/dao_temporal_upscaler.h` is 30 lines of enum and cvar. **FSR 1 (EASU + RCAS) is
the exception** — already built for Vulkan and already wired into the presenter's paint
path. So "port the features over" holds for FSR 1 and nothing else on this list; the
rest are new Vulkan implementations with the D3D12 code as reference.

This also means the `Presenter::Dao*` pacer functions can correctly do nothing: no
Vulkan path produces a generated frame. `app/src/main/cpp/dao_frame_pacer_android.cc`
implements that case and justifies each of the five individually. And it means **Swappy
does not replace `dao_frame_pacer.cc`** — Swappy paces presents against the display's
refresh; the pacer paces a generated frame against the real one behind it.

**Input (4.6).** `X_INPUT_GAMEPAD` exactly as `xenia/hid/input.h` defines it,
host-endian; the big-endian conversion belongs at the Phase 5 kernel shim. The mapping
table is positional — bit `kPadA` always means the bottom face button — because Android
names key codes by label and its generic HID layout maps usages the Xbox way. Mapping
self-test: **20/20**, including that buttons clear on release and that `VOLUME_UP` is
*not* consumed.

The handheld's own pad: **`vendor 0x2020 / product 0x0111 "Odin Controller"`**, analog
triggers and hat axes. It names itself an **Odin**, AYN's earlier handheld, not a Thor —
so Phase 7's glyph detection must key on vendor/product, not on the product name.

**CPU pinning (4.8).** `sched_setaffinity` is **accepted** on this device, verified by
pinning and reading the mask back. Clusters come from `cpuinfo_max_freq` rather than
hardcoded indices: prime `7` (3,187,200 kHz), performance `3-6` (2,803,200), efficiency
`0-2` (2,016,000).

**One lesson worth carrying forward.** A `setBackgroundColor` on the `SurfaceView` hid
every presented frame — a SurfaceView not Z-ordered on top shows its surface on a layer
*below* the window and punches a transparent hole for it, so a View background covers
it permanently. Every log said the renderer was working, and it was: presenting into a
layer nothing could see. Only looking at the panel revealed it. `appbringup/run.sh` now
samples pixels off the device and prints a verdict, so "a frame was presented" and
"something is visible" are checked as the separate claims they are. [BUG-026](docs/BUGS.md).

### Phase 3.3a — the top risk is smaller than assumed

Full write-up: [`docs/EDRAM.md`](docs/EDRAM.md). Same probe:
`./tools/vkdevice/run.sh`.

R1b was that Adreno 740 forces Xenia onto `kHostRenderTargets`, which upstream
documents as possibly *"irreparably inaccurate ... at the mercy of the host API's
fixed-function output-merger, primarily because it has to perform blending"*. Read
closely, that names a mechanism — **blending in a substituted pixel format** — which
is measurable rather than speculative. It was measured:

| switch | result |
|---|---|
| gamma targets blended as linear UNORM16 | **YES** — not 8-bit gamma space |
| depth format | **`D24_UNORM_S8_UINT`** — matches Xenos exactly, no `D32_SFLOAT` fallback |
| float24 depth converted in pixel shader | **YES** — `sampleRateShading` present |
| render target formats that cannot blend | **0 of 9** |

**No format substitution is required**, so the specific degradation warned about does
not apply on this hardware.

Also worth keeping: Adreno 740 satisfies *every other* requirement of the accurate
interlock path — `fragmentStoresAndAtomics`, `sampleRateShading`,
`standardSampleLocations`, and ample storage-buffer slots. Only
`VK_EXT_fragment_shader_interlock` itself is missing, so a future Qualcomm driver
exposing it would enable the accurate path by flipping one cvar. Worth re-probing after
driver updates.

**What this does not prove.** Format capability is necessary, not sufficient.
`kHostRenderTargets` remains an approximation of a unified EDRAM buffer built from
separate images, and its residual inaccuracy is structural: tile ownership transfer
between aliasing render targets, resolve semantics, MSAA sample layout. None of that
can be answered by asking the driver about formats — it needs rendered frames compared
against the D3D12 reference, which is 3.3b and waits on Phase 4.

### Phase 3.2 — Xenia's Vulkan code runs on the Thor and accepts it

Reproduce with `./tools/vkdevice/run.sh`. Report:
[`device/vulkan-device-bringup.txt`](device/vulkan-device-bringup.txt).

The probe calls Xenia's *own* `VulkanProvider::Create(with_gpu_emulation = true,
with_presentation = false)` on the handheld. That first argument matters: it is the
path that demands what Xenos emulation needs and **refuses the device otherwise**.

```
Adreno (TM) 740   Vulkan 1.3.128   driver 0x802A4035
queue families 2,  graphics+compute = family 0,  sparse binding = family 0

RESULT: Xenia accepted this device for Xenos GPU emulation.
```

Ten of the thirteen optional extensions Xenia probes for were enabled. The three
missing ones are all fine: `VK_KHR_swapchain` was not requested (no surface yet),
and `VK_EXT_memory_budget` and `VK_EXT_conditional_rendering` are absent on this
driver but only improve reporting and optimisation respectively.

The genuinely new fact beyond the Phase 0 probe: **sparse binding resolved to a real
queue family.** Phase 0 confirmed only the feature bit; Xenia's shared-memory
emulation needs an actual family, and it got family 0, shared with graphics+compute.

### The host boundary, now enumerated

Linking turned "DAORecomp implements some things outside the Xenia subset it vendors"
into an exact list — **eight symbols**, stubbed in `tools/vkdevice/host_stubs.cc`:

| symbols | provided by | Phase 4 task |
|---|---|---|
| `Presenter::Dao{KeepAcquired,StopPacer,QueueFramePaints,BeginUIThreadPaint,EndUIThreadPaint}` | DAORecomp's frame pacer, built on Windows waitable timers | **Swappy** — task 4.7 |
| `Window::{SetPresenter,OnSurfaceChanged,OnPaint}` | Xenia's window layer, which needs ImGui | **SurfaceView** — task 4.2 |

Every stub **aborts** rather than returning something plausible. The probe never
presents, so none should be reached; if one were, the result would be meaningless. A
quiet `return false` is the difference between "the device works" and "the device
appears to work".

Four further symbols were resolved by supplying the real thing rather than stubbing:
`fmt`'s `format.cc`, snappy (so GPU trace capture works — that is how frames get
compared against the reference build in 3.3), an stb implementation unit, and snappy's
generated `snappy-stubs-public.h`. `tools/prepare_deps.py` produces the last two.

**What this does not prove:** nothing has rendered. No surface, no swapchain, no
command submission, no EDRAM.

### Phase 3.1c — source selection, and five more portability bugs

`tools/compile_runtime.py --scope android` now selects sources rather than compiling
the whole tree. **114/114 translation units compile for ARM64.**

65 files are excluded, and the two reasons are kept apart deliberately:

| | files | |
|---|---:|---|
| **Wrong for this target** | 56 | both Direct3D backends (36), Windows (17), macOS (2), x86-64 codegen (1) |
| **Deferred** | 9 | ImGui overlay and Xenia's windowing (6), GPU trace capture (2), demos (1) |

A trap worth remembering: **Xenia sets `XE_PLATFORM_LINUX` on Android as well as on
desktop Linux**, so the GTK/X11/SDL2 desktop windowing files are reachable and must be
excluded explicitly. They are not guarded out by platform macros.

Also excluded for reasons specific to this port: `platform_arm64.cc` (Xenia's
CPU-backend ARM64 feature detection via xbyak_aarch64 — we use static recompilation and
nothing references it) and `ui/dao_frame_pacer.cc` (DAORecomp's frame pacer is built on
Windows waitable timers; **Swappy is its Android replacement**, task 4.7).

Five real portability bugs were found and fixed getting there — see §5b and
`docs/BUGS.md`. Two notes on how they presented:

- They **cascade**. `graphics_system.cc` had an unguarded Windows include *masking* an
  unguarded Win32 `SetThreadPriority` call; and `threading_posix.cc`'s robust-mutex
  problem stopped the compile before a separate missing-`const` error in the same file.
  Each fix revealed the next, so the count kept looking like one-more-thing.
- Three of the five are in **Xenia's own Android path**, which had rotted from never
  being compiled: a call with the wrong number of arguments, reliance on glibc robust
  mutexes that bionic does not implement, and a helper called from a `const` method
  without being `const` — next to a mutex the author had *already* marked `mutable`.

### The host build needs AVX

simde reaches rounding through `__builtin_roundevenf`. With SSE4.1 available Clang
lowers that to a `ROUNDPS` instruction; without it, it emits a call to `roundevenf`,
which the MSVC CRT does not provide and the link fails on. So the host build carries
`/arch:AVX`, matching DAORecomp's stated AVX CPU requirement and XenonRecomp's own
`-march=sandybridge` test flags. ARM64 needs no equivalent — it has `FRINTN` natively.

### Glyph table comes from the title update

`X360_ControllerInteraction.GDA` (1,280 bytes) ships inside **TU7's own package**,
alongside a `packages/core/patch/` tree of 25 other files. The Awakening disc is not
needed for the glyph work after all — it comes from the title update we are already
building against.

### AYN Thor

Full detail in [`device/thor-profile.md`](device/thor-profile.md). Confirmed in
landscape use, with `rotation 1`:

| | Display 0 (main) | Display 4 (deck) |
|---|---|---|
| Landscape resolution | **1920 × 1080** | **1240 × 1080** |
| Physical display ID | `4630946441858561667` | `4630946482288158084` |
| Hardware port | 131 | 132 |
| Layer stack | 0 | 4 |
| Refresh | 60 / 120 Hz | 60 / 120 Hz |
| Colour | Display-P3 | Display-P3 |
| HDR peak | 420 nits | 500 nits |
| Touch | internal | **yes (external)** |
| Flags | — | adds `FLAG_PRESENTATION` |

Both in display group 0 → the deck is reachable via
`ActivityOptions.setLaunchDisplayId`.

Other: QCS8550 (Snapdragon 8 Gen 2), Adreno 740, 8 cores (1×X3 + 2×A715 + 2×A710 +
3×A510), 16 GB RAM, 209 GB free, Android 13 / **API 33**, **arm64-v8a only**.

### Session 31 — what the title and the device confirmed (2026-10-25)

- **The Adreno 740 reads `gl_FragCoord` as zero** in a fragment shader that declares the
  `DenormFlushToZero 32` execution mode (BUG-045). The translator leaves the mode out
  when the Vulkan vendor id is `0x5143` (Qualcomm).
- **XenonRecomp's XEX patcher needs `memmove`:** its block moves overlap, which x64's
  `memcpy` forgives and ARM64's does not (BUG-046).
- **The physical allocator must hand out the console's whole 512 MB:** the title sizes
  its last heap from what `MmQueryStatistics` says is left; half the range left FMOD's
  heap at 15 MB (BUG-043).
- **The script registry:** the 360 build compiles its scripts natively and registers
  each by name at startup (`sub_82306F10`). `sub_82330808(string, ascii)` makes an
  engine string, `sub_82330878(name)` returns the registry slot whose first word is the
  script's main; the dispatcher `sub_824B1EE8(event, name)` refuses `zz_*` names.
  `zz_pre_debug`'s main is `0x828D42E8` and opens the prelude debug dialogue.
- **The title's modes** (`warden_game.cc`): 0 explore, 1 combat, 2 dead, 3 menu, 4
  conversation, 5 cutscene, 6 fly camera, 7 fixed camera, 8 pregame (title screen and
  main menu), 9 loading, 10 movie, 11 character generation, 12 level-up, 13 party picker.
- **`XCONTENT_DATA` is 0x134 bytes:** device id +0, content type +4, display name +8
  (128 UTF-16 big-endian characters), file name +0x108 (42 bytes). `XamContentCreateEx`
  takes nine arguments (the overlapped at `r1 + 0x54`) and its flags' low four bits are
  the disposition. The autosave on entering an area asks open-existing first (`0x13`),
  then create-always (`0x12`), and writes `Saves\savegame.das`, `savegame.met`,
  `scrnshot.bin` and a 4-byte `.x` file beside each.
- **`XamShowKeyboardUI` may complete later:** returning `ERROR_IO_PENDING` and
  completing the overlapped when the player answers works; the title waits.
- **The King's Camp costs:** about 3,275 draws a frame. 8 fps at -O0, 13.5 at -O2 with
  the console's two 2x MSAA tiles, 20 with one tile and no MSAA; the Adreno at 99% and
  its 680 MHz maximum throughout; an 8x8 scissor on every draw, 25 fps.
- **A colour resolve in guest memory** is tiled 32bpp (`texture_address::Tiled2D`) and
  reads B G R A (`tools/resolve_to_png.py`).

### Session 32 — what the title and the device confirmed (2026-10-25)

- **The save menus.** With a save on the device the main menu starts with **Resume**
  (Resume / New Game / Load Game / Downloadable Content / Options / Extras), which loads
  the newest save. START in the game opens Resume Game / **Save Game** (focused) / Load
  Game / Options / Achievements / Exit Game. Save Game lists New Save first; a new save
  asks for a name (A keeps "Dragon Age"), an existing one "Are you sure you want to
  overwrite this saved game?"; a load from the game asks "You will lose any unsaved
  progress", seconds after the A.
- **Save names.** `0xd5332a7_<n>_u!0` is "Save (n)" and `0xd5332a7_<n>_a!0`
  "Auto-save (n)". The display name is `<character>_<save name>_<YYYYMMDDhhmmss>`, the
  time in UTC from `RtlTimeToTimeFields`; the lists sort by it, newest first, parsing it
  back with `RtlTimeFieldsToTime`, and Resume takes the newest.
- **What a load reads:** open-existing (`XamContentCreateEx` flags `...13`, disposition
  2), `savegame.das.x` (4 bytes) and `savegame.das` whole, then part of it again after a
  seek - the read log's running total passes the file size, and seeks are not logged. A
  list preview reads `savegame.met` and `scrnshot.bin` with their `.x` checksums.
- **The run-199 save is at the default start area,** not the King's Camp: its
  `savegame.das` names `default_start_area` and `wp_start`, and it loads into the
  `arena`. It is "Save (1)", a regular save, not an autosave.
- **Every `scrnshot.bin` is zeros** (BUG-049): the lists show an empty frame.
- **The bootstrap, on a machine set up before,** accepts a stale runtime tree: step 6's
  `assemble_runtime.py --check` proves the edits apply to upstream, not that the tree
  matches them (`RESUMING.md` §3.4).
- **With a save on the device** START goes straight to the main menu (no storage prompt)
  and Resume is its first item, so routes that start a new game press down onto New Game
  (`route.sh` does it for a route with a `.new-game` file).
- **The King's Camp's forced tutorials** (runs 205-206): after the `ostagar` route,
  "Level up now by pressing ◀" holds every button but BACK (◀ is the Back button's
  glyph); the Character Record's "Press A to level up" holds all but A; after the level
  up (Y auto, A, B) "Mapping Abilities" holds all but LT. START opens the pause menu
  only after that. `inject-pad` presses the triggers too since run 210, and the
  tutorials are off since run 214.
- **Resolve readback fills only part of a thumbnail** (run 204): with
  `hack-resolve-readback`, 0.71% of `scrnshot.bin` is non-zero, all of it in the last
  4 KB of each 57,344-byte slice (BUG-049).
- **The title's XAudio render callback** (`sub_8303BE88` -> `sub_8303BCC0`) starts its
  mixer with `KeSetEvent`, waits for the mix with `KeWaitForMultipleObjects` (two
  objects, any, no timeout) and submits the frame only on `STATUS_WAIT_1`. One client
  registers, at the title screen. Its frames carry sound only from the movies (Bink
  decodes on the CPU); everything XMA is silent until the decoder.
- **The XMA decoder builds for Android from upstream as it is:** xenia-canary's
  `third_party/ffmpeg-xenia/config.h` already has an AArch64 + NEON branch and a "POSIX
  (Linux, Android)" one; DAORecomp's `xenia/ffmpeg.cmake` only needs its two target
  switches flipped. FFmpeg's NEON assembly reaches its C tables with `adrp`, so in a
  shared library those must be hidden (`-fvisibility=hidden`).
- **The Thor's AAudio output** is a shared, normal-latency stream - 48 kHz, stereo,
  float, 960-frame (20 ms) bursts - though low latency and exclusive were asked for. A
  queue in front of it must hold two bursts: four 256-sample frames left 8% of the
  output as silence (run 207), nine none (run 208).
- **The tutorials switch** (runs 211-214): Options -> Game -> Interface -> Show
  tutorials, the page's last item. Leaving Options writes the profile
  (`XamUserWriteProfileSettings`), and of its three 1,000-byte blobs only
  `files/profile/63E83FFD.bin` changes (584 bytes of it).
- **The prelude debug menu** (`zz_pre_debug`, runs 215 and 218): "What do you need to
  do?" 1 Jump to an area, 2 Do something not specific to an area, 3 I'm all done. Jump
  to an area: 1 Start in the King's camp, 2 Start in the Korcari Wilds, 3 Start in the
  night version of the King's camp (after Wilds), 4 Start in the signal tower, 5 Start
  at the top of the signal tower, 6 (MORE). After the King's camp the menu comes back
  with four items (2 "Do something in the King's camp (day version)", 4 I'm all done);
  after the Korcari Wilds it does not - explore mode follows the load.
- **The Korcari Wilds** (run 218): explore mode about 108 s after the first input poll;
  27.7 fps at the start position with about 1,230 draws a frame (the King's Camp: 20 fps,
  about 3,275) - the frame rate follows the draws.
- **A jump table can be missing from DAORecomp's switch tables** (BUG-051): its
  `find_switches.py` needs a `cmplwi` bound check, and XenonRecomp emits the `bctr` of a
  table it does not know as a call through the function table.
  `tools/find_missed_switches.py` checks the generated code for that shape.
- **The first fight in the Korcari Wilds** (runs 219-220): six seconds of the left stick
  forward from the start meets wolves, and `mode is now 1 (combat)` lasts until the last
  one falls, about 52 s. The party's AI fights on its own (Alistair's Shield Bash); A on
  the targeted wolf attacks. When the controlled character falls, control passes to
  another (run 220: Alistair). After a fight A on a corpse opens its loot (mode 3) and A
  again closes it; A on a party member walks over and starts a conversation. 23.6 and
  26.6 fps in the two fights, at about 1,000 and 890 draws a frame.
- **Conversations** (run 219): the option list wraps - up from the first option selects
  the last - and the d-pad moves the selection.
- **The voice-over is in FMOD banks:** `audio\vo\en-us\vo\<area>_<speaker>.fsb` under
  `D:\modules\single player\` (Jory: `pre100_jory.fsb`), opened when the conversation
  starts, with the lip sync from `audio\vo\en-us\facialanimations\modulefacialanimations.erf`.
  While Jory spoke the output peaked at 0.36-0.43, against 0.02-0.09 between his lines.
- **The prelude debug menu's other pages** (run 221): "Do something not specific to an
  area" is quest shortcuts - 1 Get darkspawn blood, 2 Get the treaties, 3 Get flowers, 4
  (DO NOT USE IN COMBAT) Kill all hostile creatures in the area, 5 (Start over.). The
  area list's (MORE): 1 Start in Flemeth's hut (after Signal Tower), 2 Start at the end
  of the Prelude, 3 Start at the very end of the Prelude (with Morrigan), 4 Tower of
  Ishal - level 3, 5 Start in the King's camp (Includes opening cutscene), 6 (Start
  over.).
- **Death** (runs 221-222): `mode is now 2 (dead)` when the whole party has fallen; the
  death screen offers Load Last Save / Load Game / Change Difficulty / Quit with a tip,
  to `music_bank_strm_death.fsb`, and Load Last Save reloads the level. The Tower of
  Ishal's third level (`ost003d`) puts the player there alone and unarmoured, a few
  steps from genlocks.
- **Areas stream in grid cells** (run 220): walking in the Wilds opened
  `packages\core\env\ost100d\ost100d_5_6_0.rim` and its `.gpu.rim`, after the 28 chunks
  the area load opened. **An exit on foot** (run 224): walking back from the Wilds'
  start brings up "Gather your party and venture forth?" (A Yes / B No); Yes loads the
  King's Camp in 3.6 s. No save is written on that transition.
- **The HUD's portraits are GPU renders** (run 227, BUG-052): each face is a 146x146
  `k_8_8_8_8` texture written by a resolve (pitch 160) when an area loads - one in the
  start area, three at the Korcari Wilds' load - and the HUD's portrait draw samples it
  through Scaleform's stencil-masked quads. No shader exports to memory in a boot that
  reaches the Wilds.
- **The end of the Prelude with Morrigan** (runs 222-223) starts at the Korcari Wilds'
  usual spot with Morrigan, the player and Alistair; Morrigan's AI casts Winter's Grasp,
  and when the controlled character falls her quickbar's X casts it by hand.

---

## 5. Decisions already made

Rationale in [`docs/DECISIONS.md`](docs/DECISIONS.md). Don't reopen without a reason.
This table's D10 (the host toolchain) is its own; DECISIONS.md's D10 is the licence, listed
below as "D10 (DECISIONS)".

| # | Decision |
|---|---|
| D1 | **Native Vulkan port.** Fork DAORecomp, keep its recompiler config and game patches, replace the Xenia D3D12 backend with Xenia's Vulkan backend. **Validated by the Q1 probe.** |
| D2 | **Base game + TU7 only.** Awakening deferred — though its odds improved, see §4. Since superseded in part: Awakening and the DLC from disc 2 (10.10), and TU6 from disc 2 a target too (12.3). |
| D3 | **Relabel-only glyphs.** Button *functions* never change; only the drawn letter/symbol does. |
| D4 | **Ship Xbox glyphs as default**, with Nintendo and PlayStation switchable. |
| D5 | **Second-screen tabs:** Party, Quests, Map, Tactics, Codex/Journal, Inventory/Equipment. |
| D6 | **Read-only mirror first.** No writes into game state in Phase 8. Touch quickbar is the one later write path. |
| D7 | **Build the FSR 3.1 path, default it off.** Reinforced by Q1: FSR needs the 730-line motion-vector generator rebuilt too. |
| D8 | **Build HDR10, default to wide-gamut SDR (Display-P3).** Confirmed implementable. |
| D10 | **Host toolchain via xwin, not WSL or Visual Studio.** No MSVC is installed; `tools/setup_winsysroot.py` fetches just the MSVC CRT and Windows SDK (~85 MB, versions pinned to upstream's) and we build with the LLVM 23 already present. This is DAORecomp's own documented path. Accepts Microsoft's redistributable-headers licence. |
| D9 | **Pin NDK 28.2.13676358** (Clang 19.0.1) — the newest *stable* NDK; 29.0.x and 30.0.x are mostly release candidates. Revisit if codegen needs a newer Clang. |
| D10 (DECISIONS) | **GPL-3.0-or-later** for the code (11.3). |
| D11 | **Source only** (11.3) - superseded by D14 the same day. |
| D12 | **Package ids with no person's name:** `dev.daorecompandroid.duo` (debug), `prod.daorecompandroid.duo` (release). |
| D13 | **Version 0.1.0-alpha, "Open Alpha".** |
| D14 | **The game's code made on the device; a published APK** (Phase 12): the player gives the base disc and disc 2, TU7 optional. |

### Phase 3's refined shape (from the Q1 probe)

1. Graft Canary's `gpu/vulkan/` + `spirv_shader_translator*` + `ui/vulkan/` onto
   DAORecomp's near-stock frontend — mostly mechanical.
2. Port the ~1,900 lines of frontend deltas (`graphics_system.cc`, `ui/presenter.h`,
   `ui/imgui_drawer.cc`, DAO's `memory.h`).
3. Generate SPIR-V shader bytecode via the build system, not by hand.
4. **EDRAM via `kHostRenderTargets`** — the accuracy-critical work, validated against
   the D3D12 reference build.
5. Build a graphics-API-agnostic post-processing layer, then reimplement HDR on it.
6. Defer FSR until the above is stable.

### Features in scope

Tactical camera unlock · touch quickbar / radial ability wheel · skip intros + faster
loading · Swappy frame pacing · thermal & battery aware scaling · Vulkan pipeline cache
pre-warm · 120 fps mode · performance overlay · 16× anisotropic + bicubic filtering ·
MSAA/SMAA · shadow resolution & draw distance · asset override folder · in-game ImGui
settings overlay · low-latency audio + headphone virtualization

### Explicitly out of scope

Mouse & keyboard support · PC save import/export · local achievement tracker ·
Awakening · confirm/cancel swap · automatic glyph detection.
Also **confirmed unportable** by the Q1 probe: DLSS, XeSS, FSR 4 and neural upscaling
are desktop-vendor-specific.

---

## 5b. Known bugs

Tracked in [`docs/BUGS.md`](docs/BUGS.md) with what each one causes. At 0.1 Open Alpha
(2026-11-02): **102 entries - 81 fixed (one as an option), 5 open, 4 watched, 9 worked
around, 1 won't fix, 2 the game's own**; BUGS.md's index is current, and the README's
"Known issues" lists what a player meets. The table below is as of 2026-10-27:

| ID | Status | Summary |
|---|---|---|
| BUG-056 | FIXED | Coming back to the app could hang its main thread in the wake pipe: a frozen picture and "not responding", or a black screen after the power button (session 35) |
| BUG-057 | FIXED | The game ran on in the background; put away, it now holds where it was, its clocks stopped (session 35) |
| BUG-058 | OPEN | Xenia's timer queue and log writer spin-wait: about 11% of one core, playing or put away |
| BUG-061 | FIXED | Rewriting the UI's atlas descriptions while the game ran crashed its next load: the button prompts change from the next start (session 35) |
| BUG-060 | FIXED | The pipeline storage created every stored pipeline at startup, and a pipeline whose shaders spill holds 24 MB of the driver's memory; now each at its first use (session 35) |
| BUG-059 | FIXED | With no Vulkan instance the boot went on and crashed; now the title is not started and the report says why (session 35) |
| BUG-052 | FIXED | The HUD's party portraits had no faces: drawn once at an area load, they drew the pipeline cache's placeholder pixel shader (session 34) |
| BUG-049 | FIXED | Every save's thumbnail was blank: its quad, drawn once at the save, drew the pipeline cache's placeholder pixel shader (session 34) |
| BUG-047 | WATCH | Rarely (twice in ~70 boots) an indirect command buffer holds a malformed packet and the debug build's assertion kills the process |
| BUG-042 | WATCH | Intermittently the title stops kicking the ring buffer and the swaps freeze |
| BUG-034 | OPEN | Startup can skip mounting the packages (a timing race); not seen in many boots |
| BUG-028 | OPEN | `appbringup/run.sh` carries on after a failed install and reports on the old build |
| BUG-009, BUG-025 | OPEN | No HDR10 swapchain on the Adreno — affects Phase 9.2 |
| BUG-004 | WORKAROUND | XenonRecomp aborts `0xC0000409` with no output on a path mistake |

**Fixed in Phase 3.1c:** BUG-003, BUG-016, BUG-017, BUG-018, BUG-019. Three of those
are the same story — **Xenia's Android target had rotted from never being built**: a
wrong argument count, reliance on glibc robust mutexes bionic lacks, and a missing
`const`.

## 6. Open questions

| # | Question | Blocks | Status |
|---|---|---|---|
| ~~Q1~~ | ~~Does Xenia's Vulkan backend handle DAO's Xenos usage?~~ | — | **ANSWERED 2026-10-22: GREEN.** See §4 and `docs/PROBE-Q1.md`. |
| Q2 | Is the PC tactical camera still present in the 360 executable? | Feature 9.10 | **Answered (session 35, runs 379-380): no, not usably** - the PC tactical camera survives only as an inert message: `ToggleTacticalCameraMessage` is in the engine's message list, bound to the right stick's click in the controller table and accepted by the explore mode, but neither pressing R3 nor sending the message moves the camera, and no tactical-camera option or camera-distance setting exists by name (the PC's EnableShadows does). 9.10 dropped. |
| Q3 | Does the game have a 30 fps logic lock separate from its render rate? | Phase 9.1 | **Answered (session 35): no.** DAORecomp's 60 is a swap interval and nothing more; the title runs at 60 in its menus on the Thor and as fast as the GPU allows in the game (9.1). |
| Q4 | Which atlas cells hold the four face-button glyphs? | Phase 7.1 | **Answered (session 35):** `XBoxGamepad_BottomAction`, `_RightAction`, `_LeftAction` and `_TopAction` in the shared atlas, the PS3 set beside them, and eleven screens' own copies (`docs/GLYPHS.md` section 2). |
| Q5 | Can Awakening's disc addin be repackaged into what the DLC loader expects? | PLAN 10.10 | **Answered (session 38, runs 487-490): yes, as it is.** The disc's `Addins/DAO_PRC_EP_1/` placed as a DLC package's add-in (`files/content/00000002/DAO_PRC_EP_1/DAO_PRC_EP_1/`) is enumerated, mounted and played - New Game offers Origins and Awakening; Awakening's opening, first fight and conversation run. See §4 and PLAN 10.10. |
| Q6 | `Presentation` or second Activity on API 33? | Phase 8.1 | **Resolved (session 37, runs 454-457): both work on the Thor; the second Activity, in its own process, is the default (BUG-078: it gives the focus back).** Earlier note: open, but display 4 confirmed to carry `FLAG_PRESENTATION`, same display group, and is independently capturable. |
| Q7 | Will FSR 3.1 motion vectors need reconstructing from depth + camera matrices? | Phase 9.4 | **Answered (session 36, runs 410-420): yes, and done for the camera.** The static meshes' vertex shaders hold their world-view-projection in c0-c3; for a mesh that has not moved, WVP(before) x inverse(WVP(now)) is the camera's reprojection, and a compute pass reprojects every pixel's depth into motion (PLAN 9.4 step 4). Per-object motion (characters) is not reconstructed. |
| ~~Q9~~ | ~~Does the ARM64 build behave correctly, not just compile?~~ | — | **ANSWERED 2026-10-22: yes.** 98.70% of 6,635 executed functions byte-identical; all divergences explained; zero logic bugs. |
| Q10 | **New:** Do NaN / infinity / denormal payloads propagate identically? | Phase 3 polish | **Closed without its own test (10.8, session 38).** No fault in play or in 3.3b's frame comparison has pointed at it; reopen with a targeted comparison if one does. |
| Q11 | **New:** Are the FMA and estimate differences *visible* in the rendered game? | Phase 3 | **Answered (3.3b, session 35): no.** The frame comparison against DAORecomp's Windows build found nothing rendering wrongly - the still scenes within 2.5-3.2 of 255. |
| Q12 | Keep the process alive in the background with a foreground service? Put away, the game holds (BUG-057), but its state is in memory only, and Android may end a background process when memory runs short (another heavy app in front) - everything since the last save lost. A foreground service, with its notification, keeps it far less likely to be chosen. | Nothing | **Answered (the owner, 2026-10-27): no foreground service.** Instead PLAN 9.13 investigates, in Phase 9, whether the game can autosave as it is put away. |
| Q8 | How inaccurate is `kHostRenderTargets` for this game specifically? | Phase 3.3b | **Answered (3.3b, session 35): nothing shows.** Narrowed 2026-10-22: no *format* degradation is active — 0/9 formats fail to blend, depth matches Xenos, gamma blends linear UNORM16 (`docs/EDRAM.md`). What remains is structural: tile ownership transfer, resolve, MSAA layout. Needs frames, so it waits on Phase 4. **3.3b answered it (session 35): nothing shows** - the Korcari Wilds, the King's Camp's arrival and the menus against DAORecomp's Windows build; the still scenes within 2.5-3.2 of 255. |

---

## 7. Environment state

### This workstation (Windows 11) — all installed

| Tool | Version | Location |
|---|---|---|
| Python | 3.14.6 | on PATH |
| git | 2.54.0 | on PATH, user `mabencs` |
| **CMake** | **4.4.4** | winget packages dir, on PATH |
| **Ninja** | **1.13.2** | winget packages dir, on PATH |
| **LLVM / Clang (host)** | **23.1.4** | `C:\Program Files\LLVM\bin` |
| **JDK** | **Temurin 17.0.20.101** | `C:\Program Files\Eclipse Adoptium\jdk-17.0.20.101-hotspot` |
| **Android SDK** | platform **36**, build-tools **36.1.0**, CMake **3.31.6** | `%LOCALAPPDATA%\Android\Sdk` |
| **Android NDK** | **28.2.13676358** (Clang **19.0.1**) | `%LOCALAPPDATA%\Android\Sdk\ndk\28.2.13676358` |
| platform-tools / adb | 37.0.1 | SDK `platform-tools` |
| Android CLI (`android`) | 1.0.16625370 | winget `Google.AndroidCLI` |
| Gradle | 9.8.2 | downloaded by the committed wrapper (SHA-256 checked) |
| **MSVC CRT / Windows SDK** | **14.44.17.14 / 10.0.26100** | `toolchain/winsysroot` (via xwin 0.10.0) |
| **XenonRecomp** | built | `build/xenonrecomp/XenonRecomp/XenonRecomp.exe` |
| AYN Thor | over USB or Wi-Fi (`RESUMING.md` §3.1) | `adb devices` |

Environment variables set at user scope: `ANDROID_HOME`, `ANDROID_SDK_ROOT`,
`ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, `JAVA_HOME`. PATH extended with CMake, LLVM,
JDK, platform-tools, cmdline-tools.

### Gotchas worth remembering

- **CMake is 4.x.** It rejects `cmake_minimum_required(VERSION < 3.5)`. Upstream
  CMakeLists may need a compatibility flag.
- **Host Clang is 23; DAORecomp expects 22.** Close, but note it if codegen misbehaves.
- **The new Android CLI replaced `sdkmanager`.** Use `android sdk install <pkg>` with
  **slash**-separated IDs (`ndk/28.2.13676358`), not the old `ndk;28.2...` syntax.
- **Git Bash path mangling.** The NDK toolchain and `adb` are native Windows binaries
  and cannot read MSYS `/c/...` paths — convert with `cygpath -w`. Also set
  `MSYS2_ARG_CONV_EXCL='*'` so MSYS stops rewriting `adb shell` arguments.
- **`screencap -d` wants the PHYSICAL display id** (e.g. `4630946482288158084`), not
  the logical id (`4`). `tools/deploy.sh` resolves it by hardware port.
- **Screenshots of a sleeping panel come back empty.** `deploy.sh shot` wakes the
  device first.
- **`git clone --recurse-submodules=no` sets `submodule.active=no`**, which makes every
  later `git submodule update --init` a **silent no-op** — no output, exit 0, nothing
  fetched. Clear it with `git config --local --unset submodule.active`.
- **XenonRecomp resolves every TOML path against the config file's directory**, so
  `out_directory_path` must be relative, and **the output directory must already
  exist**. Either mistake aborts with `0xC0000409` and no message at all.
  `tools/recompile.py` handles both.

### Large files — not in git

```
Dragon Age - Origins (World) (En,Es,It).iso                7,838,695,424
Dragon Age - Origins - Awakening (...).iso                 7,838,695,424   (archived)
TU_12K2260_0000004000000.00000000000G1                         1,957,888   (TU2, unused)
TU_12K2260_0000004000000.00000000001O1                         3,506,176   (TU7, build target)
```

In the repo root on this machine. Expect ~25 GB more for build output and ~20 GB for
extracted game data. `third_party/` holds ~700 MB of vendored upstream (FFmpeg, 107 MB of it, since session 32) with its git data (also gitignored).

Everything else git ignores is rebuilt by `python tools/bootstrap.py` from the disc
image and the TU7 package - `third_party/`, the two executables, XenonRecomp,
`toolchain/winsysroot`, `recomp/generated/`, `build/runtime-tree` and its shaders, the
device toolchain (`build/device-toolchain`, `build/gamecode-stage`), `local.properties`,
the APK and the developer's `libdaogame.so` - and `--validation-layer` adds the one optional binary,
the Khronos validation layer in `app/src/main/jniLibs/`. `RESUMING.md` §3.4 lists each
with what makes it. What stays behind is scratch: `build/run*.log` and captures,
`work/` (scripts and dialogues unpacked for reading), `device/shots/`.

---

## 8. How to resume on a different computer

**See [`RESUMING.md`](RESUMING.md)** — the tools to install, the one-command setup, the
device's state, the daily loop, the next tasks and the gotchas that cost an hour each.
The short form:

```bash
git clone <repository URL> DragonAgeOriginsAndroid
cd DragonAgeOriginsAndroid
# put the .iso and TU_*001O1 in the repo root (gitignored, never committed)
python tools/verify_assets.py .
#    expect: "Ready: you have a supported disc and a supported title update."
python tools/bootstrap.py --accept-msvc-license    # everything else, up to the APK
adb install -r -d app/build/outputs/apk/debug/app-debug.apk   # never uninstall
```

Then continue from **Next action** at the top of this file.

---

## 9. Session log

Newest first. One short entry per session: what changed, what's next.

### 2026-11-02 (session 42) - the deck: folding, codex text, item details and actions, BUG-105 to 109

- **0.1.1-alpha** released with all of this (`CHANGELOG.md`, the tag `v0.1.1-alpha`). The game-code
  ABI is unchanged, so players keep the game code they made.
- **Quests** as the journal has them: Current and Completed pages, the game's groups by
  priority, each group and quest unfolding on a tap (PLAN 8.11, BUG-107). **Codex** the
  same, and its entries readable once unlocked, read in the game or not (BUG-105; run 603
  on an unread entry of a character that had never opened the journal). The long texts
  are read only while open (`DeckSource.watch`), for Binder's limit.
- **Items:** a tap shows what the game's Inspect does - stats, properties, requirement, text
  (`ItemDetails`; runs 609-610 line for line against Inspect on the Maul and Blood Dragon
  Plate) - with Equip, Use and Unequip, the inventory screen's own messages through the
  engine's SendMessage (PLAN 8.12), greyed as the game greys them. Left: a trap's Use does
  nothing (it wants a place on the ground).
- **BUG-108, from the owner's report:** a conversation's start froze the game for nearly 4 s
  with an upscaler and Lossless 2x: ten frames without a scene at the render size made the
  generator again, and the scene's return again (runs 607, 608). It no longer shrinks.
- **BUG-106:** the deck shown "not responding", then gone: with the second screen on top a
  key waits on a window that takes none. The deck now gives the top display back whenever
  it finds itself there, if the game is in front. Our own `input keyevent` set it off this
  time; a pad press would have.
- **BUG-109:** the game's sound did not follow the media volume on the Thor; the mixer's
  stream volume moves, the track's gain does not, why is not known. The app applies it on
  AYN devices (run 613: 13 dB from index 15 to 8, silence at 0).
- A test's side effect, repaired: run 602's second-screen test paused the game while it
  explored, and the put-away autosave took the test's changes into the owner's Auto-save
  slot. Put back from the backup taken before (the same md5); `docs/TESTING.md` says how to
  avoid it. Completed Quests has not been seen with a quest in it: neither save has one.

### 2026-11-02 (session 41, the end) - published on GitHub

- **The repository is public:** https://github.com/mabencs/Dragon-Age-Origins-Recomp-Android-Duo,
  a fresh history (the owner pushed the single first commit, made from the tree as it was
  at the private server's last commit, byte-identical). Not a fork of DAORecomp: the
  project uses it as a pinned dependency and shares none of its history, so it stands on
  its own and credits it. This working folder now follows it: `origin` is GitHub, pushing
  only `main`; the old history is the local branch `archive-main` and the private server's,
  an archive. Commits are authored with the owner's GitHub no-reply address.
- `warden.sourceUrl` is the repository's address (About this app shows it); the README links
  its Releases page and issue tracker.
- **The 0.1.0-alpha release** built from that commit, checked by `tools/release.py`, and
  published as the pre-release `v0.1.0-alpha` with the APK.

### 2026-11-02 (session 41, later still) - BUG-104: a test route left in the owner's app

- **The owner's report:** on the Thor no DLC in the main menu, and New Game going straight
  to the debug start. **The cause (BUG-104):** run 598's ostagar route (`tools/route.sh`)
  left `route`, `inject-pad` and `hide-dlc` in the app's files - by design until
  `route.sh off`, which was not run. Removed; the launcher now shows "Test files in place:
  ..." and each start logs `TEST FILES IN PLACE: ...` when any test file is there.
  RESUMING §4.3: always end with `route.sh off`.
- **Run 602 (the owner's debug install, by the Thor's controller):** the launcher Ready
  with no test files; the title, the DLC notice and check, the main menu with
  Downloadable Content; New Game offers the campaigns (Origins, Awakening and the DLC)
  rather than the debug start; back to the main menu. No fault.
- **The owner's install and the release:** the debug build is the same code as the release
  (the same -O2 native build, assertions kept in both; nothing in the app depends on the
  build type but the report's label) - only the package id, the signing key and run-as
  differ, run-as being what lets adb pull its logs, saves and reports. It is installed from
  the committed tree, so About names a commit that is on the remote.

### 2026-11-02 (session 41, later still) - reports from modded games; the mods in the log

- **The README (the owner):** bug reports from a game with mods loaded might not be
  investigated - check with every mod off first; said in the Debugging Mode section and at
  the top of Mods. The report's contents list its summary (`readme.txt`) and the log's
  `MODS LOADED` lines.
- **The log and the report say it plainly:** `MODS LOADED: none - the game as on the disc`
  now at warning level like the rest (it was an info line, "Mods: none"), or `MODS LOADED:
  yes - N mods loaded ..., M add-ins from mods on, K off` and a line each. The report's
  readme and run header: `Game's code: made from Title Update N` (for the Title Update 7
  rule), then the mods a line each in the game's order (from `files/mods-enabled.txt`,
  what the game loads; the one that wins first), each add-in on or off, and any mod in the
  list not loaded. Run 601 (Debugging Mode on for the one boot, off again after): both
  No Helmet Hack mods and their add-ins named in the log and the header, no fault.

### 2026-11-02 (session 41, later still) - bug reports need Title Update 7

- **The README says it (the owner):** a bug report is investigated only if the bug happens
  with Title Update 7 installed - its Debugging Mode section opens with it, and the
  "What you need" table's Title Update 7 row points there.

### 2026-11-02 (session 41, later) - 12.8: Build, when the player is ready

- **The launcher's Build row (the owner's ask),** under Launch: the game's code is made only
  when the player chooses it, once their files are in, rather than as soon as a title
  update is in place - a player with Title Update 7 adds it before building, and the code
  is not first made from disc 2's TU6. Offered once the disc image is extracted and a
  title update is in place, with no extraction or disc 2 install going on; "Start" (or
  "Build again" for code made for another title update or app version), "building N%",
  "Done", "failed" (Build tries again); greyed when there is nothing to build. Launch
  waits for it ("Once the game's code is made: choose Build").
- **Run 600 on the Thor (the debug build):** the launcher with the code made read Build
  "Done"; with the manifest set aside it read "Build again", Launch "not yet", and nothing
  started in 70 s; Build started the making from Title Update 7, "building N%" on the row,
  and 7 min 40 s later "Done", Launch ready.
- The README's setup (step 7 is Build), its launcher table and Updating, TESTING,
  RESUMING and ARCHITECTURE say so.

### 2026-11-02 (session 41, after the close) - the docs brought up to Phase 12

- **The release app removed from the Thor** (the owner's call): `prod.daorecompandroid.duo`
  uninstalled, its 11 GB freed; the owner's `dev.daorecompandroid.duo` untouched.
- **A sweep of every doc for what Phase 12 changed:** ARCHITECTURE's pipeline, runtime
  picture and layout as built (the game's code made on the device into `libdaogame.so`;
  no shader recompiler), PLAN's picture likewise and Phase 12 marked complete, RESUMING
  (the short version, §3.4's table with the device toolchain, §3.5 and §4.1 with
  `push_gamecode.py`, §4.10's `WARDEN_OVERRIDE`, §6's files, §7's layout per title
  update, two gotchas), TOOLCHAIN (this project's XenonRecomp patch step, the app's
  build, the staging), SIGNING, ASSETS (TU6 from disc 2 a target, its size and hash),
  IMPORTS (the stubs in the runtime), THIRD_PARTY (LLVM's licence), DECISIONS (D2's
  note), README (the launcher's rows, the release's mods path), this file's §1, §3 and
  §4. The source-only wording of D11 gone from every place that is not history.
- `push-test.txt`, a leftover from checking a push in October, removed from the root.

### 2026-11-02 (session 41, the close) - 12.6 and 12.7: the published APK, from a clean install

- **12.6, the published APK.** `tools/release.py` now also fails an APK holding
  `libdaogame.so`, any symbol of the game's translated functions (`llvm-nm` over every
  library), or a game file in the toolchain's zip, and one missing what the device makes
  the game's code with. Release builds leave out the Khronos validation layer. The About
  page says the program is translated on this device; LLVM and the NDK's parts are on the
  licences page (28 projects). The README is for players: install from Releases, give the
  two disc images, wait once, play. DECISIONS D14 supersedes D11. The 0.1.0-alpha release:
  75.6 MB, `release.py` clean.
- **12.7, a clean install from it (run 599).** The release app (`prod.daorecompandroid.duo`,
  beside the owner's debug build, which is untouched) given the base disc and disc 2 from
  Download through the picker: extracted, the DLC installed (11), disc 2's Title Update 6
  taken, the launcher reading "disc 2's TU6 instead"; the game's code made on the device
  by itself in **7 min 43 s**; Launch, the intro, the title screen, START, the
  downloadable-content notice, the content check, the main menu - no fault. Lossless.dll
  was not given (optional): the Android picker's storage provider stopped answering
  (an ANR in `com.android.externalstorage`, not the app).
- **The privacy sweep for the public repository:** the owner's characters' names, home
  app and everyday programs out of the tree (`tools/game_check.py` reads the everyday ones
  from the gitignored `tools/game_check.local.txt`); the README's screenshot with the
  hero's name pixelated. The git history still holds the old LAN address, home paths, the
  earlier package id and the original screenshot: publish with a fresh history.

### 2026-11-02 (session 41, the very last) - 12.3: disc 2's Title Update 6, end to end

- **The owner's target setup on the Thor** - the base disc, disc 2 and Lossless.dll, no
  TU7: disc 2 chosen through the picker copied its Title Update 6 into the app's files,
  the row read "disc 2's TU6 instead", and the app made the game's code from it by itself
  (7 min 30 s). The game then hung at startup (run 597): **BUG-103** - XenonRecomp's
  `PPC_LOOKUP_FUNC`, used by six runtime files for thread entries and callbacks, baked in
  TU7's image size, and TU6's image is 64 KB smaller. Fixed by redefining it with the loaded
  library's layout in `warden_game_library.h`, now force-included into every runtime file.
  Run 598: TU6's device-made code to explore mode at the King's Camp, no fault.
- Left of Phase 12: 12.6 (the published APK: release.py, the README for players) and 12.7.

### 2026-11-02 (session 41, last of all) - 12.5: the Thor makes the game's code itself

- **The making on the device (PLAN 12.5) works.** The APK (101 MB) carries clang and lld
  (`libllvm.so`), XenonRecomp (`libxenonrecomp.so`) and their headers and link inputs
  (`assets/gamecode/toolchain.zip`, 6.9 MB), and no game code. `GameCodeService` (its own
  process, foreground, a wake lock) runs `GameCode.java`: the toolchain unpacked, the
  disc's executable and the title update's patch checked by SHA-1, XenonRecomp's 211 files
  checked against both title updates' hashes, 209 compiles six at a time, the link
  against the installed runtime, read-only with a manifest. The launcher starts it by
  itself and shows "making N%". Run 595: 7 min 30 s on the Thor from opening the app;
  the device-made library booted to the title screen. Run 596: the ostagar route to the
  King's Camp on it, no fault, 25 fps as before with the owner's settings.
- **The row is "Title update 7 (optional, recommended)"** (the owner); its values "In
  place" or "disc 2's TU6 instead".
- The owner chose to put the TU7 file back for these runs; disc 2's Title Update 6 copy
  is still to be tried (12.3), then 12.6 (the published APK) and 12.7.

### 2026-11-02 (session 41, later still) - 12.2: the game's code is a library of its own

- **The split (PLAN 12.2), proven on the Thor.** `libwarden.so` no longer holds the game's
  code: the runtime resolves its 87 calls into it at load (`warden_game_library.cc`,
  `WARDEN_GAME_FUNC`), takes the image's layout from it, and its 56 overrides
  (`WARDEN_OVERRIDE`, 24 of them made by warden_game.cc's probe macros) are reached
  through forwarders compiled with the game's code (`assets/gamecode/shim.cpp`, written
  with `hooks.h` and `recipe.json` by `tools/gen_gamecode_recipe.py` - configuring stops
  when they are stale). The import stubs moved into the runtime. The BUG-052 Direct3D
  probe (540 wrappers, logging only) is retired. Run 594: the computer-built
  `libdaogame.so` (76.8 MB, `tools/push_gamecode.py`) loaded, the intro and the title
  screen, no fault.
- **12.3's code:** the runtime takes Title Update 6 or 7 (the one the game code was made
  from), disc 2's install copies its Title Update 6 into the app's files, the launcher's
  row is "Title update 7 (optional, recommended)" (the owner's words). Disc 2's copy is not
  yet tried on the device.
- **The owner cleared the app's storage on the Thor** (2026-11-02, about 13:42 UTC) and
  set it up again with the two disc images and Lossless.dll - the saves, the mods and the
  TU7 copy went with it, as intended. For run 594 the TU7 file went back into the app's
  files (the owner's choice).
- Begun, not yet in the build: `GameCode.java` and `GameCodeService.java` (12.5, the
  making on the device) and `tools/stage_gamecode_toolchain.py` (the compiler's files for
  the APK: 6.9 MB zipped, 3,320 files).
- Found on the way: two Gradle builds run at once (the second started while the first was
  still linking) wrote the same log and left ninja's log truncated ("premature end of
  file; recovering") - one build at a time, each to its own log.

### 2026-11-02 (session 41, later still) - Phase 12: the game's code made on the device

The owner: "I want to be able to publish an apk that the end user just provides the iso
files and lossless.dll for... a long compile on the device is fine"; then "the user
provides the base disc and the ultimate edition disc. Title update 7 should be
optional", and its row "marked as optional (recommended)". PLAN Phase 12.

- **Disc 2 carries the title update.** Its `title_update.bin` (and `ESI/tu00000001_...`,
  the same file) is Title Update 6 for this disc (PIRS, media `186A1115`), and its
  `default.xexp` is the very TU6 DAORecomp's config was made for (SHA-1 `4141b608...`).
  Translated with it, the code differs from TU7's in 3 of 211 files: the image size
  (0x13F0000 against 0x1400000 - the indirect-call table sits right after the image, so
  the runtime must take it from the game's code), one global 4 bytes along, and a
  version number.
- **12.1, the compiler on the device - done.** `tools/build_device_toolchain.py`: LLVM
  19.1.7 for Android arm64 as one program (`llvm`, clang and lld, AArch64 only, 89.8 MB)
  with clang's headers; its tablegen tools built for this computer first (two false
  starts: BLAKE3's MASM assembly needs Visual Studio's ml64 - `LLVM_DISABLE_ASSEMBLY_FILES`;
  and mlgo-utils' test suite wanted llvm-objcopy - `LLVM_INCLUDE_UTILS=OFF`). About 25
  minutes on this computer. **On the Thor** (`tools/gamecode_probe.py`, the toolchain,
  the headers and the 211 generated files in /data/local/tmp, deleted after): every
  file compiled with the guest's own flags, 6 at a time, in **8 min 42 s**, none
  failing, at most 402 MB a compile; `ppc_recomp.17` alone is 521 s of it - the long
  pole - the other 207 done long before. The CPUs ran at 93-95 C.
- **12.4's core.** XenonRecomp built for Android (1.3 MB) from the same patched sources;
  DAORecomp's `prefix_symbols.py` now XenonRecomp's own third argument
  (`tools/patch_xenonrecomp.py`, now in marked groups; `tools/recompile.py` passes `tu6`):
  this computer's 211 files byte for byte as before. On the Thor the executable with
  TU7 translated in 8.0 s, 69 MB - all 211 files identical to this computer's.
- **12.2 begun (not yet committed):** the runtime's side of the split
  (`warden_game_library.cc`, `WARDEN_GAME_FUNC`, `WARDEN_OVERRIDE`) and the recipe
  generator (`tools/gen_gamecode_recipe.py`). Found on the way: 56 overrides, not 32 -
  warden_game.cc's probe macros make 24 (the draw probe also blocks rendering during a
  load, so they stay) - and the BUG-052 Direct3D probe's 540 wrappers, logging only,
  to be retired.

### 2026-11-02 (session 41, last) - 0.1 Open Alpha: 11.6, the docs, a public repository

- **11.6 done:** the launcher's test launch is gone - `DebugLaunch.java`, its row in
  `OptionsPanel.java` (the rows below it moved up one) and `onDebugLaunchChosen` in
  `GameActivity.java`. Checked on the Thor: the launcher's rows from Launch to Mods,
  then the settings, no test row. The ostagar route (`tools/route.sh`) does the same job
  from the computer.
- **Version 0.1.0-alpha, "Open Alpha"** (`gradle.properties`: `warden.version`, and
  `warden.releaseName`, new - `BuildConfig.RELEASE_NAME`). The version code comes from
  the numbers before the "-" (1000). About this app reads "Version 0.1.0-alpha - Open
  Alpha, commit ...", checked on the Thor. Its text now says what the app is made of:
  the game's program, translated from the player's disc when they built the app, and
  the pictures, sounds and words read from their disc image. So each player builds
  their own, and a build is never shared (DECISIONS D11).
- **The docs, for a public repository.** The README is rewritten as the user's manual:
  what the app does, the status and known issues, the files to supply (the disc, Title
  Update 7, disc 2, `Lossless.dll`), building, setting up on the device, the controls,
  every setting with each choice (33 rows, generated from `Options.java`), recommended
  settings, the deck's tabs, saves, mods and which kinds work, bug reports, the licence
  and credits. SIGNING.md gained the release key (§5). DECISIONS.md has D10-D13 (the
  licence, source only, the package ids, the version), and D2 is marked superseded.
  RESUMING.md §0-§3 and §5, CLAUDE.md, this header, §1 and the phase board are brought
  to the present.
- **Nothing private in the repository:** no server or LAN addresses, no device serial,
  no account names in paths, no games the owner plays. `tools/game_check.py` now reads
  its exemptions from `tools/game_check.local.txt` (gitignored, one name a line).

### 2026-11-02 (session 41, later) - saves: names, thumbnails, the put-away autosave

- **Names (10.20):** the 360's Save Game box shows the area's name and no button edits
  it (no keystroke input imported; a probe on the title's keyboard routine found the box
  never calls it). Character creation's keyboard dialog reused: each manual save, once
  written, asks "Name this save"; OK renames it in its `savegame.met` (string table,
  field 0x41A8, CRC-32 in `.x`) and its display name (what the lists show - the first
  tries renamed the Location line, then a field the lists do not read). "Save (23) - At
  the ruins" listed and loaded.
- **Thumbnails (BUG-101):** blank again - the resolve was never read back (BUG-049's
  whole thumbnail had the test marker on). That one resolve read back in full.
- **Put away (BUG-100):** the autosave waited for the return: without a window FSR 3.1
  stopped upscaling, the frame's size changed, frame generation rebuilt its generator
  for 8.7 s with the game stopped. Fixed both ways; Home 1.8 s, sleep 2.5 s, hold limit
  6 s.
- **Corrupted (BUG-102):** saves are written aside and put in place whole at close.
- Test saves: the test character's "Save (20)" to "Save (23)" (21's Location renamed by
  the first try). Run 592: the build boots clean.

### 2026-11-02 (session 41, later) - 5.6: both screens in the README

- The owner asked for a screenshot of both screens as the 5.6 picture, in the README:
  the Dwarf Noble and Gorim in Orzammar's Royal Palace and the deck's Party tab, composed as on
  the Thor (`docs/images/thor-both-screens.jpg`). 5.6 done - the owner's decision on its
  open question (game imagery in the repository).

### 2026-11-02 (session 41, later) - no build machine paths in the APKs

- The owner asked for the full rebuilds: the compiler now writes the NDK's path as
  `ndk`, the repository's as `src` and anything else under the home folder as `home`
  (`-ffile-prefix-map`; `-fdebug-prefix-map` for FFmpeg's assembly, whose three files
  kept the repository's path after the first rebuild). Debug and release rebuilt in
  full; neither APK holds a build machine path or the user name, and `tools/release.py`
  now refuses a release that does. Run 591: the debug build on the Thor boots as before.

### 2026-11-02 (session 41, later) - new package ids, the Thor moved over

- The owner: the package ids should not hold their username - `dev.daorecompandroid.duo`
  for the debug build, `prod.daorecompandroid.duo` for the release. The Java package
  moved too (`dev.daorecompandroid.duo`), and every tool's package name.
- The Thor: a new id is a new app, so the old one's data was moved before the owner's
  "uninstall the old one": the private files (4.27 GB - the saves, the options, the
  installed DLC, Lossless.dll) copied app to app on the device and backed up to the
  computer; the extraction, title update and mods renamed into the new app's folder.
  The new app checked: the launcher all in place, the mods loaded, 24 saves, the owner's game loaded with her and Gorim's faces on the deck's right rows. Then the old app
  uninstalled. Release 0.11.0 rebuilt as `prod.daorecompandroid.duo`
  (tools/release.py: clean).
- Still holding the username: the native library's debug sections (the NDK's path on
  this computer) - fixed the same day ("no build machine paths in the APKs", above).

### 2026-11-02 (session 41, later) - the options that need a restart marked

- The owner asked: every option the game takes only as it starts (eleven: Rendering
  resolution, FSR 3.1 and its sharpening, Frame generation, Shader math, Texture
  filtering, Button prompts, Intro logos, Second screen, Language, Debugging Mode) has
  " *" after its name in the options panel, and the hints under it explain: "* Needs a
  restart of the game: changed while playing, it applies at the next start" - in the
  launcher and in the game. Checked on the Thor in both.

### 2026-11-02 (session 41, later) - the owner's three reports

- **Faces on the wrong rows (BUG-096):** the deck placed each portrait render by guess.
  Traced to the game's portrait builder (`sub_824F5E50`): it names each portrait
  ("portrait_gen00fl_alistair2", the HUD's own name) and draws it right after to its own
  texture; hooked, the name binds the next new resolve address, and each row shows the
  face its HUD name says. Probes on the way (the GUI's render passes; the texture
  objects, which hold no plain address) are gone again. Runs 587-590; the morrigan
  route's rows right, and right after Home and back.
- **Home (BUG-097, BUG-098):** the deck stayed on the second screen when Home sent the
  main one home; a second press reached the home app already home, which can relaunch the game.
  The deck now follows the game, and keeps its faces when it is made again.
- **Volume (BUG-099):** the output peaked near -20 dBFS while exploring - the mixdown's
  headroom for combat. Options -> Volume boost, a gain and a lookahead limiter, off by
  default; the owner's Thor at +12 dB. The audio report gives the output's peak.
- **Routes:** New Game failed with "Character limit reached" - seven characters' saves;
  session 40's three DLC tour characters (GB__Gerod, LS__Leliana, DK__Hurlock
  Vanguard) moved to files/test-saves-moved/ (kept, not deleted). The launcher's test
  launch needed the same.

### 2026-11-02 (session 41) - 11.3 and 11.4: the release build and a clean first run

- **The owner's answers:** I make and keep the release key; the app goes to a public
  repository, "primarily based on" DAORecomp (96dot/Dragon-Age-Origins-Recomp - the
  same repository as 96dot/DAORecomp, renamed), under whichever licence lets everything
  be shared; the clean run on a second copy on the Thor.
- **The licence:** GPL-3.0-or-later. Everything built in allows it but UnRAR, whose
  licence forbids making a RAR-compatible archiver with it - a restriction the GPL does
  not take (BUG-094). .rar is now read by libarchive v3.8.11 (BSD; its read side, a
  static library; the bootstrap fetches it, sparse): the No Helmet Hack .rar imported
  on the Thor through the Mods page, its .dazip byte-identical to Windows' tar.exe's.
  LICENSE, README, THIRD_PARTY.md, About this app and the licences page updated.
- **The release build:** the key (RSA 4096, 30 years) in ~/.android with its
  passwords beside it; the version from gradle.properties; assertions kept in the
  release (RelWithDebInfo's -DNDEBUG stripped - a first try was stopped and rebuilt for
  it); `tools/release.py` proves the APK and keeps its symbols - 0.11.0 and 0.11.0-clean
  pass, a doctored copy fails three ways. Its first draft flagged the app's own text
  ("Lossless.dll", ".iso"); it now looks for files and for Lossless.dll's bytes.
- **The clean first run (11.4):** a second app, <the former id>.clean, from the release
  build with -Pwarden.clean, walked with the Thor's controller written through its
  input device (`sendevent` on the "Odin Controller", /dev/input/event9 - the game
  ignores injected virtual keys): pickers, extraction with progress, the title, the
  storage question answered by the app, the menu, New Game, character creation, the
  options at the owner's defaults. Found: the deck's task affinity was the package
  written out (BUG-095, fixed before the install). The copy removed after; the owner's
  install untouched.

### 2026-11-01 (session 40) — BUG-084 fixed; the areas' tour finished, Awakening and the DLC included

- **BUG-084 fixed** (docs/BUGS.md): the logger's thread buffer overrun, not a race. Two
  logger edits recorded in `tools/assemble_runtime.py`, the port's reports split
  (`warden_trace.cc`). About 150 loads since, none crashed.
- **The tour of the main campaign** went on from the Urn's mountain top: the Gauntlet,
  the prelude, the party camp, the five origins (their menus read with `menu_read.sh`:
  plot points, sub-lists, "Jumping. GO!" prompts), Brecilian's forest and ruins, the
  Deep Roads, Eamon's estate (the loading wait fixed: any loading mode since the call
  counts), Redcliffe, the epilogue. 58 areas in all.
- **The DLC:** `tools/dao_erf.py` reads ERF V2.0 (the DLC packages' archives), which
  showed the DLC's debug scripts; a dispatch through the event dispatcher ran them without
  a menu, the engine's RunScript (`sub_8263FFD8`, as DAORecomp's cheats call it) opened
  every one: `files/route-call` holding `!zz_gwb_debug` (and `!zz_drk market` with a
  value: RUNSCRIPT_VAR, for the console scripts). Routes: `probe-dlc` (the main campaign
  with its add-ins), `probe-awakening`, `probe-drk`, `probe-lel`, `probe-gib`, `probe-str`
  (`.keep-dlc`: the campaign chooser answered); `skip-start` hides any
  `*module_start.ncs`. Toured: Warden's Keep, Return to Ostagar, The Stone Prisoner (13),
  Awakening (28), the Darkspawn Chronicles (6), Leliana's Song (7), the Golems (2), Witch
  Hunt (4). The jump and probe scripts stop at a death screen (its A would load the newest
  save).
- **Found:** BUG-086 (Smuggler's Cove black on arrival). BUG-085's estate draws fully in
  the City Elf origin's own state.
- **The reference build** got a patch (`DAO_RUN_SCRIPT`, `tools/reference_build.py`'s
  PATCHES) and was rebuilt; installing it over `C:\Games\DAORecomp` was refused by the
  permission classifier - the owner's to allow.
- **The owner's asks:** status updates every two hours on the Warden's Port page (three
  today), and the page's table of every area's frame rate and bugs, with each area's six
  screenshots.
- **BUG-085 and BUG-086 - the game's own.** The reference build, with the owner's
  permission to install the patched executable, shows the same two black scenes: the
  estate's debug jump puts the player outside the level, the cove's arrival faces out of
  the area. For the cove, `files/dlc-only` let a test game run with only Awakening, so
  its autosave loads in the reference (Awakening is installed there now; the reference's
  test save slot holds that autosave). Occlusion-query culling is off, as in DAORecomp
  (the camp 37.6 -> 38.9 fps uncapped; `files/occlusion-queries` keeps it).
- **The plan grew (the owner, evening):** rendering resolution as an option (720p and
  1080p mandated, 9.16); upstream surveyed for the rest - its mods system and PC-to-360
  converter planned as 10.12/10.13, and of its other features the owner picked field of
  view, motion blur, depth of field, photo mode (9.17-9.20), the cheats menu, a save
  restore page and the language setting (10.14-10.16); the update checker declined (the
  no-online rule). **Phase 11 - Release** added: Debugging Mode with a one-button
  bug-report .zip into Downloads, save export and import, a release key, the first run
  without adb, licences in the app (11.1-11.5).
- **Phase 8 closed:** 8.4 and 8.5 ticked; 8.8, the touch quickbar - the deck's one write
  path - built and proved (runs 544-545): taps are the slots' own button presses, merged
  into the pad the game polls; BUG-087 (a pause-menu tap got through the first guard)
  fixed. A build ran unasked while the game check reported a possible game on the PC
  (GONEfishing); the owner allowed the next one.
- **The Thor:** back on the owner's settings, the markers off. The tour's test games left
  autosaves (Gerod, Leliana, Hurlock Vanguard, Jaden), the owner's to keep or remove.
- **The party's faces on the deck (8.9, runs 546-548):** the HUD's portrait renders read
  back from the GPU while the deck pulls (one resolve per face, at an area load or a
  member joining), untiled with the head's alpha, placed by the HUD's portrait names.
  Two slips of the first try, fixed before the commit: the faces blue (R G B A, not the
  front buffer's B G R A) and a two-face burst given to slots 0 and 1.
- **Performance tab/overlay (8.10, runs 546-549):** native and total fps, frame time,
  1% low, power drawn, frame generation, upscaler and quality, rendered and shown size -
  on the deck's Perf tab, or on the main screen with the second screen off; the option off
  hides both. The owner allowed the build while a game ran on the PC.
- **Touch (10.17, 10.18, runs 550-551):** the launcher and the options panel by touch, and
  the on-screen controller as an option (Xbox prompts with it). Checked by taps and drags
  through adb from the launcher to the main menu; the three-finger tap that opens the
  panel without the controller is not checked on the device. The owner allowed this
  build too while the game ran.
- **Renamed (the owner):** Dragon Age Origins Recomp Android Duo - the app's label, the
  Gradle project, the native strings, the doc titles and the project page; the package
  id and the repository folder kept (a new id is a new app without the game data). The
  owner then allowed every Phase 9 build of the session with the game running.
- **9.16 (runs 552-566):** the Rendering resolution row. Native 1920x1080 drew its bottom
  black; an EDRAM map (`files/hack-edram-map`) showed why - the game's full-screen passes
  put depth at 0 and colour at 1024 at every size, which overlap at 1080p (no 64-bit
  target, as first thought). Then the owner asked for 1080p anyway: the device at half
  the size and Xenia's 2x draw resolution scale - 1600x900, 1920x1080 and 2560x1440 all
  right, frame generation and the menus too. **9.17 (run 554):** the field of view, upstream's hook, 80-120.
- **9.18 (runs 557-558):** motion blur, upstream's tile, neighbourhood and gather passes in
  Vulkan, along FSR 3.1's camera motion vectors; checked turning at the King's Camp.
- **9.19 (runs 559-560):** depth of field in conversations, upstream's focus, prefilter,
  blur and combine passes in Vulkan; talking to Duncan, the background soft and the two
  of them sharp; about 2.1 ms - the default the owner's call.
- **9.20 (runs 561-562):** photo mode - the fly camera, paused, HUD hidden, photos to
  Downloads; checked by touch alone (the panel's row, the on-screen A and B, the stick).
- **The launcher's test launch (run 568):** a "Test: launch to the King's Camp" row writes
  the ostagar route, adapted as route.sh adapts it (the first try landed in the debug
  menu's night-camp page: without route.sh's d-pad down past Resume and hide-dlc),
  launches and clears the files after; the King's Camp hands off. PLAN 11.6 removes it.
- **Phase 10 begun:** 10.10 closed; **10.14, the cheats** (run 569): five rows in the in-game
  panel running the developers' scripts at the next frame of play; +100 gold, immortality
  and +5,000 XP checked at the camp. The owner allowed that build with Heroes of the
  Storm running, one build at a time.
- **10.15 and 10.16 (runs 570-571):** the launcher's Restore an earlier save (a test
  character's save restored and restored back), and Options -> Language (Spanish checked
  on the title screen). The Thor back on English.
- **10.12 (run 572):** the Mods page and the mod layers; tested with a zip through the
  file picker, a dummy PC mod and a mod holding a copy of a game patch file (read from
  the mod). The test mods removed after.
- **10.13 (run 573):** DAORecomp's PC-to-360 converter built into the app (three upstream
  files edited by `tools/assemble_dao_convert.py`, the rest stood in for in `dao_compat/`
  and `dao_convert_android.cc`; the bootstrap's step 6 now runs the script). A PC-style
  texture mod (`tools/make_test_texture_mod.py`: magenta .dds for Duncan's body) converted
  from the Mods page in 0.15 s, and Duncan was magenta at the King's Camp. The build
  needed libc++'s `-fexperimental-library` for std::stop_token, and stdafx.h first in
  `dao_convert_android.cc` (std::span). No bugs in the app found. The test mod, its
  originals and the empty `mods/.adding/` removed after; Phase 10 done but for 9.14.
- **10.19 (run 574, past midnight):** the owner put two downloads of the No Helmet Hack
  (Nexus 202) in the project folder and asked for a way to import such mods. They are
  add-ins (.dazip inside a .rar and a .7z), which the converter left out; the 360's DLC
  are add-ins in the same layout, so the app now installs a PC add-in as a package of
  downloadable content (`dao_addin_android.cc`), and imports .7z and .rar (7-Zip's C
  decoder and UnRAR, both new pins in the bootstrap). Bugs on the way: upstream's GFF
  code mishandled generic strings (BUG-088, the add-in's .cif), the picker greyed out a
  .rar (BUG-089), the test launch hid the add-ins (BUG-090). The game's talk table loader
  reads PC-style V0.2 tables beside the 360's Huffman V0.4/V0.5 (read in the recompiled
  loader), so no encoder was needed. At the King's Camp: the rule book given on load,
  `zz_givearmor` (the game's debug script) put a helm on Jaden, the mod hid it on leaving
  the menu, showed it in the inventory, and the rule book from the radial menu toggled
  it ("Showing/Hiding Helmets on Jaden"). Both mods stay installed and on, as the owner
  asked; the downloads are git-ignored (`*.rar`, `*.7z`, `*.dazip`) and copies are in the
  Thor's Download folder.
- **2026-11-02: 9.14 closed, a conversation's stutter (runs 575-579).** The owner kept
  every option with the defaults as they were (1280x720, frame generation Off,
  Bilinear) and reported 2-5 stutters at every conversation's start on their settings.
  Reproduced with the wilds route (its presses up to explore mode only), an A that walks
  back to the camp, and an A at Daveth. Found: the pipeline cache saved on the GPU
  command thread (BUG-091) and frame generation's pacing switching out and back for the
  conversation's first frame (BUG-092) - both fixed. Ruled out with timing: pipeline
  creation (4-5 ms at the start), file loads, the GPU (the command thread's frame wait
  was 0 throughout, so that measure was dropped), the game's GUI-opening calls. One
  ~95 ms frame stays, the game's own. New log lines: title frames over 50 ms, pipelines
  over 3 ms. Next, the owner's order: 9.21, the presenter on a second device.
- **9.21, step 1 (runs 580-582):** before building the second-device presenter, its premise
  measured with a probe (`warden_priority_probe.cc`, files/hack-priority-probe): realtime
  priority is not permitted to an app; high priority is granted but a paint-sized job on
  it waits as long as at medium priority (18.7 ms p50 at the camp, 6.5 at the title, 0.37
  at best). The GPU does not preempt for it. Timing the same job on the game's own queue
  hung (Xenia's command processor holds that queue) and was taken out. Asked the owner
  whether to go on.
- **PLAN 2.4, the full way (run 583):** the owner asked what 2.4 would bring; the
  generated code held six traps the recompiler writes silently for forms it cannot
  translate (BUG-093). The owner chose to translate them: `tools/patch_xenonrecomp.py`
  (blrl as an indirect call; Xbox 360 half floats packed and unpacked as Xenia does),
  the conversions checked against Xenia's over 465,547 values, the recompiler built
  again, the title recompiled (no trap left), the APK built and booted to the King's
  Camp.
- **9.21 dropped (the owner); Phase 11 begun - 11.1, Debugging Mode (run 584).** The
  option, the log file (a sink added to Xenia's logger), the header (`BuildConfig.GIT_COMMIT`
  now built in), the loaded save kept at its first read, and the panel's Exit and export
  save/logs: tested with the load route - the zip in Downloads held everything, the game
  ended after. The test report and files/debug removed from the Thor, the option back
  Off.
- **11.2 (run 585):** the Saves page - export one or all to Downloads, import from a .zip
  (checked to be this game's 360 saves), keep both / replace / skip for a taken slot,
  the restore page beside them. Leliana's save went out and back in as a new slot, was
  listed in the game's Load Game and loaded. Found on the way: the import first lost the
  files' times (the copy showed the time of import) - now kept both ways. The owner asked
  mid-way that the log say plainly when mods are loaded: "MODS LOADED: ..." lines at
  each start (each mod and add-in, on or off), "Mods: ..." in Debugging Mode's header.
- **11.5 (run 586):** About and licences - the app's own page and every built-in
  project's licence, gathered from the pins into the app's assets by a new tool (the
  bootstrap runs it). Checked on the Thor from the launcher: the list, the app's page,
  lsfg-vk's GPL text. Noted for the owner: a distributed build would carry GPL-3.0
  obligations (lsfg-vk) and LGPL ones (FFmpeg, libmspack); the repository has no licence
  of its own.
- **The Thor:** back on the owner's settings (FSR 3.1 Performance, Lossless 2x balanced,
  60 fps, Nintendo prompts, the deck on, the Performance option on, the on-screen
  controller off; the new options at their defaults).


### 2026-11-01 (session 39) — the areas' tour wrapped up after the computer crashed

- **What happened.** The computer crashed at 01:40 with `tools/areas/tour.sh` driving the
  Thor; the game stayed up on the Urn's mountain top at 64 fps. Session 39 (this one, from
  01:50) connected again - only the wireless-debugging port answered (`adb mdns services`;
  5555 refused) - read what the tour had left in `build/areas/`, stopped the game and took
  the route markers off. The driving of the running game (the mountain top's probe again)
  was not allowed by the session's permission classifier, so that area is for tomorrow.
- **The tour since the Circle (session 38's last commit, 929f9f5, had no log entry: the
  loader through the game's debug scripts, `files/route-call`, `tools/areas/`, and the
  Circle's twelve areas).** From 00:46 to 01:40: Lothering's three, the Dalish camp,
  Denerim's four, Orzammar's four, the Urn's four - 16 loaded, 15 profiled; four skipped.
  The 90 screenshots read on contact sheets (`sheets.py`, three areas a sheet): nothing
  renders wrongly but the Arl of Denerim's estate, black with only the player and a few
  props (**BUG-085**, open: to check against DAORecomp's Windows build with the same jump
  first). The Dalish camp's numbers are Zathrian's conversation, which 120 presses did not
  end; the tour's "File" column can name a neighbour's file (Lothering's probe read the
  Korcari Wilds' last). Uncapped the areas run 40-68 fps, the GPU at 99%: the Urn's ruined
  temple the heaviest (40), Lothering's chantry 44, Denerim's market 44-48 - all above
  the camp's 37. docs/AREAS.md has the table and the notes.
- **BUG-084 three times in the night.** The app's exit reports (10.7) hold three native
  crashes: 00:20 (Flemeth's hut after a world-map travel - the one logged in session 38),
  01:22 and 01:27 (the debug menu's jumps into Orzammar's Noble's Quarter and Slums - the
  tour saw "still-loading", rebooted and the second try loaded). The tombstones decoded
  (a 40-line protobuf reader, in the session's scratchpad): the same frames each time -
  `memcpy_opt` from `xe::RingBuffer::Write` from `xe::Logger::AppendLine` from the port's
  `ReportLoop()` - and the fault address 0x1 in the two new ones: the memcpy's destination
  was address 1, a null write pointer or buffer, not a write past the ring's end.
  Severity raised to high: one load in ten dies, and the owner's loads too.
- **Three jumps failed on sub-lists** (one more A: Brecilian's ruins ask which part, the
  Deep Roads which thaig) and Eamon's estate ended in his conversation with no load seen
  within 20 s (a plot scene in place, or a load finished between two presses). The
  presses are in `tools/areas/tour.txt`, which now holds the whole list with the done
  lines commented out, so the tour resumes from its first live line. `results_table.py`
  stripped every `al_` from the file names (`ntb01al_` lost its `al_`): the prefix only
  now; `sheets.py` skips a truncated screenshot (three of the mountain top's were).
- **The Thor at the end:** the game stopped; `route`, `inject-pad` and `hide-dlc` removed
  (a boot from the icon is a plain boot); the uncapped options left for the tour - Frame
  rate 120, Frame generation off, FSR 3.1 Performance (the owner's: 60, Lossless 2x
  balanced, Performance); on its charger, 100%, 31 C.
- **Next:** fix BUG-084 before the tour resumes; then `bash tools/route.sh probe`, a boot,
  `bash tools/areas/tour.sh tools/areas/tour.txt`; BUG-085 against the reference
  renderer; the owner's settings back when the tour is done.

### 2026-10-31 (session 38) — the paints' own queue tried and dropped

- **9.14 (runs 473-474).** The paint-queue test switch (session 37's last commit) at the
  camp, paints on queue 0 and on a queue of their own in turn: on their own queue 95-98
  frames in 5 s on the panel against 234 and 296-314 ms from the swap to the panel against
  about 182, the same with that queue at the top priority (473) and at the bottom (474) -
  the driver ignores per-queue priorities, and a paint waiting on another queue's
  semaphore lands behind the frames already sent. Dropped: `warden_paint_queue.cc`, its
  ten graft edits (225 again) and its run script removed; PLAN 9.14 keeps the finding and
  the one way left (a second device at a higher global priority - large, not started).
  9.14's look and feel is the owner's.
- **10.2, done (runs 475-477).** `warden_thermal.cc`: the hottest GPU sensor, big core and
  board, the GPU's clock and thermal level, read by the game every 5 s (the `GPU:` line's
  `thermal:` part) - Android's thermal service has no HAL on the Thor. Run 475, 14 minutes
  at the camp with Lossless 2x over FSR 3.1: no throttling (680 MHz, level 0, the GPU at
  82 C, the game at 23.8 throughout). Options -> Heat guard, Off by default (the owner):
  the frame rate a step lower while the GPU is hot (the kernel's cap or 90 C), back after a
  minute at 80 or less, 30 in battery saver; run 476 (test temperatures) off and on, run
  477 the battery saver.
- **BUG-079 fixed (run 478).** The status loop's guest output captures went on every
  minute in every session - 4 MB PNGs, never removed (about 240 MB an hour). Now the
  first eight only (the boot and the menus, overwritten at each start), more only with
  `files/capture-every`; leftovers removed at the start (13, 50 MB, in run 478).
- **10.4 closed, nothing to gain beyond 10.3.** `shaders.erf` holds the game's 499
  compiled shaders (Xenos microcode in the 360's container); against the Thor's storage
  30 of 331 pixel shaders match byte for byte and none of 116 vertex shaders (patched
  as they are bound), and the pipelines need the draw's state.
- **10.7 done (run 479).** `LastExit.java`: at each start, Android's record of the app's
  exits (`getHistoricalProcessExitReasons`); the process tagged with what the app is doing
  (`setProcessStateSummary`: pre-launch, playing, held, ending) so its own end is told
  apart. Unexpected exits go to `files/exits/` (with a native crash's tombstone or an
  ANR's dump) and the launch row tells the last one. Run 479: killed in play, then SIGSEGV
  - both reported, the tombstone kept, each told at the next start.
- **10.8 and old items closed.** STATUS §6: Q7 answered by 9.4, Q8 and Q11 by 3.3b, Q10
  closed without its own test. PLAN 5.1i and 5.2 closed (BUG-034 not seen in about 450
  boots since 2026-10-24 - now WATCH; the game never asks for `cache:`).
- **10.9 resumed, paused again (runs 505-508).** A pass moving each translated shader's
  uniform-only operations (about 1,100 in the heaviest) into its entry block: valid SPIR-V,
  but the Adreno's compiler kept them per pixel and used more registers (occupancy 25 ->
  12%), no fps gained - removed, never committed. The depth prepass's vertex shader is
  cheap (365 instructions, full occupancy). Frame generation at the camp: Lossless quality
  14.0 ms - 22 fps, Lossless fast 3.3 ms - 28.7 fps, FSR 3.1 2x 24.3 fps, Lossless's quality
  network with half-size flow 5.7 ms - 27.2 fps. Lossless 2x, balanced (that last) is
  a fourth Frame generation choice - run 509: 5.6 ms, the game at 26-27 fps. The owner's
  setting is Lossless 2x (it held `lossless-balanced` after the stopped run at 14:00; put
  back).
- **The owner's choices: Lossless 2x, balanced and Shader math (runs 510-512).** The
  owner's Frame generation set to `lossless-balanced`. Options -> Shader math: Exact (the
  default) or Fast - IEEE multiplication in place of Xenos's zero-times-anything rule, from
  the next start. At the camp with Lossless 2x, balanced: Fast 30 fps (the cap; the traced
  frame 25.8 ms against 32.1), Exact 26-27. Screenshots of each twice: the light shafts vary
  from boot to boot, Fast's slightly fainter but within that; nothing else differs. The
  owner's Shader math left at Exact.
- **FSR 3.1's accumulate pass with the Lanczos lookup table (runs 513-515).** The SDK
  picks the table over its sine weights only where subgroups are 32 to 64 lanes (AMD);
  its backend reported 32 to 32 here. Answered 32 to 64: the accumulate pass 3.70 ms
  against 4.17, alternating each window at the camp (run 513); 1,468 instructions
  against 2,116 by the driver's statistics, now logged for FSR's own pipelines with
  `files/hack-pipeline-stats` (run 514); the default, its screenshot within boot-to-boot
  variation (run 515). `files/fsr3-lut off` keeps the sines.
- **10.5 and 10.6 dropped by the owner; the fetch constants decoded on the CPU (runs
  516-518), 10.9 done.** Of the heaviest pixel shader's 1,911 optimized operations, 157
  depended only on the fetch and system constants - each texture's size, its reciprocal
  (two divisions), its LOD and exponent biases, decoded per pixel. The command processor
  now decodes the 32 fetch constants as it uploads them (three float vectors each, after
  them in their buffer; `SpirvShaderTranslator::DecodeFetchConstants`) and the fetches read
  the values: the same values but for the reciprocals, now correctly rounded. `CBA8DC01`
  1,631 -> 1,510 ALU instructions, 31 -> 19 complex; the traced frame 31.3 -> 29.7 ms; the
  camp 27.2 -> 28 fps (Exact shader math, Lossless 2x, balanced); screenshots alike.
  Ten graft edits (234). `files/hack-fetch-raw` keeps the per-pixel decode.
- **The vertex fetches' binding and endianness known per draw (runs 519-521); the camp at
  30 fps with Exact shader math.** The Adreno's storage buffers are 128 MB at most, so
  guest memory is four bindings and every vertex fetch word went through a switch over
  them - the loads one after another - with the byte swap under branches. The pipeline
  cache now reads each of the shader's first four vertex buffers' binding and endianness
  from the fetch constants at the draw into the vertex modification (version 21; the
  storage kept its entries and run 520 added 112), and the translator loads straight
  and swaps without branches; exact, the bounds check kept. The depth prepass's shader
  365 -> 254 instructions (8 -> 2 loads), the foliage's 2,038 -> 1,088 (76 -> 19); the
  prepass 3.69 -> 3.34 ms, the scene pass 12.55 -> 10.87, the foliage passes -30%; the
  game 28 -> 30 fps (149-150 frames in 5 s), the screenshots alike. The FMin rule measured
  on the way (run 519): +0.6 fps, set aside as unverified on the hardware.
- **Uncapped measurements (the owner: Frame rate 120, Frame generation off; runs
  523-526).** The camp 36.8 fps, the GPU 99% busy: FSR 3.1 6.3 ms, the composite 0.7, the
  game about 20. The game's post-processing off: 40.0 fps - the chain costs 2.2 ms, not
  the traced frame's 7-9, so its copies are not worth deep work. Then the multiplication
  rule's FMin form proved exact on the Adreno 740 with a standalone compute test
  (`tools/mintest`: min with a NaN operand returns the other operand, 84 of 84; the two
  forms equal on 100 pairs) and made the default: the camp 36.8 -> 37.3 fps uncapped (runs 523, 526), the picture the same. The owner's rule from
  this session: any speed that costs fidelity is an Options row.
- **The options' costs uncapped, and two more options (runs 527-530).** Shader math Fast
  42.2 fps against 37.3; texture filtering off 37.7 (16x stays free). Added, today's look
  the default: FSR 3.1 -> Ultra performance (640x360 to 1080p): 46.6 fps, the scene whole, the picture and the HUD's text noticeably softer (the HUD is drawn at 640x360 and scaled three times; a mean difference of 3.7 of 255 from Performance's frame against about 2 between two boots of one setting);
  FSR 3.1 sharpening Off (no RCAS pass): 38.1 fps.
- **16-bit shader arithmetic tried and dropped (runs 531-532).** RelaxedPrecision on the
  pixel shaders' float results, all or all but the precision-critical: valid SPIR-V, +3%
  (38.6 fps against 37.3), and the scene black with only the HUD - the driver's doing, not
  worth chasing. Nothing kept.
- **The Ultimate Edition's disc 2 (runs 502-504; PLAN 10.10).** Eleven Live packages for
  this game on it - Awakening, Leliana's Song, The Golems of Amgarrak, Witch Hunt, The
  Darkspawn Chronicles, Return to Ostagar, Warden's Keep, The Stone Prisoner, Blood Dragon
  Armor, Feastday Gifts and Pranks - and Title Update 6. The launcher's file rows are
  four, as the owner asked: the disc image, disc 2, the title update, Lossless.dll; disc 2
  installs every package (4.1 GB, about 6 s, with progress) and takes the standalone
  disc's Awakening package away. In the game: all eleven mounted; New Game's carousel
  Origins, Awakening, The Darkspawn Chronicles, Leliana's Song, The Golems of Amgarrak,
  Witch Hunt; Leliana's Song's opening and Awakening's creation and opening cutscene play.
  The disc 2 image is in the Thor's Download folder; the standalone Awakening image I had
  put there is removed (the computer keeps the owner's).
- **10.9, GPU headroom, started (runs 494-501), paused.** Under the owner's settings a
  camp frame is about 45 ms of GPU: the game about 23 (the main pass 20.9 ms traced, 914
  draws; the depth prepass 3.7), FSR 3.1 7.7, Lossless 2x quality 14.0. The heavy pixel
  shaders: 1,600-2,900 ALU instructions, no spills; the multiplication rule 31-44% of
  them (IEEE: the game 22 -> 24 fps); the fetch constants decoded per pixel. The pipeline
  statistics now a log line a stage; `tools/gpu_profile.sh`. BUG-083: with Awakening
  installed the New Game routes went astray (the campaign chooser) - routes hide the DLC
  now (`files/hide-dlc`).
- **The Downloadable content row (run 493).** `warden_dlc.cc`: a Live package picked, or
  every one in a disc image (Ultimate Edition's disc 2 holds them), this game's type-2
  packages extracted by the app's STFS reader into `files/content/00000002/<content ID>/`
  with their display names - DAORecomp's `InstallDlc`. A test program built from the same
  sources on the Thor: a copy of the title update marked type 2 installed (26 files, the
  executable patch byte for byte), "installed already" the second time, the title update
  refused, the Awakening disc searched in 2.7 s (none). The picker itself was not driven:
  DocumentsUI stalled ("Can't load content") with a 7.8 GB file new in Download; the
  Awakening row's pick went through the same path (run 491).
- **The launcher's Awakening row (runs 491-492).** Awakening (disc image): the image
  picked and checked by its executable's header, its `Addins/DAO_PRC_EP_1/` extracted by
  the disc extractor (now able to take one folder of a disc) into a staging folder the
  game does not list, renamed into the package's place when whole, the display name
  written. 2.0 GB in seconds on the Thor; New Game offers both campaigns (run 492). The
  Awakening image is in the Thor's Download folder now.
- **Awakening runs (runs 487-490; PLAN 10.10, the owner asked).** DAORecomp installs DLC
  as Live packages; the owner's image is the standalone Awakening disc (its own
  executable, Awakening as loose files in `Addins/DAO_PRC_EP_1/`, 2.0 GB). The TU7 game
  looks for that add-in in its own folder (and then calls Origins "Requires ... disc"),
  and as a DLC package (`files/content/00000002/DAO_PRC_EP_1/`) takes it with both
  campaigns: Awakening's character creation, opening, first fight and conversation play.
  Installed on the Thor by hand (the test character's new save removed after). GPU
  headroom noted as PLAN 10.9 (the owner: to fix later).
- **BUG-081 fixed (runs 482-483), the owner's.** Closing the game's Select menu froze
  play for 4-5 s with Lossless 2x: the first frame after the menu is not upscaled, so
  lsfg-vk's generator was made again for its size and then again for the next (about 2 s
  each). Now made again only for a size that holds 10 outputs; three closes in run 483,
  no freeze (the worst frame about 0.3 s, the game reopening its HUD). Not the deck: run
  482's third close froze with the deck's process killed.
- **BUG-082 fixed (runs 484-486), the owner's.** The deck's Quests tab showed `<emp>`
  tags and the journal's bookkeeping (`Completed: 0`, `PlotGroup: ...`). Now the deck's
  text renders the emphasis bold (`DeckView.rich`), and each quest shows its name and
  text, open ones first.
- **BUG-080 fixed (runs 480-481).** The deck's Items tab now shows the plot items' icons
  (the 120 `plt_ico_` cells; the Runic Worry Token's gold coin), and logs any icon it
  cannot find.

### 2026-10-30 (session 37) — frame generation in play: Lossless 2x, presented in pairs

- **9.14, the paints on a queue of their own (run 473), last.** A test switch,
  `files/paint-queue` (`warden_paint_queue.cc`; ten graft edits, 235 now): the presenter's
  paints, the UI's fence and the present on a third queue of the graphics family, with
  timeline semaphores between the refreshes on queue 0 and the paints. Run 473
  (`alternate high`, the camp): on its own queue above the others, 95-98 frames in 5 s on
  the panel against 234 and 296-310 ms from the swap to the panel against 182 - worse. The
  switch's own waits cost nothing (at most 0.1 ms). Next: run 474, `alternate low` (built,
  not run), then keep or drop the switch.
- **9.14 step 2 (runs 427-441).** lsfg-vk's generation recorded into the game's own command
  stream on queue 0 - no handover between queues (the Adreno time-slices them anyway):
  `warden_frame_generation.cc` takes each guest output image after the gamma ramp's pass
  into the generator and writes the frame between the last two into a second image the
  Vulkan presenter now keeps per mailbox slot (graft edits, 221 now; two more lsfg-vk
  edits wrap Xenia's command buffer). Options -> Frame generation: Off, Lossless 2x,
  Lossless 2x fast.
- **The presenting took five runs (BUG-069 to BUG-073).** DAORecomp's pacer, ported first,
  times the real frame by the CPU - on a GPU-bound single queue both paints ran right
  after the title's frame anyway, and it kept pausing generation (run 427). Presented at
  once with desired times instead: the generated frames "due now" made Android drop the
  held real ones (428); MAILBOX dropped one of each pair (429); FIFO moved the painting to
  the UI thread, real frames only (430); held frames counted as late wound the readiness
  up to 101 ms (431); and the pair's second present waits in `queueBuffer` until the first
  is latched, about one GPU frame - from the guest output thread, the title fell to 22 fps
  (432-438, found with simpleperf). Now a thread of the presenter's presents each pair,
  every present on one timeline, the real frame half a title frame after the generated.
- **At the King's Camp, the camera turning (runs 439-440):** about 60 frames a second on
  the panel, mostly two refreshes apart; the title at 31.8 against 38-40 without (lsfg-vk
  quality at 1280x720: 6.2 ms of GPU a generated frame); the title's swap to the panel
  113-115 ms against 70-74. Untimed, nearly every generated frame is dropped (run 441).
- **Lossless 2x fast (run 442):** lsfg-vk's performance mode at half flow, 1.17 ms of GPU a
  generated frame - the title at 38.4 as without, 70-90 frames a second on the panel, the
  swap to the panel about 95 ms. Its frames ghost the floating name labels: the UI is
  interpolated with the scene.
- **The UI never interpolated (run 443):** lsfg-vk gets the image before the UI (9.4's
  capture, now made with FSR 3.1 off too while frame generation is set) with the game's
  gamma ramp applied, and the real frame's UI is put over each generated frame where the
  front buffer differs from that image (`warden_framegen_ui.cs.glsl`). The HUD and the
  floating names are crisp in generated frames; 0.6 ms more GPU (6.8 a generated frame,
  quality). 222 graft edits.
- **The frame rate a target on the screen (run 444):** with frame generation the game runs
  at half the Frame rate - 60 is its own 30 - and at the camp the panel shows an even 60
  (275 of 318 frames two refreshes apart).
- **3x and 4x (runs 445-446):** Lossless 3x fast and 4x fast in the panel - up to three
  generated frames a guest output, the presenter's thread presenting the group. 4x at 120:
  the game at its own 30, 3.9 ms of GPU for three frames, but 78-87 frames a second on the
  panel rather than 120 - each present in FIFO waits for the one before to be latched, so a
  group of four takes 28-43 ms to present. 2x unchanged.
- **The owner in play:** frame generation "feels bad with high input latency without
  looking smooth" - with FSR 3.1 Quality too the game at 23 and a pair's frames a refresh
  apart then four or five; without it the game at 30, 47-57 on the panel, 84 ms. 3x and 4x
  dropped (the owner).
- **BUG-074:** the Thor's pad reports its face buttons by their printed Nintendo letters,
  so with the Nintendo prompts confirm and cancel were the other way round; A/B and X/Y
  now trade for that pad before anything reads them.
- **BUG-075:** with FSR 3.1 on, the presenter still sharpened its frames again (Upscaling
  on FSR 1 or CAS); FSR 3.1's frames are now marked sharpened, and the panel greys
  Upscaling out while FSR 3.1 is chosen.
- **Upstream checked (the owner asked):** DAORecomp moved to `eb2360a` - its Aeducan
  Thaig fix is BUG-051's own (recompiled code byte-identical, our addition dropped), its
  Ostagar load crash fix ported (BUG-076), its pipeline storage seek taken (BUG-077); the
  FidelityFX SDK 2.x has nothing for a full-screen rect and HLSL-only shaders (stay on
  1.1.4); xenia-canary's nine GPU fixes noted for a later graft move.
- **FSR 3.1's frame generation, started:** the SDK's frame interpolation and optical flow
  components build into the app, their shaders through `build_fsr3_shaders.py` (now by
  component).
- **FSR 3.1's frame generation, hooked up (runs 447-450).** After the upscaler's dispatch
  and composite, optical flow and frame interpolation (the upscaler's own dilated depth
  and motion, its scene before the UI) into a half-float image; frame generation's "fsr3"
  mode applies the ramp and the real frame's UI in its UI pass (0.9 ms) and hands it to
  the presenter's pairs. Options -> Frame generation -> FSR 3.1 2x (with FSR 3.1 on). Run
  447 worked first time but cost 14.4 GPU ms (the game 22.8 at the camp); run 448 timed
  every pass - the optical flow's search 5.3 ms at 1080p - so the flow now runs on the
  render-size image (the SDK's frame interpolation source copied by
  `build_fsr3_shaders.py` with its flow pass sized by the flow): 9.5 ms, the game at 25.8
  fps with FSR 3.1 Performance (28.8 without), 45-62 on the panel, latency p50 54-65 ms
  (runs 449-450; Lossless 2x without FSR 3.1: 55-63, 49-52 ms). Screenshots during the
  camera turn clean. The rest is AMD's algorithm (the game motion vector field 2.4 ms,
  the interpolation 2.2). The owner's settings put back after (FSR 3.1 off, Lossless 2x).
- **The other profiles (runs 451-452):** Balanced - frame generation 11.7 ms, the game at
  22.2 fps, 39-52 on the panel; Quality - 14.5 ms, 19.1 fps, 35-44 (its cost follows the
  render size). The option's text gives the three.
- **The app opens on its settings (the owner asked; run 453):** from the icon, the
  options panel comes first, with Launch Dragon Age, Disc image (.iso) and Title update
  at its top - the files' rows open the document picker and say what was chosen; every
  option is taken as the game launches. The title now loads at launch (the prompts, the
  intro logos and the title update read then), and frame generation's setting is read
  again as the game boots - checked: Off chosen before launching over Lossless 2x fast
  gave the game a MAILBOX swapchain and `Frame generation: off`. The scripts'
  `--ez boot true` still boots at once. The owner's settings put back after.
- **Phase 8, the second screen's deck (the owner asked; runs 454-458).** The game's UI
  observes the engine's data model by token path; two read-only hooks
  (`warden_deck.cc`) name each observer and read each changed value, and the deck - its
  own process on the lower screen, pulling through `DeckService` - shows Party (health,
  mana or stamina, conditions, names as each is selected) and Perf (the game's and the
  screen's frames, frame times, battery, GPU). Both routes onto the display work (8.1);
  BUG-078 (the launch took the controller's focus) fixed; killed mid-combat, the game
  went on unaffected (8.7). Options -> Second screen.
- **Phase 8, the other tabs (runs 459-463).** The journal, inventory and tactics screens
  pull their data while open, so the mirror reads it itself the way their handlers do -
  the engine's own getters, on the game's thread, half a millisecond at most in each
  16 ms, only while the deck pulls. Quests (the journal's plots and the givers), Map
  (the area's notes, the givers and the party by position), Codex (unlocked entries; the
  journal's conversations as text from the talk table), Tactics (each member's slots by
  number), Items (the purse, what each wears, the backpack). Run 462 switched the reads
  on one at a time: the game's draws unchanged. Next: 8.4, the game's own fonts and frame
  art, portraits and icons; the tactics' names.
- **8.4, the game's typefaces:** "Nogard" and "Dragon Text", made into TrueType on the
  device from `fonts_en-us.gfx`'s DefineFont3 outlines (164 ms the first time, cached) -
  the deck's every word in the game's own faces. The portraits are runtime renders, not
  files; the icons' archive is still to find.
- **8.4, the HUD's art (run 464):** the shared atlas decoded on the device (265 ms, once):
  the party cards wear the HUD's portrait ring with its health and stamina or mana arcs
  cut to each member's values (the Korcari Wilds' fight: Alistair 98/115), the chosen tab
  the main menu's gold-edged bar, the popups' ornate divider, the journal's heading bars.
  Left of Phase 8: the items' icons, the map's area image, 8.8.
- **The tactics' names (run 465):** each slot as the tactics screen words it - the
  target, the condition and the command from the 2DA tables through the talk table
  ("Self: Being attacked by a ranged attack -> Activate mode").
- **Phase 8 done for its exit criteria:** six tabs of live game state, read-only, in the
  game's own faces and art; the deck isolated from the game. Left: the items' icons (not
  in any archive by their names), the map's area image, the abilities' names; 8.8 later.
- **The pre-launch settings' Lossless.dll row (the owner asked):** the file picker, the file
  checked as a Windows DLL and copied into the files folder where frame generation reads
  it. The owner picked theirs on the Thor: `Lossless.dll in place (7345 KB)`.
- **The items' icons and the tactics' abilities, seen (run 468, the Korcari Wilds):** the
  Items tab with the game's own icons (177 cells of the shared DXT1 atlas), the Tactics
  tab naming each ability ("Activate mode: Shield Cover", "Use ability: Shield Bash").
- **The Map tab's area picture (runs 469-472):** the game's own parchment map of the area
  (`Area.Minimap`, from the area's GPU archive), the markers on it by its world edges,
  turned as the game's map screen turns it (`Area.North`) - checked against the game's
  own map at the King's Camp.
- **Frame generation under the game's rate, measured (run 466):** the pacing adapts
  (pairs asked 21 ms apart for the game's 42), but a quarter of the pairs' frames land a
  refresh apart and the latency is 130 ms from the title's swap to the panel - the
  timeline's 40-50 ms readiness, the half-frame hold and Xenia's three frames in flight
  with the GPU at 99%. Next experiments in PLAN 9.14: two frames in flight, then one;
  pair steps in whole refreshes; readiness capped.
- **Frames in flight (run 467):** 2 no different from Xenia's 3 (130 ms from the swap to
  the panel either way), 1 halves the game. Under the game's rate the latency is the
  GPU's - its frame, the half-frame hold, the paints waiting behind the next frame - so
  the levers are headroom (FSR 3.1 Performance) or, later, the paints on a queue of
  higher priority. The switch stays, 3 by default.
- **The deck's tactics name their abilities** ("Use ability: ..."), from the abilities'
  M2DA table - built, not yet run on the Thor (the owner was on it).
- **Next:** the owner's look at the deck, and at FSR 3.1 2x against Lossless 2x; the owner's hands on BUG-074's fix (installed now: run 447
  logs `pad 10 reports its face buttons by label: A and B, X and Y traded`).

### 2026-10-30 (session 36) — FSR 3.1: 16-bit passes, one capture a frame, Balanced and Performance

- **Resumed from PLAN 9.4's list** on the original workstation; the Thor over Android's
  wireless debugging now (`adb mdns services` finds its port - 5555's legacy listener is
  off until `adb tcpip 5555` over USB).
- **Step 1, the 16-bit passes (runs 402-404).** The graft enables `shaderInt16` and
  Vulkan 1.2's `shaderFloat16`; `warden_fsr3.cc` answers `VK_KHR_shader_float16_int8`,
  always one extension so the SDK's scratch layout never changes (BUG-064's note). Run
  403 alternated the two precisions on one scene (`files/fsr3-half` holding
  `alternate`): FSR 7.8 ms against 8.4, all in the accumulate pass (4.2 against 4.8);
  23.3 fps against 23.0 at the camp. Every pass timed now (the SDK's `vkCmdDispatch` taken
  through the procedure lookup it is given): accumulate 4.2 ms, the shading-change pyramid
  1.56 (its source is ten luma loads and powers and two sorting networks a pixel; the SDK
  runs both pyramids 32-bit on purpose), the rest under 0.6 each.
- **BUG-065:** run 402 died (SIGTRAP) at the first context made again - the upscaler
  waited for the submission still being recorded, which makes Xenia end it mid-swap.
  Fixed: it waits for what was submitted before the frame; the context made again 14
  times in run 403.
- **Step 2, one capture a frame (runs 403-407).** A frame trace (`files/fsr3-trace`)
  shows the camp's post-processing as three passes reading the scene, each resolved for
  the next, then the tonemapping (`20BA772900FB8AA3`, DAORecomp's tonemap shader) with the
  UI drawn over it, all in EDRAM tile 1024 - so the last pass is learned from the frame
  before and the earlier ones are not copied; the kept render target is copied at the next
  draw into any render target. The image once a frame (run 404). Then a per-frame summary of
  the draws (run 406) found BUG-066: the post-processing's half-size 2x MSAA passes were
  taken for the console's MSAA scene tiles - jittered, and a source of FSR's depth - and
  the depth copied at the depth prepass's resolve too. Fixed: two copies a frame, only the
  prepass and the scene's colour draws jittered; the camp 24.2 fps exploring (run 407),
  from 23.1 at the session's start.
- **Step 3, the render sizes (runs 408-409).** The device set-up (`sub_82692230`) and,
  inside it, Direct3D's device creation (`sub_82D8E8E0`) are wrapped: the present
  parameters' back buffer size and the engine's two size globals rewritten as the device
  is made. At any size but 1280x720 the game's MSAA tiling asks for one tile with 4x MSAA
  (the console's 640x480 rule), so the tiling hook makes any MSAA tiling one tile without
  it. Balanced is 1120x630 (whole 80-pixel EDRAM tiles), Performance 960x540. The UI lays
  out as before (softer at Performance, scaled up twice); the title screen's 3D
  background is upscaled too, at 60 fps. The camp exploring: Quality 24.2, Balanced 26.5,
  Performance 29.3 fps; FSR 7.8, 7.05 and 6.5 ms, about 5.3 of it at the 1080p output.
- **Into the Radius 2 ran on the PC** for part of the session: no build while it did
  (CLAUDE.md); the docs and step 3's code were written meanwhile.
- **Step 4, the camera's motion vectors (runs 410-420).** The static meshes' vertex
  shaders keep their world-view-projection in c0-c3, so WVP(before) x inverse(WVP(now))
  of any mesh that has not moved is the camera's reprojection; meshes drawn once in both
  frames are matched (instanced copies skipped) and the reprojection most agree with
  within a quarter pixel is taken - known in 96-100% of frames while turning. A compute
  pass reprojects each pixel's depth (0.32 ms); the camera's planes from the same
  matrices (near 0.15, far 4008, 60 degrees). A probe at five pixels read real motion
  (3-16 px a frame, the orbit's parallax), yet a turn smeared as before.
- **BUG-067, the find of the session:** FSR 3.1 takes three shared resources from the
  caller with every dispatch (dilated depth and motion, reprojected depth); the port never
  passed them and the SDK does not check, so FSR has run without depth or motion since it
  was built - a still scene fine, any motion smeared, vectors or not. Found with FSR's
  debug view (`files/fsr3-debug`: its motion flat grey, its depth black) and a
  deliberately wrong 20 px of motion that changed nothing (run 417). Passed now: the debug
  view's motion field smooth with the character apart by parallax, the shading-change
  mask 17.7 -> 1.1, a turn without streaks (runs 418-420); FSR 9.2 ms at Quality, its
  reprojection no longer skipped. On the way: the Adreno 740 cannot filter 32-bit float
  textures linearly (vkjson) - only FSR's debug view's depth panel samples one so.
- **Step 5, the option (run 424):** Options -> FSR 3.1: Off, Quality, Balanced,
  Performance, from the next start (the device's size is fixed as it is made) - pressed
  through on the Thor, the file written each way.
- **The three profiles again, with FSR doing its depth work (runs 421-423):** the King's
  Camp at Quality 23.4 fps (FSR 8.35 ms), Balanced 25.9 (7.55), Performance 28.8 (6.78);
  off 31.1.
- **9.15, LS1 looked into (the owner asked):** Lossless.dll's 300 shaders are 202 DXBC
  and 98 SPIR-V, and the SPIR-V is only LSFG 3; LS1 and the other upscalers are DXBC only,
  their pass logic in the DLL's x64 code (the `LS1`, `LS1_P` classes) with no open
  reimplementation - building it would take doitsujin's dxbc-spirv and reverse-engineering
  that code. Deferred by the owner, to revisit later.
- **9.14, frame generation, step 1 (runs 425-426).** The Thor exports semaphores only as
  sync fds (vkjson), so lsfg-vk - frames and a timeline semaphore traded as opaque fds - cannot
  run as built; its backend runs on the game's own device instead: the bootstrap fetches
  lsfg-vk at the pin, `tools/prepare_lsfg.py` gives its externally managed device a queue
  index and the 16-bit choice, `warden_lsfg.cc` (GPL-3.0) takes the place of its context with
  images and a timeline semaphore of the device, and the graft makes a second queue and
  enables timeline semaphores. With the owner's Lossless.dll in the files folder, a test
  (`files/lsfg-test`) generated from the game's frames for minutes, the frame between two
  lying between them as it should; about 4-7 ms of GPU a generated frame at 1280x720,
  judged from the camp's frame rate (the timestamps' span takes in the game's work too).
- **BUG-068:** the game check stopped on an emoji in a window title before its verdict, so
  no build could start; fixed.
- **Next:** 9.14 step 2, the presenting; the owner's look at FSR 3.1 in play; per-object
  motion for FSR would need transform feedback in Xenia's shader translator.

### 2026-10-27 (session 35) — put away and back: the main thread hung in the wake pipe; the game now holds where it was

- **The owner's report:** the voice-over heard (6.4 done); switching apps or going home and
  back froze the picture and ended in "not responding"; the power button left the screen
  black with the sound playing. Both fixed first, as asked.
- **BUG-056, the freeze and the black screen.** Both ANRs' traces (the system's dropbox)
  show the main thread in `read` under `AppContext::OnWake`: the wake pipe was blocking,
  and `OnWake` drained it until a read failed - never. A paint request from the guest
  output thread, made only between a surface's return and its first paint (about one
  return in three), parked the main thread for good; the window never drew again, never
  took focus, and Android called the app not responding. **Fixed:** `pipe2(O_NONBLOCK)`.
- **BUG-057, the game running on in the background.** Only the sound output paused.
  **Fixed:** `warden_suspend.cc` - the title's threads hold at their waits, sleeps and
  yields, the audio worker, the kernel's clock and the 5 s reports with them; the title's
  clocks stop, so it sees no time pass; the GPU and the vblank go on, so Direct3D's render
  thread, which polls the GPU without waiting, never spins in the background.
- **Runs 319-322:** fourteen returns - home, the power button, quick toggles, at the title,
  in the intro movie and in play - each window focused within 0.6 s, every paint posted
  (three through the pipe), no ANR, no fault; the title's clock 7.618 s over 7.615 s of
  play between two suspensions; the scene, the movie and the pause menu as they should be
  after a return. While away, 10-13% of one core - nearly all of it two of Xenia's threads
  that spin-wait whatever happens (BUG-058, open; the blocking wait is the fix).
- New: `tools/suspend_check.sh <run>` - beside a capture, away and back five ways at the
  title, with a verdict (run 322: PASS - five returns, focus back within 0.51 s each).
- **The owner confirmed suspend and resume on the Thor**, by hand, and asked to go on
  with Phase 3.
- **3.9, validation-clean (runs 323-327).** The Khronos layer (Vulkan SDK 1.4.363.0, in
  the debug APK's `jniLibs`), core and synchronization validation: no error and no
  hazard through the boot, the intro movie, the title, the menus, a save loaded into
  the Fade, the King's Camp by the prelude debug menu, combat with Morrigan's spells in
  the Korcari Wilds, the intro cutscene and character creation. The only warning: the
  swapchain is not pre-rotated for the portrait panel - harmless, the compositor rotates
  on the display hardware (Device composition, transform 90). The best-practice checks
  (run 324) gave only advisories that follow from emulating the Xenos or from choices
  made on purpose. The layer stays in `jniLibs` (git-ignored, 27.7 MB), doing nothing
  unless switched on.
- **3.10, capture-based debugging (runs 328-330): GFXReconstruct.** `tools/gfxr.sh`: the
  layer in the app's own data directory (the replay app's copy is hidden from ours by
  Android 11's package visibility), a capture of chosen frames or of one frame on a
  trigger, and the replay on the Thor with screenshots - a King's Camp frame, 164 MB,
  replays as the game drew it. A reference capture, `camp.gfxr`, stays in the Thor's
  Download folder. Replays are for looking, not timing.
- **BUG-059 (runs 328, 331-332):** with no Vulkan instance - the first capture's layer
  could not be loaded - the boot went on and crashed in the command processor's
  constructor. Fixed: a recorded edit fails `VulkanGraphicsSystem::Setup` without a
  provider, and the title is not started without the GPU; the report names the Vulkan
  failure. 167 recorded edits.
- **3.7, memory measured (runs 333-337).** 731 MB PSS and 1.8 GB of GPU memory at the
  King's Camp, with 8.6 GB of the Thor's 15.6 available. Every `vkAllocateMemory` is now
  counted with its caller (`warden_gpu_memory.cc`, 169 recorded edits): 614 MB - textures
  224, the shared memory's sparse chunks 160 (sparse binding saves 350 MB), render targets
  154. The other 1.18 GB is the driver's memory for pipelines, about 10 MB each - and the
  pipeline storage (10.3) creates all 106 it holds at startup, from every area visited
  before, a number that grows with play (BUG-060, open).
- **BUG-060 found out and fixed; 3.7 done (runs 338-345).** The driver's statistics per
  pipeline (`VK_KHR_pipeline_executable_properties`, `files/hack-pipeline-stats`): a
  pipeline whose shaders spill registers holds about 24 MB - 44 of 181, 1.04 GB - and
  the scratch is the compiler's spilling under register pressure, not the register
  file's array (run 341's SPIR-V: every index a constant). The stored pipelines are now
  created when first drawn with - hits in the driver's cache - and not written to the
  storage again: the King's Camp 1,027 MB of GPU memory against 1,799, the title 386
  against 1,511, the camp's load as smooth. 179 recorded edits.
- **3.8 deferred to Phase 9** (the owner): the game holds its own 30 fps cap, and the
  headroom work comes back with 60 fps and FSR. **3.3b laid out for the owner**: a
  reference renderer on this PC is the cost - DAORecomp's Windows build (25 GB, an hour's
  build; the same recompiled code, so a difference is the GPU's; Phase 9's reference too)
  or Xenia (20 MB; emulates the disc, its accurate EDRAM path); the comparison is still
  scenes by eye rather than pixel diffs, and what it finds may be the Adreno's missing
  interlock, fixable only effect by effect. Recommended: at Phase 9's start, with
  DAORecomp's build.
- **3.3b moved to Phase 9's start** (the owner), with DAORecomp's Windows build. **Can it
  run headless? Unattended, yes; without a window, no** (its source; PLAN 3.3b): the
  installer builds from the command line (`install.py --iso --title-update --dest
  --accept-msvc-license`, the build tools' license the owner's to accept), and a run takes
  `--play`, `DAO_BACKGROUND=1` (a window behind, focus kept), `DAO_INPUT_SCRIPT` (a
  scripted gamepad) and `DAO_CAPTURE_EVERY` (the guest output as PNGs, the image our own
  captures save), wrapped by its `scripts/test/run_game.sh`; frames need a window on the
  desktop and the D3D12 GPU. Phase 3 is done, its two open items in Phase 9.
- **Q12 answered (the owner): no foreground service.** Instead PLAN 9.13: in Phase 9,
  investigate whether the game can autosave as it is put away - the game's own autosave
  started through `warden_game.cc`'s script machinery, the states that allow it, and a
  save finished before the hold.
- **The app icon starts the game (runs 346-349).** Opt-in since bring-up, when every boot
  ended in a crash: now a launch boots unless it says `--ez boot false`
  (`tools/appbringup/run.sh` does, for the report alone). The report is hidden while the
  game starts, after the surface exists, and comes back with the reason
  (`NativeBridge.bootReport`) if it cannot; a first launch starts the game when the
  disc's extraction completes. From the launcher's intent: booted 0.8 s after the start,
  the intro movie on screen at 4 s (run 347); with `--ez boot false`, the report and no
  boot (run 348).
- **Phase 7 done (runs 346-352).** Every button prompt is an image named by the
  button's position - `XBoxGamepad_BottomAction` and so on, in the UI's shared atlas -
  and the text's tokens (`<A/>`, `<LB/>`) expand to the same images; the game's PS3
  release left its own set beside them, `PS3Gamepad_*`, in the same coin art. A glyph
  set is a list of redirects applied to the atlas descriptions (`atl_*_dat.xml`, no pixel
  changed), written as the title loads into a new override tree (`files/override/`, laid
  over D:\ by `warden_fs.cc`) as `packages/core/patch/` files, which the title reads in
  place of its archive's. On the Thor: △ □ ○ and X Y A on the quickbar, △ and X on the
  main menu, ✕ in the pause menu, L2/R2, L, ✕, △, ○ in the in-game menu. The setting is
  Back -> Settings -> Button prompts, from the next start; a switch while the game ran
  crashed the next load (BUG-061, run 350), so it is not done. New tools:
  `tools/dao_xds.py` (atlas pages), `tools/dao_gfx.py` (the Scaleform movies, their
  ActionScript), `tools/check_glyphs.py` (each set drawn from the disc's data, on the
  computer).
- **Phase 9 opened (the owner): DAORecomp's Windows build, and 3.3b done.** The owner's
  rule: every start of DAORecomp in the background, never disturbing the owner, and under
  the build's game check - now CLAUDE.md's, with `tools/game_check.ps1` (the four
  checks of the owner's rule, a verdict) and `tools/dao_reference.py` (the only way to
  start it: the check, `DAO_BACKGROUND=1`, muted, windowed, the owner's config put back,
  and a check every second that its window stays behind). `tools/reference_build.py`
  builds it from a clone of the pinned DAORecomp with the port's recompiler corrections:
  without them the reference crashed entering the Korcari Wilds, the port's BUG-051. The
  test character's autosave (Jaden's - session 35 found the owner's own game is
  the owner's character's) and the profile copied from the Thor (`tools/reference_save.sh`), the same
  scenes on both (`tools/scene_capture.sh`, `tools/compare_frames.py`): the Korcari
  Wilds, the travel to the King's Camp and its cutscene, the menus - nothing renders
  wrongly on the Thor; the reference's own portraits are orbs (BUG-052's kind, in
  DAORecomp's Direct3D 12 build). Found on the way: launched with the screen off, the
  app is paused and the game booted without a surface held until the screen came on,
  then took its window (run 353) - the capture tools wake the Thor first.
- **PLAN 9.6: 16x anisotropic filtering, on by default (runs 355-358).** Xenia's own
  `anisotropic_override` from `files/anisotropic` (no file: 16x). At the King's Camp
  (`tools/measure_ostagar.sh`) the GPU 93% busy with it and without, 30 fps both; the
  receding stone path, a smear without it, keeps its flagstones. Bicubic filtering not
  done: four fetches for each of the game's. The settings are a list now - Button
  prompts, Texture filtering - each from the next start. The route runs touched none of
  the owner's saves or profile (checked against a copy kept on the Thor).
- **PLAN 9.1: 60 fps as a setting (runs 359-362).** DAORecomp's swap-interval hook
  (`warden_frame_rate.cc`, `files/frame-rate`): the title's menus at 60, the Korcari
  Wilds 43-44, the King's Camp 31.5 with the GPU 99% busy. The paced timeline held
  frames two refreshes and then five at 44 fps (p95 42 ms); unpaced, two or three (the
  1% low 39.4 fps) - so at 60 frame pacing is off by default. Q3 answered: no 30 fps
  logic tick. 30 stays the default; 60 as the default, or a steady 40, the owner's call.
- **PLAN 9.5: FSR 1 and CAS as settings (runs 363-364).** Xenia's own presenter effects,
  set as the title boots (`files/upscaling`). From the panel's own screenshots at the
  King's Camp: bilinear the softest, FSR 1 the sharpest (the foliage's stair-steps
  sharpened too) for 96-97% of the GPU against 93% and 29.8 fps; CAS between them to
  the eye but 28.6 fps with the GPU less busy - a cost on the presenting side, not
  followed up. Bilinear stays the default.
- **PLAN 9.11: the intro logos as a setting (runs 365-366)** - Settings -> Intro logos;
  skipped, the movies folder lists no logo and the title asks for none. 9.9's mechanism
  exists (the override tree and the title's patch folder); converting PC texture packs
  to the 360's `.xds` does not.
- **PLAN 9.12: the speakers' mixdown and the headphone surround (runs 367-369).** The
  title's six channels recorded (`files/audio-dump`, `tools/audio_levels.py`): the back
  pair mostly the front pair 7 dB down, the centre in use, the low-frequency channel
  hardly; combat past full scale, the menus 25 dB below; Xenia's mixdown -17.9 LUFS over
  the run. Now ITU-R BS.775's (`warden_audio_mix.cc`): -17.7 LUFS, the voices 3 dB
  forward. Settings -> Headphones -> Surround: the five speakers placed around the
  listener by a spherical-head model, at the same level, only while the stream plays on
  headphones (GameActivity's audio device callback); 6 µs a frame on the Thor. Stereo
  by default; listening is the owner's. `tools/audio_host.cc` runs the device's mixer
  over a recording on the computer.
- **The game check is Python now (`tools/game_check.py`).** This PC's execution policy
  refuses unsigned PowerShell scripts, so `game_check.ps1` ran only with the policy
  bypassed, which the session's permission check refused as a security weakening; the
  same four checks through the Windows API and `typeperf` need no policy at all.
  `dao_reference.py` and `reference_build.py` import it; CLAUDE.md names it.
- **The Warden's Port page has a Status update log (the owner).** At the top, from the
  page's database: an entry every two hours (`status/<UTC date>`, entries keyed by
  time), the page republished only when a plan item finishes.
- **PLAN 9.3: Display-P3 and 10 bits (runs 370-372).** The panel runs in Display-P3
  (SurfaceFlinger's colour mode) and Android converts the game's sRGB frames into it:
  D8's default held all along. The swapchain is now 10-bit sRGB by default
  (`warden_surface_format`, the presenter asks it first): the colours unchanged, about
  1% of the GPU at the King's Camp (95% against 94%, 29.9 fps both), still composed by
  the display hardware. Settings -> Colour -> Vivid tags the same values Display-P3, the
  game's colours stretched to the panel's gamut (+9% saturation at the camp).
- **PLAN 9.13 investigated (runs 374-375, `files/save-chain`).** A save is a worker job,
  0.6 s from its build to the container's close (293 KB, a 458 KB thumbnail); Android's
  cached-app freezer is on and freezes a put-away app some seconds after it is cached,
  so a save started on pause fits. Autosaves go to `<character>_1_a!0`. Mapped: the save
  manager (the game object's +292, `sub_823027E8` saving to a slot), the job
  (`sub_822D0370`, `sub_822CFC30`), and the area-load autosave's source, a load/save
  state machine whose state 3 saves (`sub_82390360` -> `sub_82391E30`). Left: the call
  that asks for the save, and where the game refuses it.
- **The routes' saves (found on the way).** The wilds and morrigan routes play a new game
  as "Jaden" and autosave into his slot entering the Wilds - runs 367-368 and 374-375
  replaced his autosave, as earlier route runs had; 3.3b's "owner's autosave" was his.
  The owner's own game is the owner's character (autosaved in the Fade, 2026-10-27), untouched, and
  the profile came out byte-identical. `tools/route.sh` now refuses a resuming route
  (`.resumes`, the load route) unless the newest save is Jaden's.
- **PLAN 9.8, closed the other way round (runs 376-378).** No increases: the engine reads
  no shadow or distance setting, DAORecomp has none, and the GPU is 93-95% busy at the
  King's Camp at 30 fps. Its PC switches instead, as DAORecomp's graphics menu uses them -
  Settings -> Dynamic shadows and Post-processing, cleared every frame before the
  scene's tiling. At the camp at 60 fps: 31.6 fps; shadows off 36.5; both off 41.5 (the
  haze and light shafts gone, darker). Both on by default.
- **PLAN 9.10 dropped - Q2 answered (runs 379-380).** In the executable,
  `ToggleTacticalCameraMessage` is a message type, bound to the right stick's click in
  the controller table (`sub_823E0A48`; its input codes are XInput's buttons in order,
  the gaps closed) and accepted by the explore mode. At the King's Camp neither R3 nor
  the message sent through the engine's own SendMessage (`sub_825E47B0`; new test
  marker `files/send-message`) moves the camera, and no tactical-camera option or
  camera-distance setting exists by name. 9.13's `"SaveGameMessage -1 2"`, accepted by
  the explore mode too, saved nothing either way it was sent (run 381).
- **PLAN 9.7: FXAA as an Upscaling choice (runs 382-383).** Xenia's Vulkan backend has
  no anti-aliasing, so the app has its own pass: NVIDIA's FXAA 3.11 with the bilinear
  upscale (`shaders/warden_fxaa.ps.glsl`, compiled by `build_xenia_shaders.py` against
  the header patched for Vulkan's separate texture and sampler), the presenter's
  bilinear module when Settings -> Upscaling says FXAA. At the King's Camp 28.2 fps
  against 29.9, the edges a little smoother; the game's own 2x MSAA there: 20.2 fps,
  not offered.
- **PLAN 9.2 and 9.4 decided: not built, the owner's to revisit.** HDR: the Thor still
  offers no PQ or HLG swapchain, and on Android 13 scRGB values above 1.0 are not shown
  brighter (extended-range brightness came with Android 14) - on a 420-nit panel, with
  no way to see the result in a screenshot. FSR 3.1: new Vulkan code and motion vectors
  the game never produced, paying off only below 720p, on a GPU with no room.
- **PLAN 9.13: the autosave-on-pause mechanism built (runs 384-386).** On pause the
  update thread asks for the autosave (exploring only), the hold waits for an autosave
  container to close or 3 s, and coming back cancels it - all three seen working, behind
  the test file `files/autosave-on-pause`. No trigger found yet: `ExternalSaveMessage`
  saves nothing; the load/save state machine's state 3, pumped, opens the slot its last
  job left (a missing player slot) and stays. Next: the area load's own sequence.
- **FXAA's and CAS's 30 fps cost, explained (runs 370, 382):** with FXAA, 30-32 of 140
  frames take five refreshes against 1-2 of 150 bilinear - frames tipping past their
  four-refresh slot. The camp's post-processing chain spends about 4.7 ms a frame in the
  copies between its one-draw passes (run 296's trace), the 3.8 target that both FXAA
  and 60 fps would gain from.
- **PLAN 9.13 done: an autosave as the game is put away (runs 387-390).** The trigger
  is the engine's own DoAutoSave (`sub_824BB520`, what some 200 compiled scripts call),
  found by following the Korcari Wilds load's autosave back from the load/save state
  machine's state 3. Called from the update thread on the frame after the pause, it
  saves as the game itself does - the autosave slot written in about 2.5 s, the previous
  one kept in `.backup/` - and the guest holds after it; the autosave loads (run 389).
  Settings -> Autosave when put away, on by default; only while exploring.
- **3.8, looked at again (runs 391-392).** A fresh traced frame at the King's Camp: the
  main scene pass 18.2 ms, the foliage 6.1, a depth-only pass 3.8, the post-processing
  chain 10.1 ms in 49 passes plus 3.9 ms of copies, 18 render-target transfers 3.5 ms.
  Without the transfers the scene is white (they carry it to the post chain): not a
  saving. An exact skip of the multiplication rule's test can prove little (constants,
  interpolants and overflow can all bring an inf). Left, as before: the rule's inexact
  forms (the owner's call) or smaller translated shaders - and the 9.8 switches.
- **More than 512 MB? (the owner asked.)** DAORecomp keeps 512 MB and warns texture-heavy
  mods may run the game out. The game's memory is physical, in fixed heaps - 214 MB,
  2 MB, 18 MB, then all but 6 MB of what is left, 270 MB - and every texture of a traced
  frame lies in the first, fixed one, so reporting more memory would grow only the last.
  More physical memory has no addresses in the 360's layout either. Measured instead (run
  393, `files/heap-scan`): in the Korcari Wilds the textures' heap reaches +127 MB of
  214 MB - some 85 MB of room for a texture pack there - and the last heap 135 MB of
  270 MB written.
- **10.1, the options panel - the owner's ask (2026-10-27), first of a list.** Select +
  Start together during play (or Back -> Settings) open every option over the game, which
  holds (`Runtime::SetHeld`: its threads, clocks and sound, without the autosave). Plain
  Android views, not ImGui: the presenter untouched, touch too, and the views never take
  focus - the activity hands them the pad's keys and motion. Start and Select are held
  back 150 ms, so the combination reaches neither the game's map nor its menu, and alone
  they reach it that much later. Live: the frame rate, the upscaling (FXAA now the
  dithered bilinear variant, so it switches too), the colour (the swapchain made again),
  shadows and post-processing (back on, the engine's own option), the headphone mix, the
  autosave; from the next start, marked: texture filtering, the prompts, the logos.
  Run 394 (the camp, adb's keys): every live change taken, the game held and resumed five
  times. Run 395 (character creation): 60.0 fps at 60, 68.8 at 120, 45.2 at 45 (every
  frame three refreshes), 30.0 at 30. Run 396, presses written into the Thor's own
  controller's input node: Start alone opens the game's menu (0.28 s), Select + Start the
  panel with the game in explore mode throughout, the hat and the D-pad move it.
- **The owner's defaults (2026-10-27):** 60 fps, with 30, 45 and 120 offered - 45 and 120
  DAORecomp's way, each frame flipped at once and the title's render thread held to the
  rate - and the display's cadence left to the owner to judge ("just build the option");
  autosave when put away off; headphones stereo and colour accurate, as they were.
- **9.2, HDR10 - the owner's second ask (run 397).** The Thor's Vulkan driver offers no PQ
  colour space (BUG-025), but the colour space only decides the dataspace Android puts on
  the window's buffers: the HDR10 swapchain is the 10-bit sRGB one, its buffers tagged
  BT.2020 PQ once it is made, and the presenter's last pass draws with an HDR variant -
  Xenia's pass untouched, its result encoded: the highlights expanded toward the panel's
  420 nits, white at the paper white, BT.2020 primaries (Display-P3's with Vivid), PQ.
  SurfaceFlinger lists the layer `dataspace=BT2020_PQ`, composed by the display
  hardware, and the composer's dynamic range turns HDR - and back when it is switched
  off, live from the panel. 30.9 fps at the camp against 31.1.
- **BUG-062:** upstream's presenter never recorded the format its last pass's pipeline was
  made for, so it made the pipeline again at every paint, after waiting for the GPU.
  Fixed: the camp's frames evenly 4 refreshes apart (p95 37.1 -> 33.2 ms, the 1% low
  26.2 -> 29.7 fps).
- **9.4, FSR 3.1 - the owner's third ask, in progress (runs 398-401).** The FidelityFX
  SDK v1.1.4 (MIT) built from source for Android: its compiler `FidelityFX_SC.exe` is a
  downloaded executable and is not run (the auto mode refused it), so
  `tools/build_fsr3_shaders.py` compiles the ten passes in all 64 permutations with the
  NDK's glslc and writes the reflection the host code binds by; two SDK faults worked
  around (BUG-063, the context size with 4-byte `wchar_t`; BUG-064, paths chosen by what
  the GPU has, not what the device enabled). DAORecomp's design on the Vulkan backend
  (`warden_upscale.cc`, graft-hooked): jitter, the scene's depth at its resolve, the
  image before the UI, FSR's dispatch through a new deferred-command callback, the UI
  composited back. Run 400: every 3D frame at the camp upscaled, the HUD sharp, ~50% more
  edge detail than bilinear; run 401: FSR 8.4 ms, composite 0.7 ms, six copies a frame -
  23.1 fps against 31.1. Run 398: the title ignores the video mode's size; its device
  set-up (`sub_82692230`) fixes 1280x720 - where Balanced and Performance's sizes go.
  No motion vectors yet. Resume with PLAN 9.4's list.
- **9.14 added, the owner's:** frame generation - FSR 3.1's (after the motion vectors)
  and lsfg-vk's (v2.0.0-dev, `0213be9`, GPL-3.0: v2.0.0 is CC BY-NC-ND 4.0, no
  derivatives). The user supplies Lossless Scaling's `Lossless.dll`; the owner's copy in
  the repo root is gitignored.
- **9.14 refined (the owner, 2026-10-28): the frame rate is a target on the screen**, for
  both kinds. The game runs at the target divided by the multiplier and generation
  makes up the rest - 60 with Lossless 2x: the game at 30, one frame generated after
  each. FSR 3.1's is 2x only (one interpolated frame); lsfg-vk's takes any whole
  multiplier (the panel: 2x, 3x, 4x). The options panel says the split in plain words
  under the Frame rate and Frame generation rows, and why a choice is dimmed.
- **Next:** PLAN 9.4's list (the 16-bit paths, one capture a frame, the render sizes,
  motion vectors, the option), then 9.14. Then the owner's hand
  playthrough to Ostagar; the owner's calls listed in the header: BUG-058's blocking
  waits; Headphones' default once heard; Vivid colour, a taste.

### 2026-10-27 (session 34) — the save thumbnails and the HUD portraits: a placeholder pixel shader

- **BUG-049, runs 309-312.** The scene copy texture's history - an event ring of each
  load and watch firing of the textures at `00DC3000`, printed at the thumbnail draw -
  showed the texture the quad samples reloaded 4 ms before it: the texture cache was not
  at fault. Barrier experiments (runs 311-312) showed the real pattern instead: the
  second save of a run came out whole without help (the experiment fired a draw late),
  the first never (runs 306, 311, 312). Run 308 had been stopped 27 s in by run 307's
  capture, still running - one capture at a time (run 295 was the same).
- **The cause, run 313: Xenia's pipeline hot-swap.** With `async_shader_compilation`
  (upstream's default) a new pipeline is drawn with a placeholder pixel shader while the
  real one compiles in the background; the thumbnail's quad, a pipeline used once, drew
  the placeholder, and by the second save the real one was ready. Pipelines created when
  first drawn with: the first save's thumbnail whole (449,733 non-zero bytes) - and the
  HUD's portraits with their faces at the load: **BUG-052 had the same cause** (the
  bone-pose reading of runs 257-275 was wrong). **Fixed:** synchronous pipeline creation
  by default (`warden_gpu.cc`; `files/hack-async-pipelines` brings the hot-swap back).
- **The cost, run 314 (the King's Camp, eleven minutes):** 29.9 fps and 6.4% off the
  cadence as in run 301, no fault; the camp's first load 4.4 s instead of 1.7, and two
  hitches in its first seconds (0.48 s, 0.57 s) while new pipelines compiled, none after.
  Each pipeline costs once a session; Xenia's shader and pipeline storage, never set up
  in this runtime, would create those seen before at startup (PLAN 10.3).
- The diagnostics stay behind their markers; the barrier experiment, which fired a draw
  late, is gone (161 recorded edits). Test saves "Save (13)" to "Save (19)".
- **The shader and pipeline storage (10.3), runs 315-318.** Xenia's storage set up in
  the files directory (`files/pipeline-storage-v1/`), and three faults in its path to the
  disk fixed (BUG-055): an `a+` stream starts at the end on Android, so the storage was
  emptied at every start; nothing asked the writer to flush; and the driver's pipeline
  cache was only saved at a shutdown this app never has. Run 317 stored 106 pipelines
  and 2.3 MB of the driver's cache from an empty start; run 318 loaded them and created
  the pipelines in the background - the King's Camp's load no longer stalls (its worst
  frame 112 ms against 924 and 3,474 ms in run 314), startup 0.4 s slower. Two hitches
  after arriving (0.23 s, 0.45 s) remain, as with the background compilation of run 301
  (0.31 s): not pipelines. 166 recorded edits.
- **Next:** the owner's checks - a person hearing the voice-over (6.4) and the hand
  playthrough, with the portraits and thumbnails fixed.

### 2026-10-26 (session 33) — the portraits drawn before the face is posed; the GPU's cost, 20.6 -> 30 fps (the game's cap); the baseline

- **BUG-052 located** (runs 228-239). New instruments in `tools/assemble_runtime.py`
  (recorded edits extended, still 101): a `hack-resolve-dump` holding a destination
  pitch dumps those resolves in any frame and logs the 64 draws before each, with six
  textures and the shader constants (`tools/recent_draws.py` decodes them). The
  portraits' dumps hold the armour but no head or hair; every part is drawn, with the
  same textures and pixel shader constants as a working render. A portrait the game
  redraws in a fight has its face, and the difference is in the vertex constants: about
  43 of the head's 256 are zero at the load and set in the fight - the face's bones,
  some fourteen 3x4 matrices, not yet posed when the load draws the portraits. Why the
  order differs from the console's is not known. Also tried on the way: a frame trace of
  the load (a trace armed by the mode change cannot reach the frame it changes in, where
  the portraits are drawn), and the head's textures (populated). No fault in any run.
- **The GPU's cost (3.8), runs 240-255.** New instruments (recorded edits now 134):
  timestamp queries around each render pass of a traced frame, written to
  `files/gpu-timing.txt`; `hack-split-pass` cuts passes into timed pieces; a ranged
  `hack-tiny-scissor`. The King's Camp frame: about 70 ms of GPU time, 47 of it the main
  scene pass, one terrain draw 7 ms; per-draw setup about 4 µs (small); per-pixel
  shading the cost, and a third of the main pass the texture-fetch emulation - the
  unused per-component sign handling (about 12 ms; every texture in the frame is
  unsigned), explicit-gradient sampling (5 ms), texel snapping (3 ms), separated by
  experiments run together where they did not overlap. **The fix:** two shader
  modification bits from the fetch constants (all textures unsigned; equal per-axis
  gradient adjustments) select a translation without the sign handling and with
  implicit LOD for 2D fetches - exact for those draws, the general translation for the
  rest. The traced frame 70.6 -> 57.1 ms, **the camp 20.6 -> 24.9 fps**, the picture
  unchanged. On the way: implicit LOD for 3D or stacked textures crashed the
  translator (their path reads the gradients regardless; run 244) - 2D only. **Then
  (runs 249-251)** each texture size's reciprocal once per fetch, and a third
  modification bit for shaders whose textures need no more of the post-fetch conversion
  than its 16-bit rounding (a select without branches) - both exact: **24.9 -> 25.7
  fps**. Skipping the rounding too (an inexact experiment) gives 26.2; it is Xenos
  behaviour and stays. The rest of run 251's traced frame: 20 render-target transfer
  passes with no guest draws, 4.1 ms; 6.9 ms between passes; and every pass's render
  area the whole EDRAM-sized framebuffer, 1280x2048. **Then (runs 252-255):** narrowing
  the render areas changed nothing (the driver does not pay for the untouched part;
  reverted). The cost was elsewhere: the translator keeps Xenos's `0 * anything = +0`
  (even for inf and NaN) by testing `NMin(|a|, |b|) == 0` at every multiplication, and
  the Adreno compiler expands NMin's NaN handling - about a third of the frame. IEEE
  multiplication (an inexact experiment) gave 30.0 fps, the game's cap; testing `(|a| ==
  0) || (|b| == 0)` instead - exact - also gives **30.0 fps** (run 255), with the GPU 98%
  busy at 680 MHz: no headroom left at the camp. Two forms were measured in one run by
  giving each to the shaders of one ucode-hash parity.
- **Combat at the cap (run 256):** the `morrigan` route's fight in the Korcari Wilds
  ran at 29.9 fps for 63 s, its traced frame about three quarters of the camp's GPU
  time. The exact savings left are small (the resolve without the dump, about 1 ms);
  the larger ones - the FMin form of the rule, IEEE multiplication - trade exactness and
  are the owner's call (PLAN 3.8).
- **BUG-052, runs 257-258:** the portraits' zero vertex constants by index - the
  face's part of the portrait's bone palette is c186-c204 and c209-c232, zero at the
  load and set in the fight's working redraw (c18-c29, zero in both, are not the face).
  No GPU constant load fills them: the CPU writes them into the command buffer, from an
  array that holds zeros at the load. A save load (run 258) shows the same bare discs,
  so it is not the debug jump; `face.erf` was read two seconds before the render.
- **BUG-052, run 259:** a search of every resident guest page at the fight's working
  redraw found the face's matrices only in GPU memory - the command buffer's constant
  data, four copies - and nowhere in the title's heap: the palette is computed into the
  command buffer as the portrait is recorded, so a CPU-side write watch has nothing to
  watch. The fourth portrait's redraw came 60 ms after an ability's impact: a status
  change re-renders a portrait. New instrument: `warden_memory.cc` searches resident
  guest memory for a value and write-watches the pages holding it (page protection; the
  writer's host pc, guest lr and return addresses), used here from the portrait's head
  draw.
- **BUG-052, runs 260-267 - the cause narrowed to the skeleton.** Wrapping the
  recompiled Direct3D functions in our own code (`PPC_FUNC` overrides - no recompile)
  found the code: at a load the update thread draws the portraits from the GUI's
  per-frame 3D render, the head through `sub_82D9DB20` from the mesh draw
  `sub_82698558`. Every Direct3D call in that path logged (run 266) shows the skinning
  palette uploaded as c33 + 103 vectors - 34 bones - while the head's vertices reach
  bones up to about 66 (c232): the face's bones are not yet in the character's skeleton
  palette when the load renders the portraits. Not a race (runs 263-265: the render
  thread shares the device during loads, but the zeros never vary, and holding it only
  stretches the load). On the way: samples of every guest thread when the GPU reaches a
  portrait (run 260), a scan of each kick's indirect buffers for the head's draw (run
  261), the trace ring at 32,768 entries.
- **BUG-052, runs 268-275:** the palette records hold up to 154 vectors (c32-c185) in
  every phase, and no upload through the vertex-constant setter or the GPU's constant
  loads ever fills the face's slots c186-c232 - yet the fight's working redraw has them.
  The loads' portraits are the update thread's; the working redraw comes from the render
  thread's side (portrait kicks: `Eclipse::Update` at the loads, `DragonAge::Render`
  mid-way through its mesh draws in the fight). Both threads share the Direct3D device.
- **BUG-052 parked** - the owner's decision after run 275, with the plan to go on: the
  next steps are in `docs/BUGS.md` and `RESUMING.md` §5.4.
- **The performance baseline (6.7, runs 277-278).** New: `warden_frame_times.cc` - each
  present (a recorded edit after `vkQueuePresentKHR`; edits now 136) and each of the
  title's swaps, summarised every 5 s as a `frames:` line (mean, percentiles, 1% low, a
  histogram in 120 Hz refreshes or 60 Hz vblanks); `tools/sample_power.sh` (GPU load and
  clock, temperatures, battery and USB power every 10 s) and `tools/frame_report.py`.
  Eleven minutes at the King's Camp, twice, agreeing to a tenth of a percent: 29.9 fps;
  the presents 4 refreshes apart for 88.8% of frames and 3 or 5 for 11% - judder at an
  even rate; 1% low 22 fps; the title's swaps 1, 2 or 3 vblanks apart (11/78/9%). The
  GPU at its top clock, 680 MHz, 96-98% busy throughout at 79-83 C - no throttling; the
  big CPU cores 84-93 C. Power, plugged into the computer's 3.9 W USB port with the
  battery still discharging: about 9-10 W, 2.2-2.7 hours from a full battery at minimum
  brightness (the battery's current and its charge counter disagree by a fifth; an
  unplugged session would settle it). The `display:` stream is when the presenter
  queued each frame, not when the panel showed it - that needs the presentation
  engine's own times. No fault in either run.
- **Frame pacing (4.7), runs 279-283.** `VK_GOOGLE_display_timing` enabled (recorded
  edits in `vulkan_device.cc/.h`; with the presenter's hooks, edits now 139) and
  `warden_frame_pacing.cc`: each present carries an id and, paced, a time before which
  the compositor must not show it; `vkGetPastPresentationTimingGOOGLE` reads back when
  each frame reached the panel, into a `pacing:` line every 5 s per mode;
  `files/frame-pacing` = `off` / `paced` / `alternate` (a minute each). **The panel
  refreshes every 8.445 ms - 118.4 Hz** (SurfaceFlinger's vsync model says so too;
  Choreographer and the display mode say 120): 30 fps cannot sit on every fourth refresh,
  and 4.2% of frames at three is the floor (`docs/BUGS.md`, constraints). Unpaced, on the
  panel: 11-12% off the four-refresh cadence; the title's swap reaches the panel 77 ms
  later; the presenter never waits for a swapchain image. Paced - each frame held for the
  refresh nearest its place on a timeline at the title's rate, the offset regulated from
  late and early frames: no gain at the nominal 30 fps (run 280: the title makes 29.9 and
  the timeline ran ahead), 9.3% at the measured rate (281), 7.6% retuned (282), 7.4% with
  a drift correction, paced throughout (283) - at 16 ms more latency, with a third of the
  frames held three refreshes. Off by default until it is cheaper. Most of what is left
  is the title's own: 9% of its frames take three vblanks with the GPU 97% busy. Fixed on
  the way: the pacer took its mode from the first device to present, the bring-up
  provider's, before the marker was read; it now starts again for each device. Power,
  run 279: the battery's current and its charge counter agree this time - about 10.3 W,
  2.2 hours. No fault in runs 279-283.
- **Frame pacing done (4.7), runs 284-285.** The offset now tracks a late-frame rate in
  small steps (half a refresh later for a late frame, a 200th of that earlier for each
  on time, a fast pull-in for frames held two refreshes, the period's correction only
  once the offset has found its edge): run 285, a minute of each - paced **6.8%** against
  11.1% off the four-refresh cadence, at 7 ms more from the title's swap to the panel
  (85 against 78). **Pacing is on by default** (`files/frame-pacing` holding `off`
  turns it off).
- **The app's end (4.9), runs 286-291.** `warden_lifecycle.cc`: the title's own end
  (`XamLoaderTerminateTitle`, `XamLoaderLaunchTitle`, `HalReturnToFirmware`, now
  implemented), the runtime's quit and Back (which asks first) are recorded natively and
  polled by Java, which finishes the task and ends the process - the guest cannot be
  stopped from outside, and the runtime cannot start twice in one process. A lost host
  GPU shows a message instead of aborting (a recorded edit in `graphics_system.cc`; 141
  edits now): run 286, `files/hack-gpu-loss` reporting a loss at 60 s. Back -> Quit (run
  289) and a clean launch after it (run 290); `uiautomator` cannot read the game's
  window, so the tests tap by position from screenshots.
- **BUG-053 fixed:** the system's Back never reached the activity - an empty pad slot's
  -1 is also the virtual keyboard's device id, so Back (and every key `adb shell input`
  sends) went to the guest as a press on a pad that is not there. There since 4.6.
- **The Thor on its own charger, adb over Wi-Fi.** The computer's USB port gives it 3.9 W
  against the game's 10 W: the eleven-minute runs took the battery from 74% to 28%. Now
  on a 25 W charger, with `adb connect <the Thor's address>:5555` (`RESUMING.md` §3.1).
  `boot_capture.sh`'s summary counts faults (guest faults and native crashes), no longer
  every fatal-level line - the mode-request probe logs twelve call chains at that level
  in every boot. The route is off the Thor again. No fault in runs 284-291.
- **Character creation checked screen by screen (6.1, run 292).** Driven live, a press
  at a time through `inject-pad` (the left stick moves the appearance sliders), read
  from `adb exec-out screencap`: male, female, human, elf and dwarf models; the name
  dialog; Appearance and Voice - a preset, the skin tone, hair and eye colour tints and
  an eye-size slider, each in the close-up and in the screen's portrait; attributes,
  skills, talents, difficulty; then the Dwarf Commoner origin's Dust Town (`orz201d`,
  new) into its opening conversation with the customised character, 30 fps. No fault.
- **BUG-049, runs 293-294:** the save thumbnail is the GPU's own render at the save -
  a quad sampling a 1280x720 copy of the scene into a 448x256 target, resolved to
  `01E13000` and written from there - and the GPU's result is already empty but for its
  rightmost 32-pixel column: not the readback, not memory export (a new
  `hack-memexport-readback`: no change), not the one-tile scene (`scene-msaa`: no
  change). Next for it: the quad's coverage and the scene copy (`docs/BUGS.md`). Two test
  saves on the Thor ("Save (5)", "Save (6)"). The device is clean again: no route, no
  marker.
- **The resolve straight from the render target (3.8), runs 295-298.** DAORecomp's
  `dao_resolve_direct` wired into the Vulkan backend (eight recorded edits): a plain 32bpp
  copy of one single-sampled k_8_8_8_8 render target owning the whole area is done from it
  directly, skipping its dump to the EDRAM buffer - 71% of the King's Camp's resolves. Run
  296: 29.9 fps, the GPU 91-93% busy (98% before), the traced frame 42.1 ms in 68 passes
  (run 255: 121-130 passes), the title's own frames even (1.5% off two vblanks against
  21%). The picture is unchanged (run 298's captures against run 248's). Run 295 was
  force-stopped from outside the app.
- **Frame pacing's three faults (BUG-054), runs 296-301.** With the title's frames even,
  the panel showed more judder, not less: 9.9% off the cadence (run 296). Run 297's new
  counters showed every frame reaching the panel a refresh before its slot - so a frame a
  refresh late had never been counted - and the period's correction winding up (+0.1 ms a
  frame; 19 timelines in eleven minutes). Run 298, aiming at the slot itself (four
  desired-time phases, `files/frame-pacing` holding `phases`): no frame early, 6.2-6.9%
  off in all four, where 4.1% is the floor. Run 299 (the default, a quarter of a refresh
  after the slot): 6.0%, but 72 ms from present to panel - each of the catch-up frames
  after a stall had moved the timeline a period ahead, the offset pinned at its floor.
  Run 300 tried a deeper floor and a capped correction: 5.5%, but the correction wound up
  again (+0.13 ms a frame). The fix keeps run 298's correction and starts a new timeline
  once the old one is a period ahead. Run 301, eleven minutes: 6.4% off the cadence, 38 ms from present to panel (p50), no frame early, the period's correction within 0.025 ms a frame either way, two new timelines once at the camp.
- **A crash in the ring poller (BUG-047), run 297.** A minute in, on a load, the command
  processor met a malformed packet in the primary ring in the same millisecond as one of
  the poller's "unhooked" kicks. Those were nearly all the poller reading the window's
  word between the hooked store and its hook, kicking ahead of the hook's release - on
  ARM64, with XenonRecomp's barriers no-ops, possibly into packets not yet visible; three
  went backwards. Now only a value still unhooked a poll later is kicked: none in runs
  298-301 (256 in run 296). A failed packet now logs its surroundings and the last kicks
  before the trap (three recorded edits; 152 now).
- **BUG-049, runs 302-308: a stale texture.** New logging around the dumped resolve
  (each draw's viewport, scissor, window offset, stream 0 and fetch constant; the
  resolve's source rectangle; the scene copy dumped beside the thumbnail; the texture
  bindings of one pixel shader) shows the thumbnail's quad and resolve right and the
  scene copy at `00DC3000` full at the save - but bound from the texture cache as up to
  date while stale. Marked written just before the draw, it reloads and the thumbnail
  comes out whole (run 306: 449,734 non-zero bytes, the party in the Korcari Wilds). Not
  the mips (run 305). Open: why a resolve's write left it up to date; run 308's
  timeline logging never reached the save. Eight recorded edits (160 now), all
  diagnostics behind markers. Test saves "Save (7)" to "Save (12)"; run 303 crashed in
  the new dump code mid-save (fixed; its empty save package removed). The device is
  clean.
- **Next:** BUG-049 - the scene copy texture's load and watch timeline through a save
  (`docs/BUGS.md`).


### 2026-10-25 (session 32) — back on the original workstation; saving and loading work

- **Set up from `RESUMING.md`** on the original workstation (36 commits pulled). The
  shared signing key replaced this machine's own (kept as
  `~/.android/debug.keystore.pre-dao-2026-10-18`). The bootstrap's step 6 accepted the
  stale 28-edit runtime tree - `--check` proves the edits apply to upstream, not that
  the tree matches - so it was re-assembled by hand first (`RESUMING.md` §3.4); the APK
  then built in 6 minutes. Run 201: the title screen, no fault.
- **Loading works (6.5).** Run 202: the injected A meant for the storage prompt landed on
  the main menu's new first item, **Resume**, which loaded the run-199 save - into the
  `arena`, because that save was made at the default start area, not the King's Camp as
  the docs had it. A manual save from the pause menu followed. Run 203: Load Game from
  the main menu, a save over Save (2) (the old one moved to `.backup/`), and loads of
  the overwritten save and the autosave from the pause menu. No fault in either run;
  the menus were driven one press at a time from the 5-second captures.
- **BUG-048 fixed:** the save path reached two stubs. `RtlTimeToTimeFields` (void) left
  the time fields unwritten, and the title wrote the stack's leftovers into every save's
  display name; `RtlTimeFieldsToTime` refused every date, so the lists could not sort by
  time. Implemented, checked on the host against Python's `datetime` for 20,011 values;
  a save written since is `Jaden_Dragon Age_20261025191739` and sorts first.
- **BUG-049 open:** every `scrnshot.bin` is 458,752 zero bytes, so no save has a
  thumbnail - most likely a GPU resolve never copied back to guest memory
  (`readback_resolve` is `none`). One save under `hack-resolve-readback` will say.
- `tools/routes/load` (START, then A on Resume). Corrected on the way: the counts in
  `docs/BUGS.md`'s index (and BUG-047's place in it) and `docs/ASSETS.md`'s "still to do"
  section on the title update's overlay files, which the app has written since 5.0f.
- **Runs 204-206.** BUG-049's experiment: one save under resolve readback, and its
  thumbnail went from all zeros to 0.71% non-zero - the rightmost column of tiles only, so
  readback is part of the cause, not all of it; memory export is the next suspect. Then a
  save at Ostagar, which did not happen: with saves on the device the `ostagar` route's
  A at 56 s hits Resume (`route.sh` now turns it into a down for routes with a
  `.new-game` file), and at the King's Camp the title's forced tutorials hold the pad -
  BACK, A, then LT, which `inject-pad` cannot press. No fault in any of the six runs.
- **The sound output (4.5, runs 207-208).** The frames the title submits are mixed down to
  stereo as Xenia does and played through AAudio; while the stream consumes, its queue
  holds the render worker back, so the title's audio clock follows the device's, and the
  steady clock takes over when it does not (the stream pauses with the app). The Thor
  gives a shared stream with 20 ms bursts, so the queue is two bursts deep: no silence for
  want of a frame. The movies' sound plays; music and voice are XMA, still silent.
- **BUG-050 fixed on the way:** `KeWaitForMultipleObjects` read its timeout from the
  stack at r1 + 0x54 instead of r9 - the seventh of eight arguments, all in registers.
  In the XAudio callback that slot holds an event's address, so its infinite wait for
  the mixer polled and 11% of callbacks submitted nothing; since the fix every one does.
- **The XMA decoder (4.5, run 209): music.** DAORecomp's real decoder - a port of Xenia's
  `XmaContextNew` over FFmpeg's `xmaframes` - compiled unmodified from DAORecomp's tree:
  four small headers in `app/src/main/cpp/dao_compat/` stand in for the parts of
  DAORecomp's runtime it includes (its precompiled header, log, guest memory, GPU host),
  and `warden_xma_real.cc` is the plain interface `warden_xma.cc` calls, one decoder per
  context. FFmpeg (xenia-canary's submodule, has207's fork, now fetched by the bootstrap)
  is built from DAORecomp's `xenia/ffmpeg.cmake`, read at configure time with its two
  target switches flipped from x86-64 to AArch64 (asserted), with the NEON assembly - and
  position-independent with hidden symbols, or its assembly's `adrp` to the C tables
  cannot link into a shared library. The title screen's music plays: the output's peak
  went from 0 to 0.02-0.07 and the silent frames stopped. No fault.
- **`inject-pad` presses the triggers and sticks** (run 210): a line may go on with
  `<lt> <rt> [<lx> <ly> [<rx> <ry>]]`. At the King's Camp an injected LT opened the
  radial menu, which the "Mapping Abilities" tutorial wanted - and the tutorial went on
  to "Open the Spells or Talents menu", which B, BACK, RT and the left stick with A did
  not satisfy. The chain so far is in `docs/TESTING.md`.
- **The tutorials switch** (runs 211-213): Options -> Game -> Interface -> Show tutorials,
  the page's last item. **Switched off in run 214** with the owner's permission - it is
  their profile setting, so it holds for hand play too. Leaving Options wrote the
  profile, and only `63E83FFD.bin` changed; copies from before and after are in
  `build/profile-before-tutorials-off.tar` and `build/profile-after-tutorials-off.tar`.
- **A save at Ostagar, loaded back - 6.5 done** (runs 215-216). The `ostagar` route with
  the tutorials off reached the King's Camp, START opened the pause menu at once, and
  Save Game -> New Save made "Save (4)" there; `route.sh load` then loaded it back into
  the King's Camp through Resume. No fault in either; 295 s of sound, 0.1 s of it
  silence for want of a frame (the area load).
- **BUG-051 fixed: entering the Korcari Wilds crashed** (runs 217-218). A new route,
  `wilds` ("Start in the Korcari Wilds" from the prelude debug menu), loaded the area,
  and eight seconds later the title called host address 0: `sub_82D7D148` ends in a
  jump table missing from DAORecomp's config - its switch finder needs a `cmplwi`
  bound, and this index is bounded by float comparisons - so XenonRecomp cut the
  function at the `bctr` and two of its cases had no code. `tools/recompile.py` now adds
  the function's size and its table to the config copy it hands XenonRecomp
  (`ADDITIONS`, asserted), and the new `tools/find_missed_switches.py` found no other
  table of that shape (and does find this one in the old code). Run 218: the Wilds,
  explore mode, 27.7 fps, no fault in over two minutes. The debug menu does not come
  back after this jump, so the route ends with the area pick.
- **The first fight** (runs 219-220, 6.3). Run 219, driven live: six seconds of the
  left stick forward from the Wilds' start met wolves, A attacked the targeted one, and
  the party's AI won in 53 s - talents (Alistair's Shield Bash), damage numbers, blood
  on faces afterwards, loot - at 23.6 fps, no fault. The `wilds` route now does the same
  with no hands (the walk at 114 s, A at 122, 126 and 130 s): run 220's fight lasted 52 s
  at 26.6 fps. Further up the path is hard to find blind (run 220 walked into a cliff).
- **The voice-over, as far as the output can tell** (run 219, 6.4): a leftover A started a
  conversation with Jory, which read his voice bank (`pre100_jory.fsb`) and the lip
  sync, and while he spoke the output peaked at 0.36-0.43, against 0.02-0.09 between his
  lines. Nobody has listened yet. Ten minutes, no fault, 0.1 s of the output silence for
  want of a frame.
- **The owner's calls:** the two-hour soak (6.6) is dropped from the plan, and the
  party portraits do not render correctly - BUG-052: each shows its coloured disc but no
  face (one face appeared during the first fight); the faces are not in the GUI
  archive, so they are most likely rendered at run time.
- **Combat done (6.3, runs 221-223).** The debug menu's other pages: quest shortcuts,
  and under (MORE) the Tower of Ishal's third level and the end of the Prelude with
  Morrigan. In the tower (run 221) the player, alone and unarmoured, fell to genlocks in
  seven seconds; the death screen and Load Last Save worked - and after that reload the
  player's portrait showed his face (BUG-052: so the faces can render). The end of the
  Prelude puts Morrigan in the party at the Wilds' start: her Winter's Grasp, a spell
  cast by hand with X, the level-up light (runs 222-223); the new `morrigan` route does
  it with no hands - explore at 119 s, the fight from 130 s, won in 66 s at 18.6 fps
  (run 223). No fault in any of the three.
- **Streaming and an exit on foot - 6.2 done** (runs 220, 224). Walking in the Wilds
  streams the environment in grid cells (a new `ost100d_5_6_0` chunk mid-walk in run
  220, the frame rate steady at 26-30 fps around it); walking back from the Wilds' start
  reaches "Gather your party and venture forth?", and Yes loads the King's Camp in 3.6 s
  (run 224). No fault in either. That completes the tests at Ostagar.
- **BUG-052, the portraits, narrowed** (runs 225-227). Not a missing readback: with
  every resolve copied back (run 225) they stayed bare discs. New instruments in
  `tools/assemble_runtime.py` (three recorded edits extended, still 101): the texture key
  on each traced binding, and the first 256 distinct resolve destinations and memory
  export ranges of a boot (`docs/TESTING.md`). Run 227: each face is a 146x146 texture
  the GPU resolves at an area load, and the HUD's fourth portrait samples the one the
  Wilds' load wrote (`04013000`) - yet shows its gold disc. No memory export in the
  whole boot. No fault in any of the three runs.
- **Left on this workstation:** the runtime tree assembled with those instruments and the
  shaders rebuilt; the APK on the Thor built from them (run 227). The Thor has no route
  and no trace or capture markers (`route.sh off`), so it plays by hand; the tutorials
  stay off, and Save (4) at the King's Camp is the newest manual save.
- **Next:** the portraits - dump their resolves (`RESUMING.md` §5.4); then the shaders'
  GPU cost (3.8); someone listening to a conversation (6.4). Another
  machine needs `python tools/recompile.py tu` after pulling.

### 2026-10-25 (session 31) — main menu to Ostagar; the keyboard, saves, and a bootstrap

A long session, runs 96-199. In order:

- **BUG-043 fixed, and it was ours:** the physical allocator handed out only the top
  half of the console's 512 MB, the title sizes its last heap from what
  `MmQueryStatistics` says is left, so that heap was 15 MB and FMOD ran dry at the title
  screen. The whole range (`kPhysicalBase` 1 MB, `warden_guest_heap.cc`): a 270 MB heap.
- **The main menu (run 96).** START shows the title's own storage prompt; the system UI,
  storage and achievement calls are answered as DAORecomp does (`warden_xam.cc`), and
  `files/inject-pad` presses buttons from a file for unattended runs. A on "New Game"
  then loaded the `arena` test area: a write-watch race killed it once (BUG-044, fixed);
  then the HUD over a black 3D scene (BUG-045).
- **BUG-046, found by the owner:** XenonRecomp's XEX patcher moves blocks with an
  overlapping `memcpy`, which x64 forgives and ARM64 does not, so 54 KB of read-only
  data were smeared and "New Game" skipped the intro and character creation. Fixed with
  `memmove` (`tools/gen_xex_patcher.py`); BUG-040's colour cast was the same bug.
  `warden_game.cc` hooks the title's own code for its mode, log, scripts, screens,
  cutscenes and character generation.
- **BUG-045 fixed:** on the Adreno 740 a fragment shader declaring the
  `DenormFlushToZero` execution mode reads `gl_FragCoord` as zero, and the title
  redraws each resolved frame with a shader that fetches at its own position. The
  translator no longer declares the mode on Qualcomm (`spirv_shader_translator.cc`,
  recorded in `tools/assemble_runtime.py`). Character creation renders (run 167). The
  hunt left instruments (`docs/TESTING.md`): a vertex-position probe, draw skipping, a
  dump of every resolve (`tools/resolve_to_png.py`), float-control bisection.
- **Run 168, by hand:** character creation through the Mage origin's intro, Greagoir's
  conversation, the Fade in explore mode - five minutes, no fault.
- **Ostagar without hands (6.0, run 175).** The 360 build compiles its scripts natively
  and registers them by name; the runtime calls BioWare's `zz_pre_debug` through that
  registry and scripted presses jump to the King's Camp (`bash tools/route.sh ostagar`).
  8 fps there at first.
- **8 -> 20 fps (6.7):** the runtime and app code at -O2 in the debug APK (13.5 fps),
  then one scene tile without MSAA by default, as DAORecomp's upscaler path (20 fps;
  `files/scene-msaa` for the console's). GPU-bound at 99% and 680 MHz; the rest is the
  translated shaders (3.8). BUG-047 (a malformed indirect packet, twice) is WATCH.
- **The name keyboard (6.1, run 193):** `XamShowKeyboardUI` returns pending and the app
  shows an Android text dialog with the title's prompt and default name.
- **Saves (5.3, run 199):** content roots in `warden_fs.cc` (creating, writing,
  truncating, renaming, deleting) and `XamContentCreateEx` / `XamContentClose` /
  the enumerator in `warden_xam.cc`. The route's autosave at the King's Camp wrote
  six files to `files/content/00000001/0xd5332a7_1_u!0/`. Loading is untested.
  *Corrected in session 32:* that save is "Save (1)", a regular save made at the default
  start area, not an autosave at the King's Camp (run 202).
- **Wrap-up for a move to another workstation:** `tools/bootstrap.py` rebuilds
  everything git does not hold from the disc image and the TU7 package, run end to end
  on a fresh clone; the Gradle wrapper checks its download; `RESUMING.md` rewritten;
  the docs brought up to date. BUG-036 closed.

### 2026-10-24 (session 30) — true colours, a signed-in user, and FMOD's stall

- The title screen renders in true colours with a signed-in user (`warden_xam.cc`: user
  0 signed in locally, the music player's messages answered, a notification listener
  that never fires). The yellow-green frame of session 29 was the EA logo phase drawn
  with the key art bound as its luma plane (BUG-040; later found to be BUG-046).
- About a second into the title screen every run stalled: the Update thread blocked in
  FMOD's non-blocking loader, whose 8192-slot request pool was never allocated because
  a 360 KB FMOD allocation failed (BUG-043, fixed in session 31).
- Diagnostics added: a thread sampler (`files/sample-threads`), the signals-and-waits
  report, the memory probe on virtual addresses with a word dump, XMA call counters,
  `files/trace-swap` (`docs/TESTING.md`).

### 2026-10-24 (session 29) — the audio clock, the XMA pool, and where the black is

- **BUG-039 narrowed.** The movies load (whole-file reads of 3.5-23.5 MB; the read log
  had hidden them). The audio clock: `XAudioRegisterRenderDriverClient` and a render
  worker (`warden_audio.cc`, 256 samples at 48 kHz, DAORecomp's design) - after which
  the title unregistered its client 43 ms later because `XMACreateContext` was a stub,
  so the XMA decoder contexts followed (`warden_xma.cc`: DAORecomp's pool of 320 in
  physical memory, the 16 exports, a worker that consumes packets and writes silence).
  FMOD now keeps its client and submits real audio (peak 0.49), discarded until AAudio
  (PLAN 4.5).
- **Then the renderer.** The Bink planes hold data (`files/probe.txt` memory probes),
  the traced draw's pixel shader is Bink's YUV->RGB with the right constants (microcode
  and constants now in the `FT` trace), and the front buffer read back from the GPU
  (resolve readback, on for the test) is opaque black: the quad rasterises, the
  texture fetches return zero. A pre-movie frame sampling a DXT1 texture is black the
  same way. The movie is not the bug; the suspect is the texture path - Xenia's shared
  memory and texture cache over our memory, or the load shaders.
- **Also:** `NtCreateMutant` / `NtReleaseMutant` (a recursive mutant object),
  `NtFreeVirtualMemory` (accepted, kept), `RtlFillMemoryUlong`,
  `VdInitializeScalerCommandBuffer`, `VdPersistDisplay`, `VdSetDisplayMode`,
  `VdGetCurrentDisplayInformation`; a `skip_intro_logos` marker file hides the logo
  movies as DAORecomp's option does; faults on threads with no guest context
  (Android's runtime, which faults on purpose) are no longer reported as fatal.
- **Later the same session: the shader build was wrong for this driver.** All 121 of
  Xenia's precompiled compute shaders carried SPIR-V 1.6 `LocalSizeId` workgroup sizes
  (the build targeted Vulkan 1.3); the Adreno 740 driver ignores them, so the resolve
  copied one row per workgroup row (the 1280x90 band) and texture loads were partial.
  Retargeted to Vulkan 1.1 (`tools/build_xenia_shaders.py`); the resolve now covers
  the frame. The frame is still black: with forced-red pixel shaders, forced
  full-screen vertex shaders, a direct readback of the colour render target image
  (new instrument), and every host-side state of the draw logged and found ordinary,
  the quad draw leaves the image untouched on this driver. Next: the Khronos
  validation layer on the device. Instruments added to the `FT` trace: texture binds
  and loads, uploads, vertex buffers, viewport and scissor, pipeline description,
  constants, system constants, render pass and framebuffer, transfers, the dump, the
  resolve rectangle; `files/hack-rt-readback` and `files/hack-no-sparse` markers;
  `dump_shaders` and resolve readback behind commented lines in `warden_gpu.cc`.
- **First light.** The validation layer (installed by hand into the debug APK, enabled
  through the GPU debug layer settings, with a `files/hack-vk-validation` marker making
  Xenia request it and log its messages) reported nothing wrong. Reading the translated
  pixel shader again: it declares `gl_SampleMask` for alpha to coverage and never
  writes it when early fragment tests are on - undefined coverage, which this driver
  takes as none. Writing full coverage at the start of every fragment shader lit the
  screen: the main menu's key art, 921,600 of 921,600 pixels, with a yellow-green cast
  (BUG-040). One crash in the thread report 40 s in (BUG-041).

### 2026-10-24 (session 28) — the title runs at 60 Hz; the frames are black

- **BUG-038 fixed, three causes deep.** The null call was `KeDebugMonitorData`'s
  import record still holding its ordinal word: the loader now reads the XEX import
  table and patches all twelve variable records with homes that hold Xenia's values
  (`warden_kernel_variables.cc`, PLAN 5.1d done). The present storm was
  `VdIsHSIOTrainingSucceeded` answering 0 - "the GPU link failed" - so Direct3D stopped
  kicking the ring after the first frame. Then a `SEGV_ACCERR`: the fault handler
  declined write-watch faults whenever the GPU thread held the memory lock; it now waits
  for other threads and refuses only its own.
- **The result:** 60 frames a second, ~52 draws each, one resolve into the front buffer,
  presented through Xenia's Vulkan presenter, no crash in 40 s, and the title keeps
  loading - the single-player module 30 s in.
- **And black frames (BUG-039).** Instruments built to say why: the presenter's guest
  output captured to PNG (0 pixels lit), DAORecomp's per-swap draw counts and its
  one-frame `FT` trace wired into the Vulkan backend (three asserted runtime edits), a
  resolve-region check, paint-path counters, the presenter's paint mode. The traced
  frame is a Bink movie's luma plane drawn full screen.
- `DbgPrint` prints; the Java overlay hides once the title boots; the generated hooks
  header is written only when it changes, so adding a source no longer recompiles the
  208 guest objects.
- **Next:** are the movies decoded, and does the title need the XAudio render callback
  to advance them.

### 2026-10-24 (session 27) — who mounts the packages, and the 10 ms in every log line

- **BUG-034, worked from the logs rather than waited for.** Every file log line now
  names its guest thread (`[Eclipse::Update]`), because logcat's tid is the logger's
  thread, never the caller's. Four boots agree: the package mount is one synchronous
  pass on the Update thread, right after it hands the Render thread the splash frame -
  the audio directories, then `packages\core\data` listed and every archive opened in
  order, then the PC-path probes, then the FileIO handoff. Nothing is posted elsewhere.
- **The timing lever was ours.** `xe::FlushLog()` sleeps 10 ms, and every logged guest
  call ended in one - so the listing took 2.3 s and a thread's pace was set by how much
  it logged. The per-line flushes are gone; the fault handler drains the logger once
  before its report instead. The mount now takes 70 ms. Three boots since: clean, 28
  archives each, the same 30 stubs.
- **Reading:** the mount was outrun by another thread's shader request, not skipped.
  The report now opens with the faulting thread's name, which decides it next time.
- Directory-entry logging is budgeted per listing (48 entries, then the end), not per
  call site - the voice-over directory had used up the budget before the mount.
- **Every guest thread accounted for:** `warden_trace.cc` prints a `guest threads` line
  every 5 s - per thread, the calls since the last report and the last functions
  entered with repeats collapsed - and the blocking imports record themselves in the
  trace, so a parked thread's last entry names its wait. `KeDelayExecutionThread` logs
  each distinct three-deep return-address chain once, since the first is always the
  title's `Sleep()` wrapper.
- **The network, as a console with no cable** (`warden_net.cc`): the owner wants no
  online functionality in the app, and none is here - startups succeed, the link is
  down, no address, DNS finds nothing, `socket` fails. Winsock events are real
  dispatcher events, so the network thread blocks instead of spinning through two stubs
  at 24 million calls per five seconds (BUG-035, fixed). The 42 other `NetDll_*` stubs
  return `SOCKET_ERROR`.
- **What is left spinning (BUG-036):** `Eclipse::Update`, in `Sleep(0)` on the byte at
  `0x832127E5` after its sign-in screen code; nothing stores it directly. The D3D
  worker times out every 30 ms. The graphics interrupt callback has never been called.
- **The trace had never been compiled in (BUG-037).** The shadowed header was never
  reached by the quoted include; the hook is force-included now, verified with
  `llvm-nm`. Every "trace" before this showed only the imports that record
  themselves. A lesson recorded in the bug.
- **The graphics interrupt, then the GPU itself.** A vblank thread and swap-complete
  on their own changed nothing. So the real thing: Xenia's Vulkan graphics system
  over the guest memory (`warden_gpu.cc`, DAORecomp's `xenia_host.cpp` design), the
  ring buffer and read-pointer write-back routed to the command processor, the
  write-pointer kick through a `PPC_MM_STORE_U32` hook force-included into the
  generated code, the swap packet written on `VdSwap`, interrupts delivered through
  `xe::dao_host::DispatchInterrupt`. The command processor ran the ring - and wrote
  nothing back until the interrupt callback could see which core it was on: the
  title clears a per-core pending bit read from the PCR at r13 + 0x10C, and r13 was 0.
- **Every guest thread has a PCR, a KTHREAD and TLS now** (`FillThreadBlock`): the
  title reads them straight through r13 - its TLS pointer, its thread id, its core.
  `KeTls*` keep their values in guest memory. The Update thread had died calling a
  method off a TLS pointer the host-side TLS had never put in guest memory.
- **Controller:** `XamInputGetState` and friends answer from the pad the Java side
  tracks (`warden_xam_input.cc`); the title polls it from the Update thread.
- **The result:** the title's main loop runs - Update in game code, Render presenting
  - and then the Render thread presents ~50,000 frames a second without kicking the
  ring again and, after ~5 s, calls a null function pointer in `sub_82DABE98`
  (BUG-038). Nothing on the panel yet.
- **Diagnostics:** a `GPU:` status line every 5 s (write pointer and kicks, swap
  packets, vblanks, the read pointer written back, the D3D device's counters), the
  ring's first words once, and a `WAIT_REG_MEM stalled` line when the command
  processor waits on something that is not coming (an asserted edit in
  `tools/assemble_runtime.py`).
- Stopped for a reboot; the next task is written up in `RESUMING.md` §2.2b.

### 2026-10-23 (session 26) — the guest call chain, and the first crash solved

- **`boot_repeat.sh 3` first, as asked:** three identical runs on the new
  workstation's build, matching the original - `lr=8251B26C`, a call to `00000000`,
  one import, two allocations.
- **5.1e done: the fault handler walks the guest stack.** `[frame]` is the caller's
  frame and `[caller - 8]` its return address, re-verified against
  `__tu6_savegprlr_29` and an inline `stw r12,-8(r1)` prologue. Each frame is named by
  the function containing its return address and checked against the `bl` before the
  next one up; reads stay between `r1` and the thread's stack top. The first run's
  chain was consistent end to end.
- **It named the crash:** `sub_8251B240` making a virtual call on a null singleton,
  null because `operator new(24)` failed, failed because the allocator had sized
  itself from the **`MmQueryStatistics`** stub's zeroed answer. Details in §4. Reading
  the generated code once the chain named the function took minutes; without it, two
  sessions went on a wrong address.
- **`MmQueryStatistics` implemented** with Xenia's 104-byte layout, which was in
  `third_party/xenia-canary` all along - the earlier "not documented in this
  checkout" looked only at DAORecomp's subset. Available pages report what this port's
  physical heap can actually give, not Xenia's 512-MB figure.
- **Startup now runs much further** - ten more imports, through `NtOpenFile` and
  `NtQueryDirectoryFile` - then crashes with its own code overwritten. Every stub
  returns `STATUS_SUCCESS` without writing anything (BUG-030), so that is the suspect.
- Two smaller findings. Every attached guest thread is given the same stack top
  (BUG-031, not reached yet - only one guest thread attaches). And Gradle's
  incremental packager leaves dead space in the APK after a native change - a 336 MB
  file holding 174 MB of entries. Harmless; a clean build compacts it.
- **Then 5.1f: the code range is read-only** from just before the entry point, as on
  the console, and the guest-address fault path prints registers and the chain. The
  first write into the code was caught at once: `FindFirstFile`'s conversion of a
  directory entry, copying `FileNameLength` bytes from the buffer the
  `NtQueryDirectoryFile` stub never wrote - a stack smear climbing ~300 MB into the
  image. BUG-030 confirmed for the second crash as well as the first. §4 has it.
- **Then 5.1g: stubs fail instead of succeeding** - BUG-030 fixed. Each returns its
  import's failure value, the family derived from Xenia's kernel (three passes to get
  it right; §4). The title stopped crashing: it takes its error paths through sixteen
  imports and then spins in a wait loop the `KeDelayExecutionThread` stub cannot slow.
  `NtOpenFile` failing is what matters - the title raises straight after it - and
  `RtlRaiseException` returning instead of unwinding is a new bug (BUG-032).
- **Then 5.2 began:** `RtlInitAnsiString` logs the paths - `D:\packages\core\audio\vo\*`
  and `D:\NS\*`, two directory listings the title shrugs off - and `RtlRaiseException`
  now stops loudly for anything but a thread name. Which corrected me: the raise I had
  blamed on `NtOpenFile` was the title naming `Eclipse::StartupThread`. What actually
  stops startup is `ExCreateThread` failing.
- **Then `D:\` onto the extracted disc**: `NtOpenFile`, `NtQueryDirectoryFile` and
  `NtClose` over a shared object table. The voice-over listing finds `en-us`, the
  `D:\NS` listing correctly finds nothing, deterministic over three runs. The title's
  call site showed `NtOpenFile` takes six arguments where Xenia's takes five (BUG-033,
  Xenia's).
- **Next:** guest threads and kernel objects (5.1h) - the only thing now in the way.

### 2026-10-23 (session 26, continued) — threads, files, the video driver: packages load

- **5.1h done:** real guest threads on their own stacks (BUG-031 fixed), events,
  semaphores, the waits, a sleeping `KeDelayExecutionThread`, `ExTerminateThread`
  exiting the host thread. Eight engine threads, then Direct3D's and the network's.
- **Three more things in the way, each named by the log** and each a lie a stub was
  telling: `ExGetXConfigSetting` cancelling device creation; `MmGetPhysicalAddress`
  handing the ring buffer physical address 0; `RtlNtStatusToDosError` turning every
  failure into success. All real now, with the video driver's answers
  (`warden_video.cc`) and the file reads (`warden_fs.cc`).
- **Two things about the title that mattered:** it keeps events inside its own
  structures (adopt them from the header), and it calls imports through function
  pointers (the recompiled thunk table already maps those to the implementations).
- **The result:** the title mounts its packages, reads ~8 MB, opens the intro movie,
  queries input, audio and the user, and parks in networking stubs. §4 has the run.
- **One crash is timing-dependent** - a shader whose package had not been mounted
  (BUG-034) - and the guest function trace was built for it and is in the fault report.
- Stopped here for the day, on the owner's instruction; nothing half-finished.
- **Next:** that race, with the trace; then the networking stubs (return failures so
  the title gives up on Live), `XamInputGetState`, the XAudio pair, and the GPU command
  processor for first light.

### 2026-10-23 (session 25) — a second workstation; one signing key; files, not a folder

- **Set up a second Windows workstation from `RESUMING.md`**, at the §7 versions:
  Python 3.14.6, CMake 4.4.4, Ninja 1.13.2, LLVM 23.1.4, Temurin 17.0.20.101, SDK
  platform 36, build-tools 36.1.0, SDK CMake 3.31.6, NDK 28.2.13676358, platform-tools
  37.0.1, Gradle 9.8.2. Every host check in `RESUMING.md` §5 passed: 28/28 graft edits,
  121/121 shaders, 118/118 runtime units, `check_xex` 839/839 and 843/843 with TU7,
  `check_stfs`, `check_xdvdfs` on both discs.
- `check_xdvdfs` now lists one more file per disc than this file records (9,453 vs
  9,452; 12,321 vs 12,320). Both readers still agree; the counts here were written
  before `ce44f32` changed the tool.
- **The first APK from the new machine would not install** -
  `INSTALL_FAILED_UPDATE_INCOMPATIBLE`, because AGP silently generated a new debug key.
  Uninstalled; this machine's key is now committed as `app/debug.keystore` and must be
  installed before building anywhere else, the original workstation included
  ([`docs/SIGNING.md`](docs/SIGNING.md)). `run.sh` carried on after the failed install
  and reported on the old build: BUG-028, open.
- **The reinstall exposed BUG-029:** the app needed a folder grant, and Android 11+
  refuses one for `Download`. It now asks for the files - the disc image and the title
  update, picked together - identifies each from its header, keeps the image's grant
  and copies the title update into app storage. Verified on the Thor: both picked, 9,450
  files extracted and checked, and on the next launch the copied TU7 applied (25 overlay
  files written, 70.5%, *matches the linked recompilation*).
- Two environment notes. PowerShell 5.1 splits an unquoted
  `-DCMAKE_TOOLCHAIN_FILE=...cmake` at the `.cmake`, so quote every `-D` there. And the
  first `git push` of a session fails authentication while the retry succeeds - Git
  Credential Manager refreshing its token.
- **Next:** unchanged - 5.1e, the guest stack walk. Re-run `boot_repeat.sh 3` first, as
  `RESUMING.md` §2 asks.

### 2026-10-23 (session 24) — the crash diagnosis was wrong; retracted

- **Retracting 5.1c.** Two documents said the crash was a zero at guest `0x832F13CC`
  that startup should have filled, "inside `tu6_sub_8251C810`". Reading the translated
  function kills both halves: it is a lazily-initialised singleton getter that *tests*
  that slot for zero, constructs the object, and caches it back - zero is its correct
  empty state - and it contains **no indirect branch at all**. The address was never
  implicated. I had reasoned from a register dump and a disassembly comment without
  reading the surrounding control flow.
- **Both crash-site identifiers turn out to be unreliable.** The host backtrace shifts
  between builds because `-O2` inlines and tail-calls translated functions: six guest
  frames one build, three the next, with identical guest state. And `ctx.lr` is a plain
  store the generated code makes before each call, which the compiler may elide when no
  callee reads it - stable across runs, but not necessarily the faulting frame.
  `boot_repeat.sh` now reports guest `lr` plus the decoded call target instead of the
  host frame, which at least compares runs by something that does not move.
- **So the next task is the guest stack walk, and it is blocking** - 32,377 indirect
  call sites in the translation, nothing to narrow them with. Recipe verified against
  the generated prologues rather than assumed: `[frame]` is the caller's frame, and
  `[caller - 8]` is its return address (`__savegprlr_N` does `stw r12,-8(r1)`).
- **One hypothesis gets promoted.** The unpatched XEX import table (5.1d) was recorded
  as unrelated to the crash on the strength of the `0x832F13CC` check. With that gone,
  an imported *variable* holding a function pointer that nothing fills is exactly this
  crash's shape.

### 2026-10-23 (session 23) — null calls name themselves; the import hypothesis dies

- **5.1b done, and better than planned.** Instead of a trap stub per slot, the fault
  handler *decodes* the table offset - it is invertible - and reports
  `INDIRECT CALL to guest 00000000 whose table slot is empty`. No memory, no thunks.
  It re-derives the fault address from what it recovered and only believes itself on an
  exact match, because it runs on unrelated crashes too.
- **5.1c: the import-table hypothesis is disproved.** Parsed the executable's import
  table - 200 xam, 314 xboxkrnl, 514 slots - and `0x832F13CC` is not among them, nor
  within 0x4000 of one. So that load is ordinary image data the title expects to hold a
  pointer, and the cause is ordering within startup.
- **But the check found a real gap.** Those 514 import slots are patched by the
  console's loader and nothing here patches them. XenonRecomp handles imported
  *functions* through `__imp__` calls; imported **variables** are data, and a title
  reading one would read whatever the image holds. Not this crash, still unaddressed.
- My first parse of the import table reported 12,800 imports per library spanning the
  whole address space - visibly nonsense, from reading `numberOfImports` four bytes
  early. Worth noting only because the corrected numbers look plausible and the wrong
  ones did not, which is the only reason I checked.
- **Next:** read what the loaded image holds at `0x832F13CC`, which is a few lines and
  settles whether the title means to fill it.

### 2026-10-23 (session 22) — the crash is confirmed: a null indirect call

- **Confirmed arithmetically**, not inferred: the fault address is *exactly*
  `PPC_LOOKUP_FUNC(base, 0)`. The title executes `bctrl` through a function pointer it
  read as zero, inside `tu6_sub_8251C810`.
- The types are the catch. `PPC_CODE_BASE` is `0x82210000`**ull**, so
  `uint32_t(y) - PPC_CODE_BASE` is evaluated in 64-bit unsigned and does not wrap at 32
  bits. My first prediction masked to 32 bits and missed by exactly 2^33 - a near-match
  that would have been easy to accept, and checking the types instead is what turned a
  hypothesis into a result.
- **A consequence worth acting on:** a null indirect call faults at
  `base - 0x81020000`, which looks like a wild pointer and says nothing, and every null
  call lands in the same place. Filling unmapped table slots with a trap stub that
  names the guest address would turn this entire investigation into one log line.
- **Next:** that trap stub, then find the zero's source - a guest structure the title
  expects initialised, or `MmQueryStatistics`, the one import still stubbed.

### 2026-10-23 (session 21) — the fault diagnoses itself

- **Made guest faults self-describing.** The write-watch fault handler runs on the
  faulting thread, so for a guest fault it can read that thread's `PPCContext`. It now
  reports the guest registers and, for an address outside the guest space, its signed
  distance from the base - because that distance identifies the arithmetic behind it.
- **Two hypotheses ruled out by it.** The stack is fine (`r1 = 700FFBE0`, the top less
  0x420 of frames), and the bad address is not an untruncated guest register - every
  relevant one is zero in both halves.
- **One hypothesis raised.** The distance from the base is identical across runs, so
  the faulting expression is `base + a constant` with all registers zero - which is
  what `PPC_LOOKUP_FUNC(base, 0)` looks like, i.e. calling through a function pointer
  read as zero. Stated as a hypothesis: the arithmetic has not been checked against the
  macro and `tu6_sub_8251C810` has not been read.
- `lr` placed execution one frame deeper than the tombstone's innermost named frame,
  in `tu6_sub_8251C810`.
- **Next:** read that function, confirm or kill the hypothesis, and if it holds find
  where the zero comes from - `MmQueryStatistics` is the one import still stubbed.

### 2026-10-23 (session 20) — eleven imports; the crash is not an import problem

- **Implemented eleven of the twelve imports** the title reaches, dropping stub hits
  from 12 to 1. All fire with plausible arguments - including a 214 MB physical
  allocation, which is the title sizing its pools against a 512 MB console.
- Two deliberate omissions, both recorded where the code is: `MmQueryStatistics` stays
  a stub because its structure layout is not documented in this checkout, and the
  critical sections keep their lock host-side rather than guess at
  `RTL_CRITICAL_SECTION`. Both say what would make them wrong.
- `KeTlsGetValue` implemented although unreached - its pair sets values, and a stub
  returning 0 for a value that had been set is a wrong answer rather than a missing one.
- **The crash did not move**: `tu6_sub_8251B240+416` with twelve imports stubbed or
  with one. So it is guest *entry state*, not imports - a different kind of problem,
  and continuing down the import list would not have found it.
- **Next:** read the faulting instruction out of the generated source, and check the
  unchecked entry state - bare stack, zero `r13`, zero `lr`, mostly-zero `PPCContext`.

### 2026-10-23 (session 19) — the heap works; startup is deterministic after all

- **Retracting last session's warning.** Guest startup *is* deterministic - three runs
  give the same crash site, the same 12 imports and the same 2 allocations. What made
  two runs look like two code paths was my own measurement: `LoadTitle` blocks the main
  thread for 10+ seconds, so a 7-second sleep sampled before guest code had run.
  `boot_repeat.sh` now polls for the boot marker. A fixed sleep is not a measurement.
- **The allocator works and was the right thing first.** A 1 MB reserve then a 64 KB
  commit at the same base, and `HalReturnToFirmware` - "reboot the console" - has gone
  from the sequence entirely.
- **The import list is now reproducible**: 12, in order, starting with
  `KeGetCurrentProcessType` and the `Rtl` critical sections. That makes it a work list.
- **Found but not fixed:** `LoadTitle` on the main thread is an ANR waiting to happen.
- **Next:** work down the 12, and move `LoadTitle` off the main thread.

### 2026-10-23 (session 18) — a guest heap, and a correction about the import list

- **Implemented `NtAllocateVirtualMemory`** over a guest heap following the console's
  page-size regions. The strong-override mechanism works: a strong `__imp__` definition
  replaces the generated weak stub cleanly.
- **Correcting the previous session.** I presented 14 imports "in the order the title
  reached them" as the work list for 5.1. Three runs since, with logging flushed, show
  **no** imports reached before the crash, and the crash moving deeper inside the same
  function. The allocator is never reached on the current path. Both cannot be true of
  one deterministic path - so guest startup is not deterministic, and that list is one
  sample rather than the sequence.
  **→ Wrong, corrected in session 19.** Startup *is* deterministic. The runs showing
  no imports were sampled before guest code had run, because `LoadTitle` blocks the
  main thread for ten-plus seconds and I was sleeping seven. Left here as written
  because the log is a record, not a summary.
- **The instrument was lying to me.** Xenia's logger buffers, so the lines that explain
  a crash are the ones most likely to be lost - one run looked as though the allocator
  had never been called when its log just had not been written. `NoteImportCall`, the
  boot marker and the allocator now flush. Noted in `RESUMING.md` as a gotcha.
- **Next:** make startup reproducible before implementing more imports. Candidates for
  uninitialised state are listed in `RESUMING.md` §2 - the guest stack is handed out as
  a bare address, `r13` is zero, and the `PPCContext` is zeroed apart from `r1`.

### 2026-10-23 (session 17) — guest code runs; the import list is measured

- **First execution of the recompiled game on the device.** `_tu6_xstart` plus five
  frames of translated PowerPC, then a SIGSEGV in `tu6_sub_8251B240`.
- **14 kernel imports reached, in order** - a C runtime startup, with
  `NtAllocateVirtualMemory` first and `HalReturnToFirmware` (reboot) fourth. The stub
  returned 0, the title got no memory, and it headed for the exit; the crash is
  downstream of that. The fault address is below the guest base, so it is a bad host
  pointer rather than a guest address.
- That turns 5.1 from "implement 251 kernel functions" into a list of 14, which is
  exactly what the self-counting stubs were for.
- Booting is **opt-in** (`--ez boot true`), not the default: once guest code runs the
  process crashes, and defaulting to that would make every routine capture a crash
  report and ruin it as a baseline.
- Also fixed `verify_extraction.py`, which had started failing because the app now
  writes the 25 title-update overlay files - they come from the package, not the disc,
  so they are legitimately present and legitimately absent from the disc listing.
- **Next:** 5.1, starting with `NtAllocateVirtualMemory`.

### 2026-10-23 (session 16) — the patch overlay, and a callable entry point

- **The title update's data files now land on the device**: 25 files under
  `packages/core/patch/`, written from the package at the moment the executable patch
  succeeds, and checked against the host extraction - every path and size identical.
  They take precedence over the base ERFs, so the game needs them. That closes the last
  gap `ASSETS.md` recorded about the title update.
- **Found a required step the plan did not have.** XenonRecomp resolves indirect calls
  at *runtime*, through a 31 MB table in guest memory that the runtime must write -
  every `bctrl` reads a host function pointer out of it. Nothing announces this; the
  first indirect call would have jumped through stale bytes. Built: 53,108 entries at
  `0x83400000`, zeroed first so an untranslated address gives a null rather than
  garbage.
- `AttachGuestThread` is real: a `PPCContext` and the 256 KB guest stack the XEX header
  asks for. Entry point `0x82AB2650` confirmed present in the function table.
- The table is built **only** when the image matches the linked build, because filling
  it from a mismatched image would be worse than not filling it.
- **Next:** task 5.0h, which is small - call the entry point on a background thread
  after the report is logged, so the import log survives the abort the first
  unimplemented import will cause.

### 2026-10-23 (session 15) — Phase 5.0f: the title update applies; the blocker is gone

- **Verified the STFS reader** that was left as an unverified draft. 26 files,
  3,370,037 bytes, every path, size **and byte** identical to DAORecomp's
  `scripts/stfs.py`. Contents rather than sizes on purpose: a fragmented STFS file is
  read by following a chain through hash tables, and getting that subtly wrong gives a
  file of the right length holding some of the wrong blocks.
- Fixed my own check while doing it: the first draft **failed** the repo's TU2 package
  for not being TU6 or TU7, when it had read perfectly. Whether a package is usable is
  `verify_assets.py`'s job; the reader check now reports and judges only the reader.
- **Integrated XenonRecomp's XexPatcher** plus libmspack's LZX, with
  `tools/gen_xex_patcher.py` trimming its one Win32-only overload via asserted edits.
  Host check first: `check_xex.py --build ppc_tu6 --patch` → **843 of 843 groups agree**.
- **Then on the device: 70.5% of 53,108 entry points**, verdict *matches the linked
  recompilation*. Against 1.8% unpatched. **The generation mismatch that blocked first
  light is cleared.**
- The app reads the update out of the LIVE package itself, so the user only drops the
  file in. The patch is identified by size and name rather than trusted, because TU2
  exists and is unusable.
- Learned that the patched executable is a **different shape** from the base one -
  plaintext and uncompressed where the base is AES-CBC with a four-block list, and with
  a different entry point (`0x82AB2650` vs `0x829E8698`).
- **Next:** task 5.1 - a guest thread. `AttachGuestThread` needs a `PPCContext` and the
  header's 256 KB stack; then call `0x82AB2650` and read the import log.

### 2026-10-22 (session 14) — stopped for the evening; handoff written

- **[`RESUMING.md`](RESUMING.md) added**, written for a fresh session on a machine that
  has never seen this project: what it is, where the work stopped, the pinned toolchain
  versions and why each is pinned, setup from a clean clone, the checks that prove the
  environment before anything is changed, and the traps that cost an hour each.
  Every command in it was run to confirm it is right — which caught two errors in my
  own first draft (`recompile.py` takes a positional argument, not `--build`, and the
  graft is 28 edits, not 27).
- **Committed an unverified draft** of the STFS reader rather than leave it on one
  machine: `app/src/main/cpp/warden_stfs.{h,cc}` and `tools/stfs_host.cc`. It has not
  been compiled for any target and has not been checked against anything. The commit
  message and `RESUMING.md` §2 both say so, and say what has to happen before it is
  trusted — the differential check against DAORecomp's `scripts/stfs.py` comes first,
  because the XEX loader's bug produced plausible-looking output rather than an error.
- Working tree clean, everything pushed.
- **Next:** finish task 5.0f in the order `RESUMING.md` §2 sets out.

### 2026-10-22 (session 13) — Phase 5.0e: the image loads, and a generation mismatch found

- **Wrote and verified the XEX loader.** AES-128-CBC decryption plus four raw blocks,
  following XenonRecomp's own algorithm. `tools/check_xex.py` confirms it against the
  recompiled source: **839 instruction groups, all 839 agreeing**, `mflr r12` at 36,403
  sites all encoding to `7D8802A6`.
- The verification method is the interesting part: grouping function entry points by
  the instruction XenonRecomp quotes above each one, and requiring agreement. No opcode
  table, no disassembler, and it tests the whole pipeline at once. It immediately caught
  a struct offset I had hand-added four bytes early, which had been silently producing
  noise.
- **Then it found a real mismatch.** The app links `ppc_tu6` - recompiled from the
  Title Update 7 **patched** image - while the disc holds the base executable. Partial
  disagreement was the clue: AES-CBC cannot decrypt partially, so 16,043 distinct
  encodings where there should be one means two generations of the same image, not a
  broken key.
- **The app now checks this itself**, from the function mapping table it already links:
  1.8% of entry points start with `mflr r12`, against ~71% for a matched pair. It
  reports the reason and what is needed rather than a bare mismatch.
- Also fixed the overlay reported as cut off: scrollable, half-width, with a status
  block at the top saying plainly what the port can and cannot do.
- **Next:** task 5.0f, applying the TU7 patch. XenonRecomp's `XexPatcher` is
  self-contained but for `lzxDecompress` + libmspack's `lzxd.c`; both compile for
  Android. Also needs `default.xexp` on the device.

### 2026-10-22 (session 12) — Phase 5.0d: the recompiled game is linked and running

- **The whole game is in the APK.** 157,061 defined symbols, **52,620 recompiled guest
  functions**, a 160 MB library in a 199 MB APK. It installs, loads and still presents
  at 133 ms startup with no crashes.
- Guest code compiled at Phase 2's exact settings (C++17, `-O2`, `-g0`), because that
  is what 6,635 functions were differentially tested at. Full compile of the 208 units
  takes about 4.5 minutes.
- **The kernel boundary is now measured: 251 imports.** `Net 47`, `Xam 42`, `Nt 30`,
  `Ke 28`, `Vd 20`, `Rtl 20`, `XMA 16`, and a long tail. Derived from
  `ppc_recomp_shared.h` rather than from linker output - lld caps its error list, which
  is why two earlier link attempts each reported a different arbitrary twenty. The two
  methods agree exactly, which checks both.
- **Networking is the largest group, in a single-player game.** 47 imports from a
  network layer the engine links in regardless. Probably the cheapest group to satisfy;
  the import log will say rather than leaving it assumed.
- **All 251 have weak, self-counting stubs.** Weak so a strong definition overrides with
  no registry to maintain; self-counting because a stub cannot know whether its caller
  reads 0 as success, false or null, so it logs instead of pretending. The report names
  what was reached, in order - which is the work list.
- `PPCQueryTimebase` implemented against Xenia's guest clock, so the guest and GPU share
  one notion of time.
- **Next:** boot it. That needs `AttachGuestThread` and a thread that calls the
  recompiled entry point, and then the import log says what to implement next.

### 2026-10-22 (session 11) — Phase 5 groundwork: renderer links, guest memory verified

- **Moved the disc image to the Thor's Download folder** as asked. That immediately
  showed a flaw worth fixing: the folder holds three disc images, two of them other
  games, so "largest .iso wins" had been picking right by luck. Images are now
  identified from their own XEX2 execution header - title `0x454108C0` for Dragon Age,
  media id separating Origins from Awakening - with the ranking in `warden_title.h` so
  the ids are written down once. Verified by the host differential check, and on the
  device against a planted 1 MB file of zeros named `not-dragon-age.iso`, which it
  rejected.
- **Enumerated the Phase 5 link boundary properly.** lld caps at 20 errors, so two
  attempts each showed a different arbitrary twenty; `--error-limit=0` gives the real
  **65**. 57 are glslang - a missing library, handled by applying Xenia's own build
  rule - and 8 are real.
- **Implemented `xe::Memory` and it works on hardware.** The grafted `memory.h` is
  DAORecomp's 177-line replacement, and its comment is the spec. Physical views alias
  via a thrice-mapped memfd, and the fault-based write watch fires, invalidates and
  lets the write retry. That was the riskiest mechanism in the whole port and it is now
  measured rather than hoped for.
- **`xe::dao_host`:** `TitleId` and `ShortSleep` real, the two that need guest code
  aborting loudly rather than quietly.
- **The whole Xenos renderer is in the APK:** 118 TUs, 85 MB library, 51,249 symbols,
  still presenting, still no crashes, 97 ms to start.
- Also fixed two bugs in the capture script itself, since the report is the artifact
  this project is read through: a greedy `sed` ate part of its own log lines, and the
  game-data section was showing the tail of a different block.
- **Next:** task 5.0d, linking the recompiled title - 208 generated `.cpp`, 265 MB.
  Everything else in Phase 5 waits on it.

### 2026-10-22 (session 10) — Phase 4.3/4.4: game data on the device, verified

- **6.39 GiB extracted and checked file by file.** 9,450 files,
  6,859,144,771 bytes, every one at exactly the size on the disc
  (`tools/verify_extraction.py`). 7m44s on the handheld.
- **The plan's "roughly 20 GB" was out by three times.** Measured: `modules/` 4.03 GB,
  `packages/` 2.81 GB, `default.xex` 19 MB, and `$SystemUpdate/` skipped as console
  payload. ASSETS.md now has the table rather than the estimate.
- **Ported XDVDFS to C++ and diffed it against the Python** rather than assuming:
  `tools/check_xdvdfs.py` builds a host harness from the same source that ships in the
  APK and compares the full recursive listing of both discs — 9,668 and 12,610 lines
  identical, zero divergence on the first run. The reader takes a `ReadAt` callable, so
  Android gives it a descriptor and the host gives it a `FILE*`.
- **SAF is only slow if you use it wrong.** `openFileDescriptor` hands back a real
  seekable fd; native then uses `pread` and the framework is out of the way. Direct
  path (`getExternalFilesDir`) is preferred over a SAF grant where available, because a
  descriptor has nothing to lose on reboot.
- **Resume tested both ways:** stamp present skips entirely; stamp removed re-scans and
  finds all 9,450 present in 57 ms. The `.part`-then-rename scheme is what makes a
  size check sufficient.
- **Found a bug in my own extractor before shipping it:** cancelling mid-file left the
  state at `kExtracting`, so the completion stamp was written and a cancelled
  extraction would have been treated as finished - with the next launch skipping the
  missing files entirely.
- **Still owed:** TU7's `packages/core/patch/` overlay (25 files, 3.3 MB) takes
  precedence over the base ERFs, so the device needs it. STFS parsing on-device is a
  bigger job than XDVDFS; copied in separately for now.
- **Next: Phase 5, first light.** Everything Phase 4 existed for is in place - an APK
  that presents, mapped input, verified game data. What remains needs the guest
  running: `dao_host`, `xe::Memory`, and the `gpu/vulkan` command processor.

### 2026-10-22 (session 9) — Phase 4: the app runs, and the renderer presents to the panel

- **An APK that works on the Thor.** Gradle 9.8.2 + AGP 9.4.1 + SDK CMake 3.31.6, one
  module, arm64 only, `targetSdk 33` to match the device. The native source list is
  generated from `compile_runtime.py` so the exclusion rules stay in one place.
  `./tools/appbringup/run.sh` does build, install, launch, capture and screen sample.
- **Xenia's VulkanPresenter is presenting on the handheld**: `Created 1920x1080
  swapchain with format 37, color space 0, presentation mode 1`, with the frame
  confirmed by pixel sampling off the device rather than inferred.
- **The thing that was supposed to block this did not exist.** The presenter needs an
  `xe::ui::Window`, and `window.cc` includes ImGui — which put presentation behind
  Phase 10. The includes are dead: ImGui is never named in the file's 758 lines. One
  graft edit, and then two small classes (`WindowedAppContext` has two pure virtuals,
  `Window` has four). The eight aborting stubs from Phase 3.2 are gone; `window_host.cc`
  is deleted.
- **A measurement I nearly recorded wrongly.** The Thor's surface first reported 6
  formats, all `SRGB_NONLINEAR` — i.e. "this Display-P3 panel can't do wide gamut under
  Vulkan". Xenia simply never requests `VK_EXT_swapchain_colorspace`, and without it the
  spec lets a driver report sRGB only. The Phase 0 capability dump had the extension
  listed as available, which is the only reason the contradiction was visible. After the
  graft edit: **12 formats, including `A2B10G10R10_UNORM_PACK32` +
  `DISPLAY_P3_NONLINEAR`** (BUG-023). D8's default is reachable — though the presenter
  still *picks* 8-bit sRGB, which is now task 9.3 rather than an assumption.
- **HDR10 is genuinely unavailable** on this driver even with the extension on, while
  Android reports HDR10/HLG at 420 nits for the same display. PQ is absent, not
  mislabelled — sharpens BUG-009 into BUG-025, and re-scopes 9.2.
- **DAORecomp's presentation features are D3D12 only** — HDR, FSR 3/4, XeSS, DLSS,
  motion vectors, frame generation. FSR 1 is the exception and is already wired up for
  Vulkan. So "port the features over" applies to FSR 1 and nothing else here. It is also
  why the `Presenter::Dao*` pacer functions can correctly do nothing, and why **Swappy
  is not a replacement for `dao_frame_pacer.cc`** — different jobs.
- **Input done (4.6):** `X_INPUT_GAMEPAD` as upstream defines it, positional mapping,
  self-test 20/20. The Thor's built-in pad calls itself **"Odin Controller"
  (0x2020/0x0111)** — so glyph detection must key on vendor/product, not the name.
- **Pinning verified (4.8):** `sched_setaffinity` is accepted here; clusters detected by
  frequency, not hardcoded. Measured now because a vendor refusing it silently would
  have surfaced in Phase 5 as an unexplained slow game.
- **The bug worth remembering (BUG-026):** `setBackgroundColor` on the SurfaceView hid
  every presented frame, while every log said the renderer was fine — and it was,
  presenting into a layer nothing could see. The capture now samples pixels, because "a
  frame was presented" and "something is visible" are different claims.
- **Next:** 4.3 SAF storage, then 4.4 first-run extraction — the path to having game
  data on the device. 4.7 Swappy and 3.3b both need sustained frames, so they follow
  Phase 5.

### 2026-10-22 (session 8) — Phase 3.3a: the top risk, measured and downgraded

- **Measured the EDRAM fallback instead of assuming it.** R1b rested on upstream's
  own warning that `kHostRenderTargets` "may be irreparably inaccurate ... at the
  mercy of the host API's fixed-function output-merger, primarily because it has to
  perform blending". That sentence names a *mechanism* — blending in a substituted
  pixel format — so it can be checked rather than feared.
- **It does not apply on this hardware.** 0 of 9 render target formats fail to blend;
  depth is `D24_UNORM_S8_UINT`, matching Xenos' 24-bit exactly rather than falling
  back to `D32_SFLOAT`; gamma targets blend as linear UNORM16 rather than in 8-bit
  gamma space. No format substitution is required, so the specific degradation the
  warning describes is not active. **R1b downgraded from top risk to moderate.**
- Extended `tools/vkdevice/vkdevice_probe.cc` with EDRAM sections that **mirror** the
  checks in `vulkan_render_target_cache.cc` rather than calling them — the cache needs
  a command processor and the probe links only the device layer. Noted in
  `docs/EDRAM.md`: if upstream changes those checks, the mirror must follow.
- New fact: **Adreno 740 satisfies every other requirement of the accurate interlock
  path** — `fragmentStoresAndAtomics`, `sampleRateShading`, `standardSampleLocations`,
  ample storage-buffer slots. Only the extension itself is missing, so a future
  Qualcomm driver would enable the accurate path by flipping one cvar. Re-probe after
  driver updates.
- **Being precise about what this is not.** Format capability is necessary, not
  sufficient. `kHostRenderTargets` is still an approximation of unified EDRAM built
  from separate images, and its residual inaccuracy is structural: tile ownership
  transfer between aliasing targets, resolve semantics, MSAA sample layout. Those
  cannot be answered by asking the driver about formats. Nothing has rendered yet.
- Wrote [`docs/EDRAM.md`](docs/EDRAM.md); split plan task 3.3 into **3.3a** (done) and
  **3.3b** (frame comparison against the D3D12 reference).
- **Next:** Phase 4 — the Android shell. Not a detour: 3.3b needs submitted frames,
  which need `dao_host`, `xe::Memory` and a real surface. The eight aborting stubs in
  `tools/vkdevice/host_stubs.cc` are exactly that contract.

### 2026-10-22 (session 7) — Phase 3.2: Xenia's Vulkan code accepts the Thor

- **Linked the runtime**, which is what made the host boundary concrete: eight
  symbols DAORecomp implements outside the Xenia subset it vendors — its Windows
  frame pacer and Xenia's window layer. Stubbed in `tools/vkdevice/host_stubs.cc`,
  each aborting rather than returning a plausible value, so the probe cannot quietly
  depend on a fake.
- **Supplied four dependencies properly instead of stubbing them**: `fmt`'s
  `format.cc`, snappy (GPU trace capture is how frames get compared in 3.3), an stb
  implementation unit, and snappy's generated header. `tools/prepare_deps.py` writes
  the generated ones.
- **Ran Xenia's own Vulkan initialisation on the handheld** and it accepted the
  device: Adreno 740, Vulkan 1.3.128, ten of thirteen optional extensions enabled.
  `tools/vkdevice/run.sh` reproduces it end to end.
- New fact beyond the Phase 0 probe: **sparse binding resolved to a real queue
  family** (0, shared with graphics+compute). Phase 0 only confirmed the feature bit,
  and Xenia's shared-memory emulation needs an actual family.
- Fixed two of my own tooling faults found here: object files were named by loop
  index, so a second run with a different scope left stale objects beside the new ones
  and the link saw the same translation unit twice; and my error classifier ignored
  `unable to find library`, which made a failed link look like a successful one with
  no output.
- Also found `BUG-020` — `filesystem_android.cc` defines a `return false`
  `SetAttributes` that collides with `filesystem_posix.cc`'s working stat/chmod
  version. Both files are needed on Android, so the duplicate made the target
  unlinkable; and had the stub won, setting a file read-only would silently have done
  nothing.
- **Next:** Phase 3.3 — EDRAM via `kHostRenderTargets`, validated against the D3D12
  reference. Still the project's top risk (R1b / Q8).

### 2026-10-22 (session 6) — bug tracker, and Phase 3.1c: 114/114 compiling

- **Created [`docs/BUGS.md`](docs/BUGS.md)**, tracking every defect found so far
  including upstream and driver ones, with **what each one actually causes** — several
  of these fail with no useful diagnostic at all. 19 entries, 11 fixed, 8 open or
  worked around. Linked from `README.md` and §5b here.
- **Phase 3.1c source selection.** `compile_runtime.py --scope android` now selects
  sources instead of compiling everything. 65 excluded, split deliberately into 56
  that are *wrong for this target* and 9 merely *deferred*. **114/114 compile.**
- Found the trap that caused most of it: **Xenia sets `XE_PLATFORM_LINUX` on Android**,
  so the GTK/X11/SDL2 desktop windowing files are reachable and are not guarded out by
  any platform macro.
- **Fixed five real portability bugs** (BUG-003, 016, 017, 018, 019). Three are in
  Xenia's own Android path, which had rotted from never being built: a call with the
  wrong argument count, reliance on glibc robust mutexes bionic does not implement, and
  a helper called from a `const` method without being `const` — beside a mutex already
  marked `mutable`. The other two are DAORecomp using Win32 unguarded in a portable
  file.
- Worth noting they **cascade**: an unguarded Windows *include* was masking an
  unguarded Win32 *call* in the same file, and one `threading_posix.cc` error hid
  another. Each fix revealed the next.
- Confirmed the Windows direction is not regressed — the guards are all
  `#if XE_PLATFORM_WIN32`, and the host target still compiles 16/30 of the Vulkan scope
  (the other 14 need the unfetched `DirectX-Headers`, which is not a defect).
- **Next:** Phase 3.2 — link the runtime, then Vulkan device bring-up on Adreno 740.

### 2026-10-22 (session 5) — Phase 3.1 complete: the Vulkan backend compiles for ARM64

- **Sized the graft before starting it** (`tools/graft_inventory.py`). The naive answer
  was 74,219 lines reaching into the whole emulator. Tracing the actual include edges
  found only six causing that, almost all in `vulkan_command_processor.cc`; cut those
  and the graft is 58 self-contained files.
- **Built the assembler** (`tools/assemble_runtime.py`): DAORecomp's 665-file subset +
  72 Canary files + 17 asserted edits, producing `build/runtime-tree/`. Nothing from
  upstream is committed, so our diff stays legible. `--check` verifies every edit.
- **Copied DAORecomp's own solution** for severing the emulator: they replaced three
  emulator includes with `xenia/dao_host.h`, a 28-line interface of four functions.
  Applying the same to Vulkan needed less than expected — the emulator usage was a
  constructor parameter and a cosmetic window title.
- **Matched DAORecomp's modified `Presenter`**, which is what the Q1 probe's 11.5%
  `ui/presenter.h` divergence actually was: two added parameters, `dao_sdr` and
  `dao_generated_ref`. Both handled honestly — `dao_generated_ref = false` is correct
  since this backend has no frame generation, and `dao_sdr` is accepted with a TODO.
- **Generated the SPIR-V shaders** (`tools/build_xenia_shaders.py`), 121/121. These are
  build artefacts nothing produced: upstream ships one placeholder and DAORecomp ships
  126 *DXBC* ones. No Vulkan SDK needed — the NDK has `glslc`, `spirv-opt`, `spirv-dis`.
- **Found two real upstream bugs**, both invisible in the configuration their authors
  build: Xenia's `vulkan_api.h` includes `vulkan_to_string.hpp` before `vulkan.hpp`,
  which guards out `vulkan.hpp`'s own re-include and breaks it; and DAORecomp's
  `dao_resolve_direct.xesli` uses HLSL `saturate()` where every other shader uses the
  portable `saturate_xe`.
- **Result: 30/30 Vulkan translation units compile for `aarch64-linux-android33`.**
  Whole tree 111/187; the other 76 are platform-wrong files (Windows COM, the D3D12
  backend, `debugging_mac.cc`, `platform_amd64.cc`) that the build does not yet select
  against — Phase 3.2 work, not porting.
- Fetched 14 Xenia submodules at Canary's recorded commits; documented in
  `THIRD_PARTY.md`.
- **Next:** Phase 3.2 — per-platform source selection, then link, then Vulkan device
  bring-up on Adreno 740.

### 2026-10-22 (session 4) — Phase 2 complete: ARM64 proven correct

- **Built a differential test** (`tools/gen_difftest.py`, `tools/run_difftest.py`). It
  selects recompiled functions that can run standalone, executes each from an identical
  deterministic state on the workstation and on the Thor, and diffs the resulting PPC
  context. 6,635 functions qualify.
- **Result: 6,549 of 6,635 byte-identical (98.70%), zero logic bugs.** All 86
  divergences classified into four understood architectural differences — and in two of
  them ARM64 is the *more* faithful target than x86-64. Details in §4 and
  `docs/PROBE-ARM64.md`.
- **Best finding:** the generated code emits a raw integer division with no zero guard,
  because on PowerPC `divwu` by zero does not trap — the original compiler's `twllei`
  check comes *after* the divide. ARM64's `udiv` returns 0 like the Xbox 360; x86-64's
  `div` raises #DE. That accounts for all 16 host faults, against zero on the handheld.
- **Found the host needs `/arch:AVX`.** simde reaches rounding via
  `__builtin_roundevenf`, which without SSE4.1 becomes a call to `roundevenf` that the
  MSVC CRT lacks. ARM64 has `FRINTN` natively, which is why only the host was affected.
- **One apparent bug turned out to be my harness**, which is worth recording. A
  function using `mffs` disagreed; running it in isolation gave byte-identical state on
  both architectures, which pointed at the harness rather than the code. `seed()` was
  normalising flush-to-zero but not the rounding mode, so a `mtfsf` earlier in the run
  leaked into it. Fixed, and it agrees.
- Also fixed two selection bugs where the generated code reaches things through macros:
  `PPC_QUERY_TIMEBASE()` and `PPC_CALL_INDIRECT_FUNC` both slipped past a filter that
  only matched the underlying names. The import stubs caught the first by aborting
  loudly rather than returning nondeterministic data.
- Confirmed upstream handles two hazards correctly: `cntlzw` is emitted with an explicit
  zero guard (`x == 0 ? 32 : __builtin_clz(x)`), and XenonRecomp has a complete ARM64
  FPSCR implementation via `mrs`/`msr`.
- **Next:** Phase 3 — graft Xenia's Vulkan backend onto DAORecomp's frontend.

### 2026-10-22 (session 3) — Phase 1 complete, Phase 2 largely validated

- **Host toolchain (D10).** No MSVC on this machine, and WSL's sudo wants a password,
  so took DAORecomp's own route: `tools/setup_winsysroot.py` fetches the MSVC CRT and
  Windows SDK with xwin (~85 MB, sha256 verified, versions pinned to upstream's) and we
  build with the existing LLVM 23. Built XenonRecomp with upstream's
  `cmake/clang-cl-xwin.cmake`.
- **1.1 / 1.2 extraction.** `default.xex` and TU7's `default.xexp` extracted and both
  **hash-matched exactly** against DAORecomp's known-good SHA1s. Used their own
  `scripts/stfs.py` for the LIVE package.
- **1.3 recompilation.** Applied `patch_recompiler.py` (+228 lines of PPC/VMX128
  instructions DAO needs), then generated both builds: 202 and 207 translation units,
  540 MB of C++, **52,857 functions, zero warnings**. Wrote `tools/recompile.py`.
- **1.4 dropped.** DAORecomp does not use XenosRecomp — it uses Xenia's *runtime*
  shader translator. The SPIR-V work is part of the Phase 3 backend swap, not a
  separate ahead-of-time step. Corrected the plan and `THIRD_PARTY.md`.
- **1.5 plus a Phase 2 preview.** Wrote `tools/compile_check.py` and compiled all 208
  translation units for **both** x86-64 and **aarch64-linux-android33**. Both passed
  first time with no source changes. Verified the ARM64 objects are genuine aarch64 and
  that the vector-heavy ones disassemble to real NEON (822 `tbl`, 19 `rev32`), which
  is the simde VMX128 path working on real game code.
- **Corrected the Xenia pin** to `1c81b867`, the commit DAORecomp's vendored subset
  actually came from; frontend divergence improved from 2.4% to 2.1%.
- **Found the glyph table in TU7 itself** — `X360_ControllerInteraction.GDA` ships in
  the title update, so the Awakening disc isn't needed for Phase 7.
- Diagnosed two silent failure modes worth remembering: `submodule.active=no` from the
  clone flags, and XenonRecomp's `0xC0000409` abort on a missing or absolute output
  directory.
- **Next:** Phase 2.3, the cross-architecture hazard audit (Q9), then Phase 3.

### 2026-10-22 (session 2) — Phase 0 complete

- **0.3 Toolchain.** Installed CMake 4.4.4, Ninja 1.13.2, LLVM/Clang 23.1.4, Temurin
  JDK 17, Android SDK platform 33 + build-tools 35.0.1, and **NDK 28.2.13676358**.
  Set the environment variables and PATH at user scope. Learned that the new Android
  CLI replaced `sdkmanager` and uses slash-separated package IDs.
- **Proved the chain on hardware.** Wrote `tools/vkprobe/`, cross-compiled it for
  arm64-v8a/API 33, pushed it to the Thor and ran it. This validates NDK, cross-compile,
  adb and native device execution in one step rather than assuming any of them.
- **0.4 Vendored and pinned** DAORecomp, XenonRecomp, XenosRecomp and Xenia Canary;
  commits recorded in `THIRD_PARTY.md`.
- **0.4 Q1 risk probe — answered GREEN.** Wrote `docs/PROBE-Q1.md`. Xenia's Vulkan
  backend is structurally complete and at 1:1 parity with D3D12; DAORecomp diverged
  from the Xenos frontend by only 2.4%; Adreno 740 has every critical feature. Found
  the one real caveat (`VK_EXT_fragment_shader_interlock` missing → the less accurate
  EDRAM path), and the one underestimated cost (7,178 lines of DAO-specific rendering
  code inside the D3D12 backend needing reimplementation). Raised **Q8** for the
  accuracy question this creates.
- **0.6 Device loop.** Wrote `tools/deploy.sh` with `check / info / vulkan / shot /
  logs / crash / perf / run`. Confirmed dual-display capture works at the right
  resolutions (1920×1080 and 1240×1080), after discovering `screencap -d` needs the
  physical display id and that sleeping panels capture empty.
- **Resolved the TU6/TU7 worry** — DAORecomp's `tu6` config handles TU7, confirmed
  from upstream's README.
- **Awakening prospects improved.** DAORecomp supports Awakening as DLC on the TU
  build; the obstacle is only that it wants the Marketplace package rather than the
  retail disc. Q5 refined to a data-packaging question.
- Added `device/shots/` and `device/capture-*.txt` to `.gitignore` — device captures
  can show anything that was on screen.
- **Next:** Phase 1.1 — extract `default.xex`, apply TU7, run XenonRecomp.

### 2026-10-22 (session 1) — Planning and groundwork

- Researched the recomp ecosystem. Confirmed `96dot/DAORecomp` is **D3D12-only** and
  that the two "Android recomp" projects in the wild (`ForzaHorizon2Recomp-Android`,
  `nimauria/Xenon-Recomp`) are **scaffolds, not working ports**. Confirmed XenonRecomp
  handles ARM64 via **SIMD Everywhere**, and that **Xenia's Vulkan backend is at
  parity** with its D3D12 one.
- Verified both ISOs and both TU files by parsing XDVDFS and XEX2/STFS headers.
- Captured the Thor's hardware and display profile over adb.
- Found the glyph foothold: named Scaleform MovieClips plus a patchable GDA table.
- Wrote and tested `tools/dao_iso.py`, `tools/dao_erf.py`, `tools/verify_assets.py`.
- Took the planning decisions D1–D8 plus feature scope with the project owner.

---

## 10. Updating this file

At the end of each session:

1. Update the header table — date, phase, state, **next action**, blockers.
2. Tick the relevant checklist in `docs/PLAN.md`.
3. Add anything newly proven to §4 so it is never re-derived.
4. Add or resolve entries in §6.
5. Add a session-log entry in §9.
6. Commit with the status change in the same commit as the work.
