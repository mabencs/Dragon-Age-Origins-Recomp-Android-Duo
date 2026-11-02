# AYN Thor — hardware profile

Captured from the device over adb on **2026-10-22**.
Everything here is measured, not from spec sheets. Re-capture with
`tools/capture_device.sh`.

## System

| | |
|---|---|
| Model | `AYN Thor` (manufacturer `AYN`) |
| Board / platform | `kalama` |
| SoC | `QCS8550` — Snapdragon 8 Gen 2 |
| Android | 13 (`ro.build.version.release`) |
| API level | **33** |
| ABI | **`arm64-v8a` only** (32-bit listed but irrelevant) |
| RAM | 15,621,188 kB ≈ **16 GB** |
| Storage | 942 GB total, **209 GB free** |

## CPU

8 cores, three clusters. Confirmed from `/proc/cpuinfo` CPU part IDs:

| Cores | Part | Model | Role |
|---|---|---|---|
| 7 | `0xd4e` | Cortex-X3 | Prime — pin the game's main thread here |
| 5–6 | `0xd47` | Cortex-A710 | Performance |
| 3–4 | `0xd4d` | Cortex-A715 | Performance |
| 0–2 | `0xd46` | Cortex-A510 | Efficiency — avoid for game threads |

The Xbox 360's Xenon was a 3.2 GHz in-order triple-core PowerPC. A static
recompilation removes interpretation overhead entirely, so the X3 prime core should
comfortably exceed original-hardware single-thread throughput. **Thread affinity
matters** — landing the main thread on an A510 will tank framerate.

## GPU

| | |
|---|---|
| GPU | Adreno 740 |
| Vulkan driver | `ro.hardware.vulkan=adreno`, package `com.qualcomm.qti.gpudrivers.kalama.api33` |
| OpenGL ES | 3.2 (`ro.opengles.version=196610`) |
| Available frequencies | 124.8 – 680 MHz (9 steps) |

Note: `persist.sys.force_sw_gles=1` is set on this device. It appears to be a vendor
leftover and should not affect a Vulkan path, but if GLES is ever used as a fallback,
check this first.

## Displays

Two internal displays, **both in display group 0**. Same group means the second screen
is reachable with `ActivityOptions.setLaunchDisplayId` — this is the supported route on
API 33, since `Presentation` is restricted on internal displays. Display 4 does also
carry `FLAG_PRESENTATION`, so both approaches are worth testing (open question Q6).

### Display 0 — main screen

| | |
|---|---|
| Name | `Built-in Screen` |
| Unique ID | `local:4630946441858561667` (port 131) |
| Native panel | 1080 × 1920 portrait |
| **In landscape use** | **1920 × 1080** |
| Refresh rates | 60.000 Hz, **120.000 Hz** (currently 120) |
| Density | 369 dpi (318.976 × 320.842 actual) |
| Colour modes | `DEFAULT(0)`, `SRGB(7)`, `DISPLAY_P3(9)` — **currently P3** |
| HDR types | `HDR10(2)`, `HLG(3)` — no Dolby Vision, no HDR10+ |
| **Peak luminance** | **420 nits** |
| Average luminance | 210.16 nits |
| Min luminance | 0.323 nits |
| Flags | `FLAG_SECURE`, `FLAG_SUPPORTS_PROTECTED_BUFFERS`, `FLAG_TRUSTED` |

### Display 4 — second screen

| | |
|---|---|
| Name | `Screen-2` |
| Unique ID | `local:4630946482288158084` (port 132) |
| Native panel | 1080 × 1240 portrait |
| **In landscape use** | **1240 × 1080** |
| Refresh rates | 60.000 Hz, **120.000 Hz** (currently 120) |
| Density | 369 dpi |
| Colour modes | `DEFAULT(0)`, `DISPLAY_P3(9)` — **currently P3**, no plain SRGB mode |
| HDR types | `HDR10(2)`, `HLG(3)`, 500 nits peak and average |
| **Touch** | **Yes** — `touch EXTERNAL`, so it accepts touch input |
| Layer stack | 4 |
| Flags | adds **`FLAG_PRESENTATION`** on top of display 0's flags |

**Design target for the companion deck: 1240 × 1080 landscape at 369 dpi.** That is an
unusual 1.148:1 aspect — close to square. Lay the deck out responsively rather than
pinning to that exact ratio, and verify orientation on-device early, because the
reported rotation flips between the base and override display info.

## Controller

Measured in Phase 4.6 by enumerating `InputDevice` from inside the app
(`tools/appbringup/run.sh`).

| | |
|---|---|
| Name | `Odin Controller` |
| Vendor id | `0x2020` |
| Product id | `0x0111` |
| Descriptor | `8e1073ea5832500672194344d81498833991c43c` |
| Analog triggers | **yes** |
| Hat axes | **yes** |

The built-in pad identifies as an *Odin* controller - AYN's earlier handheld - rather
than as a Thor. Harmless, but it means **glyph detection must key on vendor/product,
not on the product name**, and that a name-based lookup table would miss this device.

The descriptor is the stable identifier: it survives reconnection, where the device id
does not. Per-controller settings should key on it.

Analog triggers being present matters for the game: Dragon Age uses the triggers for
camera and radial menus, and a pad that reported them as buttons only would lose that
nuance. Hat axes being present means the d-pad arrives as `AXIS_HAT_X` / `AXIS_HAT_Y`
as well as key events, which the input layer handles in both places.

## CPU pinning

`sched_setaffinity` is **accepted** on this device for app threads - checked by pinning
a thread to the prime core and reading the mask back (Phase 4.8). Some vendors refuse
it silently, which would have shown up only as an unexplained slow game later.

Clusters are detected from `cpuinfo_max_freq` rather than hardcoded, and the
frequencies confirm the `/proc/cpuinfo` part IDs above:

| Cores | Max frequency | Cluster |
|---|---|---|
| 7 | 3,187,200 kHz | prime |
| 3-6 | 2,803,200 kHz | performance |
| 0-2 | 2,016,000 kHz | efficiency |

## HDR assessment

The main panel advertises HDR10 but tops out at **420 nits peak / 210 nits average**.
That is well below the ~600–1000 nits where HDR starts to clearly beat a good SDR
tonemap. Compounding this, there is a documented Adreno 740 issue where HDR swapchains
are created as `VK_FORMAT_A2B10G10R10_UNORM_PACK32` with
`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` instead of a true PQ
`VK_COLOR_SPACE_HDR10_ST2084_EXT` surface.

Hence decision **D8**: build the HDR10 path and make it switchable, but default to
wide-gamut Display-P3 SDR, which is what this panel is genuinely good at and is already
its active colour mode. When HDR *is* enabled, drive
`VK_COLOR_SPACE_HDR10_ST2084_EXT` with `VK_EXT_hdr_metadata`, feeding peak and average
values straight from `Display.HdrCapabilities` rather than hardcoding them.

## Implications for the port

- **arm64-v8a only.** No x86 fallback; the recompiled output must build clean for ARM64
  via XenonRecomp's SIMD Everywhere path.
- **120 Hz panel makes frame pacing mandatory.** A 60 fps game presented without
  Swappy on a 120 Hz panel judders visibly. This is correctness, not polish.
- **16 GB RAM is a luxury** against the 360's 512 MB. Raising texture, shadow and
  draw-distance budgets is safe from a memory standpoint.
- **209 GB free** comfortably fits extracted game data (~20 GB).
- **Thermals are the real constraint.** A handheld SoC sustains far less than it peaks;
  thermal-aware scaling is in scope for exactly this reason.
- **Target API 33**, and remember `Presentation` restrictions on internal displays.
