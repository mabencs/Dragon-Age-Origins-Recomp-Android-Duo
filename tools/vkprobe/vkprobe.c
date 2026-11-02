/*
 * vkprobe - report the Vulkan capabilities this device actually offers.
 *
 * Phase 0 risk probe. Xenia's Vulkan backend leans on a specific set of Vulkan
 * features and extensions; desktop GPUs provide them freely, mobile ones often
 * do not. Rather than discover a missing feature deep into the renderer port,
 * this asks the device directly.
 *
 * The features listed below are the ones Xenia's Xenos emulation depends on:
 *   geometryShader        - Xenos primitive types (rectangle/quad lists) are
 *                           expanded with geometry shaders.
 *   tessellationShader    - Xenos patch primitives.
 *   sparseBinding         - how the shared-memory emulation maps guest memory.
 *   fragmentStoresAndAtomics / fragment_shader_interlock
 *                         - the render-target ("ROV") path used to emulate
 *                           EDRAM behaviour without native equivalents.
 *   shaderStorageImage*   - render target cache paths.
 *
 * Build and run with tools/vkprobe/run.sh
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/utsname.h>

#define MAX_EXT 1024

static void *g_lib;

#define LOAD(sym) ((PFN_##sym)dlsym(g_lib, #sym))

/* Extensions Xenia's Vulkan backend looks for, plus the HDR ones this
 * project needs for its presentation path. */
static const char *kWantedDevice[] = {
    "VK_KHR_swapchain",
    "VK_KHR_dedicated_allocation",
    "VK_KHR_image_format_list",
    "VK_KHR_sampler_mirror_clamp_to_edge",
    "VK_KHR_shader_float_controls",
    "VK_KHR_spirv_1_4",
    "VK_KHR_bind_memory2",
    "VK_KHR_get_memory_requirements2",
    "VK_KHR_maintenance1",
    "VK_KHR_portability_subset",
    "VK_EXT_fragment_shader_interlock",
    "VK_EXT_shader_stencil_export",
    "VK_EXT_memory_budget",
    "VK_EXT_external_memory_host",
    "VK_EXT_hdr_metadata",
    "VK_EXT_non_seamless_cube_map",
    "VK_EXT_4444_formats",
    "VK_EXT_custom_border_color",
    "VK_EXT_provoking_vertex",
    "VK_EXT_subgroup_size_control",
    "VK_EXT_swapchain_maintenance1",
    "VK_KHR_8bit_storage",
    "VK_KHR_16bit_storage",
    "VK_KHR_shader_atomic_int64",
};

static const char *kWantedInstance[] = {
    "VK_KHR_surface",
    "VK_KHR_android_surface",
    "VK_KHR_get_physical_device_properties2",
    "VK_KHR_get_surface_capabilities2",
    "VK_EXT_swapchain_colorspace", /* required for HDR10 / PQ output */
    "VK_EXT_debug_utils",
};

static int has_ext(VkExtensionProperties *list, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(list[i].extensionName, name) == 0) return 1;
    }
    return 0;
}

static void report(const char *label, int ok) {
    printf("  %-44s %s\n", label, ok ? "yes" : "MISSING");
}

static const char *dev_type(VkPhysicalDeviceType t) {
    switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
        default:                                     return "other";
    }
}

