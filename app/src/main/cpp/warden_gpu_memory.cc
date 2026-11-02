// GPU memory accounting (PLAN 3.7): what each of Xenia's allocation sites holds.
//
// The Adreno driver says only that the process holds about 1.9 GB of GPU memory at the
// King's Camp (/sys/class/kgsl/kgsl/proc/<pid>/gpumem_mapped, run 333); its per-type
// counts cover 72 MB of it, and VK_EXT_memory_budget is not there. So the device's
// vkAllocateMemory and vkFreeMemory are wrapped where Xenia loads them (a recorded edit
// in vulkan_device.cc), and every live allocation is kept with its size, its memory
// type and the address it was made from - an offset into libwarden.so, which
// llvm-addr2line on the unstripped library turns into Xenia's function (VMA's, for the
// texture cache). Two devices are wrapped: the bring-up one, made at startup for the
// report's presenter, and the graphics system's.

#include "warden_gpu_memory.h"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xenia/base/logging.h"  // fmt, as the rest of the runtime has it
#include "xenia/ui/vulkan/vulkan_api.h"

namespace {

struct Originals {
  PFN_vkAllocateMemory allocate = nullptr;
  PFN_vkFreeMemory free = nullptr;
  uint32_t index = 0;  // 0 the first device wrapped, 1 the next
};

struct Allocation {
  uint64_t site;  // (device index << 32) | caller offset
  uint64_t size;
};

struct Site {
  uint64_t bytes = 0;
  uint32_t count = 0;
  uint32_t memory_type = 0;
};

std::mutex g_mutex;
std::unordered_map<VkDevice, Originals> g_devices;
std::unordered_map<VkDeviceMemory, Allocation> g_allocations;
std::unordered_map<uint64_t, Site> g_sites;
uint64_t g_total = 0;
uint64_t g_reported = 0;
uintptr_t g_base = 0;

Originals OriginalsFor(VkDevice device) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto found = g_devices.find(device);
  return found == g_devices.end() ? Originals{} : found->second;
}

VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice device, const VkMemoryAllocateInfo* info,
                                        const VkAllocationCallbacks* callbacks,
                                        VkDeviceMemory* memory) {
  const uintptr_t caller = uintptr_t(__builtin_return_address(0));
  const Originals originals = OriginalsFor(device);
  if (!originals.allocate) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  const VkResult result = originals.allocate(device, info, callbacks, memory);
  if (result == VK_SUCCESS && memory && *memory != VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint64_t site = (uint64_t(originals.index) << 32) | uint32_t(caller - g_base);
    g_allocations[*memory] = Allocation{site, info->allocationSize};
    Site& s = g_sites[site];
    s.bytes += info->allocationSize;
    ++s.count;
    s.memory_type = info->memoryTypeIndex;
    g_total += info->allocationSize;
  }
  return result;
}

VKAPI_ATTR void VKAPI_CALL Free(VkDevice device, VkDeviceMemory memory,
                                const VkAllocationCallbacks* callbacks) {
  const Originals originals = OriginalsFor(device);
  if (memory != VK_NULL_HANDLE) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_allocations.find(memory);
    if (found != g_allocations.end()) {
      Site& s = g_sites[found->second.site];
      s.bytes -= found->second.size;
      --s.count;
      g_total -= found->second.size;
      g_allocations.erase(found);
    }
  }
  if (originals.free) {
    originals.free(device, memory, callbacks);
  }
}

}  // namespace

// Called by vulkan_device.cc (a recorded edit) once the device's functions are loaded.
extern "C" void warden_vk_wrap_device_functions(VkDevice device,
                                                PFN_vkAllocateMemory* allocate,
                                                PFN_vkFreeMemory* free) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_base) {
    Dl_info info;
    if (dladdr(reinterpret_cast<const void*>(&warden_vk_wrap_device_functions), &info)) {
      g_base = uintptr_t(info.dli_fbase);
    }
  }
  Originals originals;
  originals.allocate = *allocate;
  originals.free = *free;
  originals.index = uint32_t(g_devices.size());
  g_devices[device] = originals;
  *allocate = Allocate;
  *free = Free;
}

// BUG-060: the driver's statistics for each new pipeline (VK_KHR_pipeline_executable_
// properties), with files/hack-pipeline-stats - its registers, its private memory and its
// spills, the likely reason each pipeline holds about 10 MB. Called by
// vulkan_pipeline_cache.cc (recorded edits) after a pipeline is created with
// VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR.
// BUG-060: whether the pipeline storage creates every stored pipeline at startup, as
// upstream does (files/hack-precreate-pipelines), rather than each when first drawn with.
extern "C" bool warden_precreate_stored_pipelines() {
  return std::getenv("DAO_HACK_PRECREATE_PIPELINES") != nullptr;
}

extern "C" bool warden_pipeline_statistics_wanted() {
  static const bool wanted = std::getenv("DAO_HACK_PIPELINE_STATS") != nullptr;
  return wanted;
}

