// SPDX-License-Identifier: GPL-3.0-or-later
// PLAN 9.14: frame generation with lsfg-vk's backend on the game's own Vulkan device.
//
// lsfg-vk (third_party/lsfg-vk, v2.0.0-dev at 0213be9, by PancakeTAS and contributors,
// GPL-3.0 - THIRD_PARTY.md) runs Lossless Scaling's frame generation shaders, read at run
// time from the owner's own Lossless.dll, on a Vulkan device of its own, and trades frames
// with the application as external memory and a timeline semaphore passed as opaque file
// descriptors. The Adreno 740 exports semaphores only as sync fds, which carry binary
// semaphores only, so here the backend runs on Xenia's device instead. This file takes the
// place of the backend's src/lsfgvk.cpp: its context (ContextImpl) adapted to make its own
// source and destination images, and to record a generation into a command buffer of the
// caller's - the game's own, executed in its submissions on queue 0 - where lsfg-vk
// submitted its prepass and its passes on a queue of its own, ordered by a timeline
// semaphore (session 37: the Adreno time-slices its queues, so a queue of the generation's
// own bought nothing, and one command stream needs no handover between queues).
// lsfg-vk-common's externally managed device takes the graft's second queue for the
// one-off submissions and the 16-bit shaders (tools/prepare_lsfg.py). Like what it adapts,
// this file is GPL-3.0-or-later; the app is for the owner's own use and is not distributed.
#include "warden_lsfg.h"

#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

#include "extraction/dll_reader.hpp"
#include "extraction/shader_registry.hpp"
#include "helpers/limits.hpp"
#include "helpers/utils.hpp"
#include "lsfg-vk-common/helpers/errors.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/buffer.hpp"
#include "lsfg-vk-common/vulkan/command_buffer.hpp"
#include "lsfg-vk-common/vulkan/fence.hpp"
#include "lsfg-vk-common/vulkan/image.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"
#include "shaderchains/alpha0.hpp"
#include "shaderchains/alpha1.hpp"
#include "shaderchains/beta0.hpp"
#include "shaderchains/beta1.hpp"
#include "shaderchains/delta0.hpp"
#include "shaderchains/delta1.hpp"
#include "shaderchains/gamma0.hpp"
#include "shaderchains/gamma1.hpp"
#include "shaderchains/generate.hpp"
#include "shaderchains/mipmaps.hpp"

