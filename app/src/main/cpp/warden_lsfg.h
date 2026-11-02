// SPDX-License-Identifier: GPL-3.0-or-later
// PLAN 9.14: frame generation with lsfg-vk's backend (Lossless Scaling's shaders, from the
// owner's own Lossless.dll) on the game's own Vulkan device - see warden_lsfg.cc.
#ifndef WARDEN_LSFG_H_
#define WARDEN_LSFG_H_

#include <cstdint>
#include <memory>
#include <string>

// The C API only: lsfg-vk names its own wrappers vk::Image, vk::Buffer..., which
// Vulkan-Hpp (Xenia's vulkan_api.h) also defines.
#include <vulkan/vulkan_core.h>

namespace warden::lsfg {

struct Setup {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
  // The queue of the graphics and compute family for the generator's own one-off
  // submissions (its images' first layouts, a dump) - not the emulator's: the graft makes
  // a second. The generation itself is recorded into the caller's command buffers.
  uint32_t queue_index = 1;
  std::string dll_path;
  uint32_t width = 0, height = 0;
  // Frames generated between each two of the game's (the multiplier less one).
  uint32_t count = 1;
  // The 16-bit shaders: where the device has shaderFloat16 enabled.
  bool fp16 = false;
  // lsfg-vk's flow scale (1 the full size; larger, coarser and faster) and its
  // performance mode.
  float flow = 1.0f;
  bool perf = false;
};

// A frame generator: two source images it alternates between and `count` images it
// generates between each two of its frames, recorded into the caller's command buffer.
// Destroyed only once the GPU is done with everything recorded.
class Generator {
 public:
  // nullptr, with the reason in *error, when it cannot be made.
  static std::unique_ptr<Generator> Create(const Setup& setup, std::string* error);
  virtual ~Generator() = default;

  // The image the next frame goes into (RGBA8, the size given, the general layout;
  // storage, sampled, transfer source and destination).
  virtual VkImage next_source() const = 0;
  virtual VkImageView next_source_view() const = 0;
  // The frames generated between the last two (as next_source()).
  virtual VkImage generated(uint32_t i) const = 0;
  virtual VkImageView generated_view(uint32_t i) const = 0;
  virtual uint32_t count() const = 0;
  // Into command_buffer (outside a render pass): the next frame taken from its source
  // image, and with `generate` the frames between it and the one before generated - only
  // when the frame before was taken just before it. Afterwards next_source() is the other
  // image. The caller makes its write of the source visible to compute shaders before
  // this, and waits for compute shaders after it before reading generated(i). Every
  // binding of compute pipelines and descriptor sets is changed. False, with *error, on
  // failure (nothing usable recorded).
  virtual bool Record(VkCommandBuffer command_buffer, bool generate, std::string* error) = 0;
  // Frames taken so far.
  virtual uint64_t frames() const = 0;
  // Once the GPU is done with everything recorded and nothing new is submitted: writes
  // generated frame 0 and the two frames it lies between as raw RGBA8 into dir
  // (lsfg-generated.raw, lsfg-source-before.raw, lsfg-source-after.raw).
  virtual bool Dump(const std::string& dir, std::string* error) = 0;
  // Whether the 16-bit shaders were loaded.
  virtual bool fp16() const = 0;
};

}  // namespace warden::lsfg

#endif  // WARDEN_LSFG_H_
