// PLAN 10.9: a standalone Vulkan compute test for the Thor - the Adreno's min with NaN
// operands, and the multiplication rule's FMin form against the exact Or form, on
// runtime inputs. Prints each pair's results as bit patterns and the mismatches.
#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static const uint8_t kShader[] = {
#include "min_test.spv.inc"
};

static float Bits(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
static uint32_t ToBits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

int main() {
  const uint32_t kQNaN = 0x7FC00000, kSNaN = 0x7F800001, kNegNaN = 0xFFC00000,
                 kInf = 0x7F800000, kNegInf = 0xFF800000, kNegZero = 0x80000000;
  const uint32_t values[] = {0x00000000, kNegZero, 0x3F800000 /*1*/, 0xC0000000 /*-2*/,
                             kInf, kNegInf, kQNaN, kSNaN, kNegNaN, 0x00000001 /*denormal*/};
  const int n = sizeof(values) / sizeof(values[0]);
  std::vector<float> pairs;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      pairs.push_back(Bits(values[i]));
      pairs.push_back(Bits(values[j]));
    }
  const uint32_t count = uint32_t(pairs.size() / 2);

  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  VkInstance instance;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) return printf("no instance\n"), 1;
  uint32_t pdc = 1;
  VkPhysicalDevice pd;
  vkEnumeratePhysicalDevices(instance, &pdc, &pd);
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(pd, &props);
  printf("device: %s, driver %u.%u.%u\n", props.deviceName, VK_VERSION_MAJOR(props.driverVersion),
         VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));
  uint32_t qfc = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfc, nullptr);
  std::vector<VkQueueFamilyProperties> qf(qfc);
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfc, qf.data());
  uint32_t family = 0;
  for (uint32_t i = 0; i < qfc; ++i)
    if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { family = i; break; }
  float prio = 1.0f;
  VkDeviceQueueCreateInfo dqci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  dqci.queueFamilyIndex = family;
  dqci.queueCount = 1;
  dqci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &dqci;
  VkDevice device;
  if (vkCreateDevice(pd, &dci, nullptr, &device) != VK_SUCCESS) return printf("no device\n"), 1;
  VkQueue queue;
  vkGetDeviceQueue(device, family, 0, &queue);

  VkPhysicalDeviceMemoryProperties mem;
  vkGetPhysicalDeviceMemoryProperties(pd, &mem);
  auto make_buffer = [&](VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void** map) {
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    vkCreateBuffer(device, &bci, nullptr, &buffer);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    uint32_t type = 0;
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
      const VkMemoryPropertyFlags want =
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if ((req.memoryTypeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want) {
        type = i;
        break;
      }
    }
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    vkAllocateMemory(device, &mai, nullptr, &memory);
    vkBindBufferMemory(device, buffer, memory, 0);
    vkMapMemory(device, memory, 0, size, 0, map);
  };
  VkBuffer in_buffer, out_buffer;
  VkDeviceMemory in_memory, out_memory;
  void *in_map, *out_map;
  make_buffer(pairs.size() * 4, in_buffer, in_memory, &in_map);
  make_buffer(count * 16, out_buffer, out_memory, &out_map);
  std::memcpy(in_map, pairs.data(), pairs.size() * 4);
  std::memset(out_map, 0xEE, count * 16);

  VkDescriptorSetLayoutBinding bindings[2] = {};
  for (int i = 0; i < 2; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo dslci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslci.bindingCount = 2;
  dslci.pBindings = bindings;
  VkDescriptorSetLayout dsl;
  vkCreateDescriptorSetLayout(device, &dslci, nullptr, &dsl);
  VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &dsl;
  VkPipelineLayout layout;
  vkCreatePipelineLayout(device, &plci, nullptr, &layout);
  VkShaderModuleCreateInfo smci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smci.codeSize = sizeof(kShader);
  smci.pCode = reinterpret_cast<const uint32_t*>(kShader);
  VkShaderModule module;
  if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) return printf("no module\n"), 1;
  VkComputePipelineCreateInfo cpci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  cpci.stage.module = module;
  cpci.stage.pName = "main";
  cpci.layout = layout;
  VkPipeline pipeline;
  if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline) != VK_SUCCESS)
    return printf("no pipeline\n"), 1;

  VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
  VkDescriptorPoolCreateInfo dpci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &pool_size;
  VkDescriptorPool pool;
  vkCreateDescriptorPool(device, &dpci, nullptr, &pool);
  VkDescriptorSetAllocateInfo dsai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = pool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &dsl;
  VkDescriptorSet set;
  vkAllocateDescriptorSets(device, &dsai, &set);
  VkDescriptorBufferInfo infos[2] = {{in_buffer, 0, VK_WHOLE_SIZE}, {out_buffer, 0, VK_WHOLE_SIZE}};
  VkWriteDescriptorSet writes[2] = {};
  for (int i = 0; i < 2; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &infos[i];
  }
  vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

  VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpi.queueFamilyIndex = family;
  VkCommandPool command_pool;
  vkCreateCommandPool(device, &cpi, nullptr, &command_pool);
  VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = command_pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  VkCommandBuffer cb;
  vkAllocateCommandBuffers(device, &cbai, &cb);
  VkCommandBufferBeginInfo cbbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cb, &cbbi);
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
  vkCmdDispatch(cb, (count + 63) / 64, 1, 1);
  vkEndCommandBuffer(cb);
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(device ? queue : queue);

  const float* out = static_cast<const float*>(out_map);
  auto name = [&](uint32_t u) -> const char* {
    switch (u) {
      case 0x00000000: return "+0";
      case 0x80000000: return "-0";
      case 0x3F800000: return "1";
      case 0xC0000000: return "-2";
      case 0x7F800000: return "+inf";
      case 0xFF800000: return "-inf";
      case 0x7FC00000: return "qNaN";
      case 0x7F800001: return "sNaN";
      case 0xFFC00000: return "-qNaN";
      case 0x00000001: return "denorm";
    }
    return "?";
  };
  int nan_suppressed = 0, nan_returned = 0, mismatches = 0;
  printf("%-7s %-7s | %-8s %-8s | %-9s %-9s\n", "a", "b", "min(a,b)", "min(b,a)", "mul_or", "mul_fmin");
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t a = ToBits(pairs[2 * i]), b = ToBits(pairs[2 * i + 1]);
    const uint32_t r0 = ToBits(out[4 * i]), r1 = ToBits(out[4 * i + 1]), r2 = ToBits(out[4 * i + 2]),
                   r3 = ToBits(out[4 * i + 3]);
    const bool a_nan = std::isnan(pairs[2 * i]), b_nan = std::isnan(pairs[2 * i + 1]);
    if (a_nan != b_nan) {
      // One NaN operand: does min return the other one?
      if (std::isnan(out[4 * i])) nan_returned++; else nan_suppressed++;
      if (std::isnan(out[4 * i + 1])) nan_returned++; else nan_suppressed++;
    }
    const bool same = r2 == r3 || (std::isnan(out[4 * i + 2]) && std::isnan(out[4 * i + 3]));
    if (!same) mismatches++;
    if (a_nan || b_nan || !same)
      printf("%-7s %-7s | %-8s %-8s | %-9s %-9s%s\n", name(a), name(b), name(r0), name(r1), name(r2),
             name(r3), same ? "" : "  MISMATCH");
  }
  printf("min with one NaN operand: the other returned %d times, NaN returned %d times\n",
         nan_suppressed, nan_returned);
  printf("mul_fmin against mul_or: %d mismatches of %u pairs\n", mismatches, count);
  return 0;
}