namespace warden::lsfg {

namespace {

using namespace lsfgvk;
using namespace lsfgvk::backend;

// lsfg-vk's device function table, loaded here: its own loader requires the timeline
// functions by their KHR names and the fd functions, which Xenia's device - Vulkan 1.2's
// timeline semaphores in core, no external fds - does not offer.
bool LoadDeviceFuncs(const vk::VulkanInstanceFuncs& fi, VkDevice device,
                     vk::VulkanDeviceFuncs& df, std::string* error) {
  bool ok = true;
  const auto load = [&](const char* name, bool required) {
    PFN_vkVoidFunction f = fi.GetDeviceProcAddr(device, name);
    if (!f && required) {
      *error = std::string("no ") + name;
      ok = false;
    }
    return f;
  };
#define WARDEN_LSFG_FUNC(field, name) \
  df.field = reinterpret_cast<PFN_vk##field>(load(name, true));
#define WARDEN_LSFG_CORE(field) WARDEN_LSFG_FUNC(field, "vk" #field)
  WARDEN_LSFG_CORE(GetDeviceQueue)
  WARDEN_LSFG_CORE(DeviceWaitIdle)
  WARDEN_LSFG_CORE(CreateCommandPool)
  WARDEN_LSFG_CORE(DestroyCommandPool)
  WARDEN_LSFG_CORE(CreateDescriptorPool)
  WARDEN_LSFG_CORE(DestroyDescriptorPool)
  WARDEN_LSFG_CORE(CreateBuffer)
  WARDEN_LSFG_CORE(DestroyBuffer)
  WARDEN_LSFG_CORE(GetBufferMemoryRequirements)
  WARDEN_LSFG_CORE(AllocateMemory)
  WARDEN_LSFG_CORE(FreeMemory)
  WARDEN_LSFG_CORE(BindBufferMemory)
  WARDEN_LSFG_CORE(MapMemory)
  WARDEN_LSFG_CORE(UnmapMemory)
  WARDEN_LSFG_CORE(AllocateCommandBuffers)
  WARDEN_LSFG_CORE(FreeCommandBuffers)
  WARDEN_LSFG_CORE(BeginCommandBuffer)
  WARDEN_LSFG_CORE(EndCommandBuffer)
  WARDEN_LSFG_CORE(CmdPipelineBarrier)
  WARDEN_LSFG_CORE(CmdBlitImage)
  WARDEN_LSFG_CORE(CmdClearColorImage)
  WARDEN_LSFG_CORE(CmdBindPipeline)
  WARDEN_LSFG_CORE(CmdBindDescriptorSets)
  WARDEN_LSFG_CORE(CmdDispatch)
  WARDEN_LSFG_CORE(CmdCopyBufferToImage)
  WARDEN_LSFG_CORE(QueueSubmit)
  WARDEN_LSFG_CORE(AllocateDescriptorSets)
  WARDEN_LSFG_CORE(FreeDescriptorSets)
  WARDEN_LSFG_CORE(UpdateDescriptorSets)
  WARDEN_LSFG_CORE(CreateFence)
  WARDEN_LSFG_CORE(DestroyFence)
  WARDEN_LSFG_CORE(ResetFences)
  WARDEN_LSFG_CORE(WaitForFences)
  WARDEN_LSFG_CORE(CreateImage)
  WARDEN_LSFG_CORE(DestroyImage)
  WARDEN_LSFG_CORE(GetImageMemoryRequirements)
  WARDEN_LSFG_CORE(BindImageMemory)
  WARDEN_LSFG_CORE(CreateImageView)
  WARDEN_LSFG_CORE(DestroyImageView)
  WARDEN_LSFG_CORE(CreateSampler)
  WARDEN_LSFG_CORE(DestroySampler)
  WARDEN_LSFG_CORE(CreateSemaphore)
  WARDEN_LSFG_CORE(DestroySemaphore)
  WARDEN_LSFG_CORE(CreateShaderModule)
  WARDEN_LSFG_CORE(DestroyShaderModule)
  WARDEN_LSFG_CORE(CreateDescriptorSetLayout)
  WARDEN_LSFG_CORE(DestroyDescriptorSetLayout)
  WARDEN_LSFG_CORE(CreatePipelineLayout)
  WARDEN_LSFG_CORE(DestroyPipelineLayout)
  WARDEN_LSFG_CORE(CreatePipelineCache)
  WARDEN_LSFG_CORE(DestroyPipelineCache)
  WARDEN_LSFG_CORE(GetPipelineCacheData)
  WARDEN_LSFG_CORE(CreateComputePipelines)
  WARDEN_LSFG_CORE(DestroyPipeline)
  // Vulkan 1.2's names for the timeline functions; the KHR types are the same.
  df.SignalSemaphoreKHR =
      reinterpret_cast<PFN_vkSignalSemaphoreKHR>(load("vkSignalSemaphore", true));
  df.WaitSemaphoresKHR =
      reinterpret_cast<PFN_vkWaitSemaphoresKHR>(load("vkWaitSemaphores", true));
  // Not used on one device; the swapchain's neither (not "graphical").
  df.GetMemoryFdKHR = nullptr;
  df.ImportSemaphoreFdKHR = nullptr;
  df.GetSemaphoreFdKHR = nullptr;
  df.CreateSwapchainKHR = nullptr;
  df.GetSwapchainImagesKHR = nullptr;
  df.AcquireNextImageKHR = nullptr;
  df.QueuePresentKHR = nullptr;
  df.DestroySwapchainKHR = nullptr;
#undef WARDEN_LSFG_CORE
#undef WARDEN_LSFG_FUNC
  return ok;
}

vk::Image MakeImage(const vk::Vulkan& vk, VkExtent2D extent, VkFormat format) {
  return vk::Image(vk, extent, format,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}

// lsfg-vk's createCtx (lsfgvk.cpp), as it is.
Ctx CreateCtx(const vk::Vulkan& vk, const ShaderRegistry& shaders, VkExtent2D extent, bool hdr,
              float flow, bool perf, size_t count) {
  std::vector<vk::Buffer> constant_buffers{};
  constant_buffers.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    constant_buffers.emplace_back(vk, backend::getDefaultConstantBuffer(i, count, flow));
  }
  return {
      .vk = std::ref(vk),
      .shaders = std::ref(shaders),
      .pool{vk, backend::calculateDescriptorPoolLimits(count, perf)},
      .constantBuffer{vk, backend::getDefaultConstantBuffer(0, 1, flow)},
      .constantBuffers{std::move(constant_buffers)},
      .bnbSampler{vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, false},
      .bnwSampler{vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, true},
      .eabSampler{vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false},
      .sourceExtent = extent,
      .flowExtent = VkExtent2D{
          .width = static_cast<uint32_t>(static_cast<float>(extent.width) / flow),
          .height = static_cast<uint32_t>(static_cast<float>(extent.height) / flow)},
      .hdr = hdr,
      .flow = flow,
      .perf = perf,
      .count = count};
}

// The device and the shaders, made before the context that refers to them (members are
// made in order).
struct Base {
  Base(vk::Vulkan&& device, bool fp16,
       const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources)
      : vk(std::move(device)), shaders(backend::buildShaderRegistry(vk, fp16, resources)) {}
  vk::Vulkan vk;
  ShaderRegistry shaders;
};

class GeneratorImpl final : public Generator {
 public:
  GeneratorImpl(std::unique_ptr<Base> base, VkExtent2D extent, size_t count, float flow,
                bool perf);

