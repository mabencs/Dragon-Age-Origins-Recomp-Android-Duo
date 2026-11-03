# Decision log

Decisions taken, with the reasoning behind them. Recorded so that a future session —
possibly on another machine, possibly months later — doesn't relitigate settled
questions or quietly contradict them.

D1-D9 were taken **2026-10-22**; D10-D14, for the public releases, on **2026-11-02**.

---

## D1 — Native Vulkan port

**Decision:** Fork DAORecomp. Keep its recompiler configuration and game patch set.
Replace the Xenia-derived Direct3D 12 backend with Xenia's **Vulkan** backend. Compile
the recompiled PowerPC output for `arm64-v8a`.

**Context:** DAORecomp is D3D12-only. Android has no D3D12. Three routes were considered:

| Route | Verdict |
|---|---|
| Native Vulkan port | **Chosen** |
| D3D12→Vulkan shim (vkd3d-proton style) | Rejected |
| Validate under x86 emulation first | Not chosen |

**Why:** Only the native route reaches the project's actual goals. 60/120 fps, HDR
output and FSR all require real control of the presentation path; a translation shim
taxes performance permanently and vkd3d-proton's deep Wine coupling makes it a likely
dead end. Two facts make the native route viable rather than speculative:

- XenonRecomp already routes ARM64 through **SIMD Everywhere**, so the PowerPC→C++
  output is expected to compile for ARM64 without a new code generator.
- **Xenia's Vulkan backend is now broadly at parity** with its D3D12 backend, and Xenia
  Edge is actively prioritising it. We are swapping a backend, not writing one.

**Cost:** This is the bulk of the project. Phase 3 is measured in months.

**VALIDATED 2026-10-22.** The Q1 probe ran in Phase 0.4 and came back green — full
writeup in [`PROBE-Q1.md`](PROBE-Q1.md). Xenia Canary's Vulkan backend has 1:1 file
parity with its D3D12 backend at comparable size; DAORecomp diverged from the
backend-agnostic Xenos frontend by only **2.4%**; and Adreno 740 exposes every feature
Xenia's Xenos emulation depends on, including `geometryShader`, `tessellationShader`,
`sparseBinding` and `textureCompressionBC`. Risk R1 is retired.

Two things the probe changed:

- **New top risk (R1b / Q8).** Adreno 740 lacks `VK_EXT_fragment_shader_interlock`, so
  Xenia's accurate EDRAM path is unavailable and we run `kHostRenderTargets`, which
  upstream documents as possibly "irreparably inaccurate" for blending and format
  precision. This is now the critical accuracy work of Phase 3, to be validated against
  the Windows D3D12 build as a reference renderer.
- **One cost was underestimated (R1c).** 7,178 lines of DAO-specific rendering code —
  the upscaler framework, motion-vector generation, HDR output, motion blur, frame
  pacing — live *inside* `gpu/d3d12/` with no API-agnostic abstraction to inherit.
  That has to be **reimplemented**, not ported, and the abstraction built from scratch.

**Note on the emulation probe:** running the existing Windows x86-64 build under
FEX/Box64 + Wine + vkd3d was offered as a cheap feasibility check and not chosen. It
remains available if motivation ever needs a quick proof the game is fun on the device.

---

## D2 — Base game plus TU7 only; Awakening deferred

**Superseded in part (2026-10-31, PLAN 10.10):** Awakening and every DLC are installed
from the Ultimate Edition's disc 2 as the game's own downloadable content, on the same
TU7 build. The base game and TU7 as the build target stand. **And (2026-11-02, PLAN
12.3):** disc 2's Title Update 6 is a target too - the game's code is made on the device
from whichever the player has, TU7 optional (D14).

**Decision:** Target the base game, media ID `186A1115`, patched to **Title Update 7**.
Archive the Awakening disc. Do not pursue it in this plan.

**Context:** Direct inspection of both discs:

| Disc | Title ID | Media ID | XEX version |
|---|---|---|---|
| Origins (World, En/Es/It) | `454108C0` | **`186A1115`** | 0.0.0.1 |
| Awakening (USA/Europe) | `454108C0` | **`2E6F7560`** | 0.0.0.9 |

**Why:** Same title ID, **different media ID and a different executable**. The
recompiler's configuration — function database, kernel import map, patch set — is built
against one specific executable. It will not match Awakening's. Supporting Awakening
properly means a second full recompilation: effectively doubling the project before the
first half works.