int main(void) {
    struct utsname u;
    if (uname(&u) == 0) {
        printf("host: %s %s (%s)\n", u.sysname, u.release, u.machine);
    }
    printf("built for: %s\n\n",
#if defined(__aarch64__)
           "aarch64 (arm64-v8a)"
#elif defined(__arm__)
           "arm32"
#elif defined(__x86_64__)
           "x86_64"
#else
           "unknown"
#endif
    );

    g_lib = dlopen("libvulkan.so", RTLD_NOW);
    if (!g_lib) {
        printf("FATAL: cannot load libvulkan.so: %s\n", dlerror());
        return 1;
    }
    printf("libvulkan.so loaded\n");

    PFN_vkEnumerateInstanceVersion pEnumVer = LOAD(vkEnumerateInstanceVersion);
    PFN_vkEnumerateInstanceExtensionProperties pInstExt =
        LOAD(vkEnumerateInstanceExtensionProperties);
    PFN_vkCreateInstance pCreate = LOAD(vkCreateInstance);
    if (!pInstExt || !pCreate) {
        printf("FATAL: core entry points missing\n");
        return 1;
    }

    uint32_t loader_ver = VK_API_VERSION_1_0;
    if (pEnumVer) pEnumVer(&loader_ver);
    printf("loader api version: %u.%u.%u\n\n", VK_VERSION_MAJOR(loader_ver),
           VK_VERSION_MINOR(loader_ver), VK_VERSION_PATCH(loader_ver));

    /* ---- instance extensions ---- */
    uint32_t n_inst = 0;
    pInstExt(NULL, &n_inst, NULL);
    if (n_inst > MAX_EXT) n_inst = MAX_EXT;
    VkExtensionProperties *inst =
        calloc(n_inst ? n_inst : 1, sizeof(VkExtensionProperties));
    pInstExt(NULL, &n_inst, inst);
    printf("INSTANCE EXTENSIONS (%u total)\n", n_inst);
    for (size_t i = 0; i < sizeof(kWantedInstance) / sizeof(*kWantedInstance); i++) {
        report(kWantedInstance[i], has_ext(inst, n_inst, kWantedInstance[i]));
    }

    /* ---- instance ---- */
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkprobe";
    app.apiVersion = loader_ver;

    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = pCreate(&ici, NULL, &instance);
    if (r != VK_SUCCESS) {
        printf("\nFATAL: vkCreateInstance failed (%d)\n", r);
        return 1;
    }

    PFN_vkGetInstanceProcAddr gipa = LOAD(vkGetInstanceProcAddr);
#define IPA(sym) ((PFN_##sym)gipa(instance, #sym))
    PFN_vkEnumeratePhysicalDevices pEnumDev = IPA(vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceProperties pProps = IPA(vkGetPhysicalDeviceProperties);
    PFN_vkGetPhysicalDeviceFeatures pFeats = IPA(vkGetPhysicalDeviceFeatures);
    PFN_vkEnumerateDeviceExtensionProperties pDevExt =
        IPA(vkEnumerateDeviceExtensionProperties);
    PFN_vkGetPhysicalDeviceMemoryProperties pMem =
        IPA(vkGetPhysicalDeviceMemoryProperties);

    uint32_t n_dev = 0;
    pEnumDev(instance, &n_dev, NULL);
    if (n_dev == 0) {
        printf("\nFATAL: no Vulkan physical devices\n");
        return 1;
    }
    VkPhysicalDevice *devs = calloc(n_dev, sizeof(VkPhysicalDevice));
    pEnumDev(instance, &n_dev, devs);
    printf("\nphysical devices: %u\n", n_dev);

    for (uint32_t d = 0; d < n_dev; d++) {
        VkPhysicalDeviceProperties p;
        pProps(devs[d], &p);
        printf("\n==================================================\n");
        printf("DEVICE %u: %s\n", d, p.deviceName);
        printf("==================================================\n");
        printf("  type              %s\n", dev_type(p.deviceType));
        printf("  api version       %u.%u.%u\n", VK_VERSION_MAJOR(p.apiVersion),
               VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion));
        printf("  driver version    0x%08X\n", p.driverVersion);
        printf("  vendor / device   0x%04X / 0x%04X\n", p.vendorID, p.deviceID);

        const VkPhysicalDeviceLimits *L = &p.limits;
        printf("\n  LIMITS THAT MATTER\n");
        printf("    maxImageDimension2D          %u\n", L->maxImageDimension2D);
        printf("    maxUniformBufferRange        %u\n", L->maxUniformBufferRange);
        printf("    maxStorageBufferRange        %u\n", L->maxStorageBufferRange);
        printf("    maxPushConstantsSize         %u\n", L->maxPushConstantsSize);
        printf("    maxBoundDescriptorSets       %u\n", L->maxBoundDescriptorSets);
        printf("    maxColorAttachments          %u\n", L->maxColorAttachments);
        printf("    maxSamplerAnisotropy         %.0f\n", (double)L->maxSamplerAnisotropy);
        printf("    maxViewports                 %u\n", L->maxViewports);
        printf("    minUniformBufferOffsetAlign  %llu\n",
               (unsigned long long)L->minUniformBufferOffsetAlignment);
        printf("    minStorageBufferOffsetAlign  %llu\n",
               (unsigned long long)L->minStorageBufferOffsetAlignment);
        printf("    sparseAddressSpaceSize       %llu\n",
               (unsigned long long)p.sparseProperties.residencyStandard2DBlockShape);

        VkPhysicalDeviceFeatures f;
        pFeats(devs[d], &f);
        printf("\n  FEATURES XENIA DEPENDS ON\n");
        report("geometryShader", f.geometryShader);
        report("tessellationShader", f.tessellationShader);
        report("sparseBinding", f.sparseBinding);
        report("sparseResidencyBuffer", f.sparseResidencyBuffer);
        report("fragmentStoresAndAtomics", f.fragmentStoresAndAtomics);
        report("vertexPipelineStoresAndAtomics", f.vertexPipelineStoresAndAtomics);
        report("independentBlend", f.independentBlend);
        report("samplerAnisotropy", f.samplerAnisotropy);
        report("textureCompressionBC", f.textureCompressionBC);
        report("shaderStorageImageExtendedFormats",
               f.shaderStorageImageExtendedFormats);
        report("shaderStorageImageWriteWithoutFormat",
               f.shaderStorageImageWriteWithoutFormat);
        report("shaderClipDistance", f.shaderClipDistance);
        report("shaderCullDistance", f.shaderCullDistance);
        report("depthClamp", f.depthClamp);
        report("depthBiasClamp", f.depthBiasClamp);
        report("fillModeNonSolid", f.fillModeNonSolid);
        report("imageCubeArray", f.imageCubeArray);
        report("multiViewport", f.multiViewport);
        report("occlusionQueryPrecise", f.occlusionQueryPrecise);
        report("shaderImageGatherExtended", f.shaderImageGatherExtended);
        report("shaderTessellationAndGeometryPointSize",
               f.shaderTessellationAndGeometryPointSize);

        uint32_t n_de = 0;
        pDevExt(devs[d], NULL, &n_de, NULL);
        uint32_t cap = n_de > MAX_EXT ? MAX_EXT : n_de;
        VkExtensionProperties *de =
            calloc(cap ? cap : 1, sizeof(VkExtensionProperties));
        pDevExt(devs[d], NULL, &cap, de);
        printf("\n  DEVICE EXTENSIONS (%u total)\n", n_de);
        for (size_t i = 0; i < sizeof(kWantedDevice) / sizeof(*kWantedDevice); i++) {
            report(kWantedDevice[i], has_ext(de, cap, kWantedDevice[i]));
        }

        VkPhysicalDeviceMemoryProperties mp;
        pMem(devs[d], &mp);
        printf("\n  MEMORY HEAPS (%u)\n", mp.memoryHeapCount);
        for (uint32_t h = 0; h < mp.memoryHeapCount; h++) {
            printf("    heap %u: %6llu MB%s\n", h,
                   (unsigned long long)(mp.memoryHeaps[h].size / (1024 * 1024)),
                   (mp.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                       ? "  DEVICE_LOCAL" : "");
        }
        free(de);
    }

    printf("\nprobe complete\n");
    return 0;
}