  VkImage next_source() const override { return source(fidx_); }
  VkImageView next_source_view() const override {
    return fidx_ % 2 ? source_images_.second.imageview() : source_images_.first.imageview();
  }
  VkImage generated(uint32_t i) const override { return dest_images_.at(i).handle(); }
  VkImageView generated_view(uint32_t i) const override {
    return dest_images_.at(i).imageview();
  }
  uint32_t count() const override { return uint32_t(dest_images_.size()); }
  bool Record(VkCommandBuffer command_buffer, bool generate, std::string* error) override;
  uint64_t frames() const override { return fidx_; }
  bool Dump(const std::string& dir, std::string* error) override;
  bool fp16() const override { return base_->shaders.is_fp16; }

 private:
  // Frame n's: lsfg-vk's mipmaps take source n % 2.
  VkImage source(uint64_t frame) const {
    return frame % 2 ? source_images_.second.handle() : source_images_.first.handle();
  }

  std::unique_ptr<Base> base_;
  // From here on lsfg-vk's ContextImpl (lsfgvk.cpp): the images its own, not imported.
  std::pair<vk::Image, vk::Image> source_images_;
  std::vector<vk::Image> dest_images_;
  vk::Image black_image_;
  size_t fidx_{0};
  Ctx ctx_;
  Mipmaps mipmaps_;
  std::array<Alpha0, 7> alpha0_;
  std::array<Alpha1, 7> alpha1_;
  Beta0 beta0_;
  Beta1 beta1_;
  struct Pass {
    std::vector<Gamma0> gamma0;
    std::vector<Gamma1> gamma1;
    std::vector<Delta0> delta0;
    std::vector<Delta1> delta1;
    ls::lazy<Generate> generate;
  };
  std::vector<Pass> passes_;
};

std::vector<vk::Image> MakeDestImages(const vk::Vulkan& vk, VkExtent2D extent, size_t count) {
  std::vector<vk::Image> images;
  images.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    images.push_back(MakeImage(vk, extent, VK_FORMAT_R8G8B8A8_UNORM));
  }
  return images;
}

GeneratorImpl::GeneratorImpl(std::unique_ptr<Base> base, VkExtent2D extent, size_t count,
                             float flow, bool perf)
    : base_(std::move(base)),
      source_images_(MakeImage(base_->vk, extent, VK_FORMAT_R8G8B8A8_UNORM),
                     MakeImage(base_->vk, extent, VK_FORMAT_R8G8B8A8_UNORM)),
      dest_images_(MakeDestImages(base_->vk, extent, count)),
      black_image_(base_->vk, VkExtent2D{4, 4}),
      ctx_(CreateCtx(base_->vk, base_->shaders, extent, false, flow, perf, count)),
      mipmaps_(ctx_, source_images_),
      alpha0_{Alpha0(ctx_, mipmaps_.getImages().at(0)), Alpha0(ctx_, mipmaps_.getImages().at(1)),
              Alpha0(ctx_, mipmaps_.getImages().at(2)), Alpha0(ctx_, mipmaps_.getImages().at(3)),
              Alpha0(ctx_, mipmaps_.getImages().at(4)), Alpha0(ctx_, mipmaps_.getImages().at(5)),
              Alpha0(ctx_, mipmaps_.getImages().at(6))},
      alpha1_{Alpha1(ctx_, 3, alpha0_.at(0).getImages()),
              Alpha1(ctx_, 2, alpha0_.at(1).getImages()),
              Alpha1(ctx_, 2, alpha0_.at(2).getImages()),
              Alpha1(ctx_, 2, alpha0_.at(3).getImages()),
              Alpha1(ctx_, 2, alpha0_.at(4).getImages()),
              Alpha1(ctx_, 2, alpha0_.at(5).getImages()),
              Alpha1(ctx_, 2, alpha0_.at(6).getImages())},
      beta0_(ctx_, alpha1_.at(0).getImages()),
      beta1_(ctx_, beta0_.getImages()) {
  // lsfg-vk's main passes, as it builds them.
  for (size_t i = 0; i < dest_images_.size(); ++i) {
    auto& pass = passes_.emplace_back();
    pass.gamma0.reserve(7);
    pass.gamma1.reserve(7);
    pass.delta0.reserve(3);
    pass.delta1.reserve(3);
    for (size_t j = 0; j < 7; j++) {
      if (j == 0) {
        pass.gamma0.emplace_back(ctx_, i, alpha1_.at(6 - j).getImages(), black_image_);
        pass.gamma1.emplace_back(ctx_, i, pass.gamma0.at(j).getImages(), black_image_,
                                 beta1_.getImages().at(5));
      } else {
        pass.gamma0.emplace_back(ctx_, i, alpha1_.at(6 - j).getImages(),
                                 pass.gamma1.at(j - 1).getImage());
        pass.gamma1.emplace_back(ctx_, i, pass.gamma0.at(j).getImages(),
                                 pass.gamma1.at(j - 1).getImage(),
                                 beta1_.getImages().at(6 - j));
      }
      if (j == 4) {
        pass.delta0.emplace_back(ctx_, i, alpha1_.at(6 - j).getImages(), black_image_,
                                 pass.gamma1.at(j - 1).getImage());
        pass.delta1.emplace_back(ctx_, i, pass.delta0.at(j - 4).getImages0(),
                                 pass.delta0.at(j - 4).getImages1(), black_image_,
                                 beta1_.getImages().at(6 - j), black_image_);
      } else if (j > 4) {
        pass.delta0.emplace_back(ctx_, i, alpha1_.at(6 - j).getImages(),
                                 pass.delta1.at(j - 5).getImage0(),
                                 pass.gamma1.at(j - 1).getImage());
        pass.delta1.emplace_back(ctx_, i, pass.delta0.at(j - 4).getImages0(),
                                 pass.delta0.at(j - 4).getImages1(),
                                 pass.delta1.at(j - 5).getImage0(), beta1_.getImages().at(6 - j),
                                 pass.delta1.at(j - 5).getImage1());
      }
    }
    pass.generate.emplace(ctx_, i, source_images_, pass.gamma1.at(6).getImage(),
                          pass.delta1.at(2).getImage0(), pass.delta1.at(2).getImage1(),
                          dest_images_.at(i));
  }
  // Every image into the general layout, the sources and destinations too (lsfg-vk left
  // its imported ones to the application).
  std::vector<VkImage> images{black_image_.handle(), source_images_.first.handle(),
                              source_images_.second.handle()};
  for (const vk::Image& image : dest_images_) {
    images.push_back(image.handle());
  }
  mipmaps_.prepare(images);
  for (size_t i = 0; i < 7; ++i) {
    alpha0_.at(i).prepare(images);
    alpha1_.at(i).prepare(images);
  }
  beta0_.prepare(images);
  beta1_.prepare(images);
  for (const auto& pass : passes_) {
    for (size_t i = 0; i < 7; ++i) {
      pass.gamma0.at(i).prepare(images);
      pass.gamma1.at(i).prepare(images);
      if (i < 4) {
        continue;
      }
      pass.delta0.at(i - 4).prepare(images);
      pass.delta1.at(i - 4).prepare(images);
    }
  }
  std::vector<vk::Barrier> barriers{};
  barriers.reserve(images.size());
  for (const auto& image : images) {
    barriers.emplace_back(vk::Barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1}});
  }
  // On the generator's own queue, waited for: done before any frame is recorded.
  const vk::CommandBuffer cmdbuf{ctx_.vk};
  cmdbuf.begin(ctx_.vk);
  cmdbuf.insertBarriers(ctx_.vk, barriers);
  cmdbuf.end(ctx_.vk);
  cmdbuf.submit(ctx_.vk);
}

