// The GPU: Xenia's Vulkan graphics system driven by the title's own Direct3D. See
// warden_gpu.cc.
#ifndef WARDEN_GPU_H_
#define WARDEN_GPU_H_

#include <cstdint>
#include <string>

struct ANativeWindow;

namespace xe {
class Memory;
namespace ui {
class Presenter;
class WindowedAppContext;
}  // namespace ui
}  // namespace xe

namespace warden::gpu {

// Creates the graphics system - its Vulkan device, presenter and command processor
// thread - over the guest memory. Needs the guest heap (its threads attach as guest
// threads), so after the title is loaded and before its first instruction. False if
// Vulkan would not come up; the title then runs without a GPU, as before.
// capture_dir: where the status thread writes the presenter's guest output as PNG
// files (guest-output-N.png) while the title is being brought up, so that what the
// GPU produced can be seen apart from what the panel shows.
bool Start(xe::Memory& memory, xe::ui::WindowedAppContext* app_context,
           const std::string& capture_dir);
void Shutdown();
bool Active();

// The presenter the graphics system paints into; the window shows it. Null until Start.
xe::ui::Presenter* Presenter();

// The settings' Upscaling (PLAN 9.5) onto the graphics system's presenter: on the UI
// thread, once it presents the title.
void ApplyPaintSettings();

// PLAN 9.2, HDR10 (warden_gpu.cc). The window the presenter's swapchains are made on, for
// tagging their buffers (null while there is none); the display's peak in nits when it
// takes HDR10, 0 when it does not (GameActivity, from Display.HdrCapabilities); and an
// HDR setting changed - the paper white, or Vivid's primaries - so the presenter's final
// pass is made again with it.
void SetWindow(::ANativeWindow* window);
void SetDisplayHdrPeak(float nits);
void HdrSettingsChanged();

// The Vd* imports' side (warden_video.cc).
void InitializeRingBuffer(uint32_t physical_address, uint32_t size_log2);
void EnableReadPointerWriteBack(uint32_t address, uint32_t block_size_log2);
void SetInterruptCallback(uint32_t callback, uint32_t user_data);
// VdSwap: the front buffer's fetch constant and an XE_SWAP packet into the 64 words
// the title reserved at `buffer` in its ring.
void WriteSwapPacket(uint32_t buffer, uint32_t fetch_constant, uint32_t front_buffer_virtual,
                     uint32_t width, uint32_t height);

// A guest store into the GPU register window at 0x7FC80000, from the recompiled code's
// memory-mapped stores (PPC_MM_STORE_U32, hooked in app/src/main/cpp/CMakeLists.txt).
void WriteRegister(uint32_t address, uint32_t value);

}  // namespace warden::gpu

#endif  // WARDEN_GPU_H_
