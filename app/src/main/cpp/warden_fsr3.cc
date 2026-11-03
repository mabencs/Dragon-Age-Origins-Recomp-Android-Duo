// PLAN 9.4: FSR 3.1 - the FidelityFX SDK's upscaler (third_party/FidelityFX-SDK, v1.1.4,
// its sdk folder) with its Vulkan backend, on the game's own device.
//
// The SDK's host code is built from source (app/src/main/cpp/CMakeLists.txt, warden_ffx,
// with ffx_compat/warden_ffx_compat.h for the few Microsoft C runtime calls it makes); its
// shaders are tools/build_fsr3_shaders.py's - the SDK's GLSL passes compiled by the NDK's
// glslc into the permutation headers its blob accessor reads. The SDK chooses between
// shader paths - 64-lane subgroups, 16-bit arithmetic, synchronization2's barriers - by
// the device extensions it finds, but a path needs its features enabled on the device,
// and Xenia's device enables none of them. So the SDK's sources ask for the device's
// extensions through warden_ffx_vkEnumerateDeviceExtensionProperties (the compat header
// redirects the call), which answers what the graft has enabled: core Vulkan 1.1's
// subgroup operations and two storage image features (assemble_runtime.py: extended
// formats, writes without a format) are all the 32-bit paths need, and with shaderInt16
// and Vulkan 1.2's shaderFloat16 enabled too (session 36), VK_KHR_shader_float16_int8
// takes the SDK to its 16-bit permutations (FFX_HALF) - SetHalfPrecision.
#include "warden_fsr3.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include "xenia/base/logging.h"

// libvulkan's own export: the runtime's vulkan_api.h declares no prototypes.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                       const char* name);

namespace {
std::atomic<bool> g_half{false};
}  // namespace

// Always one extension, so the count never changes: the SDK sizes its scratch memory by
// the count when its interface is made (ffxGetScratchMemorySizeVK) and reads the list
// again as each context is made on an idle interface. 32-bit: a name it does not know.
extern "C" VKAPI_ATTR VkResult VKAPI_CALL warden_ffx_vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice, const char*, uint32_t* count, VkExtensionProperties* properties) {
  if (!properties) {
    *count = 1;
    return VK_SUCCESS;
  }
  if (!*count) {
    return VK_INCOMPLETE;
  }
  *count = 1;
  properties[0] = {};
  std::strncpy(properties[0].extensionName,
               g_half.load(std::memory_order_relaxed) ? VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME
                                                      : "VK_WARDEN_fsr3_32_bit",
               VK_MAX_EXTENSION_NAME_SIZE - 1);
  properties[0].specVersion = 1;
  return VK_SUCCESS;
}

// The backend's interface table names frame generation's swapchain hook, which lives with
// the SDK's replacement swapchain - not built: FSR 3.1's frame generation is not used.
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) {
  return FFX_ERROR_INVALID_ARGUMENT;
}

namespace warden {
bool VideoFourByThree();  // warden_video.cc: Aspect ratio 4:3 (PLAN 9.22)
}  // namespace warden