// lsfg-vk's scheduleFrames (lsfgvk.cpp), into one command buffer: there the prepass and
// each generated frame's passes were submissions of their own, ordered by its timeline
// semaphore; here the shader chains' own barriers order them - each dispatch waits for the
// compute writes of the images it samples (and its storage images for their reads).
bool GeneratorImpl::Record(VkCommandBuffer command_buffer, bool generate, std::string* error) {
  try {
    const vk::CommandBuffer cmdbuf{command_buffer};
    mipmaps_.render(ctx_.vk, cmdbuf, fidx_);
    for (size_t i = 0; i < 7; ++i) {
      alpha0_.at(6 - i).render(ctx_.vk, cmdbuf);
      alpha1_.at(6 - i).render(ctx_.vk, cmdbuf, fidx_);
    }
    beta0_.render(ctx_.vk, cmdbuf, fidx_);
    beta1_.render(ctx_.vk, cmdbuf);
    if (generate && fidx_) {
      for (const Pass& pass : passes_) {
        for (size_t j = 0; j < 7; j++) {
          pass.gamma0.at(j).render(ctx_.vk, cmdbuf, fidx_);
          pass.gamma1.at(j).render(ctx_.vk, cmdbuf);
          if (j < 4) {
            continue;
          }
          pass.delta0.at(j - 4).render(ctx_.vk, cmdbuf, fidx_);
          pass.delta1.at(j - 4).render(ctx_.vk, cmdbuf);
        }
        pass.generate->render(ctx_.vk, cmdbuf, fidx_);
      }
    }
    fidx_++;
    return true;
  } catch (const std::exception& e) {
    *error = e.what();
    return false;
  }
}

