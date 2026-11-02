// PLAN 9.4: FSR 3.1 in the game's frames - DAORecomp's temporal upscaling
// (third_party/DAORecomp/xenia/src/xenia/gpu/d3d12/dao_upscale*.cc, Direct3D 12 only) on
// Xenia's Vulkan backend, with the FidelityFX SDK's FSR 3.1 upscaler (warden_fsr3.cc has
// how the SDK is built). The class is declared in vulkan_command_processor.h by the
// graft (tools/assemble_runtime.py), which also calls its hooks and makes it a friend of
// the render target cache.
//
// A 3D frame of the game (Eclipse engine, Xbox 360 Direct3D 9), as dao_upscale.h has it:
//   depth prepass, shadow maps -> scene draws (one single-sampled 1280x720 tile here,
//   warden_game.cc's SingleTileScene, as DAORecomp makes it for its upscaler), resolved:
//   colour to the scene texture, depth to a depth texture -> full-screen post-processing
//   passes reading the scene texture (fog, bloom, the tonemapper) -> the UI on the same
//   render target -> resolve to the front buffer -> swap.
// What FSR 3.1 gets, as DAORecomp gives it:
//   - jitter: added to the viewport offset of the scene's depth-tested screen-wide draws
//     before the scene's colour is resolved, so everything up to the UI is jittered;
//   - depth: the scene's depth, copied at the first resolve after the scene's draws, from
//     the depth buffer they used;
//   - colour: the render target right after the last full-screen pass that reads the
//     scene texture - tonemapped, no UI yet;
//   - motion vectors: the camera's (session 36) - each pixel's place in the frame before
//     from the scene's depth and the camera's reprojection, which the static meshes'
//     world-view-projection matrices in both frames give (warden_upscale_motion.cs.glsl).
//     What moves of its own gets the camera's motion only; DAORecomp runs each opaque
//     draw's vertex shader again with stream output for that, not done here.
// At the swap, FSR 3.1's output is put together with the UI (warden_upscale_composite:
// where the front buffer differs from the image FSR 3.1 got, the front buffer scaled up
// instead) and replaces the front buffer as what the presenter shows, at the size it is
// shown.
//
// The setting is files/fsr3: "quality" upscales the title's 1280x720 to the panel's
// 1920x1080, FSR's own Quality ratio; off without it. files/fsr3-show-ui (a test
// switch) paints where the UI was found red over the scene in gray. FSR 3.1 takes its
// 16-bit permutations where the device has 16-bit arithmetic; files/fsr3-half holding
// "off" keeps the 32-bit ones, "alternate" (a test switch) changes between the two after
// each timed window of frames, so one run compares them on the same scene. PLAN 10.9: the
// accumulate pass reprojects the history with the SDK's Lanczos lookup table, as the SDK
// does on AMD's GPUs, rather than its reference weights (sines) - 3.70 ms a frame against
// 4.17 at the King's Camp's 1080p (run 513), the weights within about 0.0003;
// files/fsr3-lut (a test switch) holding "off" keeps the reference weights, "alternate"
// changes between the two the same way.
#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>

#include "warden_deck.h"
#include "warden_camera_fov.h"
#include "warden_fsr3.h"
#include "warden_frame_generation.h"
#include "warden_hudless.h"
#include "warden_gpu.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/vulkan/vulkan_util.h"

extern "C" bool warden_pipeline_statistics_wanted();
extern "C" void warden_pipeline_statistics(VkDevice device, VkPipeline pipeline,
                                           uint64_t vertex_shader, uint64_t pixel_shader);

// libvulkan's own export: the runtime's vulkan_api.h declares no prototypes.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                       const char* name);

namespace xe {
namespace gpu {

namespace {

namespace shaders {
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_upscale_composite_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_motion_blur_tile_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_motion_blur_neighbor_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_dof_focus_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_dof_prefilter_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_dof_blur_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_dof_combine_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_upscale_copy_color_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_upscale_copy_depth_cs.h"
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_upscale_motion_cs.h"
}  // namespace shaders

using vulkan::DeferredCommandBuffer;
using vulkan::VulkanCommandProcessor;
using vulkan::VulkanRenderTargetCache;

// Descriptor pools, one per frame in flight (Xenia keeps three, kMaxFramesInFlight),
// each reset when the frame that used it is done, and the sets a frame may take.
constexpr uint32_t kFrames = 3;
constexpr uint32_t kSetsPerFrame = 64;
// FSR 3.1's sharpening (RCAS) after the upscale, 0 (off) to 1: DAORecomp's default.
constexpr float kSharpness = 0.3f;
// The camera FSR 3.1 is told of until the static meshes' matrices give the game's own
// (CameraPlanes): DAORecomp's stand-ins (dao_upscale_impl.h), which FSR's depth
// heuristics - its disocclusion test in metres - would otherwise use.
constexpr float kCameraNear = 0.1f;
constexpr float kCameraFar = 1000.0f;
constexpr float kCameraFovVertical = 1.0f;

struct CopyConstants {
  int32_t source_offset[2];
  int32_t dest_offset[2];
  uint32_t size[2];
  int32_t source_min[2];
};

struct CompositeConstants {
  uint32_t dest_size[2];
  float render_size[2];
  uint32_t show_ui;
  // PLAN 9.18: motion blur's samples per pixel (0: none) and render pixels per tile.
  uint32_t blur_samples;
  uint32_t blur_tile;
};

// PLAN 9.18: warden_motion_blur_tile.cs.glsl's and warden_motion_blur_neighbor.cs.glsl's.
struct BlurConstants {
  uint32_t size[2];
  uint32_t tile;
  float scale;
  float cut;
  uint32_t counter;
  uint32_t cut_tiles;
  uint32_t previous_valid;
};

// PLAN 9.18: motion blur's level, 0 off, 1 low, 2 high (-1: not read yet) - DAORecomp's
// Low and High: the shutter open for a quarter or half of the frame, 8 or 12 samples.
std::atomic<int> g_motion_blur{-1};
constexpr float kBlurShutter[2] = {0.25f, 0.5f};
constexpr uint32_t kBlurSamples[2] = {8, 12};
// The longest blur: a tile, 1/24 of the render height (30 pixels at 720p), at least 8.
constexpr uint32_t kBlurTilesPerHeight = 24;
constexpr uint32_t kBlurMinTile = 8;
// Motion over a quarter of the render height in a frame is a camera cut; with a cut in
// more than an eighth of the tiles, the whole frame is one.
constexpr float kBlurCut = 0.25f;
constexpr uint32_t kBlurCutTileFraction = 8;

// PLAN 9.19: warden_dof.glsli's constants.
struct DofConstants {
  uint32_t display_size[2];
  uint32_t half_size[2];
  uint32_t render_size[2];
  float aperture;
  float max_radius;
  float strength;
  float focus_rate;
  float gamma;
  uint32_t samples;
  uint32_t flags;
};
constexpr uint32_t kDofFlagSnap = 1;
constexpr uint32_t kDofFlagShow = 2;
// PLAN 9.19: depth of field in conversations and cutscenes, on or off (-1: not read).
std::atomic<int> g_depth_of_field{-1};
// DAORecomp's lens: the blur of a surface at infinity with the lens focused at the near
// plane, and the most blur, as fractions of the display height, at a 60-degree vertical
// field of view (narrower shots blur more, by the square root of the zoom).
constexpr float kDofAperture = 0.009f;
constexpr float kDofMaxRadius = 0.014f;
constexpr double kDofReferenceHalfFov = 3.14159265358979323846 / 6.0;
// The blur's samples: one per this many half size pixels of its disc, a multiple of 8.
constexpr float kDofAreaPerSample = 4.0f;
constexpr uint32_t kDofMinSamples = 16;
constexpr uint32_t kDofMaxSamples = 64;
// The effect comes and goes over this, the focus follows a change with this time
// constant, and a longer frame counts as this (ms).
constexpr float kDofEaseMs = 300.0f;
constexpr float kDofFocusMs = 150.0f;
constexpr float kDofMaxFrameMs = 100.0f;
// A change of the field of view in a frame that is a cut to another shot.
constexpr float kDofFovCut = 1.02f;
// Frames the focus is taken at once for after a cut.
constexpr uint32_t kDofSnapFrames = 2;
constexpr double kDofMinHalfFov = 0.05;
constexpr double kDofMaxHalfFov = 1.5;
constexpr float kDofDefaultFov = 1.0f;
// The game's picture is encoded with this gamma (DAORecomp's present_hdr_gamma).
constexpr float kDofGamma = 2.2f;

// warden_upscale_motion.cs.glsl's.
struct MotionConstants {
  float reprojection[16];
  float viewport_scale[4];
  float viewport_offset[4];
  float jitter[2];
  uint32_t size[2];
};

// The motion vectors are the camera's: the clip space of the frame before from this
// one's is WVP(before) * inverse(WVP(now)) for any mesh that has not moved - its world
// matrix cancels out - so the static meshes' world-view-projection matrices give it. These
// vertex shaders compute the position as c0 x + c1 y + c2 z + c3 from a 16-bit float
// position without bones: their c0-c3 are that matrix's columns (the King's Camp's
// commonest two, 444 and 158 of a frame's draws - run 391's trace).
constexpr uint64_t kStaticMeshVertexShaders[] = {0xF003B6862AAE7F16ull,
                                                 0x737FF30C11453B86ull};
// Static meshes kept a frame (a frame at the camp has some 600), and matched at most.
constexpr size_t kMaxMeshDraws = 1024;
constexpr size_t kMaxMeshMatches = 64;

// A 4x4 matrix as the vertex shader's constants hold it: m[column][row].
struct Matrix {
  double m[4][4];
};

Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix r = {};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      double sum = 0.0;
      for (int k = 0; k < 4; ++k) {
        sum += a.m[k][row] * b.m[column][k];
      }
      r.m[column][row] = sum;
    }
  }
  return r;
}

// Gauss-Jordan with partial pivoting.
bool Invert(const Matrix& in, Matrix& out) {
  double a[4][8];
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      a[row][column] = in.m[column][row];
      a[row][column + 4] = row == column ? 1.0 : 0.0;
    }
  }
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(a[row][column]) > std::abs(a[pivot][column])) {
        pivot = row;
      }
    }
    if (!(std::abs(a[pivot][column]) > 1.0e-30)) {
      return false;
    }
    if (pivot != column) {
      for (int k = 0; k < 8; ++k) {
        std::swap(a[pivot][k], a[column][k]);
      }
    }
    const double scale = 1.0 / a[column][column];
    for (int k = 0; k < 8; ++k) {
      a[column][k] *= scale;
    }
    for (int row = 0; row < 4; ++row) {
      const double factor = a[row][column];
      if (row != column && factor != 0.0) {
        for (int k = 0; k < 8; ++k) {
          a[row][k] -= factor * a[column][k];
        }
      }
    }
  }
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      out.m[column][row] = a[row][column + 4];
      if (!std::isfinite(out.m[column][row])) {
        return false;
      }
    }
  }
  return true;
}

// The camera's planes and vertical field of view from a world-view-projection matrix
// (columns c0-c3): with Direct3D's perspective, clip w is the view depth and clip z is
// a w + b, so the z row is a times the w row plus b in the last column - near -b / a,
// far near a / (a - 1); the y row's length against the w row's is 1 / tan(fov / 2). A
// uniform world scale cancels out of both.
bool CameraPlanes(const float* wvp, float& near_out, float& far_out, float& fov_out) {
  const double z[4] = {wvp[2], wvp[6], wvp[10], wvp[14]};
  const double w[4] = {wvp[3], wvp[7], wvp[11], wvp[15]};
  const double y[3] = {wvp[1], wvp[5], wvp[9]};
  const double w_length2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
  const double y_length2 = y[0] * y[0] + y[1] * y[1] + y[2] * y[2];
  if (!(w_length2 > 0.0) || !(y_length2 > 0.0)) {
    return false;
  }
  const double a = (z[0] * w[0] + z[1] * w[1] + z[2] * w[2]) / w_length2;
  const double b = z[3] - a * w[3];
  const double near = -b / a;
  const double far = near * a / (a - 1.0);
  const double fov = 2.0 * std::atan(std::sqrt(w_length2 / y_length2));
  if (!(near > 0.0) || !(far > near) || !std::isfinite(far) || !(fov > 0.05) ||
      !(fov < 3.0)) {
    return false;
  }
  near_out = float(near);
  far_out = float(far);
  fov_out = float(fov);
  return true;
}

// Where a point in this frame's normalized device coordinates was in the frame before's.
bool Reproject(const Matrix& m, const double ndc[3], double previous[2]) {
  double clip[4];
  for (int row = 0; row < 4; ++row) {
    clip[row] = m.m[0][row] * ndc[0] + m.m[1][row] * ndc[1] + m.m[2][row] * ndc[2] +
                m.m[3][row];
  }
  if (!(clip[3] > 1.0e-9)) {
    return false;
  }
  previous[0] = clip[0] / clip[3];
  previous[1] = clip[1] / clip[3];
  return true;
}

// Two reprojections compared where it matters - in pixels, at test points across the
// screen near and far: a perspective matrix's elements, inverted from float constants,
// carry errors where the picture does not (run 410). Points that do not reproject
// (behind the camera before) count as far apart.
constexpr double kTestPoints[][3] = {{0.0, 0.0, 0.9},    {-0.5, -0.5, 0.9},
                                     {0.5, 0.5, 0.9},    {0.0, 0.0, 0.999},
                                     {-0.5, 0.5, 0.999}, {0.5, -0.5, 0.999}};

double ReprojectionDistance(const Matrix& a, const Matrix& b, const float scale[2]) {
  double distance = 0.0;
  for (const auto& point : kTestPoints) {
    double pa[2], pb[2];
    if (!Reproject(a, point, pa) || !Reproject(b, point, pb)) {
      return 1.0e9;
    }
    distance = std::max({distance, std::abs(pa[0] - pb[0]) * std::abs(scale[0]),
                         std::abs(pa[1] - pb[1]) * std::abs(scale[1])});
  }
  return distance;
}

// An image of the upscaler's own, with its usage tracked for the barriers.
struct Image {
  VkImageCreateInfo create_info = {};
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkAccessFlags access = 0;
  uint32_t width() const { return create_info.extent.width; }
  uint32_t height() const { return create_info.extent.height; }
};

std::string FilesDir() {
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  return dir ? dir : "";
}

std::string SettingWord(const char* name) {
  char text[32] = {};
  if (FILE* file = std::fopen((FilesDir() + "/" + name).c_str(), "r")) {
    if (std::fscanf(file, "%31s", text) != 1) {
      text[0] = 0;
    }
    std::fclose(file);
  }
  return text;
}

void FfxMessage(FfxMsgType type, const wchar_t* message) {
  static std::atomic<uint32_t> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) >= 64) {
    return;
  }
  char text[512] = {};
  std::wcstombs(text, message, sizeof(text) - 1);
  if (type == FFX_MESSAGE_TYPE_ERROR) {
    XELOGE("FSR 3.1: {}", text);
  } else {
    XELOGW("FSR 3.1: {}", text);
  }
}

// Each of FSR 3.1's passes timed: the SDK records its dispatches through the
// vkCmdDispatch the device procedure lookup we hand it returns, so that one writes a
// timestamp after each while the dispatch is timed (ExecuteDispatch sets the range).
// The passes in the order the SDK dispatches them (ffx_fsr3upscaler.cpp).
constexpr uint32_t kMaxPassTimes = 12;
constexpr const char* kPassNames[] = {"prepare inputs",      "luma pyramid",
                                      "shading change pyramid", "shading change",
                                      "prepare reactivity",  "luma instability",
                                      "accumulate",          "RCAS"};
struct PassTiming {
  PFN_vkCmdDispatch dispatch = nullptr;
  PFN_vkCmdWriteTimestamp write_timestamp = nullptr;
  VkQueryPool pool = VK_NULL_HANDLE;
  uint32_t next = 0;
  uint32_t end = 0;
} g_pass_timing;

VKAPI_ATTR void VKAPI_CALL TimedDispatch(VkCommandBuffer command_buffer, uint32_t x,
                                         uint32_t y, uint32_t z) {
  g_pass_timing.dispatch(command_buffer, x, y, z);
  if (g_pass_timing.pool && g_pass_timing.next < g_pass_timing.end) {
    g_pass_timing.write_timestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  g_pass_timing.pool, g_pass_timing.next++);
  }
}

// PLAN 9.14: frame generation's passes named for their timing - its backend's job
// scheduling wrapped, each compute job's pipeline name kept in the order they run.
FfxScheduleGpuJobFunc g_fg_schedule = nullptr;
std::vector<std::string>* g_fg_job_names = nullptr;

// PLAN 10.9: the upscaler's device capabilities wrapped - the SDK takes its Lanczos
// lookup table for the history's reprojection only on GPUs whose subgroups are 32 to 64
// lanes (AMD's), and its backend reports 32 to 32 here (it is told of no subgroup size
// control); this answers 32 to 64 as each context is made, unless files/fsr3-lut is off.
// Nothing asks for 64-lane subgroups by it: the SDK forces those only with shader model
// 6.6, and the backend reports 5.1 (the Adreno runs the passes 64 lanes wide anyway -
// the driver's statistics, run 514).
FfxGetDeviceCapabilitiesFunc g_get_capabilities = nullptr;
std::atomic<bool> g_lanczos_lut{false};

FfxErrorCode LutDeviceCapabilities(FfxInterface* backend, FfxDeviceCapabilities* capabilities) {
  const FfxErrorCode error = g_get_capabilities(backend, capabilities);
  if (error == FFX_OK && g_lanczos_lut.load(std::memory_order_relaxed)) {
    capabilities->waveLaneCountMin = 32;
    capabilities->waveLaneCountMax = 64;
  }
  return error;
}