**Revisit later (Q5) — odds improved 2026-10-22.** Reading DAORecomp's own
documentation during Phase 0.4 confirmed that **Awakening is already supported**, as
DLC on the title-update build. Their README lists *Awakening*, *Leliana's Song*, *The
Darkspawn Chronicles*, *The Golems of Amgarrak* and *Witch Hunt* as needing TU6 or TU7,
with DLC packages extracted to `userdata/content/454108C0/00000002/` where the game
finds them. `config/DAO_tu6.toml`'s header states outright that the TU build is
*"Needed by Awakening and the DLC campaigns after it."*

The remaining obstacle is narrower than expected: the loader reads **Xbox Live packages
(LIVE/PIRS)** — the Marketplace download — whereas we have the **retail disc**, whose
content sits in `Addins/DAO_PRC_EP_1/`.

So Q5 is now a **data-packaging question, not a recompilation one**: can the disc's
addin content be repackaged into what the DLC loader expects? That is a much smaller
problem than a second port.

D2 still stands — do not chase this before Phase 6 passes — but it is now a plausible
follow-on rather than a probable dead end.

Also noted: Awakening's disc carries `packages/core/patch/` with overridable GDA tables
and an expanded `guiexport.erf` (22.3 MB vs the base game's 18.6 MB), both of which
informed D3.

---

## D3 — Relabel-only glyphs

**Decision:** Changing the glyph set changes only what is **drawn**. Button functions
are untouched. The bottom button always confirms, regardless of whether it draws as `A`,
`B` or ✕.

**Why:** All three controller families share the same four physical positions; only the
labels differ. So this is purely a presentation problem, and treating it as one keeps
muscle memory intact and makes it impossible for a prompt to disagree with what the game
actually does.

**Rejected:** relabel plus a confirm/cancel swap to Nintendo convention, and relabel plus
full rebinding. Both add gameplay-affecting surface area for no benefit to the stated
goal.

**Enabler:** the mechanism is data-driven, not hardcoded — see
[`GLYPHS.md`](GLYPHS.md) §2 for the evidence (named Scaleform MovieClips per glyph,
XML atlas layouts, and a `X360_ControllerInteraction.GDA` table shipped in a folder
named `patch/`).

---

## D4 — Xbox default, Nintendo and PlayStation switchable

**Decision:** Ship with Xbox glyphs as the default. Provide Nintendo and PlayStation as
selectable options in settings.

**Why:** Xbox is the game's native art, so it is correct with no asset work and is the
safe fallback. Switching is a user-facing setting rather than something inferred.

**Note:** Nintendo reduces to an A↔B and X↔Y swap of cells that already exist in the
atlas, so it may need no new art at all. PlayStation needs genuine new symbol art.

**Not chosen:** automatic detection from the gamepad's vendor/product ID. It remains
available as an optional convenience later, but manual selection is the requirement and
the default stays Xbox.

---

## D5 — Second-screen tabs

**Decision:** Six tabs — **Party, Quests, Map, Tactics, Codex/Journal,
Inventory/Equipment.** Plus a performance overlay as a developer tab.

**Why:** Party, Quests and Map were the original request. Tactics, Codex and Inventory
were added because all three are screens the console UI handles awkwardly and a touch
second screen handles well. All six draw on data the game already assembles for its own
screens, so the incremental cost per tab is mostly presentation.

---

## D6 — Read-only mirror first

**Decision:** Phase 8 ships a read-only deck. No writes into game state. Touch
interaction is added afterwards, per tab.

**Why:** Every write path into a recompiled game's state is a potential crash or save
corruption. Mirroring data the game already pushes to its own HUD is safe by
construction and still delivers nearly all the value. Tactics and Inventory are
therefore read-only views in Phase 8 despite being interactive screens in the game.

**The one sanctioned write path:** the touch quickbar (task 8.8), added after the mirror
is proven. One well-tested write path, not six.

---

## D7 — Build the FSR 3.1 path, default it off

**Decision:** Implement full FSR 3.1 Vulkan integration. Ship with native 1080p
rendering as the default and FSR 3.1 as an option.

**Why:** Keeps feature parity with the PC project while keeping the safe path as
default. Two reasons FSR 3.1 is unlikely to be the right default here:

- Adreno 740 can very probably drive this 2009 game at native 1920×1080, which makes
  upscaling unnecessary — and FSR's compute cost is not free on a mobile GPU.
- FSR 3.1 is a **temporal** upscaler requiring motion vectors. Dragon Age: Origins never
  produced them, so they must be reconstructed from the depth buffer and camera
  matrices (Q7), with real risk of temporal artefacts on a game never designed for it.

FSR 1 spatial scaling plus sharpening is the cheap fallback for heavy scenes.

---

## D8 — Build HDR10, default to wide-gamut SDR

**Decision:** Implement the HDR10 PQ output path and make it switchable. Default to
Display-P3 SDR presentation.

**Why:** The Thor's main panel reports HDR10 support but peaks at **420 nits**, with 210
nits average — well below where HDR clearly beats a good SDR tonemap. The panel is
already running in Display-P3 and is genuinely good at wide-gamut SDR. Additionally
there is a documented Adreno 740 issue where HDR swapchains come back as 10-bit sRGB
rather than true PQ.

So HDR is built and available to try, without shipping a default that may look washed
out. When enabled: `VK_COLOR_SPACE_HDR10_ST2084_EXT` with `VK_EXT_hdr_metadata`, driven
by luminance values read from `Display.HdrCapabilities` rather than hardcoded.

**Rejected:** dropping HDR entirely. It was explicitly requested and the path is worth
having.

**CONFIRMED IMPLEMENTABLE 2026-10-22.** The device probe found both required
extensions present on Adreno 740: `VK_EXT_swapchain_colorspace` (instance) for
requesting an HDR10 PQ colour space, and `VK_EXT_hdr_metadata` (device) for supplying
mastering luminance. HDR still defaults off because of the 420-nit panel, not any
capability gap.

Note the implementation cost: DAORecomp's HDR path (`dao_upscale_hdr.cc`, 278 lines)
is written against D3D12 and must be reimplemented on Vulkan.

---

## D9 — Pin Android NDK 28.2.13676358

**Decision:** Build with NDK **28.2.13676358** (Clang 19.0.1), pinned.

**Context:** The SDK repository offers NDK 27.x, 28.x, 29.x and 30.x. Phase 0's plan
required r27 or newer.

**Why this one:** Almost every 29.0.x and 30.0.x entry is a release candidate
(`-rc.1` … `-rc.4`). `28.2.13676358` is the newest *stable patch* release. For a
codebase consisting largely of machine-generated C++ — well over a hundred thousand
translated functions — compiler stability matters more than having the newest Clang.

**Trade-off accepted:** Clang 19 rather than the Clang 22 DAORecomp uses on the host,
and rather than the newer Clang in NDK 29/30. If ARM64 codegen quality or a missing
compiler feature becomes a problem in Phase 2, revisit — but start from the stable one.

**Verified:** the pinned NDK's `arm64-v8a` / API 33 sysroot and CMake toolchain file
are present, and a cross-compiled binary was executed successfully on the device.

---

## D10 — GPL-3.0-or-later

**Decision (the owner, 2026-11-02, PLAN 11.3):** the project's code is licensed
GPL-3.0-or-later (`LICENSE`), and the repository is public.

**Why:** lsfg-vk, compiled into the app for frame generation, is GPL-3.0 at the pinned
v2.0.0-dev (later lsfg-vk is CC BY-NC-ND, which forbids a port), so the app as a whole
is a GPL-3.0 work. Everything else built in allows that: MIT (DAORecomp, XenonRecomp, the
FidelityFX SDK, SIMD Everywhere), BSD (Xenia, libarchive), ISC, Apache-2.0, BSL-1.0,
LGPL-2.1 (FFmpeg, libmspack), the Unlicense and public domain (`THIRD_PARTY.md`).

**Consequence:** UnRAR, whose licence forbids using its code to make a RAR-compatible
archiver, was replaced by libarchive's RAR readers (BUG-094).

---

## D11 — Source only: each player builds their own app

**Superseded by D14 (2026-11-02, Phase 12 done):** the APK now holds no game code - the
app makes it on the player's device - so it is published; the reasoning below still holds
for any APK that does hold the game's code (a developer's own build with libdaogame.so
packed in would).

