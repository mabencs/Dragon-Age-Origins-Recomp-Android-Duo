// PLAN 9.14: the image before the UI, from the upscaler (warden_upscale.cc, 9.4's capture)
// to frame generation (warden_frame_generation.cc), which makes its frames from it and puts
// the real frame's UI over them. The upscaler sets it at each swap - FSR 3.1 on, or only
// capturing for frame generation - and frame generation reads it in the same swap's
// refresh, before anything of the next frame is recorded.
#ifndef WARDEN_HUDLESS_H_
#define WARDEN_HUDLESS_H_

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace warden {

struct HudlessFrame {
  // This swap's: the image before the UI captured, and the views below in place.
  bool valid = false;
  // The image before the UI at the guest output's size (the render size, or FSR 3.1's
  // upscaled scene at the panel's): in the shader-read layout, sampled.
  VkImageView image = VK_NULL_HANDLE;
  uint32_t width = 0, height = 0;
  // The front buffer and the image before the UI at the render size - where they differ
  // is the UI - in the shader-read layout.
  VkImageView front_render = VK_NULL_HANDLE;
  VkImageView hudless_render = VK_NULL_HANDLE;
  uint32_t render_width = 0, render_height = 0;
  // FSR 3.1's frame generation (files/frame-generation "fsr3"): the frame between the last
  // swap's and this one's, before the UI, at the image's size - half floats, before the
  // gamma ramp, in the shader-read layout. None at a cut or the first frame after one.
  VkImageView generated = VK_NULL_HANDLE;
};

// The one the upscaler sets and frame generation reads (the GPU command processor's
// thread only).
HudlessFrame& CurrentHudlessFrame();

// Whether frame generation wants the image before the UI: the upscaler captures it then
// even with FSR 3.1 off.
bool FrameGenerationWantsHudless();

}  // namespace warden

#endif  // WARDEN_HUDLESS_H_