FfxErrorCode NamedScheduleGpuJob(FfxInterface* backend, const FfxGpuJobDescription* job) {
  if (g_fg_job_names && job->jobType == FFX_GPU_JOB_COMPUTE) {
    std::string name;
    const wchar_t* text = job->computeJobDescriptor.pipeline.name;
    for (uint32_t i = 0; i < FFX_RESOURCE_NAME_SIZE && text[i]; ++i) {
      name += text[i] < 128 ? char(text[i]) : '?';
    }
    g_fg_job_names->push_back(name.empty() ? "?" : name);
  }
  return g_fg_schedule(backend, job);
}

// PLAN 10.9: with files/hack-pipeline-stats, the driver's statistics for the SDK's own
// compute pipelines too - each made with VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR,
// logged by warden_gpu_memory.cc as it is made (VS: the pass, PS: its permutation).
PFN_vkCreateComputePipelines g_create_compute_pipelines = nullptr;
FfxCreatePipelineFunc g_create_pipeline = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL StatisticsCreateComputePipelines(
    VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo* infos, const VkAllocationCallbacks* allocator,
    VkPipeline* pipelines) {
  std::vector<VkComputePipelineCreateInfo> captured(infos, infos + count);
  for (VkComputePipelineCreateInfo& info : captured) {
    info.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
  }
  return g_create_compute_pipelines(device, cache, count, captured.data(), allocator,
                                    pipelines);
}

FfxErrorCode StatisticsCreatePipeline(FfxInterface* backend, FfxEffect effect, FfxPass pass,
                                      uint32_t permutation,
                                      const FfxPipelineDescription* description,
                                      FfxUInt32 effect_context, FfxPipelineState* out) {
  const FfxErrorCode error =
      g_create_pipeline(backend, effect, pass, permutation, description, effect_context, out);
  if (error == FFX_OK && out->pipeline) {
    const auto* device = reinterpret_cast<const VkDeviceContext*>(backend->device);
    warden_pipeline_statistics(device ? device->vkDevice : VK_NULL_HANDLE,
                               reinterpret_cast<VkPipeline>(out->pipeline), uint64_t(pass),
                               permutation);
  }
  return error;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL FfxDeviceProcAddr(VkDevice device, const char* name) {
  if (!std::strcmp(name, "vkCreateComputePipelines") && warden_pipeline_statistics_wanted()) {
    g_create_compute_pipelines =
        reinterpret_cast<PFN_vkCreateComputePipelines>(vkGetDeviceProcAddr(device, name));
    return reinterpret_cast<PFN_vkVoidFunction>(StatisticsCreateComputePipelines);
  }
  if (!std::strcmp(name, "vkCmdDispatch")) {
    g_pass_timing.dispatch =
        reinterpret_cast<PFN_vkCmdDispatch>(vkGetDeviceProcAddr(device, name));
    g_pass_timing.write_timestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
        vkGetDeviceProcAddr(device, "vkCmdWriteTimestamp"));
    return reinterpret_cast<PFN_vkVoidFunction>(TimedDispatch);
  }
  return vkGetDeviceProcAddr(device, name);
}

}  // namespace

// Nested in WardenUpscaler, so the graft's friend declarations - the render target
// cache's render targets and their transitions - reach it.
class WardenUpscaler::Impl {
 public:
  using VulkanRenderTarget = VulkanRenderTargetCache::VulkanRenderTarget;

  Impl(VulkanCommandProcessor& cp, VulkanRenderTargetCache& rtc) : cp_(cp), rtc_(rtc) {}
  ~Impl() { Shutdown(); }

  bool Initialize();
  void Shutdown();

  void OnDraw(const RegisterFile& regs, draw_util::ViewportInfo& viewport_info,
              reg::RB_DEPTHCONTROL depth_control, uint32_t color_mask,
              uint32_t pixel_shader_texture_mask, uint64_t pixel_shader_hash,
              uint64_t vertex_shader_hash, bool window_offset_in_edram);
  void AfterDraw();
  void OnResolve(const draw_util::ResolveInfo& resolve_info);
  bool last_upscaled() const { return last_upscaled_; }
  void OnSwap(VkImageView& view, uint32_t& width, uint32_t& height,
              xenos::TextureFormat& format);

 private:
  const ui::vulkan::VulkanDevice* Device() const { return cp_.GetVulkanDevice(); }

  bool EnsureImage(Image& image, VkFormat format, uint32_t width, uint32_t height,
                   VkImageUsageFlags usage, const char* name);
  void ReleaseImage(Image& image);
  void AwaitSubmitted();
  void Transition(Image& image, VkPipelineStageFlags stage, VkAccessFlags access,
                  VkImageLayout layout);
  VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);
  void CopyFromRenderTarget(VulkanRenderTarget& source, VkImageView source_view, Image& dest,
                            VkPipeline pipeline, const CopyConstants& constants);
  bool IsHudlessPass(const RegisterFile& regs, uint32_t color_mask,
                     uint32_t pixel_shader_texture_mask) const;
  void KeepHudless();
  void CaptureHudless();
  bool EnsureContext(uint32_t render_width, uint32_t render_height, uint32_t display_width,
                     uint32_t display_height);
  void DestroyContext();
  void GetDisplaySize(uint32_t render_width, uint32_t render_height, uint32_t& width,
                      uint32_t& height) const;
  static void ExecuteDispatch(void* data, VkCommandBuffer command_buffer);
  // PLAN 9.14: FSR 3.1's frame generation.
  bool EnsureFrameGeneration(uint32_t render_width, uint32_t render_height,
                             uint32_t display_width, uint32_t display_height);
  void DestroyFrameGeneration();
  bool RecordFrameGeneration(uint32_t render_width, uint32_t render_height,
                             uint32_t display_width, uint32_t display_height,
                             float frame_ms);
  static void ExecuteFrameGeneration(void* data, VkCommandBuffer command_buffer);
  void NoteMeshDraw(const RegisterFile& regs);
  bool CameraReprojection(Matrix& reprojection);
  void EndFrame(bool upscaled, const char* reason);
  void ReadTimings();

  VulkanCommandProcessor& cp_;
  VulkanRenderTargetCache& rtc_;
  bool initialized_ = false;

  // The pipelines.
  VkDescriptorSetLayout copy_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout composite_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout copy_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout composite_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout motion_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout motion_set_layout_ = VK_NULL_HANDLE;
  // The motion pass's probe: five pixels' depth, motion and clip w a frame in flight,
  // host-visible, logged with each timed window.
  static constexpr VkDeviceSize kProbeSize = 5 * 4 * sizeof(float);
  VkBuffer probe_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory probe_memory_ = VK_NULL_HANDLE;
  float* probe_mapping_ = nullptr;
  bool probe_pending_[3] = {};
  uint64_t probe_submission_[3] = {};
  float last_probe_[20] = {};
  bool probe_seen_ = false;
  VkPipeline copy_color_pipeline_ = VK_NULL_HANDLE;
  VkPipeline copy_depth_pipeline_ = VK_NULL_HANDLE;
  VkPipeline composite_pipeline_ = VK_NULL_HANDLE;
  // PLAN 9.18: motion blur's two tile passes.
  VkDescriptorSetLayout blur_tile_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout blur_neighbor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout blur_tile_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout blur_neighbor_layout_ = VK_NULL_HANDLE;
  VkPipeline blur_tile_pipeline_ = VK_NULL_HANDLE;
  VkPipeline blur_neighbor_pipeline_ = VK_NULL_HANDLE;
  // PLAN 9.19: depth of field's four passes, one set layout.
  VkDescriptorSetLayout dof_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout dof_layout_ = VK_NULL_HANDLE;
  VkPipeline dof_focus_pipeline_ = VK_NULL_HANDLE;
  VkPipeline dof_prefilter_pipeline_ = VK_NULL_HANDLE;
  VkPipeline dof_blur_pipeline_ = VK_NULL_HANDLE;
  VkPipeline dof_combine_pipeline_ = VK_NULL_HANDLE;
  VkPipeline motion_pipeline_ = VK_NULL_HANDLE;

  // The camera's motion: this frame's static mesh draws and the last upscaled frame's -
  // what identifies the mesh (its vertices, indices and count) and its
  // world-view-projection - and the scene's guest viewport.
  struct MeshDraw {
    uint64_t key;
    float wvp[16];
  };
  std::vector<MeshDraw> mesh_draws_, previous_mesh_draws_;
  float viewport_scale_[3] = {};
  float viewport_offset_[3] = {};
  // Over a timed window: frames with the camera's motion, without (no match or no
  // agreement), cuts; the matches made and those that agreed.
  uint32_t motion_frames_ = 0, motion_unknown_frames_ = 0, motion_cuts_ = 0;
  uint64_t motion_matches_ = 0, motion_agreeing_ = 0;
  VkSampler linear_sampler_ = VK_NULL_HANDLE;
  VkDescriptorPool pools_[kFrames] = {};
  uint64_t pool_submissions_[kFrames] = {};
  uint32_t pool_index_ = 0;
  bool pool_ready_ = false;

  // FSR 3.1's inputs and output, and the composite shown.
  Image color_, depth_, motion_, upscaled_, composite_;
  // PLAN 9.18: each pixel's blur and log depth, the tiles' longest blur and their
  // neighbourhoods', the motion of the frame before (two, in turn), the cut counters.
  Image blur_pixels_, blur_tiles_, blur_around_, blur_motion_[2], blur_cuts_;
  uint32_t blur_turn_ = 0;
  bool blur_previous_valid_ = false;
  bool blur_cuts_cleared_ = false;
  // PLAN 9.19: the focus (2x1), the half size scene and its blur, the result.
  Image dof_focus_, dof_half_, dof_blurred_, dof_out_;
  float dof_strength_ = 0.0f;
  bool dof_shot_ = false;
  float dof_fov_ = 0.0f;
  uint32_t dof_snap_frames_ = 0;
  bool dof_show_ = false;
  // Records depth of field over the upscaled scene; the image the composite then takes.
  VkImageView RecordDepthOfField(uint32_t width, uint32_t height, uint32_t display_width,
                                 uint32_t display_height, float frame_ms);
  // FSR 3.1's shared resources, which the caller makes and passes with each dispatch
  // (FfxFsr3UpscalerSharedResourceDescriptions; BUG-067): the depth and the motion
  // vectors dilated from the inputs, and the depth reprojected into the frame before.
  Image dilated_depth_, dilated_motion_, reconstructed_depth_;

  // The FidelityFX SDK's Vulkan backend and the upscaler's context.
  std::vector<uint8_t> scratch_;
  FfxInterface interface_ = {};
  bool interface_made_ = false;
  FfxFsr3UpscalerContext context_ = {};
  bool context_made_ = false;
  uint32_t context_sizes_[4] = {};
  bool failed_ = false;
  // What the deferred dispatch needs when the command buffer is executed.
  FfxFsr3UpscalerDispatchDescription dispatch_ = {};
  bool dispatch_pending_ = false;

  // The setting, read as the upscaler starts; the test switch.
  bool enabled_ = false;
  // PLAN 9.14: FSR 3.1 off, the image before the UI captured for frame generation only -
  // no jitter, no upscaling (warden_hudless.h).
  bool capture_only_ = false;
  // PLAN 9.14: FSR 3.1's frame generation (files/frame-generation "fsr3"): its optical
  // flow on the upscaled scene and its frame interpolation from the upscaler's own
  // dilated depth and motion (as the SDK's ffx_fsr3.cpp does, without its prepare), into
  // fg_output_ - the frame between the last two, before the UI, at the panel's size -
  // for frame generation's presenting (warden_frame_generation.cc).
  bool fg_enabled_ = false;
  // The optical flow on the image before the UI at the render size (the upscaler's input),
  // a quarter of the work of the upscaled scene (run 448: its search 5.3 ms at 1080p);
  // files/fsr3-flow holding "display" (a test switch) runs it on the upscaled scene, as
  // the SDK's FSR 3 does.
  bool fg_flow_display_ = false;
  bool fg_failed_ = false;
  bool fg_made_ = false;
  uint32_t fg_sizes_[4] = {};
  std::vector<uint8_t> fg_scratch_;
  FfxInterface fg_interface_ = {};
  bool fg_interface_made_ = false;
  FfxOpticalflowContext of_context_ = {};
  FfxFrameInterpolationContext fi_context_ = {};
  Image of_vector_, of_scd_, fg_output_;
  uint64_t fg_frame_id_ = 0;
  // The last swap's frame interpolated: the frame before had one to interpolate from.
  bool fg_previous_ = false;
  bool fg_pending_ = false;
  FfxOpticalflowDispatchDescription of_dispatch_ = {};
  FfxFrameInterpolationDispatchDescription fi_dispatch_ = {};
  uint64_t fg_generated_ = 0;
  // Its GPU time, two timestamps a frame slot, with the upscaler's own timed frames.
  // Two timestamps around it and one after each of its passes.
  static constexpr uint32_t kFgMaxPasses = 48;
  static constexpr uint32_t kFgQueriesPerFrame = 2 + kFgMaxPasses;
  VkQueryPool fg_timing_pool_ = VK_NULL_HANDLE;
  int fg_timing_slot_ = -1;
  bool fg_timed_[kFrames] = {};
  uint32_t fg_slot_passes_[kFrames] = {};
  std::vector<std::string> fg_slot_names_[kFrames];
  double fg_timing_sum_ms_ = 0.0;
  uint32_t fg_timing_frames_ = 0;
  // Each pass name's GPU ms, summed over its dispatches, in the order first run.
  std::vector<std::pair<std::string, double>> fg_pass_sums_;
  bool show_ui_ = false;
  // FSR 3.1's 16-bit permutations: whether the device has the arithmetic enabled, the
  // setting, the precision the context is made with, a change between timed windows.
  bool half_supported_ = false;
  bool half_alternate_ = false;
  bool half_ = false;
  bool precision_change_ = false;
  // The history reprojected with the Lanczos lookup table (files/fsr3-lut), alternating.
  bool lut_alternate_ = false;
  bool lut_ = false;
  // Jitter: on while enabled and FSR 3.1 works; the frame's offset in render pixels.
  bool jitter_active_ = false;
  uint32_t jitter_index_ = 0;
  float jitter_[2] = {};
  // The frame being drawn.
  uint32_t render_size_[2] = {};
  uint32_t scene_texture_ = 0;
  uint32_t scene_draws_ = 0;
  uint32_t depth_tiles_ = 0;
  bool single_sample_scene_drawn_ = false;
  // Once a frame has drawn its scene single-sampled (one tile, the default), for good.
  bool single_sample_scene_seen_ = false;
  bool scene_depth_pending_ = false;
  VulkanRenderTarget* scene_depth_target_ = nullptr;
  int32_t scene_depth_window_offset_[2] = {};
  bool hudless_pending_ = false;
  bool hudless_captured_ = false;
  // The image before the UI is copied once a frame: the last full-screen pass's render
  // target is kept, and copied as a later draw goes into it (the UI's), at a resolve or
  // at the swap - whichever comes first.
  VulkanRenderTarget* hudless_target_ = nullptr;
  CopyConstants hudless_constants_ = {};
  uint64_t hudless_shader_ = 0;
  uint64_t hudless_target_shader_ = 0;
  // The post-processing chain's last pass - the King's Camp's: three passes reading the
  // scene, each resolved for the next, then the tonemapping (20BA772900FB8AA3, as
  // DAORecomp knows it) with the UI drawn over it (run 403). Learned from the frame
  // before: its image before the UI came from this pixel shader. While it is known, the
  // passes before it are ordinary draws, never copied; a pass after it, or a 3D frame
  // without it, has the next frame copy every pass again and learn anew.
  uint64_t last_pass_shader_ = 0;
  uint64_t captured_shader_ = 0;
  bool last_pass_seen_ = false;
  bool relearn_ = false;
  // A frame's post-processing and UI traced (files/fsr3-trace: one every 1000 upscaled
  // frames, at most eight): the passes, the render targets they draw into, the resolves
  // and their clears, and the copies - "FSR 3.1 trace:" lines.
  bool trace_enabled_ = false;
  // files/fsr3-no-motion (a test switch): motion vectors zero, to compare.
  bool no_motion_ = false;
  float no_motion_x_ = 0.0f;
  // files/fsr3-debug (a test switch): FSR 3.1's debug view over the frame.
  bool debug_view_ = false;
  // The camera FSR 3.1 is told of: from the static meshes' matrices once seen
  // (CameraPlanes), DAORecomp's stand-ins before.
  float camera_near_ = kCameraNear, camera_far_ = kCameraFar, camera_fov_ = kCameraFovVertical;
  bool camera_logged_ = false;
  bool tracing_ = false;
  // In a traced frame, every draw grouped by what the scene's detection looks at - the
  // depth test, colour writes, the depth and colour 0 render targets' EDRAM bases, the
  // surface pitch - and what it decided: logged at the swap.
  struct DrawGroup {
    uint32_t z_enable, colour_writes;
    int32_t depth_base, colour_base;
    uint32_t pitch, msaa, outcome, count, first, last;
  };
  std::vector<DrawGroup> draw_groups_;
  void NoteDraw(uint32_t z_enable, uint32_t colour_writes, uint32_t pitch, uint32_t msaa,
                uint32_t outcome);
  uint32_t traces_ = 0;
  uint32_t trace_lines_ = 0;
  uint32_t draw_index_ = 0;
  bool Tracing() { return tracing_ && trace_lines_++ < 80; }
  // GPU time, a frame in flight's timestamps: the frame done, FSR 3.1's dispatch begun
  // and done, the composite done, then one after each of FSR's passes - averaged and
  // logged every kTimedFrames.
  static constexpr uint32_t kTimedFrames = 150;
  static constexpr uint32_t kQueriesPerFrame = 4 + kMaxPassTimes;
  VkQueryPool timing_pool_ = VK_NULL_HANDLE;
  double timestamp_period_ns_ = 0.0;
  uint32_t timing_base_ = 0;
  bool dispatch_timed_ = false;
  bool timing_pending_[kFrames] = {};
  uint64_t timing_submission_[kFrames] = {};
  uint32_t timing_passes_[kFrames] = {};
  double timing_sum_ms_[3] = {};
  double timing_pass_sum_ms_[kMaxPassTimes] = {};
  uint32_t timing_pass_count_ = 0;
  uint32_t timing_frames_ = 0;
  // Frames timed before this submission belong to the other precision (alternate).
  uint64_t timing_from_submission_ = 0;
  // The title's frame rate over a timed window, from the context's making or the last log.
  std::chrono::steady_clock::time_point window_start_;
  uint64_t window_frames_start_ = 0;
  uint32_t captures_ = 0;
  uint32_t captures_sum_ = 0;
  uint64_t captures_from_frame_ = 0;
  // Across frames.
  bool reset_history_ = true;
  std::chrono::steady_clock::time_point last_swap_;
  uint64_t frames_ = 0;
  uint64_t frames_upscaled_ = 0;
  // The last swap's frame upscaled (FrameUpscaled).
  bool last_upscaled_ = false;
  const char* last_reason_ = nullptr;
  // PLAN 10.9: FSR 3.1's sharpening (files/fsr3-sharpen), kSharpness or 0.
  float sharpness_ = kSharpness;
};

