# Feature matrix

Every feature in scope, where it landed, and its state at **0.1.0-alpha, "Open Alpha"**
(2026-11-02). The task numbers are [`PLAN.md`](PLAN.md)'s, where each one's detail and
measurements are. What each setting does, for a player, is the README's settings
reference.

Status values: `done` · `partial` · `deferred` (by the owner) · `not done` · `dropped`

---

## Ported from the PC recomp

| Feature | Task | Default | Status | Notes |
|---|---|---|---|---|
| Frame rate 30, 45, 60, 120 | 9.1 | 60 | **done** | The GPU is the limit: the 118 areas at about 40-70 uncapped with FSR 3.1 Performance (`AREAS.md`) |
| Rendering resolution | 9.16 | 1280x720 | **done** | 960x540 to 2560x1440, from the next start; set aside while FSR 3.1 is on |
| Field of view, in play and in conversations | 9.17 | 91° | **done** | 80°-120°, live |
| HDR10 output | 9.2 | off (D8) | **done** | Where the display takes HDR10: the 10-bit swapchain's buffers tagged BT.2020 PQ (no PQ swapchain on the Thor, BUG-025); paper white 150-300 nits |
| Wide-gamut colour | 9.3 | Accurate | **done** | 10-bit always; *Vivid* stretches the game's colours to Display-P3 |
| FSR 3.1 upscaling | 9.4 | off (D7) | **done** | Quality, Balanced, Performance, Ultra performance, with the camera's motion vectors (Q7); its sharpening a switch |
| Frame generation | 9.14 | off | **done** | Lossless 2x in three modes (lsfg-vk v2.0.0-dev with the shaders from the player's own `Lossless.dll`) and FSR 3.1 2x |
| FSR 1, CAS, FXAA | 9.5, 9.7 | Bilinear | **done** | The *Upscaling* choices for the game's own size |
| Motion blur, depth of field in conversations | 9.18, 9.19 | off | **done** | Upstream's passes, ported; they need FSR 3.1's motion and depth |
| Photo mode | 9.20 | - | **done** | L3 + R3 or the panel's row; photos to `Download/Dragon Age Origins/` |
| 16× anisotropic filtering | 9.6 | 16x | **done** | Free at the King's Camp |
| Intro logos skipped | 9.11 | shown | **done** | |
| Headphone surround, 5.1 → stereo | 9.12 | stereo | **done** | And a *Volume boost* for the handheld's speakers (session 41) |
| Autosave when put away | 9.13 | off | **done** | The game's own autosave (about 2.5 s) before it holds; finished before Android suspends the app (BUG-100) |
| PC mods, the converter, add-ins | 10.12, 10.13, 10.19 | - | **done** | Mods from .zip, .7z, .rar, .dazip or a folder; PC textures, GFF files and add-ins converted in the app; add-ins installed as DLC |
| Cheats | 10.14 | - | **done** | The developers' own scripts: immortal party, skill checks, kill hostiles, gold, experience |
| Save restore page | 10.15 | - | **done** | Saves the game wrote over, kept and put back |
| Language | 10.16 | English | **done** | Spanish and Italian text (English voices) |
| Shader math *Fast* | 10.9 | Exact | **done** | The GPU's own multiplication: faster, not exact (an option, as fidelity trades are) |
| Lossless Scaling's LS1 upscaler | 9.15 | - | **deferred** | Looked into, not started |
| Asset override folder for texture packs | 9.9 | - | **partial** | Mods (10.12) and the converter (10.13) cover PC texture packs; 9.9 itself is deferred |
| Bicubic texture filtering | 9.6 | - | not done | Four fetches for each of the game's, on a GPU already at its limit |
| MSAA | 9.7 | - | not offered | The game's own 2x MSAA: 20.2 fps at the King's Camp against 30 |
| SMAA | 9.7 | - | FXAA instead | |
| Shadow resolution, draw distance | 9.8 | - | not done | No engine setting for either; *Dynamic shadows* and *Post-processing* switches instead |
| Tactical camera | 9.10 | - | **dropped** | Q2: inert in the 360 build |

### Deliberately not ported

| Feature | Why |
|---|---|
| FSR 4 | AMD RDNA-specific |
| DLSS 4.5 | NVIDIA only |
| XeSS-SR | Intel only |
| AMD Anti-Lag 2 / NVIDIA Reflex | Desktop-driver features with no Android equivalent |
| Neural rendering (RDNA 4) | Desktop GPU only |
| 240 fps / unlimited | Meaningless on a 120 Hz panel |
| The update checker | No online functionality in the app |

---

## Android platform features

| Feature | Task | Status | Notes |
|---|---|---|---|
| Frame pacing | 4.7 | **done** | On by default; frames held to the panel's even cadence |
| Thread affinity | 4.8 | **done** | The game's thread pinned to the prime core (Cortex-X3) |
| AAudio sound | 4.5 | **done** | The game's XMA decoded with FFmpeg |
| The file picker (Storage Access Framework) | 4.3 | **done** | The launcher's four file rows; no raw storage paths |
| Put away and back | 4.9 | **done** | Home, another app, the lid or the power button: the game holds, its clocks stopped, both screens go home together (BUG-097) |
| Device loss | 4.9 | partial | A lost GPU ends the process cleanly; recovering in place is out of reach in Xenia's design |
| Pipeline cache | 10.3 | **done** | Pipelines kept between sessions: an area seen before loads without compile stalls |
| Heat guard | 10.2 | **done** | Off by default (the owner's rule: automatic scaling is opt-in) |
| How the last session ended | 10.7 | **done** | On the launcher, with reports kept |
| On-screen controller | 10.17 | **done** | Off by default |
| The launcher and the panel by touch | 10.18 | **done** | Tap a row, then the side of its value; three fingers open the panel |
| Debugging Mode | 11.1 | **done** | One .zip in Downloads: logs, saves, a screenshot, how runs ended |
| Saves out and in | 11.2 | **done** | Export to a .zip, import with Keep both / Replace / Skip |
| Naming a manual save | 10.20 | **done** | The system keyboard after each manual save |
| About and licences | 11.5 | **done** | The version, what the app is, every project built in with its licence |

---

## Project-specific features

The three things this port exists for.

### Glyph remapping - Phase 7

Full design in [`GLYPHS.md`](GLYPHS.md).

| Item | Status | Notes |
|---|---|---|
| Xbox glyph set | **done** | Default (D4); the game's own |
| Nintendo glyph set | **done** | A↔B and X↔Y, no new art |
| PlayStation glyph set | **done** | The game's own PS3 art, shipped in the 360's shared atlas |
| Atlas cell redirection (Layer 1) | **done** | The atlas descriptions, through the title's patch folder |
| Text prompt remap (Layer 2) | not needed | The text's tokens are images of the same names |
| GDA table override (Layer 3) | not needed | `X360_ControllerInteraction.GDA` holds interaction flags, not prompts |
| Switching while the game runs | not done | From the next start; a live rewrite crashed the next load (BUG-061) |

### Second-screen deck - Phase 8

Full design in [`SECOND-SCREEN.md`](SECOND-SCREEN.md). Read-only (D6) but for the
quickbar's taps.

| Tab | Status | Notes |
|---|---|---|
| Party | **done** | Each member's face (read back from the GPU, 8.9), name, class, health, mana or stamina, level-up and conditions |
| Quickbar | **done** | 8.8: the selected character's slots; a tap uses one, as its button would |
| Quests | **done** | The journal's quests and objectives, the followed one first |
| Map | **done** | The area's markers and the party, north up, fitted to the screen; pan, pinch and the world map not done |
| Tactics | **done** | Each member's slots: condition, command, target |
| Codex | **done** | Entries found, read by a tap; the journal's conversations |
| Items | **done** | What each member wears, the backpack, the purse |
| Perf | **done** | 8.6, 8.10: with *Performance tab/overlay* on; an overlay on the main screen when the second screen is off |
| DAO-authentic styling | **done** | 8.4: the game's own typefaces and HUD art, made on the device |
| Its own process | **done** | 8.7: a fault in the deck never stops the game |

---

## The settings surface

**Built (10.1, then every phase since): the options panel** - the launcher before the
game, Select + Start (or three fingers) during play, with the game held while it is open.
Every setting is listed with each choice in the README's settings reference, which is
generated from `Options.java`; those marked \* apply from the next start.
