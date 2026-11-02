// PLAN 9.21, the first step: whether a second Vulkan device of higher global priority
// gets its work through while the game's rendering keeps the GPU busy - the presenter on
// such a device rests on it, and a queue of its own on the game's device did not (the
// driver ignores per-queue priorities, runs 473-474). A test switch, files/
// hack-priority-probe (its content, if any, the seconds to wait before measuring:
// default 240, the King's Camp on the test launch).
//
// What it does, on a thread of its own: the device's extensions that matter logged
// (global priority, external memory and semaphores as fds or Android hardware buffers);
// then a device made for each global priority in turn - realtime, high, medium - with
// what the driver says to each; then, on each device that was made, a paint-sized job
// (a 1920x1080 RGBA8 image blitted to another, filtered, as a presenter's paint reads and
// writes) submitted every 50 ms for 10 s, each from submission to its fence timed on the
// CPU. A job on a device the GPU really favours finishes in about its own time while the
// game renders; one that waits behind the game's frames takes tens of milliseconds.
// (Measuring the same job on the game's own queue hung - Xenia's command processor holds
// that queue - so it is not here.)

#include "warden_priority_probe.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_api.h"
#include "xenia/ui/vulkan/vulkan_device.h"

// libvulkan's own export: the runtime's vulkan_api.h declares no prototypes.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* name);

namespace warden::priority_probe {

namespace {

constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;

#define WARDEN_PROBE_FUNCTIONS(X)            \
  X(vkGetDeviceProcAddr)                     \
  X(vkCreateDevice)                          \
  X(vkDestroyDevice)                         \
  X(vkEnumerateDeviceExtensionProperties)    \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceMemoryProperties)

#define WARDEN_PROBE_DEVICE_FUNCTIONS(X) \
  X(vkGetDeviceQueue)                    \
  X(vkCreateImage)                       \
  X(vkDestroyImage)                      \
  X(vkGetImageMemoryRequirements)        \
  X(vkAllocateMemory)                    \
  X(vkFreeMemory)                        \
  X(vkBindImageMemory)                   \
  X(vkCreateCommandPool)                 \
  X(vkDestroyCommandPool)                \
  X(vkAllocateCommandBuffers)            \
  X(vkBeginCommandBuffer)                \
  X(vkEndCommandBuffer)                  \
  X(vkCmdPipelineBarrier)                \
  X(vkCmdBlitImage)                      \
  X(vkCreateFence)                       \
  X(vkDestroyFence)                      \
  X(vkResetFences)                       \
  X(vkWaitForFences)                     \
  X(vkQueueSubmit)                       \
  X(vkDeviceWaitIdle)

struct Instance {
#define X(name) PFN_##name name = nullptr;
  WARDEN_PROBE_FUNCTIONS(X)
#undef X
};

struct Device {
  VkDevice device = VK_NULL_HANDLE;
#define X(name) PFN_##name name = nullptr;
  WARDEN_PROBE_DEVICE_FUNCTIONS(X)
#undef X
};

const char* PriorityName(VkQueueGlobalPriorityKHR priority) {
  switch (priority) {
    case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      return "realtime";
    case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return "high";
    case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return "medium";
    default:
      return "low";
  }
}

uint32_t MemoryType(const VkPhysicalDeviceMemoryProperties& properties, uint32_t bits,
                    VkMemoryPropertyFlags wanted) {
  for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) {
      return i;
    }
  }
  return UINT32_MAX;
}