bool WardenUpscaler::Impl::Initialize() {
  const std::string setting = SettingWord("fsr3");
  enabled_ = setting == "quality" || setting == "balanced" || setting == "performance" ||
             setting == "ultra";
  // PLAN 10.9: files/fsr3-sharpen holding "off" leaves out FSR 3.1's sharpening pass
  // (RCAS, about 0.5 ms a frame at 1080p) - an Options row, on unless chosen.
  sharpness_ = SettingWord("fsr3-sharpen") == "off" ? 0.0f : kSharpness;
  show_ui_ = !SettingWord("fsr3-show-ui").empty() ||
             std::fopen((FilesDir() + "/fsr3-show-ui").c_str(), "r") != nullptr;
  if (FILE* file = std::fopen((FilesDir() + "/fsr3-trace").c_str(), "r")) {
    std::fclose(file);
    trace_enabled_ = true;
  }
  if (FILE* file = std::fopen((FilesDir() + "/fsr3-no-motion").c_str(), "r")) {
    std::fclose(file);
    no_motion_ = true;
    // A number in it: that many pixels of motion to the right everywhere instead - wrong
    // on purpose, to see FSR 3.1 follow its input.
    no_motion_x_ = float(std::atof(SettingWord("fsr3-no-motion").c_str()));
    XELOGW("FSR 3.1: no motion vectors - {} px everywhere (files/fsr3-no-motion)",
           no_motion_x_);
  }
  if (FILE* file = std::fopen((FilesDir() + "/fsr3-debug").c_str(), "r")) {
    std::fclose(file);
    debug_view_ = true;
    XELOGW("FSR 3.1: its debug view over the frame (files/fsr3-debug)");
  }
  capture_only_ = !enabled_ && warden::FrameGenerationWantsHudless();
  fg_enabled_ = enabled_ && SettingWord("frame-generation") == "fsr3";
  fg_flow_display_ = SettingWord("fsr3-flow") == "display";
  // The performance tab and overlay: FSR 3.1 as it runs, by the option's own words.
  if (enabled_) {
    ::warden::deck::SetRenderNote(::warden::deck::RenderNote::kFsr3,
                                  setting == "quality"    ? "Quality"
                                  : setting == "balanced" ? "Balanced"
                                  : setting == "ultra"    ? "Ultra performance"
                                                          : "Performance");
  }
  if (!enabled_ && !capture_only_) {
    XELOGI("FSR 3.1: off");
    return true;
  }
  uint32_t render_width = 0, render_height = 0;
  warden::fsr3::RenderSize(render_width, render_height);
  if (capture_only_) {
    XELOGI("FSR 3.1: off - the image before the UI captured for frame generation");
  } else {
    XELOGI("FSR 3.1: on ({}) - the game renders at {}x{}{}", setting, render_width,
           render_height, fg_enabled_ ? ", with its frame generation" : "");
  }
  const ui::vulkan::VulkanDevice* device = Device();
  const auto& dfn = device->functions();
  const VkDevice vk_device = device->device();

  // The copies: a render target view to read, a storage image to write.
  VkDescriptorSetLayoutBinding copy_bindings[2] = {};
  copy_bindings[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                      nullptr};
  copy_bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                      nullptr};
  VkDescriptorSetLayoutCreateInfo set_layout_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_layout_info.bindingCount = 2;
  set_layout_info.pBindings = copy_bindings;
  if (dfn.vkCreateDescriptorSetLayout(vk_device, &set_layout_info, nullptr,
                                      &copy_set_layout_) != VK_SUCCESS) {
    XELOGE("FSR 3.1: no descriptor set layout for the copies");
    return false;
  }
  // The composite: the upscaled scene, the final frame, the image before the UI, a
  // linear sampler, the destination.
  // PLAN 9.18: and motion blur's pixels and tile neighbourhoods.
  VkDescriptorSetLayoutBinding composite_bindings[7] = {};
  for (uint32_t i = 0; i < 3; ++i) {
    composite_bindings[i] = {i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                             VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  }
  composite_bindings[3] = {3, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                           nullptr};
  composite_bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                           VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  for (uint32_t i = 5; i < 7; ++i) {
    composite_bindings[i] = {i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                             VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  }
  set_layout_info.bindingCount = 7;
  set_layout_info.pBindings = composite_bindings;
  if (dfn.vkCreateDescriptorSetLayout(vk_device, &set_layout_info, nullptr,
                                      &composite_set_layout_) != VK_SUCCESS) {
    XELOGE("FSR 3.1: no descriptor set layout for the composite");
    return false;
  }
  VkPushConstantRange push_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CopyConstants)};
  VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &copy_set_layout_;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &copy_layout_) !=
      VK_SUCCESS) {
    XELOGE("FSR 3.1: no pipeline layout for the copies");
    return false;
  }
  // The motion vectors: the depth read, the motion written, the probe.
  VkDescriptorSetLayoutBinding motion_bindings[3] = {copy_bindings[0], copy_bindings[1]};
  motion_bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                        nullptr};
  VkDescriptorSetLayoutCreateInfo motion_set_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  motion_set_info.bindingCount = 3;
  motion_set_info.pBindings = motion_bindings;
  if (dfn.vkCreateDescriptorSetLayout(vk_device, &motion_set_info, nullptr,
                                      &motion_set_layout_) != VK_SUCCESS) {
    XELOGE("FSR 3.1: no descriptor set layout for the motion vectors");
    return false;
  }
  push_range.size = sizeof(MotionConstants);
  layout_info.pSetLayouts = &motion_set_layout_;
  if (dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &motion_layout_) !=
      VK_SUCCESS) {
    XELOGE("FSR 3.1: no pipeline layout for the motion vectors");
    return false;
  }
  if (ui::vulkan::util::CreateDedicatedAllocationBuffer(
          device, kProbeSize * kFrames, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          ui::vulkan::util::MemoryPurpose::kReadback, probe_buffer_, probe_memory_)) {
    void* mapping = nullptr;
    if (dfn.vkMapMemory(vk_device, probe_memory_, 0, VK_WHOLE_SIZE, 0, &mapping) ==
        VK_SUCCESS) {
      probe_mapping_ = static_cast<float*>(mapping);
    }
  }
  if (!probe_mapping_) {
    XELOGE("FSR 3.1: no buffer for the motion vectors' probe");
    return false;
  }
  push_range.size = sizeof(CompositeConstants);
  layout_info.pSetLayouts = &composite_set_layout_;
  if (dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &composite_layout_) !=
      VK_SUCCESS) {
    XELOGE("FSR 3.1: no pipeline layout for the composite");
    return false;
  }
  // PLAN 9.18: motion blur's tile pass - the motion, the depth, the motion before read;
  // the tiles, the pixels, the cut counters and this motion's copy written - and its
  // neighbourhood pass - the tiles read; the neighbourhoods and the counters written.
  {
    VkDescriptorSetLayoutBinding tile_bindings[7] = {};
    for (uint32_t i = 0; i < 7; ++i) {
      tile_bindings[i] = {i,
                          i < 3 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo blur_set_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    blur_set_info.bindingCount = 7;
    blur_set_info.pBindings = tile_bindings;
    VkDescriptorSetLayoutBinding neighbor_bindings[3] = {};
    for (uint32_t i = 0; i < 3; ++i) {
      neighbor_bindings[i] = {i,
                              i < 1 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                    : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                              1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo neighbor_set_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    neighbor_set_info.bindingCount = 3;
    neighbor_set_info.pBindings = neighbor_bindings;
    push_range.size = sizeof(BlurConstants);
    if (dfn.vkCreateDescriptorSetLayout(vk_device, &blur_set_info, nullptr,
                                        &blur_tile_set_layout_) != VK_SUCCESS ||
        dfn.vkCreateDescriptorSetLayout(vk_device, &neighbor_set_info, nullptr,
                                        &blur_neighbor_set_layout_) != VK_SUCCESS) {
      XELOGE("FSR 3.1: no descriptor set layouts for motion blur");
      return false;
    }
    layout_info.pSetLayouts = &blur_tile_set_layout_;
    if (dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &blur_tile_layout_) !=
        VK_SUCCESS) {
      XELOGE("FSR 3.1: no pipeline layout for motion blur's tiles");
      return false;
    }
    layout_info.pSetLayouts = &blur_neighbor_set_layout_;
    if (dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr,
                                   &blur_neighbor_layout_) != VK_SUCCESS) {
      XELOGE("FSR 3.1: no pipeline layout for motion blur's neighbourhoods");
      return false;
    }
    blur_tile_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        device, blur_tile_layout_, shaders::warden_motion_blur_tile_cs,
        sizeof(shaders::warden_motion_blur_tile_cs));
    blur_neighbor_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        device, blur_neighbor_layout_, shaders::warden_motion_blur_neighbor_cs,
        sizeof(shaders::warden_motion_blur_neighbor_cs));
    if (!blur_tile_pipeline_ || !blur_neighbor_pipeline_) {
      XELOGE("FSR 3.1: motion blur's pipelines were not made - no motion blur");
    }
  }
  // PLAN 9.19: depth of field's passes - the depth, the focus, two of the pass's own
  // read; its output written; a linear sampler.
  {
    VkDescriptorSetLayoutBinding dof_bindings[6] = {};
    for (uint32_t i = 0; i < 6; ++i) {
      dof_bindings[i] = {i,
                         i < 4    ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                         : i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                  : VK_DESCRIPTOR_TYPE_SAMPLER,
                         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo dof_set_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dof_set_info.bindingCount = 6;
    dof_set_info.pBindings = dof_bindings;
    push_range.size = sizeof(DofConstants);
    layout_info.pSetLayouts = &dof_set_layout_;
    if (dfn.vkCreateDescriptorSetLayout(vk_device, &dof_set_info, nullptr,
                                        &dof_set_layout_) != VK_SUCCESS ||
        dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &dof_layout_) !=
            VK_SUCCESS) {
      XELOGE("FSR 3.1: no layouts for depth of field");
      return false;
    }
    dof_focus_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        device, dof_layout_, shaders::warden_dof_focus_cs, sizeof(shaders::warden_dof_focus_cs));
    dof_prefilter_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        device, dof_layout_, shaders::warden_dof_prefilter_cs,
        sizeof(shaders::warden_dof_prefilter_cs));
    dof_blur_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        device, dof_layout_, shaders::warden_dof_blur_cs, sizeof(shaders::warden_dof_blur_cs));
    dof_combine_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        device, dof_layout_, shaders::warden_dof_combine_cs,
        sizeof(shaders::warden_dof_combine_cs));
    if (!dof_focus_pipeline_ || !dof_prefilter_pipeline_ || !dof_blur_pipeline_ ||
        !dof_combine_pipeline_) {
      XELOGE("FSR 3.1: depth of field's pipelines were not made - no depth of field");
    }
  }
  if (g_depth_of_field.load() < 0) {
    g_depth_of_field.store(SettingWord("dof") == "on" ? 1 : 0);
  }
  dof_show_ = !SettingWord("dof-show").empty();
  XELOGI("FSR 3.1: depth of field in conversations {}{}",
         g_depth_of_field.load() ? "on" : "off",
         dof_show_ ? " (files/dof-show: its blur shown instead of the scene)" : "");
  if (g_motion_blur.load() < 0) {
    const std::string blur_setting = SettingWord("motion-blur");
    g_motion_blur.store(blur_setting == "high" ? 2 : blur_setting == "low" ? 1 : 0);
  }
  XELOGI("FSR 3.1: motion blur {}", g_motion_blur.load() == 2   ? "high"
                                    : g_motion_blur.load() == 1 ? "low"
                                                                : "off");
  copy_color_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      device, copy_layout_, shaders::warden_upscale_copy_color_cs,
      sizeof(shaders::warden_upscale_copy_color_cs));
  copy_depth_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      device, copy_layout_, shaders::warden_upscale_copy_depth_cs,
      sizeof(shaders::warden_upscale_copy_depth_cs));
  composite_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      device, composite_layout_, shaders::warden_upscale_composite_cs,
      sizeof(shaders::warden_upscale_composite_cs));
  motion_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      device, motion_layout_, shaders::warden_upscale_motion_cs,
      sizeof(shaders::warden_upscale_motion_cs));
  if (!copy_color_pipeline_ || !copy_depth_pipeline_ || !composite_pipeline_ ||
      !motion_pipeline_) {
    XELOGE("FSR 3.1: the copy, motion and composite pipelines were not made");
    return false;
  }
  VkSamplerCreateInfo sampler_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = 0.0f;
  if (dfn.vkCreateSampler(vk_device, &sampler_info, nullptr, &linear_sampler_) !=
      VK_SUCCESS) {
    XELOGE("FSR 3.1: no linear sampler");
    return false;
  }
  VkDescriptorPoolSize pool_sizes[4] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerFrame * 3},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kSetsPerFrame * 2},
      {VK_DESCRIPTOR_TYPE_SAMPLER, kSetsPerFrame},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSetsPerFrame}};
  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = kSetsPerFrame;
  pool_info.poolSizeCount = 4;
  pool_info.pPoolSizes = pool_sizes;
  for (VkDescriptorPool& pool : pools_) {
    if (dfn.vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
      XELOGE("FSR 3.1: no descriptor pool");
      return false;
    }
  }
  VkQueryPoolCreateInfo query_info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  query_info.queryCount = kFrames * kQueriesPerFrame;
  if (dfn.vkCreateQueryPool(vk_device, &query_info, nullptr, &timing_pool_) != VK_SUCCESS) {
    timing_pool_ = VK_NULL_HANDLE;
  }
  const auto& ifn = device->vulkan_instance()->functions();
  VkPhysicalDeviceProperties physical_properties;
  ifn.vkGetPhysicalDeviceProperties(device->physical_device(), &physical_properties);
  timestamp_period_ns_ = physical_properties.limits.timestampPeriod;
  // 16-bit arithmetic: the graft enables shaderInt16 and Vulkan 1.2's shaderFloat16
  // wherever they are supported.
  VkPhysicalDeviceVulkan12Features features_1_2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &features_1_2;
  ifn.vkGetPhysicalDeviceFeatures2(device->physical_device(), &features);
  half_supported_ = device->properties().apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0) &&
                    features_1_2.shaderFloat16 && features.features.shaderInt16;
  const std::string half_setting = SettingWord("fsr3-half");
  half_alternate_ = half_supported_ && half_setting == "alternate";
  half_ = half_supported_ && half_setting != "off";
  XELOGI("FSR 3.1: 16-bit arithmetic {}{}",
         half_supported_ ? "on the device" : "not on the device - the 32-bit passes",
         !half_supported_ ? ""
         : half_alternate_ ? ", alternating with 32-bit each timed window"
         : half_          ? ""
                          : ", but the 32-bit passes (files/fsr3-half)");
  const std::string lut_setting = SettingWord("fsr3-lut");
  lut_alternate_ = lut_setting == "alternate";
  lut_ = lut_setting != "off";
  XELOGI("FSR 3.1: the history reprojected with {}",
         lut_alternate_ ? "the Lanczos lookup table, alternating with the reference weights "
                          "each timed window (files/fsr3-lut)"
         : lut_         ? "the Lanczos lookup table"
                        : "the reference Lanczos weights (files/fsr3-lut)");
  initialized_ = true;
  jitter_active_ = true;
  XELOGI("FSR 3.1: the copies and the composite are ready; the upscaler is made with the "
         "first 3D frame");
  return true;
}

void WardenUpscaler::Impl::Shutdown() {
  if (!Device()) {
    return;
  }
  const auto& dfn = Device()->functions();
  const VkDevice device = Device()->device();
  DestroyFrameGeneration();
  DestroyContext();
  for (Image* image : {&color_, &depth_, &motion_, &upscaled_, &composite_, &dilated_depth_,
                       &dilated_motion_, &reconstructed_depth_, &of_vector_, &of_scd_,
                       &fg_output_, &blur_pixels_, &blur_tiles_, &blur_around_,
                       &blur_motion_[0], &blur_motion_[1], &blur_cuts_, &dof_focus_,
                       &dof_half_, &dof_blurred_, &dof_out_}) {
    ReleaseImage(*image);
  }
  for (VkPipeline* pipeline : {&dof_focus_pipeline_, &dof_prefilter_pipeline_,
                               &dof_blur_pipeline_, &dof_combine_pipeline_}) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, *pipeline);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device, dof_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         dof_set_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, blur_tile_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         blur_neighbor_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         blur_tile_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         blur_neighbor_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         blur_tile_set_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         blur_neighbor_set_layout_);
  for (VkDescriptorPool& pool : pools_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device, pool);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroySampler, device, linear_sampler_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyQueryPool, device, timing_pool_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyQueryPool, device, fg_timing_pool_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, copy_color_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, copy_depth_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, composite_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, motion_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device, motion_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         motion_set_layout_);
  if (probe_mapping_) {
    dfn.vkUnmapMemory(device, probe_memory_);
    probe_mapping_ = nullptr;
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device, probe_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device, probe_memory_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device, copy_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         composite_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         copy_set_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         composite_set_layout_);
  initialized_ = false;
}

