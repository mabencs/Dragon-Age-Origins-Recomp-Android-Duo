# The Vulkan graft (Phase 3.1)

**Done 2026-10-22. The Vulkan backend compiles for `aarch64-linux-android33`: 30 of 30
translation units, and all 121 of Xenia's internal shaders build to SPIR-V.**

DAORecomp vendors a stripped Xenia subset containing the Xenos frontend and *only* the
Direct3D 12 backend. This replaces that backend with Xenia's Vulkan one.

---

## 1. How it is put together

Nothing from upstream is committed. `tools/assemble_runtime.py` builds the tree from
three inputs, so our real diff stays legible instead of being buried in 60,000 lines of
copied third-party code:

```
  DAORecomp's vendored Xenia subset  (665 files)   the Xenos frontend
+ Xenia Canary's Vulkan files         (72 files)   added verbatim
+ 17 edits                                         our actual changes
= build/runtime-tree/src              (737 files)
```

The build order matters, and the assembler says so when it finishes:

```bash
python tools/assemble_runtime.py          # assemble (wipes the tree)
python tools/build_xenia_shaders.py       # generate SPIR-V headers
python tools/compile_runtime.py --target android
```

The middle step is not optional: six backend files `#include` shader bytecode headers
that do not exist in either upstream.

### Why edits rather than patch files

Upstream is pinned, so the edits are stable. Expressing them as explicit find/replace
pairs with assertions means that if a pin is moved and the surrounding code has
changed, assembly **fails loudly and names the edit**, instead of a patch applying at
the wrong offset or being silently skipped. `--check` verifies all 17 without writing
anything.

---

## 2. Sizing it first

`tools/graft_inventory.py` walks the include graph before anything is copied. The first
answer was alarming — 74,219 lines, reaching into `xenia/kernel`, `xenia/cpu`,
`xenia/vfs`, `xenia/apu`, `xenia/hid`, i.e. the whole emulator.

Tracing *which* include edges caused that turned out to be the important step. There
were only six, almost all in a single file:

```
 82 headers  <-  vulkan_command_processor.cc includes xenia/kernel/kernel_state.h
 34 headers  <-  vulkan_command_processor.cc includes xenia/apu/audio_system.h
 30 headers  <-  vulkan_command_processor.cc includes xenia/emulator.h
 10 headers  <-  ui/imgui_drawer.h           includes xenia/hid/input_system.h
  5 headers  <-  vulkan_command_processor.cc includes xenia/kernel/user_module.h
```

With those cut, the graft is **58 source files that need no supporting headers at all**
— every one of the 28 headers it reaches that DAORecomp lacks is itself inside the
graft set. The `imgui_drawer` edge came only from `vulkan_window_demo.cc`, a demo we
do not build.

---

## 3. What the 17 edits do

### Severing the emulator (11 edits)

DAORecomp had already solved this for D3D12, and the answer is elegant: they replaced
three emulator includes with **`xenia/dao_host.h`**, a 28-line interface of four
functions — `AttachGuestThread`, `DispatchInterrupt`, `TitleId`, `ShortSleep`. The GPU
code needs nothing else from the emulator.

Applying the same substitution to Vulkan needed less than expected, because the
emulator usage is confined to two places: a constructor parameter and a cosmetic
window title.

| File | Change |
|---|---|
| `vulkan_command_processor.h` | drop `kernel_state.h`; constructor takes `void* unused` |
| `vulkan_command_processor.cc` | drop `audio_system.h`, `kernel_state.h`, `emulator.h`, `user_module.h`; add `dao_host.h` and `dao_frame_trace.h`; constructor no longer forwards a `KernelState`; window title no longer names the audio system |
| `vulkan_graphics_system.h/.cc` | `Setup` takes `Memory*` instead of `cpu::Processor*` + `kernel::KernelState*`, matching DAORecomp's base class |

`dao_frame_trace.h` has to come in because DAORecomp's
`pm4_command_processor_implement.h` calls `dao_trace`. It lives under `gpu/d3d12/` but
its contents are backend-agnostic — it includes only `base/logging`, `register_file`,
`registers`, `texture_info` and `xenos` — so including it from the Vulkan backend is
correct, if misleadingly located.

### Matching DAORecomp's modified Presenter (4 edits)

The Q1 probe measured `ui/presenter.h` at 11.5% divergence from upstream, and this is
what that divergence is. DAORecomp added two parameters for its own features:

- `CaptureGuestOutput(RawImage&, bool dao_sdr)` — tonemap the game's HDR output to SDR
  when capturing a screenshot
- `RefreshGuestOutputImpl(..., bool& dao_generated_ref)` — whether a mailbox slot also
  receives a generated frame

`VulkanPresenter` has to match the base class. Neither feature exists on this backend,
so both are handled honestly rather than faked: `dao_generated_ref` is set to `false`,
which is **correct** — there is no frame generation here, so no slot ever gets a
generated frame. `dao_sdr` is accepted and ignored, with a `TODO` naming the D3D12
implementation to follow.

### Two upstream bugs (2 edits)

Both only manifest outside the configuration their authors build.

**`vulkan_api.h` include order.** Xenia includes `vulkan_to_string.hpp` directly.
That header sets its include guard and then includes `vulkan.hpp`, which later tries to
include `vulkan_to_string.hpp` for its own `to_string()` calls — and is skipped,
because the guard is already set. `vulkan.hpp` then fails on an undeclared
`to_string`. Fixed by including `vulkan.hpp` first, which is Vulkan-Hpp's documented
order.

**`dao_resolve_direct.xesli` uses HLSL `saturate()`.** Every other Xenia shader uses
`saturate_xe`, the xesl abstraction that becomes `saturate()` in HLSL and
`clamp(x, 0, 1)` in GLSL. DAORecomp's own shader uses the raw HLSL form, which compiles
only in their Direct3D-only build. One word.

---

## 4. The shaders

Xenia's backend `#include`s its internal shaders as C headers of SPIR-V words — gamma
application, texture loads, host depth store, the presenter's output filters. Those
headers are **build artefacts**: upstream ships one placeholder, and DAORecomp ships
126 pre-generated *DXBC* ones because it is a Direct3D project. Nothing generated the
SPIR-V equivalents.

DAORecomp does vendor Xenia's `tools_build/compile_shader_spirv.py`, which wants
glslangValidator, spirv-opt and spirv-dis from the Vulkan SDK. We have no Vulkan SDK —
but **the Android NDK ships `spirv-opt`, `spirv-dis` and `glslc`**, shaderc's glslang
frontend. `tools/build_xenia_shaders.py` is that pipeline with glslc substituted, which
only changes how the stage and target are named.

**121 of 121 shaders compile.** Getting there needed three fixes:

- `spirv-opt --canonicalize-ids` does not exist in the NDK's build. It only renumbers
  result ids for reproducible output, so dropping it changes nothing.
- For Vulkan, take the `.glsl` variants rather than the `.hlsl` ones. Xenia ships both.
- Xenia's FidelityFX shaders include FFX by a path relative to the *repository root*
  (`../../../../third_party/FidelityFX-FSR/...`), which the compiler resolves from the
  including file, not from an `-I` path. The assembler links Canary's `third_party` in
  at the equivalent place.

The emitted header format matches upstream exactly — disassembly in an `#if 0` block,
then `const uint32_t <identifier>[]` — because the backend includes these by name and
indexes them as arrays.

---

## 5. Where it stands

```
Vulkan backend scope      30/30 translation units compile for aarch64
Xenia shaders            121/121 compile to SPIR-V
Android-selected build  114/114 translation units compile   (Phase 3.1c)
```

The 76 failures in the full tree are **not porting problems** — they are files that
should not be built for Android at all, and the build has no source selection yet:

| Cause | Files | Why |
|---|---:|---|
| `objbase.h` not found | 50 | Windows COM. The `*_win.cc` files and the entire **D3D12 backend**, which Android does not build. |
| `third_party/imgui` | 7 | Submodule not fetched; needed only for the overlay (Phase 10). |
| `io.h` | 2 | `console_win.cc`, `filesystem_win.cc` |
| `snappy-stubs-public.h` | 2 | snappy needs its generated header |
| `sys/sysctl.h` | 1 | `debugging_mac.cc` |
| `third_party/xbyak` | 1 | `platform_amd64.cc` — x86 only |
| `reinterpret_cast` rejected | 1 | `mutex.cc` — needs a look |

Choosing the right files per platform is Xenia's own build's job, and ours is Phase 3.2.

### Next

1. **3.2 Build system.** Per-platform source selection: exclude `*_win.cc`,
   `*_mac.cc`, `*_amd64.cc` and `gpu/d3d12/`, include `*_android.cc`/`*_linux.cc` and
   `gpu/vulkan/`. Then link, rather than only compile.