namespace warden::fsr3 {

void SetHalfPrecision(bool half) { g_half.store(half, std::memory_order_relaxed); }

void RenderSize(uint32_t& width, uint32_t& height) {
  struct Size {
    uint32_t width, height;
  };
  static const Size size = [] {
    char word[32] = {};
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    if (FILE* file = dir ? std::fopen((std::string(dir) + "/fsr3").c_str(), "r") : nullptr) {
      if (std::fscanf(file, "%31s", word) != 1) {
        word[0] = 0;
      }
      std::fclose(file);
    }
    // PLAN 9.22: Aspect ratio 4:3 - the profiles' 4:3 sizes, of the largest 4:3 area the
    // panel shows (1440x1080 on the Thor, 1280x960 on a 1280x960 screen), the widths in
    // whole 80-pixel EDRAM tiles: Quality 960x720, Balanced 880x660, Performance 720x540,
    // Ultra Performance 480x360. The Rendering resolution's 4:3 sizes below.
    const bool four_by_three = warden::VideoFourByThree();
    if (four_by_three) {
      if (!std::strcmp(word, "quality")) {
        return Size{960, 720};
      }
      if (!std::strcmp(word, "balanced")) {
        return Size{880, 660};
      }
      if (!std::strcmp(word, "performance")) {
        return Size{720, 540};
      }
      if (!std::strcmp(word, "ultra")) {
        return Size{480, 360};
      }
    }
    if (!std::strcmp(word, "performance")) {
      return Size{960, 540};
    }
    // PLAN 10.9: FSR's Ultra Performance ratio, 3x - the width in whole 80-pixel EDRAM
    // tiles, as the others (an option: the owner's rule of 2026-10-31).
    if (!std::strcmp(word, "ultra")) {
      return Size{640, 360};
    }
    if (!std::strcmp(word, "balanced")) {
      return Size{1120, 630};
    }
    if (!std::strcmp(word, "quality")) {
      return Size{1280, 720};
    }
    // PLAN 9.16: without FSR 3.1, the Rendering resolution option (files/resolution):
    // 960x540, 1280x720 (the game's own, the default), 1600x900 or 1920x1080 - the
    // widths in whole 80-pixel EDRAM tiles. The sizes above 1280x720 do not fit the
    // EDRAM in one tile; the scene is then drawn as two (warden_game.cc).
    char resolution[32] = {};
    if (FILE* file =
            dir ? std::fopen((std::string(dir) + "/resolution").c_str(), "r") : nullptr) {
      if (std::fscanf(file, "%31s", resolution) != 1) {
        resolution[0] = 0;
      }
      std::fclose(file);
    }
    if (four_by_three) {
      // 640x480, 960x720 (the default), 1280x960 - each in the EDRAM in one tile (240, 540
      // and 960 tiles of the 1,024) - and 1440x1080 at 720x540, twice it (RenderScale).
      if (!std::strcmp(resolution, "640x480")) {
        return Size{640, 480};
      }
      if (!std::strcmp(resolution, "1280x960")) {
        return Size{1280, 960};
      }
      if (!std::strcmp(resolution, "1440x1080")) {
        return Size{720, 540};
      }
      return Size{960, 720};
    }
    if (!std::strcmp(resolution, "960x540")) {
      return Size{960, 540};
    }
    if (!std::strcmp(resolution, "1360x768")) {
      return Size{1360, 768};
    }
    // Above 1280x720 the game's layout of the EDRAM does not fit (its depth at 0 and its
    // colour at 1024 for every pass; at 1920x1080 each needs 1,632 tiles): the device at
    // half the size, rendered at twice it (RenderScale).
    if (!std::strcmp(resolution, "1600x900")) {
      return Size{800, 450};
    }
    if (!std::strcmp(resolution, "1920x1080")) {
      return Size{960, 540};
    }
    if (!std::strcmp(resolution, "2560x1440")) {
      return Size{1280, 720};
    }
    // The EDRAM investigation's native sizes (files/resolution only, drawn wrongly).
    if (!std::strcmp(resolution, "native-1600x900")) {
      return Size{1600, 900};
    }
    if (!std::strcmp(resolution, "native-1920x1080")) {
      return Size{1920, 1080};
    }
    return Size{1280, 720};
  }();
  width = size.width;
  height = size.height;
}

uint32_t RenderScale() {
  static const uint32_t scale = [] {
    const char* dir = std::getenv("WARDEN_FILES_DIR");
    char word[32] = {};
    char resolution[32] = {};
    for (auto [name, out] : {std::pair<const char*, char*>{"fsr3", word},
                             std::pair<const char*, char*>{"resolution", resolution}}) {
      if (FILE* file = dir ? std::fopen((std::string(dir) + "/" + name).c_str(), "r")
                           : nullptr) {
        if (std::fscanf(file, "%31s", out) != 1) {
          out[0] = 0;
        }
        std::fclose(file);
      }
    }
    // FSR 3.1's profiles pick the size themselves.
    if (word[0] && std::strcmp(word, "off") != 0) {
      return 1u;
    }
    if (warden::VideoFourByThree()) {
      return !std::strcmp(resolution, "1440x1080") ? 2u : 1u;  // PLAN 9.22
    }
    return (!std::strcmp(resolution, "1600x900") || !std::strcmp(resolution, "1920x1080") ||
            !std::strcmp(resolution, "2560x1440"))
               ? 2u
               : 1u;
  }();
  return scale;
}

namespace {
std::vector<uint8_t> g_scratch;
FfxInterface g_interface = {};
FfxFsr3UpscalerContext g_context = {};
bool g_context_made = false;

void Message(FfxMsgType type, const wchar_t* message) {
  char text[512] = {};
  std::wcstombs(text, message, sizeof(text) - 1);
  if (type == FFX_MESSAGE_TYPE_ERROR) {
    XELOGE("FSR 3.1: {}", text);
  } else {
    XELOGW("FSR 3.1: {}", text);
  }
}
}  // namespace

bool Probe(VkPhysicalDevice physical_device, VkDevice device) {
  if (g_context_made) {
    return true;
  }
  const size_t size = ffxGetScratchMemorySizeVK(physical_device, FFX_FSR3UPSCALER_CONTEXT_COUNT);
  g_scratch.assign(size, 0);
  VkDeviceContext vulkan = {device, physical_device, vkGetDeviceProcAddr};
  FfxErrorCode error = ffxGetInterfaceVK(&g_interface, ffxGetDeviceVK(&vulkan), g_scratch.data(),
                                         size, FFX_FSR3UPSCALER_CONTEXT_COUNT);
  if (error != FFX_OK) {
    XELOGE("FSR 3.1: no Vulkan backend ({:08X})", uint32_t(error));
    return false;
  }
  FfxFsr3UpscalerContextDescription description = {};
  description.flags = FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
  description.maxRenderSize = {1280, 720};
  description.maxUpscaleSize = {1920, 1080};
  description.fpMessage = Message;
  description.backendInterface = g_interface;
  error = ffxFsr3UpscalerContextCreate(&g_context, &description);
  if (error != FFX_OK) {
    XELOGE("FSR 3.1: the upscaler context was not made ({:08X})", uint32_t(error));
    return false;
  }
  g_context_made = true;
  FfxEffectMemoryUsage memory = {};
  ffxFsr3UpscalerContextGetGpuMemoryUsage(&g_context, &memory);
  XELOGI("FSR 3.1: the upscaler is up on the game's device - 1280x720 to 1920x1080, "
         "{:.1f} MB of GPU memory ({:.1f} MB aliasable), {:.1f} KB of scratch",
         double(memory.totalUsageInBytes) / 1048576.0,
         double(memory.aliasableUsageInBytes) / 1048576.0, double(size) / 1024.0);
  return true;
}

}  // namespace warden::fsr3