bool WardenUpscaler::Impl::EnsureImage(Image& image, VkFormat format, uint32_t width, uint32_t height,
                           VkImageUsageFlags usage, const char* name) {
  if (image.image && image.create_info.format == format && image.width() == width &&
      image.height() == height) {
    return true;
  }
  ReleaseImage(image);
  const auto& dfn = Device()->functions();
  VkImageCreateInfo& info = image.create_info;
  info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = format;
  info.extent = {width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          Device(), info, ui::vulkan::util::MemoryPurpose::kDeviceLocal, image.image,
          image.memory)) {
    XELOGE("FSR 3.1: no {} image ({}x{})", name, width, height);
    image = Image();
    return false;
  }
  VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = image.image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = format;
  view_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
  if (dfn.vkCreateImageView(Device()->device(), &view_info, nullptr, &image.view) !=
      VK_SUCCESS) {
    XELOGE("FSR 3.1: no {} image view", name);
    ReleaseImage(image);
    return false;
  }
  image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image.stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  image.access = 0;
  XELOGI("FSR 3.1: the {} image, {}x{}", name, width, height);
  return true;
}

void WardenUpscaler::Impl::AwaitSubmitted() {
  // What the GPU was given before this frame's commands. Not the open submission: waiting
  // for it would end it in the middle of a draw or the swap (an assertion, run 402) - and
  // nothing in it has used what is let go here.
  cp_.CheckSubmissionCompletionAndDeviceLoss(cp_.GetCurrentSubmission() - 1);
}

void WardenUpscaler::Impl::ReleaseImage(Image& image) {
  if (!image.image && !image.view && !image.memory) {
    return;
  }
  // Images are made again only at a size change, rare: whatever used them is let
  // finish first.
  AwaitSubmitted();
  const auto& dfn = Device()->functions();
  const VkDevice device = Device()->device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyImageView, device, image.view);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyImage, device, image.image);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device, image.memory);
  image = Image();
}

void WardenUpscaler::Impl::Transition(Image& image, VkPipelineStageFlags stage, VkAccessFlags access,
                          VkImageLayout layout) {
  cp_.PushImageMemoryBarrier(
      image.image, ui::vulkan::util::InitializeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
      image.stage, stage, image.access, access, image.layout, layout,
      VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
  image.stage = stage;
  image.access = access;
  image.layout = layout;
}

VkDescriptorSet WardenUpscaler::Impl::AllocateSet(VkDescriptorSetLayout layout) {
  const auto& dfn = Device()->functions();
  if (!pool_ready_) {
    // The pool this frame takes: the frame that used it last is done by now (Xenia
    // keeps three frames in flight), or its sets are left alone this frame.
    if (pool_submissions_[pool_index_] > cp_.GetCompletedSubmission()) {
      return VK_NULL_HANDLE;
    }
    dfn.vkResetDescriptorPool(Device()->device(), pools_[pool_index_], 0);
    pool_ready_ = true;
  }
  VkDescriptorSetAllocateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  info.descriptorPool = pools_[pool_index_];
  info.descriptorSetCount = 1;
  info.pSetLayouts = &layout;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (dfn.vkAllocateDescriptorSets(Device()->device(), &info, &set) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  pool_submissions_[pool_index_] =
      std::max(pool_submissions_[pool_index_], cp_.GetCurrentSubmission());
  return set;
}

void WardenUpscaler::Impl::CopyFromRenderTarget(VulkanRenderTarget& source, VkImageView source_view,
                                    Image& dest, VkPipeline pipeline,
                                    const CopyConstants& constants) {
  if (!constants.size[0] || !constants.size[1]) {
    return;
  }
  VkDescriptorSet set = AllocateSet(copy_set_layout_);
  if (set == VK_NULL_HANDLE) {
    return;
  }
  const bool is_depth = source.key().is_depth;
  // The render target read by the copy, out of the render pass, then back to what its
  // draws expect: Xenia moves a render target to its drawing layout only as a render
  // pass that uses it begins with a new framebuffer.
  rtc_.TransitionRenderTarget(source, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(dest, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
             VK_IMAGE_LAYOUT_GENERAL);
  cp_.SubmitBarriers(true);
  cp_.EndRenderPass();
  ++captures_;
  VkDescriptorImageInfo images[2] = {};
  images[0].imageView = source_view;
  images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  images[1].imageView = dest.view;
  images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet writes[2] = {};
  for (uint32_t i = 0; i < 2; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType =
        i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[i].pImageInfo = &images[i];
  }
  Device()->functions().vkUpdateDescriptorSets(Device()->device(), 2, writes, 0, nullptr);
  DeferredCommandBuffer& commands = cp_.deferred_command_buffer();
  cp_.BindExternalComputePipeline(pipeline);
  commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, copy_layout_, 0, 1, &set,
                                   0, nullptr);
  commands.CmdVkPushConstants(copy_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(constants), &constants);
  commands.CmdVkDispatch((constants.size[0] + 7) / 8, (constants.size[1] + 7) / 8, 1);
  VkPipelineStageFlags draw_stage;
  VkAccessFlags draw_access;
  VkImageLayout draw_layout;
  VulkanRenderTarget::GetDrawUsage(is_depth, &draw_stage, &draw_access, &draw_layout);
  rtc_.TransitionRenderTarget(source, draw_stage, draw_access, draw_layout);
}

void WardenUpscaler::Impl::OnDraw(const RegisterFile& regs, draw_util::ViewportInfo& viewport_info,
                      reg::RB_DEPTHCONTROL depth_control, uint32_t color_mask,
                      uint32_t pixel_shader_texture_mask, uint64_t pixel_shader_hash,
                      uint64_t vertex_shader_hash, bool window_offset_in_edram) {
  hudless_pending_ = false;
  if (!jitter_active_) {
    return;
  }
  ++draw_index_;
  // A full-screen pass reading the resolved scene: the last one before the swap is the
  // image without the UI. Its render target is kept after the draw (AfterDraw) and
  // copied before the next draw into a render target - the UI's, drawn over it.
  if (IsHudlessPass(regs, color_mask, pixel_shader_texture_mask)) {
    const bool earlier_pass = last_pass_shader_ && pixel_shader_hash != last_pass_shader_ &&
                              !last_pass_seen_;
    if (!earlier_pass) {
      relearn_ |= last_pass_shader_ && pixel_shader_hash != last_pass_shader_;
      last_pass_seen_ |= pixel_shader_hash == last_pass_shader_;
      hudless_pending_ = true;
      hudless_shader_ = pixel_shader_hash;
      return;
    }
    // A pass before the chain's last: an ordinary draw.
  }
  if (hudless_target_) {
    // Into the kept render target, or into another, which may take EDRAM the kept one
    // holds and give it back through a transfer before a later draw reaches this hook.
    RenderTargetCache::RenderTarget* const* targets =
        rtc_.last_update_accumulated_render_targets();
    bool drawn = false;
    for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      drawn |= targets[i] != nullptr;
    }
    if (Tracing()) {
      const RenderTargetCache::RenderTarget* target = targets[1];
      XELOGI("FSR 3.1 trace: draw {}: pixel shader {:016X}, colour 0 at EDRAM {} pitch {} "
             "format {} - {}",
             draw_index_, pixel_shader_hash, target ? uint32_t(target->key().base_tiles) : 0u,
             target ? uint32_t(target->key().pitch_tiles_at_32bpp) : 0u,
             target ? uint32_t(target->key().resource_format) : 0u,
             drawn ? "the kept render target copied first" : "no render target");
    }
    if (drawn) {
      CaptureHudless();
    }
  } else if (scene_texture_ && Tracing()) {
    const RenderTargetCache::RenderTarget* target =
        rtc_.last_update_accumulated_render_targets()[1];
    XELOGI("FSR 3.1 trace: draw {}: pixel shader {:016X}, colour 0 at EDRAM {} pitch {} "
           "format {}",
           draw_index_, pixel_shader_hash, target ? uint32_t(target->key().base_tiles) : 0u,
           target ? uint32_t(target->key().pitch_tiles_at_32bpp) : 0u,
           target ? uint32_t(target->key().resource_format) : 0u);
  }
  const auto surface = regs.Get<reg::RB_SURFACE_INFO>();
  const uint32_t scale_x = rtc_.draw_resolution_scale_x();
  // The scene: depth-tested draws, here single-sampled and as wide as the screen (not
  // the shadow maps), until the scene's colour is resolved (after it, effects drawn
  // over it). The game's own two 2x MSAA tiles (files/scene-msaa): depth-tested
  // multisampled draws at least half the screen wide. The minimap is drawn depth-tested
  // too, in narrower pieces: it is not jittered, which would open seams between them.
  const bool single_sample_scene_draw =
      !scene_texture_ && surface.msaa_samples == xenos::MsaaSamples::k1X &&
      surface.surface_pitch * scale_x == render_size_[0];
  // (Before the render size is known, any.)
  const bool scene_wide =
      !render_size_[0] || 2 * surface.surface_pitch * scale_x >= render_size_[0];
  const bool scene_geometry = !regs.Get<reg::PA_CL_VTE_CNTL>().vtx_xy_fmt &&
                              !regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable;
  // Outcomes for the traced frame's draw groups: 0 not the scene, 1 the scene jittered,
  // 2 the scene in screen space.
  const auto note = [&](uint32_t outcome) {
    if (tracing_) {
      NoteDraw(depth_control.z_enable, color_mask ? 1 : 0,
               surface.surface_pitch * scale_x, uint32_t(surface.msaa_samples), outcome);
    }
  };
  // Multisampled draws are the scene only with the console's MSAA tiles: with the
  // single-sampled scene (the default) they are the post-processing's half-size passes,
  // depth-tested, 640 wide with 2x MSAA (run 406) - neither jittered nor their depth
  // taken.
  const bool msaa_scene_draw = surface.msaa_samples != xenos::MsaaSamples::k1X &&
                               scene_wide && !single_sample_scene_seen_;
  const bool scene_draw = depth_control.z_enable && (msaa_scene_draw || single_sample_scene_draw);
  if (!scene_draw) {
    note(0);
  }
  if (scene_draw) {
    auto* depth_target =
        static_cast<VulkanRenderTarget*>(rtc_.last_update_accumulated_render_targets()[0]);
    if (depth_target && !depth_target->key().is_depth) {
      depth_target = nullptr;
    }
    ++scene_draws_;
    single_sample_scene_drawn_ |= single_sample_scene_draw;
    single_sample_scene_seen_ |= single_sample_scene_draw;
    // Where the draw is on the screen: its render target coordinates less the tile's
    // window offset, unless that is in the EDRAM base.
    int32_t window_offset[2] = {};
    if (!window_offset_in_edram &&
        regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
      const auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
      window_offset[0] = pa_sc_window_offset.window_x_offset;
      window_offset[1] = pa_sc_window_offset.window_y_offset;
    }
    if (depth_target) {
      scene_depth_target_ = depth_target;
      scene_depth_window_offset_[0] = window_offset[0];
      scene_depth_window_offset_[1] = window_offset[1];
      scene_depth_pending_ = true;
    }
    // Screen-space draws cover their own rectangle: jitter would uncover an edge.
    if (!scene_geometry || !viewport_info.xy_extent[0] || !viewport_info.xy_extent[1]) {
      note(2);
      return;
    }
    note(1);
    // The jitter in the host's normalized device coordinates: y points down here, as in
    // Vulkan (Direct3D's flips at the end of GetHostViewportInfo), so a positive offset
    // moves the image right and down, as FSR's jitter is applied in Direct3D.
    viewport_info.ndc_offset[0] += 2.0f * jitter_[0] / float(viewport_info.xy_extent[0]);
    viewport_info.ndc_offset[1] += 2.0f * jitter_[1] / float(viewport_info.xy_extent[1]);
    if (single_sample_scene_draw &&
        std::find(std::begin(kStaticMeshVertexShaders), std::end(kStaticMeshVertexShaders),
                  vertex_shader_hash) != std::end(kStaticMeshVertexShaders)) {
      NoteMeshDraw(regs);
    }
    return;
  }
}

void WardenUpscaler::Impl::NoteMeshDraw(const RegisterFile& regs) {
  const auto float_register = [&](uint32_t index) {
    float value;
    std::memcpy(&value, &regs.values[index], sizeof(value));
    return value;
  };
  viewport_scale_[0] = float_register(XE_GPU_REG_PA_CL_VPORT_XSCALE);
  viewport_scale_[1] = float_register(XE_GPU_REG_PA_CL_VPORT_YSCALE);
  viewport_scale_[2] = float_register(XE_GPU_REG_PA_CL_VPORT_ZSCALE);
  viewport_offset_[0] = float_register(XE_GPU_REG_PA_CL_VPORT_XOFFSET);
  viewport_offset_[1] = float_register(XE_GPU_REG_PA_CL_VPORT_YOFFSET);
  viewport_offset_[2] = float_register(XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
  if (mesh_draws_.size() >= kMaxMeshDraws) {
    return;
  }
  // The mesh: its vertices (vertex fetch constant 0, which these shaders read), its
  // indices and how many.
  MeshDraw draw;
  const uint64_t vertices = regs.GetVertexFetch(0).address;
  const uint64_t indices = regs.values[XE_GPU_REG_VGT_DMA_BASE];
  const uint64_t count = regs.Get<reg::VGT_DRAW_INITIATOR>().num_indices;
  draw.key = (vertices | (count << 32)) ^ (indices * 0x9E3779B97F4A7C15ull);
  for (uint32_t i = 0; i < 16; ++i) {
    draw.wvp[i] = float_register(XE_GPU_REG_SHADER_CONSTANT_000_X + i);
  }
  mesh_draws_.push_back(draw);
}

bool WardenUpscaler::Impl::CameraReprojection(Matrix& reprojection) {
  if (mesh_draws_.empty() || previous_mesh_draws_.empty()) {
    return false;
  }
  // A mesh drawn more than once a frame - the same tree or rock in several places, the
  // same buffers - cannot be told apart from its copies: only meshes drawn once in both
  // frames are matched (each frame's draws twice: the depth prepass and the colour pass
  // draw the same mesh with the same matrix, so those count as once).
  struct Seen {
    const MeshDraw* draw;
    bool ambiguous;
  };
  const auto index = [](const std::vector<MeshDraw>& draws) {
    std::unordered_map<uint64_t, Seen> seen;
    seen.reserve(draws.size());
    for (const MeshDraw& draw : draws) {
      auto [it, inserted] = seen.emplace(draw.key, Seen{&draw, false});
      if (!inserted && std::memcmp(it->second.draw->wvp, draw.wvp, sizeof(draw.wvp))) {
        it->second.ambiguous = true;
      }
    }
    return seen;
  };
  const auto previous = index(previous_mesh_draws_);
  const auto current = index(mesh_draws_);
  const auto matrix = [](const float* wvp) {
    Matrix m;
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        m.m[column][row] = wvp[column * 4 + row];
      }
    }
    return m;
  };
  // Matched meshes spread over the frame's draws.
  std::vector<Matrix> candidates;
  const size_t step = std::max<size_t>(1, mesh_draws_.size() / kMaxMeshMatches);
  for (size_t i = 0; i < mesh_draws_.size() && candidates.size() < kMaxMeshMatches;
       i += step) {
    const MeshDraw& draw = mesh_draws_[i];
    const auto now = current.find(draw.key);
    const auto before = previous.find(draw.key);
    Matrix now_inverse;
    if (now->second.draw != &draw || now->second.ambiguous || before == previous.end() ||
        before->second.ambiguous || !Invert(matrix(draw.wvp), now_inverse)) {
      continue;
    }
    candidates.push_back(Multiply(matrix(before->second.draw->wvp), now_inverse));
  }
  motion_matches_ += candidates.size();
  // The one most of the others agree with - within a quarter of a pixel everywhere: a
  // mesh that moved of its own gives another.
  size_t best = 0, best_votes = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    size_t votes = 0;
    for (const Matrix& other : candidates) {
      votes += ReprojectionDistance(candidates[i], other, viewport_scale_) < 0.25 ? 1 : 0;
    }
    if (votes > best_votes) {
      best = i;
      best_votes = votes;
    }
  }
  if (tracing_) {
    for (size_t i = 0; i < std::min<size_t>(candidates.size(), 12); ++i) {
      double centre[2] = {}, corner[2] = {};
      Reproject(candidates[i], kTestPoints[3], centre);
      Reproject(candidates[i], kTestPoints[1], corner);
      XELOGI("FSR 3.1 trace: static mesh {} of {}: the centre from ({:.2f} {:.2f}) px, a "
             "corner from ({:.2f} {:.2f}) px; {} from the best",
             i + 1, candidates.size(), centre[0] * viewport_scale_[0],
             centre[1] * viewport_scale_[1],
             (corner[0] + 0.5) * viewport_scale_[0], (corner[1] + 0.5) * viewport_scale_[1],
             ReprojectionDistance(candidates[i], candidates[best], viewport_scale_));
    }
  }
  if (!best_votes || 2 * best_votes <= candidates.size()) {
    return false;
  }
  motion_agreeing_ += best_votes;
  reprojection = candidates[best];
  return true;
}

