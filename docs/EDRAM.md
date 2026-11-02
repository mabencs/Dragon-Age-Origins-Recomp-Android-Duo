# EDRAM emulation (Phase 3.3)

**Measured 2026-10-22. The top risk is materially smaller than the plan assumed —
though not retired, and for a reason worth being precise about.**

The Xbox 360 has 10 MB of embedded DRAM that render targets live in, with its own
tiling and resolve semantics and no modern equivalent. Emulating it is historically the
hardest part of Xenos emulation, and it was flagged as this project's top risk (R1b /
Q8) because Adreno 740 forces Xenia onto its less accurate path.

---

## 1. Why there was a risk

Xenia's render target cache has two strategies:

| path | how | needs |
|---|---|---|
| `kPixelShaderInterlock` | emulate EDRAM as a storage buffer with ordered fragment access | `VK_EXT_fragment_shader_interlock` |
| `kHostRenderTargets` | use ordinary host render targets, copying ("transferring ownership" of) tiles between them to emulate aliasing | nothing special |

Adreno 740 does not expose the interlock extension, which the Phase 0 probe already
established, so the second path is forced. Upstream's own comment on it is blunt:

> Approximate method using conventional host render targets and copying
> ("transferring ownership" of tiles) between render targets to support aliasing.
> **May be irreparably inaccurate, completely at the mercy of the host API's
> fixed-function output-merger, primarily because it has to perform blending** — and
> when using a different pixel format, it will behave differently (the most important
> factor here is the range — it's clamped for normalized formats, but not for
> floating-point ones).

Read carefully, that names a *mechanism*: the inaccuracy comes from **blending in a
substituted pixel format**. Which is measurable rather than something to speculate
about.

---

## 2. What the device actually reports

From `./tools/vkdevice/run.sh`, which mirrors the checks in
`vulkan_render_target_cache.cc`. Full output in
[`../device/vulkan-device-bringup.txt`](../device/vulkan-device-bringup.txt).

### The path, confirmed

```
fragmentShaderSampleInterlock            no
fragmentShaderPixelInterlock             no
accurate (kPixelShaderInterlock) path    unavailable
path in use                              kHostRenderTargets
```

### The three accuracy switches — all on the good side

These are the actual decisions `kHostRenderTargets` makes based on hardware, each a
place where it would otherwise degrade:

| switch | result | meaning |
|---|---|---|
| gamma targets blended as linear UNORM16 | **YES** | Xenos `k_8_8_8_8_GAMMA` targets blend in linear 16-bit, not 8-bit gamma space. This is the big one — it is precisely the "blending in a different pixel format" the warning is about. |
| depth format | **`D24_UNORM_S8_UINT`** | matches Xenos' 24-bit depth exactly; no fall back to `D32_SFLOAT_S8_UINT` and its different precision curve |
| float24 depth converted in pixel shader | **YES** | `sampleRateShading` is present, so conversion happens per-sample and antialiases under MSAA instead of at transfer time |

### Every render target format can blend

All nine formats Xenia maps Xenos render targets onto support
`SAMPLED_IMAGE | COLOR_ATTACHMENT | COLOR_ATTACHMENT_BLEND`:

```
R8G8B8A8_UNORM            k_8_8_8_8              yes
R16G16B16A16_UNORM        gamma blend            yes
A2B10G10R10_UNORM_PACK32  k_2_10_10_10           yes
R16G16B16A16_SFLOAT       k_16_16_16_16_FLOAT    yes
R16G16_SNORM              k_16_16                yes
R16G16B16A16_SNORM        k_16_16_16_16          yes
R16G16_SFLOAT             k_16_16_FLOAT          yes
R32_SFLOAT                k_32_FLOAT             yes
R32G32_SFLOAT             k_32_32_FLOAT          yes

0 of 9 render target formats cannot blend.
```

**No format substitution is needed.** Xenia maps every Xenos render target format to a
host format that the hardware can blend natively, which is the condition under which
the "at the mercy of the output-merger" concern would otherwise bite.

---

## 3. One finding worth keeping

Adreno 740 satisfies **every other requirement** of the accurate interlock path:

```
fragmentStoresAndAtomics                 yes
sampleRateShading                        yes
standardSampleLocations                  yes
maxPerStageDescriptorStorageBuffers      16777216
```

The *only* thing missing is `VK_EXT_fragment_shader_interlock` itself. So if a future
Qualcomm driver exposes it, the accurate path becomes available by flipping the
`render_target_path_vulkan` cvar to `fsi` — no other work. Worth re-probing after
driver updates.

(That storage-buffer limit is Adreno reporting an effectively unbounded value rather
than a real 16M; it is noted only because it looks like a bug and is not.)

---

## 4. What this does and does not prove

**Does:** none of the concrete degradations `kHostRenderTargets` can fall into are
active on this hardware. Every format blends natively, depth precision matches Xenos,
and gamma blending happens in linear 16-bit. The specific mechanism upstream warns
about — blending in a substituted format — does not apply here.

**Does not:** that the path is *accurate in practice*. Format capability is necessary,
not sufficient. `kHostRenderTargets` is still an approximation of a unified EDRAM
buffer using separate images, and its remaining inaccuracy is structural rather than
format-driven:

- **Tile ownership transfer.** Render targets that alias the same EDRAM range are
  separate host images, and data is copied between them when ownership moves. Copies
  happen at tile granularity and at defined boundaries, so a game that aliases in a way
  Xenia does not anticipate can read stale contents.
- **Resolve semantics.** Xenos resolves from EDRAM to main memory with its own format
  conversion and scaling rules.
- **MSAA interaction.** Sample layout and resolve behaviour differ from Xenos'.
- **Nothing has rendered yet.** No command has been submitted on this device.

Those cannot be measured by asking the driver about formats. They need frames.

---

## 5. How this changes the plan

Risk **R1b is downgraded from "top risk" to "moderate, and now specific"**. The
unknown is no longer "will the fallback path be usable at all" but "where does tile
ownership transfer diverge for this particular game" — a narrower question with a clear
test.

That test is unchanged and is the next real step: **render frames and compare them
against the Windows D3D12 reference build.** Two things already in place serve it:

1. **GPU trace capture works.** snappy is built and `trace_writer.cc` compiles, which
   is why it was worth resolving rather than excluding — Xenia traces are how frames
   get compared between backends.
2. **The x86-64 reference caveat.** A divide-by-zero crash on the host reference build
   is not necessarily a port bug (BUG-005): x86 raises #DE where PowerPC and ARM64
   both return 0.

### Still ahead before a frame exists

- Command submission needs a `VulkanCommandProcessor`, which needs the host services
  Phase 4 provides — `dao_host`, `xe::Memory`, and a real surface. The eight stubbed
  symbols in `tools/vkdevice/host_stubs.cc` are that boundary.
- Presentation needs a surface from an Android `SurfaceView` (task 4.2).

So Phase 3.3 cannot be finished before Phase 4 exists. What it *can* do — and has done
— is establish that the hardware does not force any of the known degradations.

---

## 6. Reproducing

```bash
./tools/vkdevice/run.sh
```

The EDRAM sections come after the device report. The checks mirror
`vulkan_render_target_cache.cc` rather than calling into it, because the render target
cache needs a command processor and the probe deliberately links only the device layer.
If upstream changes those checks, this probe must be updated to match — it is a mirror,
not a measurement of Xenia's own decision.
