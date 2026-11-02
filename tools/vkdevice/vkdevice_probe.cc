// Phase 3.2 device bring-up probe.
//
// Runs Xenia's *own* Vulkan initialisation on the handheld - VulkanProvider,
// VulkanInstance, VulkanDevice - rather than a hand-written Vulkan sample. The
// Phase 0 probe already measured what the driver exposes; this asks the different
// and more important question: does Xenia accept this device for Xenos emulation?
//
// VulkanProvider::Create(with_gpu_emulation = true) is the interesting call,
// because that is the path that demands the features the GPU emulation needs and
// refuses the device if they are missing. Presentation is requested separately and
// is skipped here: there is no surface until the Android app shell exists in Phase 4.

#include <cstdio>
#include <cstdlib>

#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace {

void Line() { std::printf("------------------------------------------------------------\n"); }

void Flag(const char* name, bool value) {
  std::printf("    %-46s %s\n", name, value ? "yes" : "no");
}

const char* VersionString(uint32_t version, char* buffer, size_t size) {
  std::snprintf(buffer, size, "%u.%u.%u", VK_VERSION_MAJOR(version),
                VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));
  return buffer;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  (void)argc;
  (void)argv;

  std::printf("Xenia Vulkan device bring-up probe\n");
#if defined(__aarch64__)
  std::printf("arch: aarch64\n");
#else
  std::printf("arch: other\n");