2. **3.2 Vulkan device bring-up** on Adreno 740, against the extension set the Phase 0
   probe already measured. Note Xenia's Vulkan loader already ships an
   `instance_khr_android_surface.inc` entry list.
3. **3.3 EDRAM** via `kHostRenderTargets` — the accuracy-critical work, and still the
   project's top risk (R1b / Q8), since Adreno 740 lacks
   `VK_EXT_fragment_shader_interlock`.

---

## 6. Phase 3.2 — Xenia's Vulkan code accepts the device

**Done 2026-10-22.** `tools/vkdevice/run.sh` builds, deploys and runs a probe that
calls Xenia's *own* initialisation path on the handheld —
`VulkanProvider::Create(with_gpu_emulation = true, with_presentation = false)`. That
first argument is the interesting one: it is the path that demands the features Xenos
emulation needs and **refuses the device if they are missing**.

```
Adreno (TM) 740   Vulkan 1.3.128   driver 0x802A4035   vendor 0x5143
queue families 2,  graphics+compute = family 0,  sparse binding = family 0

RESULT: Xenia accepted this device for Xenos GPU emulation.
```

Full report: [`../device/vulkan-device-bringup.txt`](../device/vulkan-device-bringup.txt).

Of the thirteen optional extensions Xenia probes for, it enabled ten. The three it did
not:

| | why |
|---|---|
| `VK_KHR_swapchain` | not requested — `with_presentation = false`, there is no surface yet |
| `VK_EXT_memory_budget` | absent on this driver; already known from the Phase 0 probe |
| `VK_EXT_conditional_rendering` | **new** — absent on Adreno 740. Xenia uses it as an optimisation, not a requirement |

Worth noting that **sparse binding resolved to the same queue family as
graphics+compute**, which matters because Xenia's shared-memory emulation depends on
sparse binding and the Phase 0 probe had only confirmed the feature bit, not that a
usable queue family existed.

### The host boundary this enumerated

Linking is what turned "DAORecomp implements some things outside the Xenia subset it
vendors" into an exact list. Eight symbols, in two groups, now stubbed in
`tools/vkdevice/host_stubs.cc`:

| symbol | what provides it | Phase 4 task |
|---|---|---|
| `Presenter::DaoKeepAcquired` `DaoStopPacer` `DaoQueueFramePaints` `DaoBeginUIThreadPaint` `DaoEndUIThreadPaint` | DAORecomp's frame pacer, built on Windows waitable timers | **Swappy** replaces it — task 4.7 |
| `Window::SetPresenter` `OnSurfaceChanged` `OnPaint` | Xenia's window layer, which needs ImGui | Android presents through a **SurfaceView** — task 4.2 |

Every stub **aborts** rather than returning a plausible value. The probe creates a
device and never presents, so none should be reached — and if one were, the probe's
result would be meaningless. A quiet `return false` there would be the difference
between "the device works" and "the device appears to work".

Four more symbols were resolved rather than stubbed, because the fix was to supply the
real thing: `fmt`'s `format.cc`, snappy (so GPU trace capture works — that is how
frames get compared against the reference build in 3.3), an stb implementation unit,
and snappy's generated `snappy-stubs-public.h`. `tools/prepare_deps.py` produces the
last two.

### What this does and does not prove

It proves Xenia's Vulkan device setup runs natively on Adreno 740 and that the driver
satisfies its GPU-emulation requirements. **It renders nothing.** No surface, no
swapchain, no command submission, no EDRAM. Those are 3.3 and Phase 4.

---

## 7. Reproducing

```bash
# one-time: fetch the Vulkan-related submodules (see THIRD_PARTY.md)
python tools/graft_inventory.py                  # size the graft
python tools/assemble_runtime.py --check         # verify every edit still applies
python tools/assemble_runtime.py                 # build the tree
python tools/build_xenia_shaders.py              # 121 SPIR-V headers
python tools/prepare_deps.py                     # snappy header, stb unit

python tools/compile_runtime.py --target android --scope vulkan   # the graft, 30 TUs
python tools/compile_runtime.py --target android --scope android  # selected build, 112
python tools/compile_runtime.py --target android --scope all       # no selection (diagnostic)

python tools/compile_runtime.py --target android --scope device    # device probe set
./tools/vkdevice/run.sh                          # build, deploy, run on the Thor
```

Order matters: assembling wipes the tree, so the shaders must be regenerated after it.
The assembler prints that reminder when it finishes.
