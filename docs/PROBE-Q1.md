# Q1 risk probe — can Xenia's Vulkan backend carry this port?

**Run 2026-10-22, Phase 0.4. Verdict: GREEN, with one specific caveat.**

Open question Q1 was the project's single largest risk: DAORecomp's renderer is
Direct3D 12 only, and the whole plan rests on replacing that backend with Xenia's
Vulkan one. If the Vulkan backend turned out to be immature, or if Adreno 740 lacked
the features Xenos emulation needs, the plan would have changed materially.

This probe answers it two ways: by reading the code, and by asking the device.

---

## 1. Verdict

| Question | Answer |
|---|---|
| Does a complete Xenia Vulkan backend exist? | **Yes** — 1:1 file parity with D3D12, comparable size |
| Did DAORecomp fork the Xenos frontend? | **Barely** — 2.4% line divergence, 220 of 256 files byte-identical |
| Is DAORecomp's runtime tied to D3D12? | **Loosely** — 7 of 144 source files |
| Does Adreno 740 have the features Xenia needs? | **Yes** — every critical feature present |
| Is HDR10 output reachable? | **Yes** — both required extensions present |
| Any blocking gap? | **No blocker.** One accuracy caveat: see §5 |

**Proceed with decision [D1](DECISIONS.md#d1--native-vulkan-port) as planned.**

The work is larger than "swap a backend" but smaller than "write a renderer". The
honest shape: **inherit ~43k lines** of mature Vulkan code from upstream, **port ~1.9k
lines** of DAO frontend deltas, and **reimplement ~7.2k lines** of DAO-specific
rendering features that were written against D3D12.

---

## 2. What DAORecomp actually vendors

DAORecomp has **no git submodules** — Xenia is vendored in-tree as a stripped subset
(18 MB), containing only `base/`, `gpu/` and `ui/`.

```
DAORecomp/xenia/src/xenia/
├── base/
├── gpu/
│   ├── d3d12/              ← the ONLY backend present
│   ├── dxbc_shader_translator*  ← D3D12 shader path
│   ├── command_processor.*      ┐
│   ├── render_target_cache.*    │ backend-agnostic
│   ├── texture_cache.*          │ Xenos frontend
│   ├── shared_memory.*          │ (reusable as-is)
│   └── xenos.*, ucode.*, ...    ┘
├── ui/
├── dao_host.h
├── dao_live_settings.h
└── dao_temporal_upscaler.h
```

There is **no `gpu/vulkan/` directory**. The only Vulkan artefact is a lone
`placeholder_ps.h` under `gpu/shaders/bytecode/vulkan_spirv/`.

### Code inventory

| Component | Lines | Fate in this port |
|---|---:|---|
| Backend-agnostic Xenos frontend | 31,478 | **Keep as-is** |
| Upstream Xenia D3D12 backend | 24,804 | Replace with Vulkan backend |
| DXBC shader translator | 16,593 | Replace with SPIR-V translator |
| **DAO-specific code inside the D3D12 backend** | **7,178** | **Reimplement for Vulkan** |

That last row is the part most easily overlooked, and it is covered in §4.

---

## 3. What upstream Xenia Canary offers

Cloned `xenia-canary/xenia-canary` at `e6cd2e4`.

### The Vulkan backend is structurally complete

Every file in the D3D12 backend has a direct Vulkan counterpart:

| D3D12 | Vulkan |
|---|---|
| `d3d12_command_processor` | `vulkan_command_processor` |
| `d3d12_graphics_system` | `vulkan_graphics_system` |
| `d3d12_primitive_processor` | `vulkan_primitive_processor` |
| `d3d12_render_target_cache` | `vulkan_render_target_cache` |
| `d3d12_shared_memory` | `vulkan_shared_memory` |
| `d3d12_texture_cache` | `vulkan_texture_cache` |
| `d3d12_shader` | `vulkan_shader` |
| `d3d12_query_pool` | `vulkan_query_pool` |
| `deferred_command_list` | `deferred_command_buffer` |
| `pipeline_cache` | `vulkan_pipeline_cache` |

Sizes confirm maturity rather than a stub:

| | D3D12 | Vulkan |
|---|---:|---:|
| Backend | 24,804 | **26,506** |
| Shader translator | 16,593 (DXBC) | **16,643** (SPIR-V) |

The SPIR-V translator is a full sibling of the DXBC one, with the same
`_alu` / `_fetch` / `_memexport` split plus `_rb` (render backend).

`ui/vulkan/` additionally provides the whole presentation layer we need:
`vulkan_presenter`, `vulkan_device`, `vulkan_instance`, `vulkan_provider`,
`vulkan_immediate_drawer`, and `vulkan_mem_alloc` (VMA integration).

### DAORecomp barely touched the frontend

This was the finding that de-risked the plan most. Comparing the 256 backend-agnostic
frontend files DAORecomp vendors against the same files upstream
(`tools/` script: `probe_divergence.py`, kept in the scratchpad):

```
frontend files compared     256
byte-identical              220 of 256
vendored lines              79,960
differing lines              1,895  (2.4%)
```

And the divergence is concentrated exactly where it does not hurt us:

| File | Divergence | Nature |
|---|---:|---|
| `memory.h` | rewritten | DAO replaces Xenia's memory system — the recomp has flat guest memory |
| `ui/d3d12/d3d12_presenter.cc` | 12.6% | D3D12-specific, replaced wholesale anyway |
| `gpu/graphics_system.cc` | 46.4% | DAO integration hooks — **must port** |
| `ui/imgui_drawer.cc` | 19.9% | DAO overlay changes — **must port** |
| `ui/presenter.h` | 11.5% | Presentation hooks — **must port** |
| `gpu/command_processor.cc` | **5.3%** | GPU emulation core — barely touched |
| `gpu/texture_cache.cc` | **5.3%** | barely touched |
| `gpu/pm4_command_processor_implement.h` | **2.6%** | barely touched |

The GPU emulation core is close to stock. DAORecomp's changes live in the
*integration* and *presentation* layers, which is precisely the seam we were planning
to cut along.

The 123 "files absent upstream" are almost entirely the 126 pre-generated
`gpu/shaders/bytecode/d3d12_5_1/*.h` DXBC headers. These are **build artefacts**, not
hand-written code — Canary ships none of them and generates them at build time, so
their SPIR-V equivalents come from the build system (`tools_build`) rather than any
porting effort.

### DAORecomp's own runtime is loosely coupled

Of **144** source files in `DAORecomp/`, only **7** reference `d3d12`, `dxgi`, `ID3D12`
or `dxbc` — chiefly `gpu/xenia_host.cpp`, `gpu/frame_history.cpp`,
`mods/convert/texture.cpp` and `neural/lmxxf_runtime.cpp`. The API coupling is
contained inside the vendored backend, not spread through the runtime.

---

## 4. The part that is not a port but a rewrite

7,178 lines of DAO-specific code live **inside** `gpu/d3d12/`. This is where
DAORecomp's headline graphics features are implemented, and they are written directly
against D3D12 and vendor SDKs:

| File | Lines | What it is |
|---|---:|---|
| `dao_upscale.cc` + `dao_upscale_impl.h` | 2,396 | Upscaler framework |
| `dao_upscale_motion.cc` | 730 | **Motion vector generation** |
| `dao_gpu_timing.h` | 456 | GPU timing queries |
| `dao_upscale_fsr.cc` | 394 | FSR integration |
| `dao_upscale_neural.cc` | 365 | Neural upscaling (RDNA 4) |
| `dao_upscale_dlss.cc` | 359 | DLSS (NVIDIA Streamline) |
| `dao_frame_guard.cc/.h` | 441 | Frame pacing / latency |
| `dao_upscale_xess.cc` | 326 | XeSS (Intel) |
| `dao_upscale_hdr.cc` | 278 | **HDR output path** |
| `dao_frame_trace.h` | 237 | Frame tracing |
| `dao_command_processor.cc` | 230 | DAO command processor hooks |
| `dao_texture_cache.cc` | 226 | DAO texture hooks |
| `dao_render_target_cache.cc` | 196 | DAO render target hooks |
| `dao_motion_blur.cc` | 162 | Motion blur |

`dao_temporal_upscaler.h` is only a cvar enum (`kOff/kAuto/kFsr4/kFsr3/kXess/kDlss`);
its implementation is explicitly "defined in `gpu/d3d12/dao_upscale.cc`". So there is
**no API-agnostic upscaler abstraction to inherit** — the abstraction has to be built.

### Consequences for the feature plan

- **DLSS, XeSS and neural upscaling are dead** on this target regardless — they are
  vendor-specific. Already out of scope; now confirmed as unportable rather than
  merely unavailable.
- **HDR output** (`dao_upscale_hdr.cc`, 278 lines) must be reimplemented on Vulkan.
  Modest, and the device supports it (§5).
- **FSR** needs the framework plus the motion-vector generator (`dao_upscale_motion.cc`,
  730 lines) reimplemented. This substantially reinforces decision
  [D7](DECISIONS.md#d7--build-the-fsr-31-path-default-it-off) — defaulting FSR off is
  the right call, and it is a bigger job than "port FSR".
- **Frame pacing** (`dao_frame_guard`) is replaced by Swappy on Android, not ported.

---

## 5. What the device actually supports

Measured on the AYN Thor with `tools/vkprobe/` — a native arm64-v8a binary that loads
`libvulkan.so` and interrogates the driver. Full report:
[`../device/vulkan-capabilities.txt`](../device/vulkan-capabilities.txt).

```
Adreno (TM) 740 — integrated
Vulkan 1.3.128  (loader 1.3.0)
driver 0x802A4035, vendor 0x5143, device 0x43050A01
113 device extensions, 13 instance extensions
memory: 15,255 MB DEVICE_LOCAL + 256 MB DEVICE_LOCAL
```

### Every critical Xenia feature is present

| Feature | Why it matters | Status |
|---|---|---|
| `geometryShader` | Xenos rectangle/quad primitives are expanded with geometry shaders | **yes** |
| `tessellationShader` | Xenos patch primitives | **yes** |
| `sparseBinding` | Shared-memory emulation strategy | **yes** |
| `sparseResidencyBuffer` | ditto | **yes** |
| `fragmentStoresAndAtomics` | Render target / EDRAM paths | **yes** |
| `vertexPipelineStoresAndAtomics` | Memexport emulation | **yes** |
| `shaderStorageImageExtendedFormats` | Render target cache | **yes** |
| `shaderStorageImageWriteWithoutFormat` | Render target cache | **yes** |
| **`textureCompressionBC`** | **The game's DXT1/DXT5 textures load natively — no transcoding** | **yes** |
| `independentBlend`, `imageCubeArray`, `multiViewport` | Various | **yes** |
| `shaderClipDistance`, `shaderCullDistance`, `depthClamp` | Various | **yes** |

`textureCompressionBC` deserves emphasis: Dragon Age's UI atlases and textures are
DXT-compressed. Many mobile GPUs lack BC support and force runtime transcoding. This
one has it.

### HDR is reachable

| Extension | Status |
|---|---|
| `VK_EXT_swapchain_colorspace` (instance) | **yes** — lets us request an HDR10 PQ colour space |
| `VK_EXT_hdr_metadata` (device) | **yes** — lets us supply mastering luminance |

So decision [D8](DECISIONS.md#d8--build-hdr10-default-to-wide-gamut-sdr) is
implementable. The reason HDR still defaults *off* is the panel's 420-nit peak, not a
capability gap.

### Gaps, and whether they matter

| Missing | Impact |
|---|---|
| **`VK_EXT_fragment_shader_interlock`** | **The one that matters — see below** |
| `VK_EXT_external_memory_host` | Xenia can import host memory directly into the shared-memory buffer. Without it, the shared-memory path must copy. Affects Phase 3.7 strategy; not blocking. |
| `VK_EXT_memory_budget` | VMA loses budget tracking. Cosmetic; we can self-account. |
| `VK_KHR_shader_atomic_int64` | 64-bit atomics unavailable in shaders. Xenia has fallbacks. |
| `VK_EXT_swapchain_maintenance1` | Slightly clumsier swapchain recreation. Minor. |
| `shaderTessellationAndGeometryPointSize` | Can't set point size from geometry/tess stages. Minor for this game. |
| `VK_EXT_non_seamless_cube_map` | Cube map edge behaviour differs subtly from Xenos. Minor. |
| `VK_KHR_portability_subset` | Expected absent — that's a MoltenVK concept. Irrelevant. |

Also worth noting: `maxBoundDescriptorSets` is **7**, where desktop GPUs commonly
offer 8 or more. Xenia's Vulkan backend uses several descriptor sets, so this is worth
watching during Phase 3.4 rather than assuming headroom.

### The caveat: no fragment shader interlock

Xenia's render target cache — the component that emulates the Xbox 360's 10 MB of
embedded DRAM — has **two strategies**, and the enum documents the trade-off plainly:

```c
enum class Path {
  // Approximate method using conventional host render targets and copying
  // ("transferring ownership" of tiles) between render targets to support
  // aliasing.
  //
  // May be irreparably inaccurate, completely at the mercy of the host API's
  // fixed-function output-merger, primarily because it has to perform
  // blending - and when using a different pixel format, it will behave
  // differently ...
  kHostRenderTargets,
  kPixelShaderInterlock,
};
```

`kPixelShaderInterlock` is the accurate path, and it needs
`VK_EXT_fragment_shader_interlock`, which Adreno 740 does not expose.

**This is not a blocker.** The selection logic shows interlock is strictly opt-in:

```c
if (cvars::render_target_path_vulkan == "fsi") {
  path_ = Path::kPixelShaderInterlock;
} else {
  path_ = Path::kHostRenderTargets;
}
```

`kHostRenderTargets` is the **default**, and it is the path we will run on. The cost is
accuracy: blending and format-precision behaviour is delegated to fixed-function
hardware that does not match Xenos exactly. Expect possible artefacts in effects that
depend on blending precision or render-target aliasing.

This sharpens risk **R2** from a vague worry into a concrete, testable one, and it
sets the Phase 3 priority: **EDRAM emulation via `kHostRenderTargets` is the critical
path, and it must be validated against the Windows D3D12 build as a reference.** That
reference comparison is now the most valuable testing tool we have.

The mitigation already in the plan still applies and is worth restating: Xenia must be
correct for the entire Xbox 360 library, whereas we need **one game** to look right.
Where `kHostRenderTargets` proves inaccurate for Dragon Age specifically, we can
special-case it. Xenia cannot.

---

## 6. Revised Phase 3 shape

Based on the above, Phase 3 resolves into clearer sub-tasks:

1. **Graft the backend.** Take Canary's `gpu/vulkan/` + `spirv_shader_translator*` +
   `ui/vulkan/` onto DAORecomp's frontend. Mostly mechanical; the frontend is near-stock.
2. **Port the frontend deltas.** ~1,900 lines, concentrated in `graphics_system.cc`,
   `ui/presenter.h`, `ui/imgui_drawer.cc`, plus DAO's `memory.h` shim.
3. **Generate SPIR-V shader bytecode** through the build system rather than porting
   the 126 pre-generated DXBC headers.
4. **EDRAM via `kHostRenderTargets`** — the accuracy-critical work, validated against
   the D3D12 reference.
5. **Build a graphics-API-agnostic post-processing layer**, then reimplement HDR output
   on it. This is the abstraction DAORecomp never needed and we do.
6. **Defer FSR entirely** until the above is stable — it needs the motion-vector
   generator rebuilt, and it defaults off anyway.

---

## 7. Reproducing this probe

```bash
# device capability report (needs the Thor connected over adb)
./tools/vkprobe/run.sh

# code divergence measurement (needs both repos under third_party/)
python tools/probe_divergence.py
```

Pinned upstream commits are recorded in [`../THIRD_PARTY.md`](../THIRD_PARTY.md).