bool GeneratorImpl::Dump(const std::string& dir, std::string* error) {
  const vk::Vulkan& vk = base_->vk;
  const auto& fi = vk.fi();
  const VkDevice dev = vk.dev();
  const auto proc = [&](const char* name) { return fi.GetDeviceProcAddr(dev, name); };
  const auto copy = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(proc("vkCmdCopyImageToBuffer"));
  const VkExtent2D extent = ctx_.sourceExtent;
  const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;
  // The last generation was frame fidx_ - 1's: its source and the one before it.
  const VkImage images[3] = {dest_images_.at(0).handle(), source(fidx_), source(fidx_ + 1)};
  const char* names[3] = {"lsfg-generated.raw", "lsfg-source-before.raw",
                          "lsfg-source-after.raw"};
  // Host-visible buffers for the three.
  VkBuffer buffers[3] = {};
  VkDeviceMemory memories[3] = {};
  bool ok = true;
  for (int i = 0; i < 3 && ok; ++i) {
    VkBufferCreateInfo info{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = size,
                            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    ok = vk.df().CreateBuffer(dev, &info, nullptr, &buffers[i]) == VK_SUCCESS;
    VkMemoryRequirements requirements{};
    if (ok) {
      vk.df().GetBufferMemoryRequirements(dev, buffers[i], &requirements);
      const auto type = vk.findMemoryTypeIndex(requirements.memoryTypeBits, true);
      VkMemoryAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                    .allocationSize = requirements.size,
                                    .memoryTypeIndex = type.value_or(0)};
      ok = type.has_value() &&
           vk.df().AllocateMemory(dev, &allocate, nullptr, &memories[i]) == VK_SUCCESS &&
           vk.df().BindBufferMemory(dev, buffers[i], memories[i], 0) == VK_SUCCESS;
    }
  }
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (ok) {
    VkCommandBufferAllocateInfo allocate{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = vk.cmdpool(),
                                         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1};
    ok = vk.df().AllocateCommandBuffers(dev, &allocate, &cmd) == VK_SUCCESS;
  }
  if (ok) {
    VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vk.df().BeginCommandBuffer(cmd, &begin);
    VkMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
    vk.df().CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
                               nullptr);
    for (int i = 0; i < 3; ++i) {
      VkBufferImageCopy region{
          .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
          .imageExtent = {extent.width, extent.height, 1}};
      copy(cmd, images[i], VK_IMAGE_LAYOUT_GENERAL, buffers[i], 1, &region);
    }
    vk.df().EndCommandBuffer(cmd);
    VkSubmitInfo submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1,
                        .pCommandBuffers = &cmd};
    const vk::Fence fence{vk};
    ok = vk.df().QueueSubmit(vk.queue(), 1, &submit, fence.handle()) == VK_SUCCESS &&
         fence.wait(vk);
  }
  for (int i = 0; i < 3 && ok; ++i) {
    void* data = nullptr;
    if (vk.df().MapMemory(dev, memories[i], 0, size, 0, &data) == VK_SUCCESS) {
      std::ofstream out(dir + "/" + names[i], std::ios::binary);
      out.write(static_cast<const char*>(data), std::streamsize(size));
      vk.df().UnmapMemory(dev, memories[i]);
    } else {
      ok = false;
    }
  }
  if (cmd) {
    vk.df().FreeCommandBuffers(dev, vk.cmdpool(), 1, &cmd);
  }
  for (int i = 0; i < 3; ++i) {
    if (buffers[i]) {
      vk.df().DestroyBuffer(dev, buffers[i], nullptr);
    }
    if (memories[i]) {
      vk.df().FreeMemory(dev, memories[i], nullptr);
    }
  }
  if (!ok) {
    *error = "the readback failed";
  }
  return ok;
}

}  // namespace

std::unique_ptr<Generator> Generator::Create(const Setup& setup, std::string* error) {
  try {
    const vk::VulkanInstanceFuncs fi =
        vk::initVulkanInstanceFuncs(setup.instance, setup.get_instance_proc_addr, false);
    vk::VulkanDeviceFuncs df{};
    if (!LoadDeviceFuncs(fi, setup.device, df, error)) {
      return nullptr;
    }
    const std::unordered_map<uint32_t, std::vector<uint8_t>> resources =
        backend::extractResourcesFromDLL(setup.dll_path);
    auto base = std::make_unique<Base>(
        vk::Vulkan(setup.instance, setup.device, setup.physical_device, fi, df, false,
                   std::nullopt, std::nullopt, setup.queue_index, setup.fp16),
        setup.fp16, resources);
    return std::make_unique<GeneratorImpl>(std::move(base),
                                           VkExtent2D{setup.width, setup.height},
                                           setup.count, setup.flow, setup.perf);
  } catch (const std::exception& e) {
    *error = e.what();
    return nullptr;
  }
}

}  // namespace warden::lsfg