// The paint-sized job on one device: submitted every 50 ms for 10 s, timed.
void Measure(const Instance& vk, VkPhysicalDevice physical_device, Device& d,
             uint32_t family, const char* label,
             const std::function<VkResult(const VkSubmitInfo&, VkFence)>& submit_to) {
  VkPhysicalDeviceMemoryProperties memory_properties;
  vk.vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
  VkImage images[2] = {};
  VkDeviceMemory memories[2] = {};
  for (int i = 0; i < 2; ++i) {
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {kWidth, kHeight, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (d.vkCreateImage(d.device, &info, nullptr, &images[i]) != VK_SUCCESS) {
      XELOGE("priority probe: {}: no image", label);
      return;
    }
    VkMemoryRequirements requirements;
    d.vkGetImageMemoryRequirements(d.device, images[i], &requirements);
    VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = MemoryType(memory_properties, requirements.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocate.memoryTypeIndex == UINT32_MAX ||
        d.vkAllocateMemory(d.device, &allocate, nullptr, &memories[i]) != VK_SUCCESS ||
        d.vkBindImageMemory(d.device, images[i], memories[i], 0) != VK_SUCCESS) {
      XELOGE("priority probe: {}: no image memory", label);
      return;
    }
  }
  VkCommandPool pool;
  VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.queueFamilyIndex = family;
  d.vkCreateCommandPool(d.device, &pool_info, nullptr, &pool);
  VkCommandBuffer command_buffer;
  VkCommandBufferAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocate_info.commandPool = pool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;
  d.vkAllocateCommandBuffers(d.device, &allocate_info, &command_buffer);
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  d.vkBeginCommandBuffer(command_buffer, &begin);
  VkImageMemoryBarrier barriers[2] = {};
  for (int i = 0; i < 2; ++i) {
    barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[i].srcAccessMask = 0;
    barriers[i].dstAccessMask =
        i == 0 ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[i].newLayout = i == 0 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                   : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[i].image = images[i];
    barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  }
  d.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                         barriers);
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {int32_t(kWidth), int32_t(kHeight), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[1] = {int32_t(kWidth), int32_t(kHeight), 1};
  d.vkCmdBlitImage(command_buffer, images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images[1],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
  d.vkEndCommandBuffer(command_buffer);
  VkFence fence;
  VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  d.vkCreateFence(d.device, &fence_info, nullptr, &fence);
  std::vector<double> times;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < end) {
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    const auto started = std::chrono::steady_clock::now();
    if (submit_to(submit, fence) != VK_SUCCESS) {
      XELOGE("priority probe: {}: the submission failed", label);
      break;
    }
    d.vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX);
    times.push_back(std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started)
                        .count());
    d.vkResetFences(d.device, 1, &fence);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  d.vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX);
  d.vkDestroyFence(d.device, fence, nullptr);
  d.vkDestroyCommandPool(d.device, pool, nullptr);
  for (int i = 0; i < 2; ++i) {
    d.vkDestroyImage(d.device, images[i], nullptr);
    d.vkFreeMemory(d.device, memories[i], nullptr);
  }
  if (times.empty()) {
    return;
  }
  std::sort(times.begin(), times.end());
  auto at = [&](double p) {
    return times[std::min(times.size() - 1, size_t(p * double(times.size() - 1) + 0.5))];
  };
  XELOGI("priority probe: {}: {} paint-sized jobs while the game renders - submission to "
         "fence ms p50 {:.2f} p90 {:.2f} p99 {:.2f} max {:.2f} min {:.2f}",
         label, times.size(), at(0.5), at(0.9), at(0.99), times.back(), times.front());
}

void Run(VkInstance instance, VkPhysicalDevice physical_device,
         const xe::ui::vulkan::VulkanDevice* game, int delay_seconds) {
  std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
  Instance vk;
#define X(name) \
  vk.name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
  WARDEN_PROBE_FUNCTIONS(X)
#undef X
  uint32_t count = 0;
  vk.vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vk.vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data());
  auto has = [&](const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [&](const auto& e) {
      return std::strcmp(e.extensionName, name) == 0;
    });
  };
  std::string listed;
  for (const char* name :
       {"VK_EXT_global_priority", "VK_KHR_global_priority", "VK_EXT_global_priority_query",
        "VK_KHR_external_memory_fd", "VK_ANDROID_external_memory_android_hardware_buffer",
        "VK_KHR_external_semaphore_fd", "VK_KHR_external_fence_fd", "VK_KHR_swapchain",
        "VK_GOOGLE_display_timing"}) {
    listed += fmt::format(" {} {};", name, has(name) ? "yes" : "no");
  }
  XELOGI("priority probe: the device's extensions:{}", listed);
  const char* priority_extension = has("VK_KHR_global_priority")   ? "VK_KHR_global_priority"
                                   : has("VK_EXT_global_priority") ? "VK_EXT_global_priority"
                                                                   : nullptr;
  if (!priority_extension) {
    XELOGI("priority probe: no global priority extension - nothing to measure");
    return;
  }
  uint32_t family_count = 0;
  vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
  uint32_t family = UINT32_MAX;
  for (uint32_t i = 0; i < family_count; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      family = i;
      break;
    }
  }
  if (family == UINT32_MAX) {
    return;
  }
  for (VkQueueGlobalPriorityKHR priority :
       {VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR, VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
        VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR}) {
    VkDeviceQueueGlobalPriorityCreateInfoKHR priority_info = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR};
    priority_info.globalPriority = priority;
    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.pNext = &priority_info;
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = &priority_extension;
    Device d;
    const VkResult result = vk.vkCreateDevice(physical_device, &device_info, nullptr, &d.device);
    XELOGI("priority probe: a device with a {} priority queue: {}", PriorityName(priority),
           result == VK_SUCCESS                  ? "made"
           : result == VK_ERROR_NOT_PERMITTED_KHR ? "not permitted"
                                                  : fmt::format("error {}", int(result)));
    if (result != VK_SUCCESS) {
      continue;
    }
#define X(name) d.name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(d.device, #name));
    WARDEN_PROBE_DEVICE_FUNCTIONS(X)
#undef X
    VkQueue queue;
    d.vkGetDeviceQueue(d.device, family, 0, &queue);
    Measure(vk, physical_device, d, family,
            fmt::format("{} priority", PriorityName(priority)).c_str(),
            [&](const VkSubmitInfo& submit, VkFence fence) {
              return d.vkQueueSubmit(queue, 1, &submit, fence);
            });
    vk.vkDestroyDevice(d.device, nullptr);
  }
  XELOGI("priority probe: done");
}

}  // namespace

void Start(const xe::ui::vulkan::VulkanDevice* game, int delay_seconds) {
  std::thread(Run, game->vulkan_instance()->instance(), game->physical_device(), game,
              delay_seconds)
      .detach();
}

}  // namespace warden::priority_probe