extern "C" void warden_pipeline_statistics(VkDevice device, VkPipeline pipeline,
                                           uint64_t vertex_shader, uint64_t pixel_shader) {
  static PFN_vkGetPipelineExecutablePropertiesKHR get_properties = nullptr;
  static PFN_vkGetPipelineExecutableStatisticsKHR get_statistics = nullptr;
  static std::once_flag once;
  std::call_once(once, [device] {
    void* loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    auto get_device_proc =
        loader ? reinterpret_cast<PFN_vkGetDeviceProcAddr>(dlsym(loader, "vkGetDeviceProcAddr"))
               : nullptr;
    if (get_device_proc) {
      get_properties = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
          get_device_proc(device, "vkGetPipelineExecutablePropertiesKHR"));
      get_statistics = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
          get_device_proc(device, "vkGetPipelineExecutableStatisticsKHR"));
    }
  });
  if (!get_properties || !get_statistics) {
    return;
  }
  VkPipelineInfoKHR pipeline_info = {VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
  pipeline_info.pipeline = pipeline;
  uint32_t executable_count = 0;
  if (get_properties(device, &pipeline_info, &executable_count, nullptr) != VK_SUCCESS) {
    return;
  }
  std::vector<VkPipelineExecutablePropertiesKHR> executables(
      executable_count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
  get_properties(device, &pipeline_info, &executable_count, executables.data());
  // The driver's count for the process as each pipeline arrives: whether pipelines are
  // what it grows with (BUG-060).
  static std::atomic<uint32_t> created{0};
  unsigned long long driver_bytes = 0;
  if (FILE* file = fopen(fmt::format("/sys/class/kgsl/kgsl/proc/{}/kernel", getpid()).c_str(),
                         "r")) {
    if (fscanf(file, "%llu", &driver_bytes) != 1) {
      driver_bytes = 0;
    }
    fclose(file);
  }
  uint64_t counted;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    counted = g_total;
  }
  const uint32_t number = created.fetch_add(1) + 1;
  XELOGI("pipeline #{} (driver {} MB, of it not allocated by Xenia {} MB) VS {:016X} PS {:016X}",
         number, driver_bytes >> 20, (driver_bytes > counted ? driver_bytes - counted : 0) >> 20,
         vertex_shader, pixel_shader);
  // One line a stage (PLAN 10.9: a line with every stage's statistics was cut short by the
  // log, losing the pixel shader's), the counts that say what a shader costs.
  for (uint32_t i = 0; i < executable_count; ++i) {
    VkPipelineExecutableInfoKHR executable_info = {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
    executable_info.pipeline = pipeline;
    executable_info.executableIndex = i;
    uint32_t statistic_count = 0;
    get_statistics(device, &executable_info, &statistic_count, nullptr);
    std::vector<VkPipelineExecutableStatisticKHR> statistics(
        statistic_count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
    get_statistics(device, &executable_info, &statistic_count, statistics.data());
    std::string line = fmt::format("pipeline #{} PS {:016X} {} (subgroup {}):", number,
                                   pixel_shader, executables[i].name,
                                   executables[i].subgroupSize);
    for (const VkPipelineExecutableStatisticKHR& s : statistics) {
      const std::string name = s.name;
      if (name.find("Preamble") != std::string::npos) {
        continue;
      }
      switch (s.format) {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
          line += fmt::format(" {}={}", name, s.value.b32 ? "yes" : "no");
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
          line += fmt::format(" {}={}", name, s.value.i64);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
          line += fmt::format(" {}={}", name, s.value.u64);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
          line += fmt::format(" {}={:.2f}", name, s.value.f64);
          break;
        default:
          break;
      }
    }
    // The log cuts lines at about 1000 characters: in two halves when longer.
    if (line.size() > 900) {
      const size_t cut = line.rfind(' ', 900);
      XELOGI("{}", line.substr(0, cut));
      XELOGI("pipeline #{} ...{}", number, line.substr(cut));
    } else {
      XELOGI("{}", line);
    }
  }
}

namespace warden::gpu {

std::string GpuMemoryReport() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const uint64_t step = uint64_t(16) << 20;
  if (g_total + step > g_reported && g_reported + step > g_total) {
    return "";
  }
  g_reported = g_total;
  std::vector<std::pair<uint64_t, Site>> sites(g_sites.begin(), g_sites.end());
  std::sort(sites.begin(), sites.end(),
            [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
  // What the driver counts against the process, for what the allocations do not show.
  double driver_mb = 0;
  if (FILE* file = fopen(fmt::format("/sys/class/kgsl/kgsl/proc/{}/kernel", getpid()).c_str(),
                         "r")) {
    unsigned long long bytes = 0;
    if (fscanf(file, "%llu", &bytes) == 1) {
      driver_mb = bytes / 1048576.0;
    }
    fclose(file);
  }
  std::string line = fmt::format("GPU memory: {:.1f} MB in {} allocations, the driver's own "
                                 "count {:.1f} MB; by site (device + offset in libwarden.so, "
                                 "memory type):",
                                 g_total / 1048576.0, g_allocations.size(), driver_mb);
  size_t shown = 0;
  for (const auto& [site, s] : sites) {
    if (!s.count || shown == 12) {
      continue;
    }
    line += fmt::format(" [{} +{:x} type {}: {:.1f} MB in {}]", uint32_t(site >> 32),
                        uint32_t(site), s.memory_type, s.bytes / 1048576.0, s.count);
    ++shown;
  }
  return line;
}

}  // namespace warden::gpu