**Decision (2026-11-02):** releases are the source. No APK is published, and the docs
say a build is never shared.

**Why:** the APK's `libwarden.so` holds the game's program - its PowerPC code
translated to C++ and compiled, from the builder's own `default.xex` and title update.
Sharing an APK would distribute Electronic Arts' code. The game's data (art, sound,
text) is never in the APK either way: it is extracted on the device from the player's
own disc image. So the README walks a player through building from their own disc,
the About page says the same, and `tools/release.py` checks a release APK holds none of
the game's files.

---

## D12 — Package ids with no person's name

**Decision (the owner, 2026-11-02):** `dev.daorecompandroid.duo` for debug builds and
`prod.daorecompandroid.duo` for release builds; the Java package is
`dev.daorecompandroid.duo`.

**Why:** the first id held a person's name. The two ids are two apps, side by side on a
device, each with its own storage: a debug build never replaces a player's release
install, and the committed debug key (`docs/SIGNING.md`) can only ever update debug
builds.

---

## D13 — Version 0.1.0-alpha, "Open Alpha"

**Decision (the owner, 2026-11-02):** the first public release is 0.1.0-alpha, named
"Open Alpha".

**How:** `gradle.properties` holds `warden.version` and `warden.releaseName`; the
version code is computed from the numbers before the "-" (major × 1,000,000 + minor ×
1,000 + patch), so a later release always installs over an earlier one. The About page
shows both, with the commit.