void WardenUpscaler::Impl::NoteDraw(uint32_t z_enable, uint32_t colour_writes, uint32_t pitch,
                                    uint32_t msaa, uint32_t outcome) {
  RenderTargetCache::RenderTarget* const* targets =
      rtc_.last_update_accumulated_render_targets();
  const int32_t depth_base =
      targets[0] ? int32_t(targets[0]->key().base_tiles) | (targets[0]->key().is_depth ? 0 : 1 << 16)
                 : -1;
  const int32_t colour_base = targets[1] ? int32_t(targets[1]->key().base_tiles) : -1;
  for (DrawGroup& group : draw_groups_) {
    if (group.z_enable == z_enable && group.colour_writes == colour_writes &&
        group.depth_base == depth_base && group.colour_base == colour_base &&
        group.pitch == pitch && group.msaa == msaa && group.outcome == outcome) {
      ++group.count;
      group.last = draw_index_;
      return;
    }
  }
  if (draw_groups_.size() < 32) {
    draw_groups_.push_back({z_enable, colour_writes, depth_base, colour_base, pitch, msaa,
                            outcome, 1, draw_index_, draw_index_});
  }
}

bool WardenUpscaler::Impl::IsHudlessPass(const RegisterFile& regs, uint32_t color_mask,
                                         uint32_t pixel_shader_texture_mask) const {
  if (!scene_texture_ || !(color_mask & 0xF) ||
      regs.Get<reg::RB_SURFACE_INFO>().msaa_samples != xenos::MsaaSamples::k1X) {
    return false;
  }
  uint32_t mask = pixel_shader_texture_mask;
  while (mask) {
    const uint32_t i = uint32_t(std::countr_zero(mask));
    mask &= mask - 1;
    if ((regs.GetTextureFetch(i).base_address << 12) == scene_texture_) {
      return true;
    }
  }
  return false;
}

void WardenUpscaler::Impl::AfterDraw() {
  if (hudless_pending_) {
    hudless_pending_ = false;
    KeepHudless();
  }
}

void WardenUpscaler::Impl::KeepHudless() {
  hudless_target_ = nullptr;
  if (!render_size_[0]) {
    return;
  }
  auto* target =
      static_cast<VulkanRenderTarget*>(rtc_.last_update_accumulated_render_targets()[1]);
  if (!target) {
    return;
  }
  const RenderTargetCache::RenderTargetKey key = target->key();
  if (key.is_depth || key.msaa_samples != xenos::MsaaSamples::k1X) {
    return;
  }
  const uint32_t scale_x = key.scale_native ? 1 : rtc_.draw_resolution_scale_x();
  const uint32_t scale_y = key.scale_native ? 1 : rtc_.draw_resolution_scale_y();
  hudless_constants_ = {};
  hudless_constants_.size[0] = std::min(render_size_[0], key.GetWidth() * scale_x);
  hudless_constants_.size[1] =
      std::min(render_size_[1],
               rtc_.GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples) *
                   scale_y);
  hudless_target_ = target;
  hudless_target_shader_ = hudless_shader_;
  if (Tracing()) {
    XELOGI("FSR 3.1 trace: draw {}: a full-screen pass reading the scene, pixel shader "
           "{:016X}, colour 0 at EDRAM {} pitch {} format {} - kept",
           draw_index_, hudless_shader_, uint32_t(key.base_tiles),
           uint32_t(key.pitch_tiles_at_32bpp), uint32_t(key.resource_format));
  }
}

void WardenUpscaler::Impl::CaptureHudless() {
  VulkanRenderTarget* target = hudless_target_;
  hudless_target_ = nullptr;
  if (!target || !EnsureImage(color_, VK_FORMAT_R8G8B8A8_UNORM, render_size_[0],
                              render_size_[1],
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              "colour")) {
    return;
  }
  CopyFromRenderTarget(*target, target->view_color_transfer(), color_, copy_color_pipeline_,
                       hudless_constants_);
  hudless_captured_ = true;
  captured_shader_ = hudless_target_shader_;
}

void WardenUpscaler::Impl::OnResolve(const draw_util::ResolveInfo& resolve_info) {
  if (Tracing()) {
    XELOGI("FSR 3.1 trace: resolve after draw {}: {} at EDRAM {} to {:08X}, clears colour "
           "{} depth {}{}",
           draw_index_, resolve_info.IsCopyingDepth() ? "depth" : "colour",
           resolve_info.IsCopyingDepth() ? resolve_info.depth_original_base
                                         : resolve_info.color_original_base,
           resolve_info.copy_dest_base, uint32_t(resolve_info.rb_copy_control.color_clear_enable),
           uint32_t(resolve_info.rb_copy_control.depth_clear_enable),
           hudless_target_ ? " - a render target kept" : "");
  }
  // A resolve only reads the render targets, unless it clears them too: then the image
  // before the UI first. (The post-processing's resolves clear nothing, run 403.)
  if (resolve_info.rb_copy_control.color_clear_enable ||
      resolve_info.rb_copy_control.depth_clear_enable) {
    CaptureHudless();
  }
  if (!jitter_active_ || !resolve_info.copy_dest_extent_length || !render_size_[0]) {
    return;
  }
  const RegisterFile& regs = rtc_.register_file();
  // Single-sampled, the scene's resolves are the first ones after its draws.
  if (regs.Get<reg::RB_SURFACE_INFO>().msaa_samples == xenos::MsaaSamples::k1X &&
      !(single_sample_scene_drawn_ && !scene_texture_)) {
    return;
  }
  // A scene resolve (colour, or depth for the game's own use).
  const bool scene_colour = !scene_texture_ && !resolve_info.IsCopyingDepth();
  if (scene_colour) {
    scene_texture_ = regs.values[XE_GPU_REG_RB_COPY_DEST_BASE];
  }
  // The bound depth buffer holds this tile's depth until a resolve clears it (the last
  // tile's depth resolve does, before the colour resolve), so with the console's tiles
  // it is taken at the first resolve after the tile's draws. One tile: once the scene
  // is drawn, at its colour's resolve - its depth prepass is resolved before the shadow
  // maps, for the game's own use (run 406) - or before a resolve clears it.
  if (!scene_depth_pending_ ||
      (single_sample_scene_seen_ && !scene_colour &&
       !resolve_info.rb_copy_control.depth_clear_enable)) {
    return;
  }
  scene_depth_pending_ = false;
  VulkanRenderTarget* target = scene_depth_target_;
  if (!target) {
    return;
  }
  if (!EnsureImage(depth_, VK_FORMAT_R32_SFLOAT, render_size_[0], render_size_[1],
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "depth")) {
    return;
  }
  const RenderTargetCache::RenderTargetKey key = target->key();
  const int32_t scale[2] = {int32_t(key.scale_native ? 1 : rtc_.draw_resolution_scale_x()),
                            int32_t(key.scale_native ? 1 : rtc_.draw_resolution_scale_y())};
  const int32_t source_size[2] = {
      int32_t(key.GetWidth()) * scale[0],
      int32_t(rtc_.GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples)) *
          scale[1]};
  // The resolved area on the screen, and where the draws put it.
  int32_t screen[4], source_offset[2];
  for (uint32_t i = 0; i < 2; ++i) {
    screen[i] = (resolve_info.dao_rect[i] - resolve_info.dao_window_offset[i]) * scale[i];
    screen[2 + i] =
        (resolve_info.dao_rect[2 + i] - resolve_info.dao_window_offset[i]) * scale[i];
    source_offset[i] = scene_depth_window_offset_[i] * scale[i];
    // Clipped to the screen and to the render target.
    screen[i] = std::max({screen[i], 0, -source_offset[i]});
    screen[2 + i] = std::min(
        {screen[2 + i], int32_t(render_size_[i]), source_size[i] - source_offset[i]});
    if (screen[2 + i] <= screen[i]) {
      return;
    }
  }
  CopyConstants constants = {};
  for (uint32_t i = 0; i < 2; ++i) {
    constants.source_offset[i] = screen[i] + source_offset[i];
    constants.dest_offset[i] = screen[i];
    constants.size[i] = uint32_t(screen[2 + i] - screen[i]);
  }
  CopyFromRenderTarget(*target, target->view_depth_color(), depth_, copy_depth_pipeline_,
                       constants);
  ++depth_tiles_;
}

void WardenUpscaler::Impl::GetDisplaySize(uint32_t render_width, uint32_t render_height,
                              uint32_t& width_out, uint32_t& height_out) const {
  width_out = render_width;
  height_out = render_height;
  ui::Presenter* presenter = warden::gpu::Presenter();
  if (!presenter) {
    return;
  }
  uint32_t surface_width = 0, surface_height = 0;
  presenter->GetPaintSurfaceSize(surface_width, surface_height);
  // With the window gone (the app put away) the last panel's size: the guest output's
  // size must not change then - frame generation would take it for a new size and make
  // its generator again, 8.7 s on the GPU thread with the game stopped behind it, and
  // the autosave asked for as the app is put away not written before the hold
  // (BUG-100, the owner, 2026-11-02).
  static std::atomic<uint32_t> last_width{0}, last_height{0};
  if (surface_width && surface_height) {
    last_width.store(surface_width, std::memory_order_relaxed);
    last_height.store(surface_height, std::memory_order_relaxed);
  } else {
    surface_width = last_width.load(std::memory_order_relaxed);
    surface_height = last_height.load(std::memory_order_relaxed);
  }
  if (!surface_width || !surface_height) {
    return;
  }
  // The largest area with the game's 16:9 that the panel shows: 1920x1080 on the Thor.
  uint32_t width = surface_width;
  uint32_t height = uint32_t(uint64_t(width) * 9 / 16);
  if (height > surface_height) {
    height = surface_height;
    width = uint32_t(uint64_t(height) * 16 / 9);
  }
  // No downscaling: below the render size the upscaler would only anti-alias.
  if (width > render_width && height > render_height) {
    width_out = width & ~uint32_t(1);
    height_out = height & ~uint32_t(1);
  }
}

bool WardenUpscaler::Impl::EnsureContext(uint32_t render_width, uint32_t render_height,
                             uint32_t display_width, uint32_t display_height) {
  if (failed_) {
    return false;
  }
  const uint32_t sizes[4] = {render_width, render_height, display_width, display_height};
  if (context_made_ && std::equal(sizes, sizes + 4, context_sizes_)) {
    return true;
  }
  DestroyContext();
  const ui::vulkan::VulkanDevice* device = Device();
  if (!interface_made_) {
    const size_t size =
        ffxGetScratchMemorySizeVK(device->physical_device(), FFX_FSR3UPSCALER_CONTEXT_COUNT);
    scratch_.assign(size, 0);
    VkDeviceContext vulkan = {device->device(), device->physical_device(),
                              FfxDeviceProcAddr};
    const FfxErrorCode error =
        ffxGetInterfaceVK(&interface_, ffxGetDeviceVK(&vulkan), scratch_.data(), size,
                          FFX_FSR3UPSCALER_CONTEXT_COUNT);
    if (error != FFX_OK) {
      XELOGE("FSR 3.1: no Vulkan backend ({:08X})", uint32_t(error));
      failed_ = true;
      return false;
    }
    interface_made_ = true;
    g_get_capabilities = interface_.fpGetDeviceCapabilities;
    interface_.fpGetDeviceCapabilities = LutDeviceCapabilities;
    if (warden_pipeline_statistics_wanted()) {
      g_create_pipeline = interface_.fpCreatePipeline;
      interface_.fpCreatePipeline = StatisticsCreatePipeline;
    }
  }
  FfxFsr3UpscalerContextDescription description = {};
  description.flags = FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
  description.maxRenderSize = {render_width, render_height};
  description.maxUpscaleSize = {display_width, display_height};
  description.fpMessage = FfxMessage;
  description.backendInterface = interface_;
  // The SDK reads the device's extensions as the context is made on the idle interface:
  // the 16-bit permutations or the 32-bit ones (warden_fsr3.cc).
  warden::fsr3::SetHalfPrecision(half_);
  g_lanczos_lut.store(lut_, std::memory_order_relaxed);
  const FfxErrorCode error = ffxFsr3UpscalerContextCreate(&context_, &description);
  if (error != FFX_OK) {
    XELOGE("FSR 3.1: the upscaler was not made ({:08X})", uint32_t(error));
    failed_ = true;
    return false;
  }
  context_made_ = true;
  std::copy(sizes, sizes + 4, context_sizes_);
  window_start_ = std::chrono::steady_clock::now();
  window_frames_start_ = frames_;
  FfxEffectMemoryUsage memory = {};
  ffxFsr3UpscalerContextGetGpuMemoryUsage(&context_, &memory);
  XELOGI("FSR 3.1: the upscaler is made - {}x{} to {}x{}, {}-bit passes{}, {:.1f} MB of GPU "
         "memory",
         render_width, render_height, display_width, display_height, half_ ? 16 : 32,
         lut_ ? ", the Lanczos lookup table" : "", double(memory.totalUsageInBytes) / 1048576.0);
  reset_history_ = true;
  return true;
}

void WardenUpscaler::Impl::DestroyContext() {
  if (!context_made_) {
    return;
  }
  AwaitSubmitted();
  ffxFsr3UpscalerContextDestroy(&context_);
  context_made_ = false;
}

bool WardenUpscaler::Impl::EnsureFrameGeneration(uint32_t render_width,
                                                 uint32_t render_height,
                                                 uint32_t display_width,
                                                 uint32_t display_height) {
  if (!fg_enabled_ || fg_failed_) {
    return false;
  }
  const uint32_t sizes[4] = {render_width, render_height, display_width, display_height};
  if (fg_made_ && std::equal(sizes, sizes + 4, fg_sizes_)) {
    return true;
  }
  DestroyFrameGeneration();
  const ui::vulkan::VulkanDevice* device = Device();
  // One backend for the two contexts, apart from the upscaler's.
  constexpr uint32_t kContexts = FFX_OPTICALFLOW_CONTEXT_COUNT + FFX_FRAMEINTERPOLATION_CONTEXT_COUNT;
  if (!fg_interface_made_) {
    const size_t size = ffxGetScratchMemorySizeVK(device->physical_device(), kContexts);
    fg_scratch_.assign(size, 0);
    VkDeviceContext vulkan = {device->device(), device->physical_device(), FfxDeviceProcAddr};
    const FfxErrorCode error = ffxGetInterfaceVK(&fg_interface_, ffxGetDeviceVK(&vulkan),
                                                 fg_scratch_.data(), size, kContexts);
    if (error != FFX_OK) {
      XELOGE("FSR 3.1: no Vulkan backend for frame generation ({:08X})", uint32_t(error));
      fg_failed_ = true;
      return false;
    }
    fg_interface_made_ = true;
    VkQueryPoolCreateInfo query_info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_info.queryCount = kFrames * kFgQueriesPerFrame;
    if (device->functions().vkCreateQueryPool(device->device(), &query_info, nullptr,
                                              &fg_timing_pool_) != VK_SUCCESS) {
      fg_timing_pool_ = VK_NULL_HANDLE;
    }
    g_fg_schedule = fg_interface_.fpScheduleGpuJob;
    fg_interface_.fpScheduleGpuJob = NamedScheduleGpuJob;
  }
  warden::fsr3::SetHalfPrecision(half_);
  FfxOpticalflowContextDescription of_description = {};
  of_description.backendInterface = fg_interface_;
  of_description.resolution = fg_flow_display_
                                  ? FfxDimensions2D{display_width, display_height}
                                  : FfxDimensions2D{render_width, render_height};
  FfxErrorCode error = ffxOpticalflowContextCreate(&of_context_, &of_description);
  if (error != FFX_OK) {
    XELOGE("FSR 3.1: no optical flow ({:08X}) - frame generation off", uint32_t(error));
    fg_failed_ = true;
    return false;
  }
  // The scene as the upscaler outputs it, half floats; motion vectors at the render size
  // without the jitter, depth not inverted - as the upscaler's context takes them.
  FfxFrameInterpolationContextDescription fi_description = {};
  fi_description.flags = 0;
  fi_description.maxRenderSize = {render_width, render_height};
  fi_description.displaySize = {display_width, display_height};
  fi_description.backBufferFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
  fi_description.previousInterpolationSourceFormat = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
  fi_description.backendInterface = fg_interface_;
  error = ffxFrameInterpolationContextCreate(&fi_context_, &fi_description);
  if (error != FFX_OK) {
    XELOGE("FSR 3.1: no frame interpolation ({:08X}) - frame generation off",
           uint32_t(error));
    ffxOpticalflowContextDestroy(&of_context_);
    fg_failed_ = true;
    return false;
  }
  fg_made_ = true;
  std::copy(sizes, sizes + 4, fg_sizes_);
  // Optical flow's shared resources, as it describes them.
  FfxOpticalflowSharedResourceDescriptions shared = {};
  ffxOpticalflowGetSharedResourceDescriptions(&of_context_, &shared);
  if (!EnsureImage(of_vector_, VK_FORMAT_R16G16_SINT, shared.opticalFlowVector.resourceDescription.width,
                   shared.opticalFlowVector.resourceDescription.height,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   "optical flow") ||
      !EnsureImage(of_scd_, VK_FORMAT_R32_UINT, shared.opticalFlowSCD.resourceDescription.width,
                   shared.opticalFlowSCD.resourceDescription.height,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   "scene change detection") ||
      !EnsureImage(fg_output_, VK_FORMAT_R16G16B16A16_SFLOAT, display_width, display_height,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   "interpolated")) {
    DestroyFrameGeneration();
    fg_failed_ = true;
    return false;
  }
  fg_previous_ = false;
  FfxEffectMemoryUsage of_memory = {}, fi_memory = {};
  ffxOpticalflowContextGetGpuMemoryUsage(&of_context_, &of_memory);
  ffxFrameInterpolationContextGetGpuMemoryUsage(&fi_context_, &fi_memory);
  XELOGI("FSR 3.1: frame generation made - {}x{} from {}x{}, optical flow {}x{} (on the {} "
         "image), {:.1f} MB of GPU memory",
         display_width, display_height, render_width, render_height, of_vector_.width(),
         of_vector_.height(), fg_flow_display_ ? "upscaled" : "render size's",
         double(of_memory.totalUsageInBytes + fi_memory.totalUsageInBytes) / 1048576.0);
  return true;
}