#endif
  Line();

  // with_gpu_emulation: require everything the Xenos emulation needs.
  // with_presentation: no surface yet, that arrives with the app shell.
  std::printf("calling VulkanProvider::Create(gpu_emulation = true, presentation = false)\n");
  auto provider = xe::ui::vulkan::VulkanProvider::Create(true, false);
  if (!provider) {
    std::printf("\nRESULT: Create() returned null.\n");
    std::printf("Xenia refused this device for GPU emulation. The reason is in the\n");
    std::printf("log above - it names the feature or extension that was missing.\n");
    return 1;
  }
  std::printf("  provider created\n");

  const xe::ui::vulkan::VulkanInstance* instance = provider->vulkan_instance();
  const xe::ui::vulkan::VulkanDevice* device = provider->vulkan_device();
  if (!instance || !device) {
    std::printf("\nRESULT: provider exists but instance=%p device=%p\n",
                (const void*)instance, (const void*)device);
    return 1;
  }

  char buf[32];
  const auto& props = device->properties();
  Line();
  std::printf("DEVICE\n");
  std::printf("    %-46s %s\n", "name", props.deviceName);
  std::printf("    %-46s %s\n", "api version",
              VersionString(props.apiVersion, buf, sizeof(buf)));
  std::printf("    %-46s 0x%08X\n", "driver version", props.driverVersion);
  std::printf("    %-46s 0x%04X\n", "vendor id", props.vendorID);

  Line();
  std::printf("EXTENSIONS XENIA ENABLED\n");
  const auto& ext = device->extensions();
  Flag("VK_KHR_swapchain", ext.ext_KHR_swapchain);
  Flag("VK_KHR_dedicated_allocation", ext.ext_1_1_KHR_dedicated_allocation);
  Flag("VK_EXT_shader_stencil_export", ext.ext_EXT_shader_stencil_export);
  Flag("VK_EXT_sample_locations", ext.ext_EXT_sample_locations);
  Flag("VK_KHR_get_memory_requirements2", ext.ext_1_1_KHR_get_memory_requirements2);
  Flag("VK_KHR_image_format_list", ext.ext_1_2_KHR_image_format_list);
  Flag("VK_KHR_sampler_ycbcr_conversion", ext.ext_1_1_KHR_sampler_ycbcr_conversion);
  Flag("VK_KHR_bind_memory2", ext.ext_1_1_KHR_bind_memory2);
  Flag("VK_KHR_spirv_1_4", ext.ext_1_2_KHR_spirv_1_4);
  Flag("VK_EXT_memory_budget", ext.ext_EXT_memory_budget);
  Flag("VK_EXT_host_query_reset", ext.ext_1_2_EXT_host_query_reset);
  Flag("VK_KHR_maintenance4", ext.ext_1_3_KHR_maintenance4);
  Flag("VK_EXT_conditional_rendering", ext.ext_EXT_conditional_rendering);

  Line();
  std::printf("QUEUES\n");
  std::printf("    %-46s %zu\n", "queue families",
              device->queue_families().size());
  std::printf("    %-46s %u\n", "graphics+compute family",
              device->queue_family_graphics_compute());
  std::printf("    %-46s %u\n", "sparse binding family",
              device->queue_family_sparse_binding());

  // ------------------------------------------------------------------ EDRAM
  // Phase 3.3. The Xbox 360's 10 MB of embedded DRAM has no modern equivalent.
  // Xenia emulates it two ways and this device forces the less accurate one:
  // kPixelShaderInterlock needs VK_EXT_fragment_shader_interlock, which Adreno 740
  // does not expose, so kHostRenderTargets is used - the path upstream's own comment
  // calls "may be irreparably inaccurate ... at the mercy of the host API's
  // fixed-function output-merger, primarily because it has to perform blending".
  //
  // That inaccuracy is not vague though: it turns on specific format capabilities.
  // The checks below mirror the ones in vulkan_render_target_cache.cc rather than
  // calling into it, because the render target cache needs a command processor and
  // this probe deliberately links only the device layer.
  const auto& ifn = instance->functions();
  VkPhysicalDevice physical_device = device->physical_device();

  auto FormatHas = [&](VkFormat format, VkFormatFeatureFlags required) {
    VkFormatProperties properties;
    ifn.vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
  };
  constexpr VkFormatFeatureFlags kColorBlend =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
  constexpr VkFormatFeatureFlags kDepthAttachment =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

  Line();
  std::printf("EDRAM PATH (Phase 3.3)\n");
  const bool fsi_available =
      props.fragmentShaderSampleInterlock || props.fragmentShaderPixelInterlock;
  Flag("fragmentShaderSampleInterlock", props.fragmentShaderSampleInterlock);
  Flag("fragmentShaderPixelInterlock", props.fragmentShaderPixelInterlock);
  std::printf("    %-46s %s\n", "accurate (kPixelShaderInterlock) path",
              fsi_available ? "AVAILABLE" : "unavailable");
  std::printf("    %-46s %s\n", "path in use",
              fsi_available ? "kPixelShaderInterlock" : "kHostRenderTargets");
  std::printf("  that path would also need:\n");
  Flag("fragmentStoresAndAtomics", props.fragmentStoresAndAtomics);
  Flag("sampleRateShading", props.sampleRateShading);
  Flag("standardSampleLocations", props.standardSampleLocations);
  std::printf("    %-46s %u\n", "maxPerStageDescriptorStorageBuffers",
              props.maxPerStageDescriptorStorageBuffers);

  Line();
  std::printf("EDRAM ACCURACY SWITCHES\n");
  const bool gamma_unorm16 = FormatHas(VK_FORMAT_R16G16B16A16_UNORM, kColorBlend);
  std::printf("  gamma targets blended as linear UNORM16   %s\n",
              gamma_unorm16 ? "YES - accurate"
                            : "NO - would blend in 8-bit gamma space");
  const bool depth24 = FormatHas(VK_FORMAT_D24_UNORM_S8_UINT, kDepthAttachment);
  std::printf("  depth format                              %s\n",
              depth24 ? "D24_UNORM_S8_UINT - matches Xenos 24-bit"
                      : "D32_SFLOAT_S8_UINT fallback - different precision");
  std::printf("  float24 depth converted in pixel shader   %s\n",
              props.sampleRateShading ? "YES - antialiases under MSAA"
                                      : "NO - transfer-time conversion instead");

  Line();
  std::printf("RENDER TARGET FORMATS (sampled + colour attachment + blend)\n");
  struct FormatCheck {
    const char* name;
    VkFormat format;
  };
  static const FormatCheck kColorFormats[] = {
      {"R8G8B8A8_UNORM           k_8_8_8_8", VK_FORMAT_R8G8B8A8_UNORM},
      {"R16G16B16A16_UNORM       gamma blend", VK_FORMAT_R16G16B16A16_UNORM},
      {"A2B10G10R10_UNORM_PACK32 k_2_10_10_10", VK_FORMAT_A2B10G10R10_UNORM_PACK32},
      {"R16G16B16A16_SFLOAT      k_16_16_16_16_FLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
      {"R16G16_SNORM             k_16_16", VK_FORMAT_R16G16_SNORM},
      {"R16G16B16A16_SNORM       k_16_16_16_16", VK_FORMAT_R16G16B16A16_SNORM},
      {"R16G16_SFLOAT            k_16_16_FLOAT", VK_FORMAT_R16G16_SFLOAT},
      {"R32_SFLOAT               k_32_FLOAT", VK_FORMAT_R32_SFLOAT},
      {"R32G32_SFLOAT            k_32_32_FLOAT", VK_FORMAT_R32G32_SFLOAT},
  };
  const size_t color_format_count =
      sizeof(kColorFormats) / sizeof(kColorFormats[0]);
  size_t unsupported = 0;
  for (const FormatCheck& check : kColorFormats) {
    bool ok = FormatHas(check.format, kColorBlend);
    if (!ok) {
      ++unsupported;
    }
    Flag(check.name, ok);
  }

  Line();
  std::printf("RESULT: Xenia accepted this device for Xenos GPU emulation.\n");
  std::printf("EDRAM:  %s path; %zu of %zu render target formats cannot blend.\n",
              fsi_available ? "accurate" : "kHostRenderTargets", unsupported,
              color_format_count);
  return 0;
}
