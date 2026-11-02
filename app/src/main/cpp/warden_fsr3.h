// PLAN 9.4: FSR 3.1 - the FidelityFX SDK's upscaler on the game's Vulkan device. See
// warden_fsr3.cc.
#ifndef WARDEN_FSR3_H_
#define WARDEN_FSR3_H_

#include "xenia/ui/vulkan/vulkan_api.h"

namespace warden::fsr3 {

// Makes an FSR 3.1 upscaler context for the title's 1280x720 to the panel's 1920x1080 on
// the device and logs how it went: the SDK's Vulkan backend, its shaders and the device's
// features together, before the game's frames go through it (files/fsr3-probe).
bool Probe(VkPhysicalDevice physical_device, VkDevice device);

// Whether the next upscaler context made on an idle interface takes the SDK's 16-bit
// permutations (FFX_HALF): only where the device has shaderFloat16 and shaderInt16
// enabled (the graft enables them where supported). 32-bit until set.
void SetHalfPrecision(bool half);

// The size the game renders at, from files/fsr3 as the process starts: Quality
// 1280x720 (the game's own, 1.5x to 1920x1080), Balanced 1120x630 (1.71x), Performance
// 960x540 (2x) - widths in whole 80-pixel EDRAM tiles, 16:9. 1280x720 without FSR 3.1.
// The game's device is made at it (warden_game.cc), so a change is from the next start.
void RenderSize(uint32_t& width, uint32_t& height);

// PLAN 9.16: how many times the game's size the GPU renders every target at (Xenia's draw
// resolution scale) - 2 for the Rendering resolution option's 1600x900, 1920x1080 and
// 2560x1440 (the game's device at 800x450, 960x540 and 1280x720), else 1. The EDRAM is
// addressed at the game's size, where its layout fits; the pixels are the scaled size's.
uint32_t RenderScale();

}  // namespace warden::fsr3

#endif  // WARDEN_FSR3_H_