void WardenUpscaler::Impl::DestroyFrameGeneration() {
  if (!fg_made_) {
    return;
  }
  AwaitSubmitted();
  ffxFrameInterpolationContextDestroy(&fi_context_);
  ffxOpticalflowContextDestroy(&of_context_);
  fg_made_ = false;
  fg_previous_ = false;
}

bool WardenUpscaler::Impl::RecordFrameGeneration(uint32_t render_width, uint32_t render_height,
                                                 uint32_t display_width,
                                                 uint32_t display_height, float frame_ms) {
  if (!EnsureFrameGeneration(render_width, render_height, display_width, display_height)) {
    return false;
  }
  // Without the frame before (or across a cut) nothing is generated: this one is only the
  // next one's before.
  const bool reset = reset_history_ || !fg_previous_;
  ++fg_frame_id_;
  const VkPipelineStageFlags compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  Transition(of_vector_, compute, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
             VK_IMAGE_LAYOUT_GENERAL);
  Transition(of_scd_, compute, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
             VK_IMAGE_LAYOUT_GENERAL);
  Transition(fg_output_, compute, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
             VK_IMAGE_LAYOUT_GENERAL);
  // (The upscaled scene in the shader-read layout, the shared resources back in the
  // general one: as FSR 3.1's dispatch and the composite leave them.)
  cp_.SubmitBarriers(true);
  const auto read = [](Image& image, const wchar_t* name) {
    return ffxGetResourceVK(image.image,
                            ffxGetImageResourceDescriptionVK(image.image, image.create_info),
                            name, FFX_RESOURCE_STATE_COMPUTE_READ);
  };
  const auto uav = [](Image& image, const wchar_t* name) {
    return ffxGetResourceVK(image.image,
                            ffxGetImageResourceDescriptionVK(image.image, image.create_info,
                                                             FFX_RESOURCE_USAGE_UAV),
                            name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
  };
  of_dispatch_ = {};
  // (The image before the UI in the shader-read layout as the composite reads it.)
  Image& flow_source = fg_flow_display_ ? upscaled_ : color_;
  of_dispatch_.color = read(flow_source, L"OF_Color");
  of_dispatch_.opticalFlowVector = uav(of_vector_, L"OF_Vector");
  of_dispatch_.opticalFlowSCD = uav(of_scd_, L"OF_SCD");
  of_dispatch_.reset = reset;
  of_dispatch_.backbufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
  of_dispatch_.minMaxLuminance = {0.0f, 1.0f};

  FfxFrameInterpolationDispatchDescription& fi = fi_dispatch_;
  fi = {};
  fi.displaySize = {display_width, display_height};
  fi.renderSize = {render_width, render_height};
  fi.currentBackBuffer = read(upscaled_, L"FI_BackBuffer");
  fi.currentBackBuffer_HUDLess = read(upscaled_, L"FI_BackBufferHudless");
  fi.output = uav(fg_output_, L"FI_Output");
  fi.interpolationRect = {0, 0, int32_t(display_width), int32_t(display_height)};
  fi.opticalFlowVector = uav(of_vector_, L"FI_OpticalFlowVector");
  fi.opticalFlowSceneChangeDetection = uav(of_scd_, L"FI_OpticalFlowSCD");
  fi.opticalFlowBufferSize = {of_vector_.width(), of_vector_.height()};
  // The flow's vectors are in its source's pixels, one a block of 8 of them.
  fi.opticalFlowScale = {1.0f / float(flow_source.width()),
                         1.0f / float(flow_source.height())};
  fi.opticalFlowBlockSize = 8;
  fi.cameraNear = camera_near_;
  fi.cameraFar = camera_far_;
  fi.cameraFovAngleVertical = camera_fov_;
  fi.viewSpaceToMetersFactor = 1.0f;
  fi.frameTimeDelta = std::clamp(frame_ms, 1.0f, 100.0f);
  fi.reset = reset;
  fi.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
  fi.minMaxLuminance[0] = 0.0f;
  fi.minMaxLuminance[1] = 1.0f;
  fi.frameID = fg_frame_id_;
  // The upscaler's own, written by its dispatch just before (the SDK's ffx_fsr3.cpp does
  // the same, without frame interpolation's prepare).
  fi.dilatedDepth = uav(dilated_depth_, L"FI_DilatedDepth");
  fi.dilatedMotionVectors = uav(dilated_motion_, L"FI_DilatedMotionVectors");
  fi.reconstructedPrevDepth = uav(reconstructed_depth_, L"FI_ReconstructedPrevNearestDepth");
  fg_pending_ = true;
  // Timed with the upscaler's timed frames.
  fg_timing_slot_ = fg_timing_pool_ && dispatch_timed_ ? int(pool_index_) : -1;
  if (fg_timing_slot_ >= 0) {
    fg_timed_[pool_index_] = true;
  }
  cp_.deferred_command_buffer().CmdWardenCallback(ExecuteFrameGeneration, this);
  // Read by frame generation's UI pass in the refresh.
  Transition(fg_output_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  cp_.SubmitBarriers(true);
  fg_previous_ = true;
  if (!reset) {
    ++fg_generated_;
  }
  return !reset;
}

void WardenUpscaler::Impl::ExecuteFrameGeneration(void* data, VkCommandBuffer command_buffer) {
  auto& self = *static_cast<WardenUpscaler::Impl*>(data);
  if (!self.fg_pending_ || !self.fg_made_) {
    return;
  }
  self.fg_pending_ = false;
  self.of_dispatch_.commandList = ffxGetCommandListVK(command_buffer);
  self.fi_dispatch_.commandList = self.of_dispatch_.commandList;
  const auto& dfn = self.Device()->functions();
  const int slot = self.fg_timing_slot_;
  const uint32_t base = uint32_t(std::max(slot, 0)) * kFgQueriesPerFrame;
  if (slot >= 0) {
    dfn.vkCmdResetQueryPool(command_buffer, self.fg_timing_pool_, base, kFgQueriesPerFrame);
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            self.fg_timing_pool_, base);
    g_pass_timing.pool = self.fg_timing_pool_;
    g_pass_timing.next = base + 2;
    g_pass_timing.end = base + kFgQueriesPerFrame;
    self.fg_slot_names_[slot].clear();
    g_fg_job_names = &self.fg_slot_names_[slot];
  }
  FfxErrorCode error = ffxOpticalflowContextDispatch(&self.of_context_, &self.of_dispatch_);
  if (error == FFX_OK) {
    error = ffxFrameInterpolationDispatch(&self.fi_context_, &self.fi_dispatch_);
  }
  if (slot >= 0) {
    self.fg_slot_passes_[slot] = g_pass_timing.next - (base + 2);
    g_pass_timing.pool = VK_NULL_HANDLE;
    g_fg_job_names = nullptr;
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            self.fg_timing_pool_, base + 1);
  }
  if (error != FFX_OK) {
    static std::atomic<uint32_t> failures{0};
    if (failures.fetch_add(1, std::memory_order_relaxed) < 8) {
      XELOGE("FSR 3.1: frame generation's dispatch failed ({:08X})", uint32_t(error));
    }
  }
}

void WardenUpscaler::Impl::ExecuteDispatch(void* data, VkCommandBuffer command_buffer) {
  auto& self = *static_cast<WardenUpscaler::Impl*>(data);
  if (!self.dispatch_pending_ || !self.context_made_) {
    return;
  }
  self.dispatch_pending_ = false;
  self.dispatch_.commandList = ffxGetCommandListVK(command_buffer);
  const auto& dfn = self.Device()->functions();
  if (self.dispatch_timed_) {
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            self.timing_pool_, self.timing_base_ + 1);
    g_pass_timing.pool = self.timing_pool_;
    g_pass_timing.next = self.timing_base_ + 4;
    g_pass_timing.end = self.timing_base_ + kQueriesPerFrame;
  }
  const FfxErrorCode error = ffxFsr3UpscalerContextDispatch(&self.context_, &self.dispatch_);
  if (self.dispatch_timed_) {
    self.timing_passes_[self.timing_base_ / kQueriesPerFrame] =
        g_pass_timing.next - (self.timing_base_ + 4);
    g_pass_timing.pool = VK_NULL_HANDLE;
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            self.timing_pool_, self.timing_base_ + 2);
  }
  if (error != FFX_OK) {
    static std::atomic<uint32_t> failures{0};
    if (failures.fetch_add(1, std::memory_order_relaxed) < 8) {
      XELOGE("FSR 3.1: the dispatch failed ({:08X})", uint32_t(error));
    }
  }
}