---

## D16 — No replaceable Vulkan driver (2026-11-03)

The owner asked for the loading of a custom Vulkan driver (Mesa's Turnip builds, as
BOTWRecompAndroid and emulators offer), it was built and tested on the debug app, and then
dropped the same day: Turnip ran the game about 16 ms a frame slower than the Thor's system
driver at every resolution - it serializes between Xenia's ~57 render passes and ~115
barriers a frame, which no setting of ours changes (BUG-112) - and its FSR 3.1 backend did not
come up. So the app loads the system's driver only, and holds no driver loader or driver
packages (`*.adpkg.zip` is ignored by git). What the investigation left: the jitter fallback
when FSR 3.1 cannot be made, and the GPU status line's counts of waits, passes, barriers,
submits and presents.

## D15 — The launcher checks for updates (2026-11-02)

The app had no network permission and the README promised it never connected to anything.
The owner chose a real check at each launch, over opening the page by hand or a check on
request, with an install from the launcher and a flag for releases that must not be installed
that way. So the app holds INTERNET and REQUEST_INSTALL_PACKAGES, and `AppUpdate.java` is their
only user: the launcher only, never a start straight into the game, and the game's runtime stays
as a console with no cable. The README says what it does and what GitHub sees. It reads the
release list, as GitHub's "latest" leaves out pre-releases, and the newest release decides: a
manual one is what is shown even over an older one that could install, as that flag means the
update needs care. Only the published package installs itself (a debug build is another app),
after the size and SHA-256 match GitHub's, and Android's installer asks the player and refuses
another key or a lower version.

## D14 — The game's code made on the device; a published APK

**Decision (the owner, 2026-11-02, PLAN 12):** "I want to be able to publish an apk that
the end user just provides the iso files and lossless.dll for... a long compile on the
device is fine." The APK holds a compiler and the recompiler instead of the game's code;
the app translates the player's own executable (XenonRecomp) and compiles it (LLVM
19.1.7) on the device, once, after the discs are chosen. **D11's source-only release is
superseded** (Phase 12 done, 2026-11-02): the APK holds none of the game, so it can be
published, and a player needs no computer.

**Inputs (the owner):** the base game's disc image and the Ultimate Edition's disc 2,
which carries Title Update 6 (and the downloadable content); Title Update 7 is optional,
recommended.

**Measured before deciding the shape:** the whole game code compiles on the Thor in under
nine minutes (12.1), and the device's XenonRecomp output is byte for byte the computer's
(12.4).

---

## Feature scope

### In

Tactical camera unlock · touch quickbar / radial ability wheel · skip intros and faster
loading · Swappy frame pacing · thermal and battery aware scaling · Vulkan pipeline
cache pre-warm · 120 fps mode · performance overlay · 16× anisotropic and bicubic
filtering · MSAA/SMAA · shadow resolution and draw distance · asset override folder ·
in-game ImGui settings overlay · low-latency audio and headphone virtualization

### Out

| Feature | Why out |
|---|---|
| Mouse and keyboard support | Not wanted |
| PC save import/export | Not wanted; would need a 360↔PC save format converter |
| Local achievement tracker | Not wanted |
| Confirm/cancel swap | D3 |
| Automatic glyph detection | D4 |

See [`FEATURES.md`](FEATURES.md) for the implementation matrix.

---

## Standing notes

- **Nothing from Phases 7–10 starts before Phase 6 passes.** The features are the point
  of the project, but they are worthless on a game that doesn't run.
- **No game content, anywhere we publish.** The repository holds none (`.gitignore` is
  verified to exclude all of it, and the pre-commit check in `CLAUDE.md` runs before
  every commit), and only an APK that `tools/release.py` passes is published - it holds
  none of the game's code or files (D14; D11 before it published none).
- **`STATUS.md` is updated in the same commit as the work it describes.**