void WardenUpscaler::Impl::OnSwap(VkImageView& view, uint32_t& width, uint32_t& height,
                      xenos::TextureFormat& format) {
  const auto now = std::chrono::steady_clock::now();
  const float frame_ms =
      last_swap_.time_since_epoch().count()
          ? std::chrono::duration<float, std::milli>(now - last_swap_).count()
          : 33.3f;
  last_swap_ = now;
  if (tracing_) {
    XELOGI("FSR 3.1 trace: the swap after {} draws{}; {} copies this frame", draw_index_,
           hudless_target_ ? ", the kept render target copied" : "",
           captures_ + (hudless_target_ ? 1 : 0));
    for (const DrawGroup& group : draw_groups_) {
      XELOGI("FSR 3.1 trace: {} draws ({}-{}): depth test {}, colour writes {}, depth at "
             "EDRAM {}, colour 0 at {}, pitch {}, msaa {} - {}",
             group.count, group.first, group.last, group.z_enable, group.colour_writes,
             group.depth_base, group.colour_base, group.pitch, group.msaa,
             group.outcome == 1   ? "the scene, jittered"
             : group.outcome == 2 ? "the scene, screen space"
                                  : "not the scene");
    }
  }
  draw_groups_.clear();
  // The last full-screen pass with nothing drawn over it since.
  CaptureHudless();
  // Alternating: the other precision's context from this frame (a stall, once a window).
  if (precision_change_) {
    precision_change_ = false;
    if (half_alternate_) {
      half_ = !half_;
    }
    if (lut_alternate_) {
      lut_ = !lut_;
    }
    DestroyContext();
    timing_from_submission_ = cp_.GetCurrentSubmission();
  }
  const bool size_changed = render_size_[0] != width || render_size_[1] != height;
  render_size_[0] = width;
  render_size_[1] = height;
  // PLAN 9.14: frame generation's image before the UI - this swap's, or none.
  warden::HudlessFrame& hudless = warden::CurrentHudlessFrame();
  hudless = warden::HudlessFrame();
  if (capture_only_) {
    // FSR 3.1 off: the capture as it is, at the render size - the guest output's.
    if (!size_changed && scene_draws_ && hudless_captured_ && color_.width() == width &&
        color_.height() == height) {
      Transition(color_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      cp_.SubmitBarriers(true);
      hudless.valid = true;
      hudless.image = color_.view;
      hudless.width = width;
      hudless.height = height;
      hudless.front_render = view;
      hudless.hudless_render = color_.view;
      hudless.render_width = width;
      hudless.render_height = height;
    }
    EndFrame(false, "off - capturing the image before the UI for frame generation");
    return;
  }
  const char* reason = nullptr;
  if (!initialized_ || !jitter_active_) {
    reason = failed_ ? "FSR 3.1 failed" : "off";
  } else if (size_changed) {
    reason = "the render size changed";
  } else if (!scene_draws_) {
    reason = "no 3D scene";
  } else if (!depth_tiles_) {
    reason = "no scene depth";
  } else if (!hudless_captured_) {
    reason = "no image before the UI";
  }
  uint32_t display_width = 0, display_height = 0;
  if (!reason) {
    GetDisplaySize(width, height, display_width, display_height);
    if (display_width == width) {
      reason = "the panel is no bigger";
    } else if (!EnsureContext(width, height, display_width, display_height) ||
               !EnsureImage(motion_, VK_FORMAT_R16G16_SFLOAT, width, height,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            "motion vector") ||
               !EnsureImage(upscaled_, VK_FORMAT_R16G16B16A16_SFLOAT, display_width,
                            display_height,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            "upscaled") ||
               !EnsureImage(composite_, VK_FORMAT_R8G8B8A8_UNORM, display_width,
                            display_height,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            "composite") ||
               !EnsureImage(dilated_depth_, VK_FORMAT_R32_SFLOAT, width, height,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            "dilated depth") ||
               !EnsureImage(dilated_motion_, VK_FORMAT_R16G16_SFLOAT, width, height,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            "dilated motion vector") ||
               !EnsureImage(reconstructed_depth_, VK_FORMAT_R32_UINT, width, height,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            "reconstructed depth")) {
      reason = "the upscaler is unavailable";
    }
  }
  VkDescriptorSet composite_set = VK_NULL_HANDLE;
  if (!reason) {
    composite_set = AllocateSet(composite_set_layout_);
    if (composite_set == VK_NULL_HANDLE) {
      reason = "no descriptor set";
    }
  }
  if (reason) {
    // Frame generation starts again from the next upscaled frame.
    fg_previous_ = false;
    EndFrame(false, reason);
    return;
  }

  DeferredCommandBuffer& commands = cp_.deferred_command_buffer();
  cp_.SubmitBarriers(true);
  cp_.EndRenderPass();
  ReadTimings();
  timing_base_ = pool_index_ * kQueriesPerFrame;
  const bool timed = timing_pool_ && !timing_pending_[pool_index_];
  if (timed) {
    commands.CmdVkResetQueryPool(timing_pool_, timing_base_, kQueriesPerFrame);
    commands.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_pool_,
                                 timing_base_);
  }
  // Motion vectors: the camera's, from the scene's depth and the static meshes'
  // matrices - or zero where the camera's motion is not known (the clear keeps the
  // image's contents defined). A camera cut (a conversation's next shot) starts the
  // history again.
  const VkPipelineStageFlags compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  Matrix reprojection;
  bool camera_motion = !reset_history_ && !no_motion_ && CameraReprojection(reprojection);
  if (camera_motion) {
    // A cut: the screen's centre, near and far, more than a quarter of the screen away.
    for (const double depth : {0.5, 0.999}) {
      const double ndc[3] = {0.0, 0.0, depth};
      double previous[2];
      if (!Reproject(reprojection, ndc, previous) ||
          std::max(std::abs(previous[0]), std::abs(previous[1])) > 0.5) {
        camera_motion = false;
        reset_history_ = true;
        ++motion_cuts_;
        break;
      }
    }
  }
  VkDescriptorSet motion_set =
      camera_motion ? AllocateSet(motion_set_layout_) : VK_NULL_HANDLE;
  if (motion_set != VK_NULL_HANDLE) {
    ++motion_frames_;
    Transition(depth_, compute, VK_ACCESS_SHADER_READ_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(motion_, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    cp_.SubmitBarriers(true);
    VkDescriptorImageInfo images[2] = {};
    images[0] = {VK_NULL_HANDLE, depth_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[1] = {VK_NULL_HANDLE, motion_.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo probe = {probe_buffer_, kProbeSize * pool_index_, kProbeSize};
    VkWriteDescriptorSet writes[3] = {};
    for (uint32_t i = 0; i < 3; ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = motion_set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = i == 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                 : i    ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                        : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      writes[i].pImageInfo = i < 2 ? &images[i] : nullptr;
      writes[i].pBufferInfo = i == 2 ? &probe : nullptr;
    }
    Device()->functions().vkUpdateDescriptorSets(Device()->device(), 3, writes, 0, nullptr);
    probe_pending_[pool_index_] = true;
    probe_submission_[pool_index_] = cp_.GetCurrentSubmission();
    MotionConstants constants = {};
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        constants.reprojection[column * 4 + row] = float(reprojection.m[column][row]);
      }
    }
    for (uint32_t i = 0; i < 3; ++i) {
      constants.viewport_scale[i] =
          std::abs(viewport_scale_[i]) > 1.0e-6f ? viewport_scale_[i] : 1.0f;
      constants.viewport_offset[i] = viewport_offset_[i];
    }
    constants.jitter[0] = jitter_[0];
    constants.jitter[1] = jitter_[1];
    constants.size[0] = width;
    constants.size[1] = height;
    cp_.BindExternalComputePipeline(motion_pipeline_);
    commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, motion_layout_, 0, 1,
                                     &motion_set, 0, nullptr);
    commands.CmdVkPushConstants(motion_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(constants), &constants);
    commands.CmdVkDispatch((width + 7) / 8, (height + 7) / 8, 1);
  } else {
    ++motion_unknown_frames_;
    Transition(motion_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    cp_.SubmitBarriers(true);
    VkClearColorValue zero = {};
    zero.float32[0] = no_motion_ ? no_motion_x_ : 0.0f;
    const VkImageSubresourceRange range =
        ui::vulkan::util::InitializeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    commands.CmdVkClearColorImage(motion_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  &zero, 1, &range);
  }
  // FSR 3.1's inputs readable by compute, its output writable; the SDK leaves them so.
  Transition(color_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(depth_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(motion_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(upscaled_, compute, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
             VK_IMAGE_LAYOUT_GENERAL);
  // The shared resources, written and read by FSR's passes; it returns them to this.
  for (Image* image : {&dilated_depth_, &dilated_motion_, &reconstructed_depth_}) {
    Transition(*image, compute, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
               VK_IMAGE_LAYOUT_GENERAL);
  }
  cp_.SubmitBarriers(true);

  if (!mesh_draws_.empty()) {
    float near = 0.0f, far = 0.0f, fov = 0.0f;
    if (CameraPlanes(mesh_draws_.front().wvp, near, far, fov)) {
      camera_near_ = near;
      camera_far_ = far;
      camera_fov_ = fov;
      if (!camera_logged_) {
        camera_logged_ = true;
        XELOGI("FSR 3.1: the camera, from a static mesh's matrix - near {:.3f}, far {:.1f}, "
               "{:.1f} degrees vertical",
               near, far, fov * 57.29578f);
      }
    }
  }
  FfxFsr3UpscalerDispatchDescription& dispatch = dispatch_;
  dispatch = {};
  dispatch.color = ffxGetResourceVK(
      color_.image, ffxGetImageResourceDescriptionVK(color_.image, color_.create_info),
      L"FSR3_InputColor", FFX_RESOURCE_STATE_COMPUTE_READ);
  dispatch.depth = ffxGetResourceVK(
      depth_.image, ffxGetImageResourceDescriptionVK(depth_.image, depth_.create_info),
      L"FSR3_InputDepth", FFX_RESOURCE_STATE_COMPUTE_READ);
  dispatch.motionVectors = ffxGetResourceVK(
      motion_.image, ffxGetImageResourceDescriptionVK(motion_.image, motion_.create_info),
      L"FSR3_InputMotionVectors", FFX_RESOURCE_STATE_COMPUTE_READ);
  dispatch.output = ffxGetResourceVK(
      upscaled_.image,
      ffxGetImageResourceDescriptionVK(upscaled_.image, upscaled_.create_info,
                                       FFX_RESOURCE_USAGE_UAV),
      L"FSR3_UpscaledOutput", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
  const auto shared = [](Image& image, const wchar_t* name) {
    return ffxGetResourceVK(image.image,
                            ffxGetImageResourceDescriptionVK(image.image, image.create_info,
                                                             FFX_RESOURCE_USAGE_UAV),
                            name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
  };
  dispatch.dilatedDepth = shared(dilated_depth_, L"FSR3_DilatedDepth");
  dispatch.dilatedMotionVectors = shared(dilated_motion_, L"FSR3_DilatedMotionVectors");
  dispatch.reconstructedPrevNearestDepth =
      shared(reconstructed_depth_, L"FSR3_ReconstructedPrevNearestDepth");
  dispatch.jitterOffset = {jitter_[0], jitter_[1]};
  dispatch.motionVectorScale = {1.0f, 1.0f};
  dispatch.renderSize = {width, height};
  dispatch.upscaleSize = {display_width, display_height};
  dispatch.enableSharpening = sharpness_ > 0.0f;
  dispatch.sharpness = sharpness_;
  dispatch.frameTimeDelta = frame_ms;
  dispatch.preExposure = 1.0f;
  dispatch.reset = reset_history_;
  dispatch.cameraNear = camera_near_;
  dispatch.cameraFar = camera_far_;
  dispatch.cameraFovAngleVertical = camera_fov_;
  dispatch.flags = debug_view_ ? FFX_FSR3UPSCALER_DISPATCH_DRAW_DEBUG_VIEW : 0;
  dispatch.viewSpaceToMetersFactor = 1.0f;
  dispatch_pending_ = true;
  dispatch_timed_ = timed;
  commands.CmdWardenCallback(ExecuteDispatch, this);

  // PLAN 9.19: depth of field over the upscaled scene, in conversations and cutscenes.
  const VkImageView scene_view =
      RecordDepthOfField(width, height, display_width, display_height, frame_ms);

  // PLAN 9.18: motion blur's tiles - with the camera's motion known this frame (a cut, or
  // motion not known, has none) and the level not off.
  uint32_t blur_samples = 0;
  uint32_t blur_tile = 1;
  const int blur_level = g_motion_blur.load(std::memory_order_relaxed);
  if (blur_level > 0 && motion_set != VK_NULL_HANDLE && blur_tile_pipeline_ &&
      blur_neighbor_pipeline_) {
    const uint32_t level = blur_level >= 2 ? 1 : 0;
    blur_tile = std::max((height + kBlurTilesPerHeight / 2) / kBlurTilesPerHeight,
                         kBlurMinTile);
    const uint32_t tiles_x = (width + blur_tile - 1) / blur_tile;
    const uint32_t tiles_y = (height + blur_tile - 1) / blur_tile;
    const VkImageUsageFlags rw = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const uint32_t turn = blur_turn_;
    bool ready =
        EnsureImage(blur_pixels_, VK_FORMAT_R16G16_SFLOAT, width, height, rw,
                    "motion blur's pixels") &&
        EnsureImage(blur_tiles_, VK_FORMAT_R16G16_SFLOAT, tiles_x, tiles_y, rw,
                    "motion blur's tiles") &&
        EnsureImage(blur_around_, VK_FORMAT_R16G16_SFLOAT, tiles_x, tiles_y, rw,
                    "motion blur's neighbourhoods") &&
        EnsureImage(blur_motion_[0], VK_FORMAT_R16G16_SFLOAT, width, height, rw,
                    "motion blur's motion before") &&
        EnsureImage(blur_motion_[1], VK_FORMAT_R16G16_SFLOAT, width, height, rw,
                    "motion blur's motion before");
    if (ready && !blur_cuts_.image) {
      blur_cuts_cleared_ = false;
      ready = EnsureImage(blur_cuts_, VK_FORMAT_R32_UINT, 2, 1,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          "motion blur's cut counters");
    }
    VkDescriptorSet tile_set = ready ? AllocateSet(blur_tile_set_layout_) : VK_NULL_HANDLE;
    VkDescriptorSet neighbor_set =
        tile_set != VK_NULL_HANDLE ? AllocateSet(blur_neighbor_set_layout_) : VK_NULL_HANDLE;
    if (neighbor_set != VK_NULL_HANDLE) {
      const VkImageSubresourceRange range =
          ui::vulkan::util::InitializeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
      if (!blur_cuts_cleared_) {
        Transition(blur_cuts_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        cp_.SubmitBarriers(true);
        VkClearColorValue zero = {};
        commands.CmdVkClearColorImage(blur_cuts_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      &zero, 1, &range);
        blur_cuts_cleared_ = true;
        blur_previous_valid_ = false;
      }
      Image& before = blur_motion_[turn];
      Image& copy = blur_motion_[turn ^ 1];
      const VkAccessFlags read_write = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      Transition(before, compute, VK_ACCESS_SHADER_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      for (Image* image : {&blur_tiles_, &blur_pixels_, &copy}) {
        Transition(*image, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
      }
      Transition(blur_cuts_, compute, read_write, VK_IMAGE_LAYOUT_GENERAL);
      cp_.SubmitBarriers(true);
      VkDescriptorImageInfo tile_images[7] = {};
      tile_images[0] = {VK_NULL_HANDLE, motion_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      tile_images[1] = {VK_NULL_HANDLE, depth_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      tile_images[2] = {VK_NULL_HANDLE, before.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      tile_images[3] = {VK_NULL_HANDLE, blur_tiles_.view, VK_IMAGE_LAYOUT_GENERAL};
      tile_images[4] = {VK_NULL_HANDLE, blur_pixels_.view, VK_IMAGE_LAYOUT_GENERAL};
      tile_images[5] = {VK_NULL_HANDLE, blur_cuts_.view, VK_IMAGE_LAYOUT_GENERAL};
      tile_images[6] = {VK_NULL_HANDLE, copy.view, VK_IMAGE_LAYOUT_GENERAL};
      VkWriteDescriptorSet tile_writes[7] = {};
      for (uint32_t i = 0; i < 7; ++i) {
        tile_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        tile_writes[i].dstSet = tile_set;
        tile_writes[i].dstBinding = i;
        tile_writes[i].descriptorCount = 1;
        tile_writes[i].descriptorType =
            i < 3 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        tile_writes[i].pImageInfo = &tile_images[i];
      }
      Device()->functions().vkUpdateDescriptorSets(Device()->device(), 7, tile_writes, 0,
                                                   nullptr);
      BlurConstants blur = {};
      blur.size[0] = width;
      blur.size[1] = height;
      blur.tile = blur_tile;
      // Blur vectors are half the motion over the shutter time.
      blur.scale = kBlurShutter[level] * 0.5f;
      blur.cut = float(height) * kBlurCut;
      blur.counter = turn;
      blur.cut_tiles = std::max(tiles_x * tiles_y / kBlurCutTileFraction, uint32_t(1));
      blur.previous_valid = blur_previous_valid_ ? 1 : 0;
      cp_.BindExternalComputePipeline(blur_tile_pipeline_);
      commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, blur_tile_layout_, 0,
                                       1, &tile_set, 0, nullptr);
      commands.CmdVkPushConstants(blur_tile_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                  sizeof(blur), &blur);
      commands.CmdVkDispatch(tiles_x, tiles_y, 1);
      // The neighbourhoods, from the tiles.
      Transition(blur_tiles_, compute, VK_ACCESS_SHADER_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      Transition(blur_around_, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
      // The counters again (the tile pass's atomics, then this pass's read and clear).
      blur_cuts_.stage = compute;
      blur_cuts_.access = read_write;
      {
        VkMemoryBarrier memory = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memory.srcAccessMask = read_write;
        memory.dstAccessMask = read_write;
        commands.CmdVkPipelineBarrier(compute, compute, 0, 1, &memory, 0, nullptr, 0,
                                      nullptr);
      }
      cp_.SubmitBarriers(true);
      VkDescriptorImageInfo neighbor_images[3] = {};
      neighbor_images[0] = {VK_NULL_HANDLE, blur_tiles_.view,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      neighbor_images[1] = {VK_NULL_HANDLE, blur_around_.view, VK_IMAGE_LAYOUT_GENERAL};
      neighbor_images[2] = {VK_NULL_HANDLE, blur_cuts_.view, VK_IMAGE_LAYOUT_GENERAL};
      VkWriteDescriptorSet neighbor_writes[3] = {};
      for (uint32_t i = 0; i < 3; ++i) {
        neighbor_writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        neighbor_writes[i].dstSet = neighbor_set;
        neighbor_writes[i].dstBinding = i;
        neighbor_writes[i].descriptorCount = 1;
        neighbor_writes[i].descriptorType =
            i < 1 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        neighbor_writes[i].pImageInfo = &neighbor_images[i];
      }
      Device()->functions().vkUpdateDescriptorSets(Device()->device(), 3, neighbor_writes, 0,
                                                   nullptr);
      blur.size[0] = tiles_x;
      blur.size[1] = tiles_y;
      cp_.BindExternalComputePipeline(blur_neighbor_pipeline_);
      commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, blur_neighbor_layout_,
                                       0, 1, &neighbor_set, 0, nullptr);
      commands.CmdVkPushConstants(blur_neighbor_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                  sizeof(blur), &blur);
      commands.CmdVkDispatch((tiles_x + 7) / 8, (tiles_y + 7) / 8, 1);
      // Read by the composite.
      Transition(blur_pixels_, compute, VK_ACCESS_SHADER_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      Transition(blur_around_, compute, VK_ACCESS_SHADER_READ_BIT,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      blur_turn_ = turn ^ 1;
      blur_previous_valid_ = true;
      blur_samples = kBlurSamples[level];
    }
  } else {
    // No motion this frame: the next starts again without the motion before.
    blur_previous_valid_ = false;
  }

  // The UI put back over the upscaled scene.
  Transition(upscaled_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(composite_, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
  cp_.SubmitBarriers(true);
  VkDescriptorImageInfo images[7] = {};
  images[0] = {VK_NULL_HANDLE, scene_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[1] = {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[2] = {VK_NULL_HANDLE, color_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[3] = {linear_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
  images[4] = {VK_NULL_HANDLE, composite_.view, VK_IMAGE_LAYOUT_GENERAL};
  // Motion blur's, or (unused without it) the upscaled scene in their place.
  images[5] = {VK_NULL_HANDLE, blur_samples ? blur_pixels_.view : upscaled_.view,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  images[6] = {VK_NULL_HANDLE, blur_samples ? blur_around_.view : upscaled_.view,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet writes[7] = {};
  for (uint32_t i = 0; i < 7; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = composite_set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = i == 3   ? VK_DESCRIPTOR_TYPE_SAMPLER
                               : i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                        : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[i].pImageInfo = &images[i];
  }
  Device()->functions().vkUpdateDescriptorSets(Device()->device(), 7, writes, 0, nullptr);
  CompositeConstants constants = {};
  constants.dest_size[0] = display_width;
  constants.dest_size[1] = display_height;
  constants.render_size[0] = float(width);
  constants.render_size[1] = float(height);
  constants.show_ui = show_ui_ ? 1 : 0;
  constants.blur_samples = blur_samples;
  constants.blur_tile = blur_tile;
  cp_.BindExternalComputePipeline(composite_pipeline_);
  commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, composite_layout_, 0, 1,
                                   &composite_set, 0, nullptr);
  commands.CmdVkPushConstants(composite_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(constants), &constants);
  commands.CmdVkDispatch((display_width + 7) / 8, (display_height + 7) / 8, 1);
  if (timed) {
    commands.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_pool_,
                                 timing_base_ + 3);
    timing_pending_[pool_index_] = true;
    timing_submission_[pool_index_] = cp_.GetCurrentSubmission();
  }
  // Read by Xenia's swap pass (the gamma ramp) as the front buffer would be.
  Transition(composite_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  cp_.SubmitBarriers(true);

  // PLAN 9.14: FSR 3.1's frame generation, from this frame and the one before.
  if (fg_enabled_ && RecordFrameGeneration(width, height, display_width, display_height,
                                           frame_ms)) {
    hudless.generated = fg_output_.view;
  }
  // Frame generation's image before the UI: the upscaled scene, the UI found at the
  // render size as the composite found it.
  hudless.valid = true;
  hudless.image = scene_view;
  hudless.width = display_width;
  hudless.height = display_height;
  hudless.front_render = view;
  hudless.hudless_render = color_.view;
  hudless.render_width = width;
  hudless.render_height = height;
  view = composite_.view;
  width = display_width;
  height = display_height;
  format = xenos::TextureFormat::k_8_8_8_8;
  EndFrame(true, nullptr);
}

// PLAN 9.19: DAORecomp's UpdateDepthOfField and RecordDepthOfField, on Vulkan. The effect
// eases in over 300 ms when the game shows a shot of its own (a conversation, a
// cutscene) and out after; the focus is taken at once when it starts and on a cut (the
// field of view changing, the shots starting or ending). Returns the image the composite
// then takes: depth of field's result, or the upscaled scene.
VkImageView WardenUpscaler::Impl::RecordDepthOfField(uint32_t width, uint32_t height,
                                                     uint32_t display_width,
                                                     uint32_t display_height,
                                                     float frame_ms) {
  const bool shot = ::warden::CameraShot();
  const float fov = ::warden::CameraFovRadians();
  const bool enabled = g_depth_of_field.load(std::memory_order_relaxed) > 0 &&
                       dof_focus_pipeline_ && dof_prefilter_pipeline_ &&
                       dof_blur_pipeline_ && dof_combine_pipeline_;
  const bool wanted = enabled && (shot || dof_show_);
  const float before = dof_strength_;
  if (!wanted && reset_history_) {
    dof_strength_ = 0.0f;
  } else {
    const float step = std::clamp(frame_ms, 0.0f, kDofMaxFrameMs) / kDofEaseMs;
    dof_strength_ = wanted ? std::min(dof_strength_ + step, 1.0f)
                           : std::max(dof_strength_ - step, 0.0f);
  }
  const bool fov_known = fov > 0.0f && dof_fov_ > 0.0f;
  const bool fov_cut =
      !fov_known || !(std::max(fov / dof_fov_, dof_fov_ / fov) < kDofFovCut);
  if (before <= 0.0f || reset_history_ || shot != dof_shot_ || fov_cut) {
    dof_snap_frames_ = kDofSnapFrames;
  }
  dof_shot_ = shot;
  dof_fov_ = fov;
  if ((before > 0.0f) != (dof_strength_ > 0.0f)) {
    XELOGI("Depth of field: {} (field of view {:.1f} degrees)",
           dof_strength_ > 0.0f ? "on" : "off", fov * 57.29578f);
  }
  if (!enabled) {
    for (Image* image : {&dof_focus_, &dof_half_, &dof_blurred_, &dof_out_}) {
      if (image->image) {
        AwaitSubmitted();
        ReleaseImage(*image);
      }
    }
    return upscaled_.view;
  }
  // Made with the first frame upscaled, not the first shot: making them takes a few
  // milliseconds, which a conversation's first frame would show.
  const uint32_t half_width = (display_width + 1) / 2;
  const uint32_t half_height = (display_height + 1) / 2;
  const VkImageUsageFlags rw = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!EnsureImage(dof_focus_, VK_FORMAT_R32_SFLOAT, 2, 1, rw, "depth of field's focus") ||
      !EnsureImage(dof_half_, VK_FORMAT_R16G16B16A16_SFLOAT, half_width, half_height, rw,
                   "depth of field's half size") ||
      !EnsureImage(dof_blurred_, VK_FORMAT_R16G16B16A16_SFLOAT, half_width, half_height, rw,
                   "depth of field's blur") ||
      !EnsureImage(dof_out_, VK_FORMAT_R16G16B16A16_SFLOAT, display_width, display_height,
                   rw, "depth of field")) {
    dof_strength_ = 0.0f;
    return upscaled_.view;
  }
  if (dof_strength_ <= 0.0f) {
    return upscaled_.view;
  }
  VkDescriptorSet sets[4];
  for (VkDescriptorSet& set : sets) {
    set = AllocateSet(dof_set_layout_);
    if (set == VK_NULL_HANDLE) {
      return upscaled_.view;
    }
  }
  DofConstants constants = {};
  constants.display_size[0] = display_width;
  constants.display_size[1] = display_height;
  constants.half_size[0] = half_width;
  constants.half_size[1] = half_height;
  constants.render_size[0] = width;
  constants.render_size[1] = height;
  const float shot_fov = dof_fov_ > 0.0f ? dof_fov_ : kDofDefaultFov;
  const float lens = float(std::sqrt(
      std::tan(kDofReferenceHalfFov) /
      std::tan(std::clamp(double(shot_fov) * 0.5, kDofMinHalfFov, kDofMaxHalfFov))));
  constants.aperture = kDofAperture * float(display_height) * lens;
  constants.max_radius = kDofMaxRadius * float(display_height);
  constants.strength = dof_strength_ * dof_strength_ * (3.0f - 2.0f * dof_strength_);
  constants.focus_rate =
      1.0f - std::exp(-std::clamp(frame_ms, 0.0f, kDofMaxFrameMs) / kDofFocusMs);
  constants.gamma = kDofGamma;
  const float half_radius = constants.max_radius * 0.5f;
  const uint32_t samples =
      uint32_t(3.14159265f * half_radius * half_radius / kDofAreaPerSample);
  constants.samples = std::clamp((samples + 7) / 8 * 8, kDofMinSamples, kDofMaxSamples);
  constants.flags = (dof_snap_frames_ ? kDofFlagSnap : 0) | (dof_show_ ? kDofFlagShow : 0);
  dof_snap_frames_ -= dof_snap_frames_ ? 1 : 0;

  DeferredCommandBuffer& commands = cp_.deferred_command_buffer();
  const VkPipelineStageFlags compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  const VkAccessFlags read_write = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  // One pass: its set (the depth, the focus, its two images, its output, the sampler),
  // then its dispatch.
  const auto pass = [&](VkDescriptorSet set, VkPipeline pipeline, VkImageView a,
                        VkImageView b, Image& out, uint32_t groups_x, uint32_t groups_y) {
    VkDescriptorImageInfo images[6] = {};
    images[0] = {VK_NULL_HANDLE, depth_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[1] = {VK_NULL_HANDLE, dof_focus_.view, VK_IMAGE_LAYOUT_GENERAL};
    images[2] = {VK_NULL_HANDLE, a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[3] = {VK_NULL_HANDLE, b, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[4] = {VK_NULL_HANDLE, out.view, VK_IMAGE_LAYOUT_GENERAL};
    images[5] = {linear_sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[6] = {};
    for (uint32_t i = 0; i < 6; ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = i < 4    ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                 : i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                          : VK_DESCRIPTOR_TYPE_SAMPLER;
      writes[i].pImageInfo = &images[i];
    }
    Device()->functions().vkUpdateDescriptorSets(Device()->device(), 6, writes, 0, nullptr);
    cp_.BindExternalComputePipeline(pipeline);
    commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, dof_layout_, 0, 1, &set,
                                     0, nullptr);
    commands.CmdVkPushConstants(dof_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                sizeof(constants), &constants);
    commands.CmdVkDispatch(groups_x, groups_y, 1);
  };
  // The focus, kept in the general layout (read and written by the focus pass, read by
  // the others).
  Transition(depth_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(upscaled_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(dof_focus_, compute, read_write, VK_IMAGE_LAYOUT_GENERAL);
  cp_.SubmitBarriers(true);
  pass(sets[0], dof_focus_pipeline_, upscaled_.view, upscaled_.view, dof_focus_, 1, 1);
  // The half size scene.
  Transition(dof_focus_, compute, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
  Transition(dof_half_, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
  cp_.SubmitBarriers(true);
  pass(sets[1], dof_prefilter_pipeline_, upscaled_.view, upscaled_.view, dof_half_,
       (half_width + 7) / 8, (half_height + 7) / 8);
  // The blur.
  Transition(dof_half_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(dof_blurred_, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
  cp_.SubmitBarriers(true);
  pass(sets[2], dof_blur_pipeline_, dof_half_.view, dof_half_.view, dof_blurred_,
       (half_width + 7) / 8, (half_height + 7) / 8);
  // Over the sharp scene.
  Transition(dof_blurred_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  Transition(dof_out_, compute, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
  cp_.SubmitBarriers(true);
  pass(sets[3], dof_combine_pipeline_, upscaled_.view, dof_blurred_.view, dof_out_,
       (display_width + 7) / 8, (display_height + 7) / 8);
  Transition(dof_out_, compute, VK_ACCESS_SHADER_READ_BIT,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  cp_.SubmitBarriers(true);
  return dof_out_.view;
}

void WardenUpscaler::Impl::ReadTimings() {
  const uint64_t completed = cp_.GetCompletedSubmission();
  for (uint32_t slot = 0; slot < kFrames; ++slot) {
    if (probe_pending_[slot] && probe_submission_[slot] <= completed) {
      probe_pending_[slot] = false;
      VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = probe_memory_;
      range.size = VK_WHOLE_SIZE;
      Device()->functions().vkInvalidateMappedMemoryRanges(Device()->device(), 1, &range);
      std::memcpy(last_probe_, probe_mapping_ + slot * 20, sizeof(last_probe_));
      probe_seen_ = true;
    }
  }
  for (uint32_t slot = 0; slot < kFrames; ++slot) {
    if (!timing_pending_[slot] || timing_submission_[slot] > completed) {
      continue;
    }
    timing_pending_[slot] = false;
    if (timing_submission_[slot] < timing_from_submission_) {
      continue;
    }
    const uint32_t passes = std::min(timing_passes_[slot], kMaxPassTimes);
    const uint32_t queries = 4 + passes;
    uint64_t results[2 * kQueriesPerFrame] = {};
    const VkResult result = Device()->functions().vkGetQueryPoolResults(
        Device()->device(), timing_pool_, slot * kQueriesPerFrame, queries,
        sizeof(uint64_t) * 2 * queries, results, 2 * sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    bool available = result == VK_SUCCESS;
    for (uint32_t i = 0; i < queries; ++i) {
      available = available && results[2 * i + 1];
    }
    if (!available) {
      continue;
    }
    const double ms_per_tick = timestamp_period_ns_ / 1.0e6;
    if (fg_timed_[slot]) {
      fg_timed_[slot] = false;
      const uint32_t fg_passes = std::min<uint32_t>(
          {fg_slot_passes_[slot], kFgMaxPasses, uint32_t(fg_slot_names_[slot].size())});
      const uint32_t fg_queries = 2 + fg_slot_passes_[slot];
      uint64_t stamps[2 * kFgQueriesPerFrame] = {};
      bool fg_available =
          fg_slot_passes_[slot] <= kFgMaxPasses &&
          Device()->functions().vkGetQueryPoolResults(
              Device()->device(), fg_timing_pool_, slot * kFgQueriesPerFrame, fg_queries,
              sizeof(uint64_t) * 2 * fg_queries, stamps, 2 * sizeof(uint64_t),
              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) == VK_SUCCESS;
      for (uint32_t i = 0; i < fg_queries; ++i) {
        fg_available = fg_available && stamps[2 * i + 1];
      }
      if (fg_available && stamps[2] >= stamps[0]) {
        fg_timing_sum_ms_ += double(stamps[2] - stamps[0]) * ms_per_tick;
        ++fg_timing_frames_;
        // Each pass from the end of the one before (the first from the start).
        uint64_t previous = stamps[0];
        for (uint32_t i = 0; i < fg_passes; ++i) {
          const uint64_t done = stamps[2 * (2 + i)];
          const std::string& name = fg_slot_names_[slot][i];
          auto sum = std::find_if(fg_pass_sums_.begin(), fg_pass_sums_.end(),
                                  [&](const auto& entry) { return entry.first == name; });
          if (sum == fg_pass_sums_.end()) {
            fg_pass_sums_.emplace_back(name, 0.0);
            sum = fg_pass_sums_.end() - 1;
          }
          sum->second += double(done - previous) * ms_per_tick;
          previous = done;
        }
      }
    }
    timing_sum_ms_[0] += double(results[2] - results[0]) * ms_per_tick;
    timing_sum_ms_[1] += double(results[4] - results[2]) * ms_per_tick;
    timing_sum_ms_[2] += double(results[6] - results[4]) * ms_per_tick;
    // Each pass from the end of the one before (the first from the dispatch's start).
    uint64_t previous = results[2];
    for (uint32_t i = 0; i < passes; ++i) {
      const uint64_t done = results[2 * (4 + i)];
      timing_pass_sum_ms_[i] += double(done - previous) * ms_per_tick;
      previous = done;
    }
    timing_pass_count_ = passes;
    if (++timing_frames_ >= kTimedFrames) {
      const auto now = std::chrono::steady_clock::now();
      const double seconds = std::chrono::duration<double>(now - window_start_).count();
      XELOGI("FSR 3.1: GPU ms a frame - the motion vectors {:.2f}, FSR 3.1 {:.2f} "
             "({}-bit), the composite {:.2f}; {:.1f} render target copies a frame; "
             "{:.1f} fps",
             timing_sum_ms_[0] / timing_frames_, timing_sum_ms_[1] / timing_frames_,
             half_ ? 16 : 32, timing_sum_ms_[2] / timing_frames_,
             double(captures_sum_) /
                 double(std::max(frames_upscaled_ - captures_from_frame_, uint64_t(1))),
             seconds > 0.0 ? double(frames_ - window_frames_start_) / seconds : 0.0);
      window_start_ = now;
      window_frames_start_ = frames_;
      captures_sum_ = 0;
      captures_from_frame_ = frames_upscaled_;
      std::string pass_times;
      for (uint32_t i = 0; i < timing_pass_count_; ++i) {
        const std::string name = i < std::size(kPassNames) ? std::string(kPassNames[i])
                                                           : fmt::format("pass {}", i);
        pass_times += fmt::format("{}{} {:.2f}", i ? ", " : "", name,
                                  timing_pass_sum_ms_[i] / timing_frames_);
      }
      XELOGI("FSR 3.1: its passes, GPU ms a frame ({}-bit{}) - {}", half_ ? 16 : 32,
             lut_ ? ", the Lanczos lookup table" : "", pass_times);
      if (fg_enabled_) {
        XELOGI("FSR 3.1: frame generation - {:.2f} GPU ms a frame for its optical flow and "
               "interpolation ({} timed); {} frames generated so far",
               fg_timing_frames_ ? fg_timing_sum_ms_ / fg_timing_frames_ : 0.0,
               fg_timing_frames_, fg_generated_);
        if (fg_timing_frames_) {
          std::string fg_times;
          for (const auto& [name, ms] : fg_pass_sums_) {
            fg_times += fmt::format("{}{} {:.2f}", fg_times.empty() ? "" : ", ", name,
                                    ms / fg_timing_frames_);
          }
          XELOGI("FSR 3.1: frame generation's passes, GPU ms a frame - {}", fg_times);
        }
        fg_pass_sums_.clear();
        fg_timing_sum_ms_ = 0.0;
        fg_timing_frames_ = 0;
      }
      const uint32_t motion_total = motion_frames_ + motion_unknown_frames_;
      XELOGI("FSR 3.1: the camera's motion in {} of {} frames ({} cuts); {:.1f} static "
             "meshes matched a frame, {:.1f} agreeing",
             motion_frames_, motion_total, motion_cuts_,
             double(motion_matches_) / std::max(motion_total, 1u),
             double(motion_agreeing_) / std::max(motion_frames_ + motion_cuts_, 1u));
      if (probe_seen_) {
        std::string probes;
        for (uint32_t i = 0; i < 5; ++i) {
          const float* v = last_probe_ + i * 4;
          probes += fmt::format("{}depth {:.5f} motion ({:.2f} {:.2f}) w {:.2f}",
                                i ? "; " : "", v[0], v[1], v[2], v[3]);
        }
        XELOGI("FSR 3.1: the motion vectors at the centre and the quarter points - {}",
               probes);
      }
      motion_frames_ = motion_unknown_frames_ = motion_cuts_ = 0;
      motion_matches_ = motion_agreeing_ = 0;
      timing_sum_ms_[0] = timing_sum_ms_[1] = timing_sum_ms_[2] = 0.0;
      std::fill(std::begin(timing_pass_sum_ms_), std::end(timing_pass_sum_ms_), 0.0);
      timing_frames_ = 0;
      precision_change_ = half_alternate_ || lut_alternate_;
    }
  }
}

void WardenUpscaler::Impl::EndFrame(bool upscaled, const char* reason) {
  if (upscaled) {
    captures_sum_ += captures_;
  }
  captures_ = 0;
  ++frames_;
  frames_upscaled_ += upscaled ? 1 : 0;
  last_upscaled_ = upscaled;
  if (upscaled != (last_reason_ == nullptr) || (!upscaled && reason != last_reason_) ||
      frames_ == 1) {
    XELOGI("FSR 3.1: {} (frame {}, {} upscaled so far)",
           upscaled ? "upscaling" : fmt::format("not upscaling - {}", reason), frames_,
           frames_upscaled_);
  }
  last_reason_ = upscaled ? nullptr : reason;
  // A frame not upscaled breaks the history.
  reset_history_ = !upscaled;
  // The chain's last pass, from this frame: the pass its image before the UI came from -
  // or none known, after a pass behind the known one or a 3D frame without that image.
  uint64_t last_pass = last_pass_shader_;
  if (upscaled || (capture_only_ && scene_draws_ && hudless_captured_)) {
    last_pass = relearn_ ? 0 : captured_shader_;
  } else if (scene_draws_ && !hudless_captured_) {
    last_pass = 0;
  }
  if (last_pass != last_pass_shader_) {
    XELOGI("FSR 3.1: {}", last_pass ? fmt::format("the image before the UI is pixel shader "
                                                  "{:016X}'s - the passes before it are not "
                                                  "copied",
                                                  last_pass)
                                    : std::string("the post-processing's last pass is "
                                                  "learned again - every pass copied"));
  }
  last_pass_shader_ = last_pass;
  captured_shader_ = 0;
  last_pass_seen_ = false;
  relearn_ = false;
  // The next frame's jitter.
  if (jitter_active_ && !capture_only_ && render_size_[0]) {
    uint32_t display_width = 0, display_height = 0;
    GetDisplaySize(render_size_[0], render_size_[1], display_width, display_height);
    const int32_t phases =
        ffxFsr3UpscalerGetJitterPhaseCount(int32_t(render_size_[0]), int32_t(display_width));
    jitter_index_ = phases > 0 ? (jitter_index_ + 1) % uint32_t(phases) : 0;
    ffxFsr3UpscalerGetJitterOffset(&jitter_[0], &jitter_[1], int32_t(jitter_index_),
                                   std::max(phases, int32_t(1)));
  }
  // This frame's static meshes are the next one's before - if it was upscaled: across a
  // frame that was not, the camera's motion is not known.
  if (upscaled) {
    previous_mesh_draws_.swap(mesh_draws_);
  } else {
    previous_mesh_draws_.clear();
  }
  mesh_draws_.clear();
  scene_texture_ = 0;
  scene_draws_ = 0;
  depth_tiles_ = 0;
  single_sample_scene_drawn_ = false;
  scene_depth_pending_ = false;
  scene_depth_target_ = nullptr;
  hudless_pending_ = false;
  hudless_captured_ = false;
  hudless_target_ = nullptr;
  draw_index_ = 0;
  trace_lines_ = 0;
  tracing_ = trace_enabled_ && upscaled && traces_ < 8 && frames_upscaled_ % 1000 == 500;
  if (tracing_) {
    ++traces_;
    XELOGI("FSR 3.1 trace: frame {} ({} upscaled), from the scene's resolve", frames_ + 1,
           frames_upscaled_ + 1);
  }
  pool_index_ = (pool_index_ + 1) % kFrames;
  pool_ready_ = false;
}

namespace {
WardenUpscaler::Impl* g_upscaler = nullptr;
}  // namespace

void WardenUpscaler::Initialize(vulkan::VulkanCommandProcessor& command_processor,
                                vulkan::VulkanRenderTargetCache& render_target_cache) {
  Shutdown();
  // PLAN 9.14: frame generation, with the upscaler's set-up and in the same swap.
  warden::FrameGenerationInitialize(command_processor);
  g_upscaler = new Impl(command_processor, render_target_cache);
  if (!g_upscaler->Initialize()) {
    XELOGE("FSR 3.1: not available");
    delete g_upscaler;
    g_upscaler = nullptr;
  }
}

void WardenUpscaler::Shutdown() {
  delete g_upscaler;
  g_upscaler = nullptr;
  warden::FrameGenerationShutdown();
}

void WardenUpscaler::OnDraw(const RegisterFile& regs, draw_util::ViewportInfo& viewport_info,
                            reg::RB_DEPTHCONTROL depth_control, uint32_t color_mask,
                            uint32_t pixel_shader_texture_mask, uint64_t pixel_shader_hash,
                            uint64_t vertex_shader_hash, bool window_offset_in_edram) {
  if (g_upscaler) {
    g_upscaler->OnDraw(regs, viewport_info, depth_control, color_mask,
                       pixel_shader_texture_mask, pixel_shader_hash, vertex_shader_hash,
                       window_offset_in_edram);
  }
}

void WardenUpscaler::AfterDraw() {
  if (g_upscaler) {
    g_upscaler->AfterDraw();
  }
}

void WardenUpscaler::OnResolve(const draw_util::ResolveInfo& resolve_info) {
  if (g_upscaler) {
    g_upscaler->OnResolve(resolve_info);
  }
}

bool WardenUpscaler::FrameUpscaled() {
  return g_upscaler && g_upscaler->last_upscaled();
}

void WardenUpscaler::OnSwap(VkImageView& view, uint32_t& width, uint32_t& height,
                            xenos::TextureFormat& format) {
  if (g_upscaler) {
    g_upscaler->OnSwap(view, width, height, format);
  }
}

}  // namespace gpu
}  // namespace xe

// PLAN 9.18: motion blur's level, live from the options panel.
namespace warden {
void SetMotionBlur(int level) {
  xe::gpu::g_motion_blur.store(level < 0 ? 0 : level > 2 ? 2 : level,
                               std::memory_order_relaxed);
}
}  // namespace warden

// PLAN 9.19: depth of field in conversations, live from the options panel.
namespace warden {
void SetDepthOfField(bool on) {
  xe::gpu::g_depth_of_field.store(on ? 1 : 0, std::memory_order_relaxed);
}
}  // namespace warden
