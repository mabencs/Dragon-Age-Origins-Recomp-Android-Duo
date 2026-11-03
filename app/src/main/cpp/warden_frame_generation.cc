// PLAN 9.14: frame generation - Lossless Scaling's (lsfg-vk's backend on the game's own
// device, warden_lsfg.cc) between the game's frames, shown in pairs by the presenter
// (dao_frame_pacer_android.cc, in place of DAORecomp's). DAORecomp's own frame generation is
// FSR 3.1's on Direct3D 12 (gpu/d3d12/dao_upscale_fg.cc), made from its upscaler's frame
// with the UI handed over apart; this one, as Lossless Scaling itself, is made from the
// frames as they are shown, the UI in them.
//
// Each guest output (the command processor's swap; the graft's edits in IssueSwap):
//   - FrameGenerationReady, before the presenter's refresh: whether this one gets a
//     generated frame - frame generation on, the generator made for its size, the guest
//     output before taken into it (and Presenter::DaoGeneratedFramesShown, upstream's
//     pause when its pacer fell behind; never here). The presenter then makes the mailbox
//     slot's generated image.
//   - OnGuestOutput, in the refresh after the gamma ramp's pass, in the same submission:
//     the guest output image copied into the generator's next source (10-bit to 8-bit),
//     the generator's work recorded - lsfg-vk's prepass of each frame, and its passes
//     for the frame between this one and the one before when there is a generated frame -
//     and that frame copied into the slot's generated image. The generation runs in the
//     game's own command stream on queue 0: no handover between queues, and the Adreno
//     time-slices its queues anyway (session 36's timestamps).
// A thread of the presenter's then presents the pair at once, both paints right after the
// title's frame on the GPU, and the compositor spaces them by their desired times: the real
// frame half the title's frame interval after the generated one (warden_frame_pacing.cc).
//
// The setting is files/frame-generation: "lossless" (lsfg-vk's quality mode, its flow at
// the full size), "lossless-balanced" (the quality mode, the flow at half the size - PLAN
// 10.9), "lossless-performance" (its performance mode, the flow at half the size),
// "fsr3" (FSR 3.1's own frame generation, with FSR 3.1 on: its optical flow and frame
// interpolation in the upscaler, from the upscaled scene before the UI and the upscaler's
// depth and motion - warden_upscale.cc - and the ramp and the real frame's UI put on it
// here); off without it. Lossless.dll is the owner's own, from their copy of Lossless Scaling, in
// the files folder (never in git, never bundled). files/frame-generation-dump (a test
// switch) writes a generated frame and the two it lies between (lsfg-*.raw, RGBA8) once
// the generator has been up the seconds it holds (120 without a number).
#include "warden_frame_generation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

// Xenia's Vulkan headers before warden_lsfg.h's <vulkan/vulkan_core.h>, which would
// otherwise be the NDK's older one, short of what Xenia's Vulkan-Hpp names.
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"

#include "xenia/ui/vulkan/vulkan_util.h"

#include "warden_deck.h"
#include "warden_frame_pacing.h"
#include "warden_gpu.h"
#include "warden_hudless.h"
#include "warden_lsfg.h"

// libvulkan's own export: the runtime's vulkan_api.h declares no prototypes.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char* name);
// Below: the frames generated between each two of the game's (0 without frame generation).
extern "C" uint32_t warden_frame_generation_count();

namespace xe {
namespace gpu {

namespace {

namespace shaders {
#include "xenia/ui/shaders/bytecode/vulkan_spirv/warden_framegen_ui_cs.h"
}  // namespace shaders

// Generations timed at once (two timestamps each), read back once their submission is done.
constexpr uint32_t kTimingSlots = 4;
// The UI pass's descriptor sets and gamma ramps: one a guest output, reused once the
// submission that used it is done (Xenia keeps three frames in flight).
constexpr uint32_t kUiRing = 4;
constexpr VkDeviceSize kRampBytes = 256 * sizeof(uint32_t);
// The most frames generated between two of the game's (the presenter's
// VulkanGuestOutputRefreshContext::kDaoMaxGenerated).
constexpr uint32_t kMaxGenerated = 3;

// The setting's frames generated between each two of the game's, 0 for none.
// (3x and 4x - lossless-3x, lossless-4x - were dropped by the owner, 2026-10-30; the
// presenting takes groups of up to kMaxGenerated still.)
uint32_t SettingCount(const std::string& setting) {
  return setting == "lossless" || setting == "lossless-balanced" ||
                 setting == "lossless-performance" || setting == "fsr3"
             ? 1
             : 0;
}

// FSR 3.1's frame generation needs FSR 3.1 itself on (warden_upscale.cc's settings).
bool Fsr3On(const std::string& setting) {
  return setting == "quality" || setting == "balanced" || setting == "performance" ||
         setting == "ultra";
}

// The UI pass's push constants (warden_framegen_ui.cs.glsl).
struct UiConstants {
  uint32_t dest_size[2];
  float render_size[2];
  uint32_t mode;
};
// Guest outputs between the log's summaries: ten seconds at 30 fps.
constexpr uint32_t kSummaryOutputs = 300;
// BUG-081: the guest outputs a new size must hold for before the generator is made again
// at it (about 2 s, the frame held meanwhile) - so a size seen for a frame or two (the
// first frame after the game's menu, which FSR 3.1 does not upscale) costs nothing.
constexpr uint32_t kStableSizeOutputs = 10;
// BUG-092: guest outputs without a generated frame that keep the pairs' pacing - a
// conversation's first frames have no 3D scene (one to ten of them, BUG-108), so no image
// to generate from, and the pacing's switch off and back for them cost the game a 60-100 ms
// frame and the panel seconds of uneven frames. A longer gap, a second at the game's 30 fps
// (a menu), paces as without frame generation.
constexpr uint32_t kPacingGapOutputs = 30;
// The dump switch's seconds without a number.
constexpr int kDumpAfterSeconds = 120;

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

bool FileExists(const std::string& path) {
  if (FILE* file = std::fopen(path.c_str(), "rb")) {
    std::fclose(file);
    return true;
  }
  return false;
}

VkImageMemoryBarrier ImageBarrier(VkImage image, VkAccessFlags src_access,
                                  VkAccessFlags dst_access, VkImageLayout old_layout,
                                  VkImageLayout new_layout) {
  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcAccessMask = src_access;
  barrier.dstAccessMask = dst_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return barrier;
}

VkImageBlit WholeBlit(uint32_t width, uint32_t height) {
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {int32_t(width), int32_t(height), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[1] = {int32_t(width), int32_t(height), 1};
  return blit;
}

class FrameGeneration {
 public:
  explicit FrameGeneration(vulkan::VulkanCommandProcessor& command_processor);
  ~FrameGeneration();

  bool Ready(uint32_t width, uint32_t height, bool generated_shown);
  void OnGuestOutput(VkImage image, VkImageView image_view,
                     const VkImage* generated_images, uint32_t width, uint32_t height);

 private:
  using Presenter = ui::vulkan::VulkanPresenter;

  bool MakeUiPass();
  void DestroyUiPass();
  // The UI pass's sets for this guest output (ring slot `slot`): mode 0 writing the
  // generator's next source, then mode 1 for each generated frame.
  bool PrepareUiSets(uint32_t slot, VkImageView image_view,
                     const warden::HudlessFrame& hudless);
  void DispatchUi(VkCommandBuffer command_buffer, VkDescriptorSet set, uint32_t mode);

  const ui::vulkan::VulkanDevice* Device() const { return cp_.GetVulkanDevice(); }
  // What the GPU was given before this frame's commands - not the open submission:
  // waiting for it would end it in the middle of the swap (BUG-065).
  void AwaitSubmitted() {
    cp_.CheckSubmissionCompletionAndDeviceLoss(cp_.GetCurrentSubmission() - 1);
  }
  bool MakeGenerator(uint32_t width, uint32_t height);
  // FSR 3.1's: the image the UI pass writes its frame into, at the guest output's size.
  bool MakeFsr3Image(uint32_t width, uint32_t height);
  void DestroyFsr3Image();
  static void Execute(void* data, VkCommandBuffer command_buffer);
  void Record(VkCommandBuffer command_buffer);
  void RecordFsr3(VkCommandBuffer command_buffer);
  void ReadTimings();
  void Summarize();

  vulkan::VulkanCommandProcessor& cp_;
  bool enabled_ = false;
  bool perf_ = false;
  bool half_flow_ = false;
  // FSR 3.1's frame generation rather than Lossless Scaling's: no generator here, the frame
  // from the upscaler (warden_hudless.h's generated).
  bool fsr3_ = false;
  VkImage fsr3_image_ = VK_NULL_HANDLE;
  VkDeviceMemory fsr3_memory_ = VK_NULL_HANDLE;
  VkImageView fsr3_view_ = VK_NULL_HANDLE;
  // Guest outputs FSR 3.1 made no frame for (a cut, a screen without a 3D scene).
  uint32_t window_fsr3_none_ = 0;
  // Frames generated between each two of the game's.
  uint32_t count_ = 1;
  bool fp16_ = false;
  bool dump_ = false;
  // BUG-112: the generator submits on the game's queue 0 (a driver with one queue).
  bool shared_queue_ = false;
  int dump_after_seconds_ = kDumpAfterSeconds;
  std::chrono::steady_clock::time_point generator_made_;
  bool failed_ = false;
  std::string dll_path_;
  std::unique_ptr<warden::lsfg::Generator> generator_;
  uint32_t width_ = 0, height_ = 0;
  // BUG-081: a size other than the generator's, and the guest outputs in a row at it.
  uint32_t new_width_ = 0, new_height_ = 0;
  uint32_t new_size_outputs_ = 0;

  // Guest outputs seen (Ready), and the last one taken into the generator (0: none since
  // it was made), so a generation always lies between two consecutive ones.
  uint64_t outputs_ = 0;
  uint64_t taken_output_ = 0;
  // This guest output's, as Ready decided.
  bool take_ = false;
  bool generate_ = false;
  bool shown_last_ = true;
  // BUG-092: guest outputs since the last with a generated frame (UINT32_MAX: none yet).
  uint32_t pacing_gap_ = UINT32_MAX;
  // The pacing told whether this guest output is paced as a group: generated, or a short
  // gap after one (kPacingGapOutputs).
  void Pace(bool generated);
  // What the callback records, set by OnGuestOutput just before its submission ends.
  struct Pending {
    VkImage image = VK_NULL_HANDLE;
    // The slot's images for the generated frames; generate: all of count_ there.
    VkImage generated[kMaxGenerated] = {};
    uint32_t width = 0, height = 0;
    bool generate = false;
    int timing_slot = -1;
    // From the image before the UI (else the guest output as shown), the UI put back.
    bool hudless = false;
    // FSR 3.1's: its frame (the UI pass's mode 2) into fsr3_image_, then the slot's image.
    bool fsr3 = false;
    uint32_t render_width = 0, render_height = 0;
    VkDescriptorSet ui_sets[1 + kMaxGenerated] = {};
  } pending_;

  // The UI pass (warden_framegen_ui.cs.glsl): files/frame-generation-ui holding "off" (a
  // test switch) makes the frames from the guest output as shown, the UI in them.
  bool ui_enabled_ = true;
  VkDescriptorSetLayout ui_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout ui_layout_ = VK_NULL_HANDLE;
  VkPipeline ui_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool ui_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet ui_sets_[kUiRing][1 + kMaxGenerated] = {};
  uint64_t ui_submissions_[kUiRing] = {};
  uint32_t ui_next_ = 0;
  VkBuffer ramp_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory ramp_memory_ = VK_NULL_HANDLE;
  uint8_t* ramp_mapping_ = nullptr;
  bool ramp_coherent_ = false;
  uint32_t window_hudless_ = 0;

  // The GPU time of what OnGuestOutput records: a slot's submission, 0 when free.
  VkQueryPool timing_pool_ = VK_NULL_HANDLE;
  double timestamp_period_ns_ = 1.0;
  uint64_t timing_submission_[kTimingSlots] = {};
  bool timing_generated_[kTimingSlots] = {};
  uint32_t timing_next_ = 0;

  // Since the last summary: guest outputs, those with a generated frame, those the presenter
  // showed only the real frame of; GPU ms with a generation and with the prepass alone.
  uint32_t window_outputs_ = 0;
  uint32_t window_generated_ = 0;
  uint32_t window_real_only_ = 0;
  double window_ms_[2] = {};
  uint32_t window_timed_[2] = {};
  uint64_t generated_total_ = 0;
  // The last frame taken had its generation recorded (what a dump reads).
  bool last_generated_ = false;
  bool dumped_ = false;
  bool record_failure_logged_ = false;
};

FrameGeneration* g_frame_generation = nullptr;

FrameGeneration::FrameGeneration(vulkan::VulkanCommandProcessor& command_processor)
    : cp_(command_processor) {
  const std::string setting = SettingWord("frame-generation");
  count_ = SettingCount(setting);
  enabled_ = count_ != 0;
  perf_ = setting == "lossless-performance";
  // PLAN 10.9: the flow at half the size but for "lossless" - in its quality mode at the
  // King's Camp's 1920x1080, 5.7 ms a generation against 14.0 (runs 499, 508).
  half_flow_ = setting != "lossless";
  fsr3_ = setting == "fsr3";
  dump_ = FileExists(FilesDir() + "/frame-generation-dump");
  if (const int seconds = std::atoi(SettingWord("frame-generation-dump").c_str())) {
    dump_after_seconds_ = seconds;
  }
  dll_path_ = FilesDir() + "/Lossless.dll";
  using ::warden::deck::RenderNote;
  using ::warden::deck::SetRenderNote;
  if (!enabled_) {
    XELOGI("Frame generation: off");
    SetRenderNote(RenderNote::kFrameGeneration, "Off");
    return;
  }
  if (fsr3_ && !Fsr3On(SettingWord("fsr3"))) {
    XELOGE("Frame generation: FSR 3.1's needs FSR 3.1 on - off");
    SetRenderNote(RenderNote::kFrameGeneration, "Off (FSR 3.1 2x needs FSR 3.1)");
    enabled_ = false;
    return;
  }
  if (!fsr3_ && !FileExists(dll_path_)) {
    SetRenderNote(RenderNote::kFrameGeneration, "Off (no Lossless.dll)");
    XELOGE("Frame generation: no Lossless.dll in the files folder - off (the owner's own, "
           "from their copy of Lossless Scaling)");
    enabled_ = false;
    return;
  }
  const ui::vulkan::VulkanDevice* device = Device();
  const auto& dfn = device->functions();
  const auto& ifn = device->vulkan_instance()->functions();
  // 16-bit arithmetic: the graft enables shaderInt16 and Vulkan 1.2's shaderFloat16
  // wherever they are supported.
  VkPhysicalDeviceVulkan12Features features_1_2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &features_1_2;
  ifn.vkGetPhysicalDeviceFeatures2(device->physical_device(), &features);
  fp16_ = device->properties().apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0) &&
          features_1_2.shaderFloat16 && features.features.shaderInt16 &&
          SettingWord("frame-generation-fp16") != "off";  // a test switch (BUG-112)
  VkQueryPoolCreateInfo query_info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  query_info.queryCount = kTimingSlots * 2;
  if (dfn.vkCreateQueryPool(device->device(), &query_info, nullptr, &timing_pool_) !=
      VK_SUCCESS) {
    timing_pool_ = VK_NULL_HANDLE;
  }
  VkPhysicalDeviceProperties properties;
  ifn.vkGetPhysicalDeviceProperties(device->physical_device(), &properties);
  timestamp_period_ns_ = properties.limits.timestampPeriod;
  // FSR 3.1's frame is put together by the UI pass always: its ramp is applied there.
  ui_enabled_ = fsr3_ || SettingWord("frame-generation-ui") != "off";
  if (ui_enabled_ && !MakeUiPass()) {
    if (fsr3_) {
      SetRenderNote(RenderNote::kFrameGeneration, "Off (FSR 3.1 2x failed)");
      XELOGE("Frame generation: no UI pass - FSR 3.1's frame generation off");
      DestroyUiPass();
      enabled_ = false;
      return;
    }
    XELOGE("Frame generation: no UI pass - the frames made with the UI in them");
    DestroyUiPass();
    ui_enabled_ = false;
  }
  if (fsr3_) {
    XELOGI("Frame generation: FSR 3.1's 2x - from the upscaled scene before the UI, the real "
           "frame's UI put back");
    SetRenderNote(RenderNote::kFrameGeneration, "FSR 3.1 2x");
    return;
  }
  // The option's own words.
  SetRenderNote(RenderNote::kFrameGeneration,
                perf_ ? "Lossless 2x, fast"
                : half_flow_ ? "Lossless 2x, balanced"
                             : "Lossless 2x");
  XELOGI("Frame generation: Lossless Scaling's (lsfg-vk) {}x, {} mode, {} - made at the "
         "first guest output",
         count_ + 1, perf_ ? "performance" : "quality",
         ui_enabled_ ? "from the image before the UI, the real frame's UI put back"
                     : "from the frames as shown (files/frame-generation-ui)");
}

bool FrameGeneration::MakeUiPass() {
  const ui::vulkan::VulkanDevice* device = Device();
  const auto& dfn = device->functions();
  const VkDevice vk_device = device->device();
  VkDescriptorSetLayoutBinding bindings[6] = {};
  for (uint32_t i = 0; i < 6; ++i) {
    bindings[i] = {i,
                   i < 4    ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                   : i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                            : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  }
  VkDescriptorSetLayoutCreateInfo set_layout_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_layout_info.bindingCount = 6;
  set_layout_info.pBindings = bindings;
  if (dfn.vkCreateDescriptorSetLayout(vk_device, &set_layout_info, nullptr,
                                      &ui_set_layout_) != VK_SUCCESS) {
    return false;
  }
  VkPushConstantRange push_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(UiConstants)};
  VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &ui_set_layout_;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (dfn.vkCreatePipelineLayout(vk_device, &layout_info, nullptr, &ui_layout_) !=
      VK_SUCCESS) {
    return false;
  }
  ui_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      device, ui_layout_, shaders::warden_framegen_ui_cs,
      sizeof(shaders::warden_framegen_ui_cs));
  if (ui_pipeline_ == VK_NULL_HANDLE) {
    return false;
  }
  const VkDescriptorPoolSize pool_sizes[3] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4 * (1 + kMaxGenerated) * kUiRing},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (1 + kMaxGenerated) * kUiRing},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (1 + kMaxGenerated) * kUiRing}};
  VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_info.maxSets = (1 + kMaxGenerated) * kUiRing;
  pool_info.poolSizeCount = 3;
  pool_info.pPoolSizes = pool_sizes;
  if (dfn.vkCreateDescriptorPool(vk_device, &pool_info, nullptr, &ui_pool_) != VK_SUCCESS) {
    return false;
  }
  VkDescriptorSetLayout layouts[(1 + kMaxGenerated) * kUiRing];
  std::fill(std::begin(layouts), std::end(layouts), ui_set_layout_);
  VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocate_info.descriptorPool = ui_pool_;
  allocate_info.descriptorSetCount = (1 + kMaxGenerated) * kUiRing;
  allocate_info.pSetLayouts = layouts;
  if (dfn.vkAllocateDescriptorSets(vk_device, &allocate_info, &ui_sets_[0][0]) !=
      VK_SUCCESS) {
    return false;
  }
  // The gamma ramps: host-visible, one a ring slot.
  VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size = kRampBytes * kUiRing;
  buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (dfn.vkCreateBuffer(vk_device, &buffer_info, nullptr, &ramp_buffer_) != VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements requirements;
  dfn.vkGetBufferMemoryRequirements(vk_device, ramp_buffer_, &requirements);
  const uint32_t memory_type = ui::vulkan::util::ChooseHostMemoryType(
      device->memory_types(), requirements.memoryTypeBits, false);
  if (memory_type == UINT32_MAX) {
    return false;
  }
  ramp_coherent_ = (device->memory_types().host_coherent & (uint32_t(1) << memory_type)) != 0;
  VkMemoryAllocateInfo memory_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  memory_info.allocationSize = requirements.size;
  memory_info.memoryTypeIndex = memory_type;
  void* mapping = nullptr;
  if (dfn.vkAllocateMemory(vk_device, &memory_info, nullptr, &ramp_memory_) != VK_SUCCESS ||
      dfn.vkBindBufferMemory(vk_device, ramp_buffer_, ramp_memory_, 0) != VK_SUCCESS ||
      dfn.vkMapMemory(vk_device, ramp_memory_, 0, VK_WHOLE_SIZE, 0, &mapping) != VK_SUCCESS) {
    return false;
  }
  ramp_mapping_ = static_cast<uint8_t*>(mapping);
  return true;
}

void FrameGeneration::DestroyUiPass() {
  const auto& dfn = Device()->functions();
  const VkDevice device = Device()->device();
  if (ramp_mapping_) {
    dfn.vkUnmapMemory(device, ramp_memory_);
    ramp_mapping_ = nullptr;
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device, ramp_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device, ramp_memory_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device, ui_pool_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, ui_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device, ui_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout, device,
                                         ui_set_layout_);
}

bool FrameGeneration::PrepareUiSets(uint32_t slot, VkImageView image_view,
                                    const warden::HudlessFrame& hudless) {
  // The game's ramp as the swap uploads it this frame.
  const reg::DC_LUT_30_COLOR* ramp = cp_.warden_gamma_ramp_256_entry_table();
  std::memcpy(ramp_mapping_ + kRampBytes * slot, ramp, kRampBytes);
  if (!ramp_coherent_) {
    VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = ramp_memory_;
    range.size = VK_WHOLE_SIZE;
    Device()->functions().vkFlushMappedMemoryRanges(Device()->device(), 1, &range);
  }
  VkDescriptorImageInfo images[1 + kMaxGenerated][5] = {};
  VkDescriptorBufferInfo ramp_info = {ramp_buffer_, kRampBytes * slot, kRampBytes};
  VkWriteDescriptorSet writes[1 + kMaxGenerated][6] = {};
  // Set 0 mode 0, sets 1 to count_ mode 1, each on its generated frame; FSR 3.1's: set 0
  // mode 2, its frame into fsr3_image_.
  const uint32_t sets = fsr3_ ? 1 : 1 + count_;
  for (uint32_t mode = 0; mode < sets; ++mode) {
    images[mode][0] = {VK_NULL_HANDLE, fsr3_ ? hudless.generated : hudless.image,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[mode][1] = {VK_NULL_HANDLE, hudless.front_render,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[mode][2] = {VK_NULL_HANDLE, hudless.hudless_render,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[mode][3] = {VK_NULL_HANDLE, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    images[mode][4] = {VK_NULL_HANDLE,
                       fsr3_       ? fsr3_view_
                       : mode == 0 ? generator_->next_source_view()
                                   : generator_->generated_view(mode - 1),
                       VK_IMAGE_LAYOUT_GENERAL};
    for (uint32_t i = 0; i < 6; ++i) {
      VkWriteDescriptorSet& write = writes[mode][i];
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = ui_sets_[slot][mode];
      write.dstBinding = i;
      write.descriptorCount = 1;
      write.descriptorType = i < 4    ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                             : i == 4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                      : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      if (i < 5) {
        write.pImageInfo = &images[mode][i];
      } else {
        write.pBufferInfo = &ramp_info;
      }
    }
  }
  Device()->functions().vkUpdateDescriptorSets(Device()->device(), 6 * sets, &writes[0][0],
                                               0, nullptr);
  return true;
}

void FrameGeneration::DispatchUi(VkCommandBuffer command_buffer, VkDescriptorSet set,
                                 uint32_t mode) {
  const auto& dfn = Device()->functions();
  UiConstants constants = {};
  constants.dest_size[0] = pending_.width;
  constants.dest_size[1] = pending_.height;
  constants.render_size[0] = float(pending_.render_width);
  constants.render_size[1] = float(pending_.render_height);
  constants.mode = mode;
  dfn.vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ui_pipeline_);
  dfn.vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ui_layout_, 0, 1,
                              &set, 0, nullptr);
  dfn.vkCmdPushConstants(command_buffer, ui_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(constants), &constants);
  dfn.vkCmdDispatch(command_buffer, (pending_.width + 7) / 8, (pending_.height + 7) / 8, 1);
}

FrameGeneration::~FrameGeneration() {
  // Xenia's context is shut down after all its queue operations completed.
  generator_.reset();
  DestroyFsr3Image();
  warden::FramePacingFrameGeneration(false);
  if (ui_set_layout_) {
    DestroyUiPass();
  }
  if (timing_pool_) {
    Device()->functions().vkDestroyQueryPool(Device()->device(), timing_pool_, nullptr);
  }
}

bool FrameGeneration::MakeFsr3Image(uint32_t width, uint32_t height) {
  if (fsr3_image_) {
    // Made again only at a size change, rare: whatever used it is let finish first.
    AwaitSubmitted();
    DestroyFsr3Image();
  }
  const ui::vulkan::VulkanDevice* device = Device();
  VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
  info.extent = {width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          device, info, ui::vulkan::util::MemoryPurpose::kDeviceLocal, fsr3_image_,
          fsr3_memory_)) {
    XELOGE("Frame generation: no image for FSR 3.1's frames ({}x{})", width, height);
    return false;
  }
  VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = fsr3_image_;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = info.format;
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (device->functions().vkCreateImageView(device->device(), &view_info, nullptr,
                                            &fsr3_view_) != VK_SUCCESS) {
    fsr3_view_ = VK_NULL_HANDLE;
    DestroyFsr3Image();
    return false;
  }
  width_ = width;
  height_ = height;
  taken_output_ = 0;
  XELOGI("Frame generation: FSR 3.1's frames put together at {}x{}", width, height);
  return true;
}

void FrameGeneration::DestroyFsr3Image() {
  const auto& dfn = Device()->functions();
  const VkDevice device = Device()->device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyImageView, device, fsr3_view_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyImage, device, fsr3_image_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device, fsr3_memory_);
}

bool FrameGeneration::MakeGenerator(uint32_t width, uint32_t height) {
  if (generator_) {
    // Made again only at a size change, rare: whatever used it is let finish first.
    AwaitSubmitted();
    generator_.reset();
  }
  taken_output_ = 0;
  const ui::vulkan::VulkanDevice* device = Device();
  warden::lsfg::Setup setup;
  setup.instance = device->vulkan_instance()->instance();
  setup.physical_device = device->physical_device();
  setup.device = device->device();
  setup.get_instance_proc_addr = vkGetInstanceProcAddr;
  // The graft's second queue where the driver has one (the Adreno's has three); a driver
  // with one queue in the family (BUG-112: Mesa's Turnip) shares the game's queue 0, held
  // through the generator's one-off submissions below. Asked for, a queue that does not
  // exist is a null one, and Android's loader dies on it.
  const uint32_t family = device->queue_family_graphics_compute();
  const bool own_queue = device->queue_families()[family].queues.size() >= 2;
  setup.queue_index = own_queue ? 1 : 0;
  setup.dll_path = dll_path_;
  setup.width = width;
  setup.height = height;
  setup.count = count_;
  setup.fp16 = fp16_;
  setup.perf = perf_;
  setup.flow = half_flow_ ? 2.0f : 1.0f;
  const auto start = std::chrono::steady_clock::now();
  std::string error;
  {
    std::optional<ui::vulkan::VulkanDevice::Queue::Acquisition> held;
    if (!own_queue) {
      held.emplace(device->AcquireQueue(family, 0));
    }
    generator_ = warden::lsfg::Generator::Create(setup, &error);
  }
  shared_queue_ = !own_queue;
  if (!generator_) {
    XELOGE("Frame generation: no generator - {}", error);
    return false;
  }
  width_ = width;
  height_ = height;
  generator_made_ = std::chrono::steady_clock::now();
  last_generated_ = false;
  XELOGI("Frame generation: the generator is up in {:.0f} ms - {}x{}, {} between two, {} "
         "mode, flow 1/{:.0f}, {}-bit shaders, {}",
         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
             .count(),
         width, height, count_, perf_ ? "performance" : "quality", setup.flow,
         generator_->fp16() ? 16 : 32,
         own_queue ? "its own queue" : "the game's queue (the driver has one)");
  return true;
}

void FrameGeneration::Pace(bool generated) {
  if (generated) {
    pacing_gap_ = 0;
  } else if (pacing_gap_ != UINT32_MAX && ++pacing_gap_ > kPacingGapOutputs) {
    pacing_gap_ = UINT32_MAX;
  }
  // Within a short gap the guest output is a group of one, paced as the groups are
  // (dao_frame_pacer_android.cc, warden_frame_pacing.cc).
  warden::FramePacingFrameGeneration(pacing_gap_ != UINT32_MAX);
}

bool FrameGeneration::Ready(uint32_t width, uint32_t height, bool generated_shown) {
  ++outputs_;
  take_ = false;
  generate_ = false;
  if (!enabled_ || failed_) {
    return false;
  }
  // No window (the app put away): nothing is shown, so nothing generated - and no
  // generator made again for a size seen meanwhile (BUG-100: 8.7 s on this thread, the
  // game stopped behind it, the autosave asked for as the app is put away not written).
  if (xe::ui::Presenter* presenter = warden::gpu::Presenter()) {
    uint32_t surface_width = 0, surface_height = 0;
    presenter->GetPaintSurfaceSize(surface_width, surface_height);
    if (!surface_width || !surface_height) {
      return false;
    }
  }
  ReadTimings();
  if (++window_outputs_ >= kSummaryOutputs) {
    Summarize();
  }
  if (dump_ && !dumped_ && generator_ && last_generated_ &&
      std::chrono::steady_clock::now() - generator_made_ >=
          std::chrono::seconds(dump_after_seconds_)) {
    dumped_ = true;
    // Nothing of this frame's touches the generator's images yet.
    AwaitSubmitted();
    std::string error;
    std::optional<ui::vulkan::VulkanDevice::Queue::Acquisition> held;
    if (shared_queue_) {
      const ui::vulkan::VulkanDevice* device = Device();
      held.emplace(device->AcquireQueue(device->queue_family_graphics_compute(), 0));
    }
    if (generator_->Dump(FilesDir(), &error)) {
      XELOGI("Frame generation: a generated frame and the two it lies between written - "
             "{}x{} RGBA8, lsfg-generated.raw, lsfg-source-before.raw, lsfg-source-after.raw",
             width_, height_);
    } else {
      XELOGE("Frame generation: no dump - {}", error);
    }
  }
  if (fsr3_) {
    if (!fsr3_image_ || width != width_ || height != height_) {
      if (!MakeFsr3Image(width, height)) {
        failed_ = true;
        warden::FramePacingFrameGeneration(false);
        return false;
      }
    }
    // The upscaler's frame between the last swap's and this one's, when it made one (at
    // the panel's size, so the guest output's); the presenter showing pairs.
    const warden::HudlessFrame& hudless = warden::CurrentHudlessFrame();
    take_ = generated_shown && hudless.valid && hudless.generated != VK_NULL_HANDLE &&
            hudless.width == width && hudless.height == height;
    if (!generated_shown) {
      ++window_real_only_;
    } else if (!take_) {
      ++window_fsr3_none_;
    }
    generate_ = take_;
    Pace(generate_);
    return generate_;
  }
  if (!generator_ || width != width_ || height != height_) {
    // BUG-081: made again only once the new size has held - until then the real frames
    // alone, the generator kept for the size it has.
    if (generator_) {
      // BUG-108: and never for a smaller size. With an upscaler the frames without a 3D
      // scene are smaller - a conversation's first ten, a menu's - and the world comes
      // back at the size the generator has: made down and up again, about 2 s each, for a
      // picture that needs no generated frames.
      if (uint64_t(width) * height < uint64_t(width_) * height_) {
        new_width_ = 0;
        new_height_ = 0;
        new_size_outputs_ = 0;
        ++window_real_only_;
        Pace(false);
        return false;
      }
      if (width != new_width_ || height != new_height_) {
        new_width_ = width;
        new_height_ = height;
        new_size_outputs_ = 0;
      }
      if (++new_size_outputs_ < kStableSizeOutputs) {
        ++window_real_only_;
        Pace(false);
        return false;
      }
      XELOGI("Frame generation: the guest output is {}x{} now, not {}x{} - the generator "
             "made again",
             width, height, width_, height_);
      // The frames meanwhile are the game's alone: no pairs to pace.
      pacing_gap_ = UINT32_MAX;
      warden::FramePacingFrameGeneration(false);
    }
    if (!MakeGenerator(width, height)) {
      failed_ = true;
      warden::FramePacingFrameGeneration(false);
      return false;
    }
  }
  new_size_outputs_ = 0;
  // While the presenter shows real frames only, nothing is taken: the first guest output
  // after it is taken alone, the next has a generated frame again.
  if (!generated_shown) {
    if (shown_last_) {
      XELOGI("Frame generation: paused - the presenter shows real frames only for now");
    }
    shown_last_ = false;
    ++window_real_only_;
    Pace(false);
    return false;
  }
  shown_last_ = true;
  take_ = true;
  generate_ = taken_output_ != 0 && taken_output_ + 1 == outputs_;
  Pace(generate_);
  return generate_;
}

void FrameGeneration::OnGuestOutput(VkImage image, VkImageView image_view,
                                    const VkImage* generated_images, uint32_t width,
                                    uint32_t height) {
  if (!take_ || !(generator_ || fsr3_) || !image || width != width_ || height != height_) {
    return;
  }
  take_ = false;
  pending_ = Pending();
  pending_.image = image;
  pending_.width = width;
  pending_.height = height;
  // (The presenter gives a generated image only when Ready said there would be one.)
  pending_.generate = generate_;
  for (uint32_t i = 0; i < count_; ++i) {
    pending_.generated[i] = generate_ ? generated_images[i] : VK_NULL_HANDLE;
    pending_.generate = pending_.generate && pending_.generated[i] != VK_NULL_HANDLE;
  }
  // From the image before the UI when this swap has one (a 3D scene) at this size, and a
  // ring slot is free.
  const warden::HudlessFrame& hudless = warden::CurrentHudlessFrame();
  if (ui_enabled_ && hudless.valid && hudless.width == width && hudless.height == height &&
      image_view) {
    const uint32_t ui_slot = ui_next_ % kUiRing;
    if (ui_submissions_[ui_slot] <= cp_.GetCompletedSubmission() &&
        PrepareUiSets(ui_slot, image_view, hudless)) {
      ui_submissions_[ui_slot] = cp_.GetCurrentSubmission();
      ++ui_next_;
      pending_.hudless = true;
      pending_.fsr3 = fsr3_;
      pending_.render_width = hudless.render_width;
      pending_.render_height = hudless.render_height;
      for (uint32_t i = 0; i <= count_; ++i) {
        pending_.ui_sets[i] = ui_sets_[ui_slot][i];
      }
      ++window_hudless_;
    }
  }
  const uint32_t slot = timing_next_ % kTimingSlots;
  if (timing_pool_ && !timing_submission_[slot]) {
    pending_.timing_slot = int(slot);
    timing_submission_[slot] = cp_.GetCurrentSubmission();
    timing_generated_[slot] = pending_.generate;
    ++timing_next_;
  }
  // After the guest output's release barrier, which Xenia has pending.
  cp_.SubmitBarriers(true);
  cp_.deferred_command_buffer().CmdWardenCallback(Execute, this);
  taken_output_ = outputs_;
  last_generated_ = pending_.generate;
  if (pending_.generate) {
    ++window_generated_;
    ++generated_total_;
  }
}

void FrameGeneration::Execute(void* data, VkCommandBuffer command_buffer) {
  auto& self = *static_cast<FrameGeneration*>(data);
  if (self.fsr3_) {
    self.RecordFsr3(command_buffer);
  } else {
    self.Record(command_buffer);
  }
}

void FrameGeneration::RecordFsr3(VkCommandBuffer command_buffer) {
  const auto& dfn = Device()->functions();
  const Pending& p = pending_;
  if (p.timing_slot >= 0) {
    dfn.vkCmdResetQueryPool(command_buffer, timing_pool_, uint32_t(p.timing_slot) * 2, 2);
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timing_pool_,
                            uint32_t(p.timing_slot) * 2);
  }
  const VkImageBlit blit = WholeBlit(p.width, p.height);
  VkImageMemoryBarrier barriers[2];
  if (!p.fsr3) {
    // No UI pass ring slot free (the GPU three frames behind): the real frame in the
    // slot's image, shown twice rather than an unwritten image.
    barriers[0] = ImageBarrier(p.image, 0, VK_ACCESS_TRANSFER_READ_BIT,
                               Presenter::kGuestOutputInternalLayout,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barriers[1] = ImageBarrier(p.generated[0], 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    dfn.vkCmdPipelineBarrier(command_buffer, Presenter::kGuestOutputInternalStageMask,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                             barriers);
    dfn.vkCmdBlitImage(command_buffer, p.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       p.generated[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_NEAREST);
    barriers[0] = ImageBarrier(p.image, 0, Presenter::kGuestOutputInternalAccessMask,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               Presenter::kGuestOutputInternalLayout);
    barriers[1] = ImageBarrier(p.generated[0], VK_ACCESS_TRANSFER_WRITE_BIT,
                               Presenter::kGuestOutputInternalAccessMask,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               Presenter::kGuestOutputInternalLayout);
    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             Presenter::kGuestOutputInternalStageMask, 0, 0, nullptr, 0,
                             nullptr, 2, barriers);
    return;
  }
  // Every write before this seen by the pass (FSR 3.1's frame, the front buffer, the guest
  // output), and the image's read by the last frame's blit done.
  VkMemoryBarrier seen = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  seen.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  seen.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barriers[0] = ImageBarrier(fsr3_image_, VK_ACCESS_TRANSFER_READ_BIT,
                             VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);
  dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &seen, 0, nullptr, 1,
                           barriers);
  DispatchUi(command_buffer, p.ui_sets[0], 2);
  // Into the slot's image, last read by paints of the presenter's.
  barriers[0] = ImageBarrier(fsr3_image_, VK_ACCESS_SHADER_WRITE_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  barriers[1] = ImageBarrier(p.generated[0], 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  dfn.vkCmdPipelineBarrier(
      command_buffer,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | Presenter::kGuestOutputInternalStageMask,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
  dfn.vkCmdBlitImage(command_buffer, fsr3_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     p.generated[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_NEAREST);
  barriers[0] = ImageBarrier(p.generated[0], VK_ACCESS_TRANSFER_WRITE_BIT,
                             Presenter::kGuestOutputInternalAccessMask,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             Presenter::kGuestOutputInternalLayout);
  dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           Presenter::kGuestOutputInternalStageMask, 0, 0, nullptr, 0, nullptr,
                           1, barriers);
  if (p.timing_slot >= 0) {
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_pool_,
                            uint32_t(p.timing_slot) * 2 + 1);
  }
}

void FrameGeneration::Record(VkCommandBuffer command_buffer) {
  const auto& dfn = Device()->functions();
  const Pending& p = pending_;
  if (p.timing_slot >= 0) {
    dfn.vkCmdResetQueryPool(command_buffer, timing_pool_, uint32_t(p.timing_slot) * 2, 2);
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timing_pool_,
                            uint32_t(p.timing_slot) * 2);
  }
  const VkImage source = generator_->next_source();
  const VkImageBlit blit = WholeBlit(p.width, p.height);
  // The generator's own images, written by the last frame's passes in this same command
  // stream, made ready for this frame's passes to read or write them again (below).
  VkMemoryBarrier memory_barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  VkImageMemoryBarrier barriers[2];

  if (p.hudless) {
    // The image before the UI, the ramp applied, into the generator's next source - every
    // write before this (the capture, the front buffer) seen by the pass, and the
    // source's reads by the last frame's passes done.
    VkMemoryBarrier seen = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    seen.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    seen.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0] = ImageBarrier(source, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &seen, 0, nullptr, 1,
                             barriers);
    DispatchUi(command_buffer, p.ui_sets[0], 0);
    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memory_barrier, 0,
                             nullptr, 0, nullptr);
  } else {
    // The guest output, from the state the presenter takes it in (its writes already made
    // available by the release barrier), into the generator's next source - whose reads
    // by the last frame's passes are done first.
    barriers[0] = ImageBarrier(p.image, 0, VK_ACCESS_TRANSFER_READ_BIT,
                               Presenter::kGuestOutputInternalLayout,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barriers[1] = ImageBarrier(source, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    dfn.vkCmdPipelineBarrier(command_buffer,
                             Presenter::kGuestOutputInternalStageMask |
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2,
                             barriers);
    dfn.vkCmdBlitImage(command_buffer, p.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source,
                       VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
    // The guest output back as the presenter takes it; the source to the generator's
    // shaders.
    barriers[0] = ImageBarrier(p.image, 0, Presenter::kGuestOutputInternalAccessMask,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               Presenter::kGuestOutputInternalLayout);
    barriers[1] = ImageBarrier(source, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    dfn.vkCmdPipelineBarrier(
        command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        Presenter::kGuestOutputInternalStageMask | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
        &memory_barrier, 0, nullptr, 2, barriers);
  }

  std::string error;
  const bool recorded = generator_->Record(command_buffer, p.generate, &error);
  if (!recorded) {
    failed_ = true;
    if (!record_failure_logged_) {
      record_failure_logged_ = true;
      XELOGE("Frame generation: recording failed - {}; off from here", error);
    }
  }

  if (p.generate && recorded && p.hudless) {
    // The real frame's UI over each generated frame, in place.
    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memory_barrier, 0,
                             nullptr, 0, nullptr);
    for (uint32_t i = 0; i < count_; ++i) {
      DispatchUi(command_buffer, p.ui_sets[1 + i], 1);
    }
  }
  for (uint32_t i = 0; p.generate && i < count_; ++i) {
    // Each generated frame into the slot's image - or, if the generator failed, this frame
    // again (its copy in the source), so the slot never shows an unwritten image. The
    // slot's images were last read by paints of the presenter's.
    const VkImage from = recorded ? generator_->generated(i) : source;
    barriers[0] = ImageBarrier(from, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    barriers[1] = ImageBarrier(p.generated[i], 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    dfn.vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | Presenter::kGuestOutputInternalStageMask,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    dfn.vkCmdBlitImage(command_buffer, from, VK_IMAGE_LAYOUT_GENERAL, p.generated[i],
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    barriers[0] = ImageBarrier(p.generated[i], VK_ACCESS_TRANSFER_WRITE_BIT,
                               Presenter::kGuestOutputInternalAccessMask,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               Presenter::kGuestOutputInternalLayout);
    dfn.vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             Presenter::kGuestOutputInternalStageMask, 0, 0, nullptr, 0,
                             nullptr, 1, barriers);
  }
  if (p.timing_slot >= 0) {
    dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing_pool_,
                            uint32_t(p.timing_slot) * 2 + 1);
  }
}

void FrameGeneration::ReadTimings() {
  const uint64_t completed = cp_.GetCompletedSubmission();
  for (uint32_t slot = 0; slot < kTimingSlots; ++slot) {
    if (!timing_submission_[slot] || completed < timing_submission_[slot]) {
      continue;
    }
    timing_submission_[slot] = 0;
    // Each timestamp with its availability word.
    uint64_t stamps[4] = {};
    if (Device()->functions().vkGetQueryPoolResults(
            Device()->device(), timing_pool_, slot * 2, 2, sizeof(stamps), stamps,
            2 * sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) != VK_SUCCESS ||
        !stamps[1] || !stamps[3] || stamps[2] < stamps[0]) {
      continue;
    }
    const int kind = timing_generated_[slot] ? 1 : 0;
    window_ms_[kind] += double(stamps[2] - stamps[0]) * timestamp_period_ns_ / 1.0e6;
    ++window_timed_[kind];
  }
}

void FrameGeneration::Summarize() {
  const auto average = [&](int kind) {
    return window_timed_[kind] ? window_ms_[kind] / window_timed_[kind] : 0.0;
  };
  if (fsr3_) {
    XELOGI("Frame generation: {} of the last {} guest outputs with FSR 3.1's frame, {} "
           "without one (a cut, no 3D scene), {} shown real only; GPU {:.2f} ms putting "
           "each together ({} timed) - its interpolation in the upscaler's log",
           window_generated_, window_outputs_, window_fsr3_none_, window_real_only_,
           average(1), window_timed_[1]);
  } else {
    XELOGI("Frame generation: {} of the last {} guest outputs with a generated frame, {} "
           "taken from the image before the UI, {} shown real only; GPU {:.2f} ms a guest "
           "output with a generation ({} timed), {:.2f} ms taken alone ({} timed)",
           window_generated_, window_outputs_, window_hudless_, window_real_only_,
           average(1), window_timed_[1], average(0), window_timed_[0]);
  }
  window_fsr3_none_ = 0;
  window_outputs_ = 0;
  window_generated_ = 0;
  window_hudless_ = 0;
  window_real_only_ = 0;
  window_ms_[0] = window_ms_[1] = 0.0;
  window_timed_[0] = window_timed_[1] = 0;
}

}  // namespace

bool WardenUpscaler::FrameGenerationReady(uint32_t width, uint32_t height,
                                          bool generated_shown) {
  return g_frame_generation && g_frame_generation->Ready(width, height, generated_shown);
}

void WardenUpscaler::OnGuestOutput(VkImage image, VkImageView image_view,
                                   const VkImage* generated_images, uint32_t width,
                                   uint32_t height) {
  if (g_frame_generation) {
    g_frame_generation->OnGuestOutput(image, image_view, generated_images, width, height);
  }
}

}  // namespace gpu
}  // namespace xe

// The swapchain's (vulkan_presenter.cc, the graft; made before Xenia's context, so the
// setting is read here): whether frame generation presents. Each guest output is two
// presents then, the second held by its desired time: two more images in flight, and FIFO
// rather than MAILBOX, whose newer present replaces the one still queued (run 429: one of
// each pair never reached the panel).
extern "C" bool warden_frame_generation_presents() {
  return warden_frame_generation_count() != 0;
}

// The frames generated between each two of the game's while frame generation presents,
// else 0 (the presenter's images, the pacing's steps, the frame rate's multiplier).
// Read from the files once, and again as the game boots (warden_frame_generation_reread):
// the bring-up presenter's swapchain asks first, at startup, and the pre-launch settings
// may change the setting before the game is launched.
namespace {
std::atomic<int> g_frame_generation_count{-1};
}  // namespace

extern "C" uint32_t warden_frame_generation_count() {
  int count = g_frame_generation_count.load(std::memory_order_acquire);
  if (count < 0) {
    const std::string setting = xe::gpu::SettingWord("frame-generation");
    // FSR 3.1's with FSR 3.1 on; Lossless Scaling's with the owner's Lossless.dll.
    const bool available = setting == "fsr3"
                               ? xe::gpu::Fsr3On(xe::gpu::SettingWord("fsr3"))
                               : xe::gpu::FileExists(xe::gpu::FilesDir() + "/Lossless.dll");
    count = available ? int(xe::gpu::SettingCount(setting)) : 0;
    g_frame_generation_count.store(count, std::memory_order_release);
  }
  return uint32_t(count);
}

extern "C" void warden_frame_generation_reread() {
  g_frame_generation_count.store(-1, std::memory_order_release);
}

namespace warden {

HudlessFrame& CurrentHudlessFrame() {
  static HudlessFrame frame;
  return frame;
}

bool FrameGenerationWantsHudless() {
  return warden_frame_generation_presents() &&
         (xe::gpu::SettingWord("frame-generation") == "fsr3" ||
          xe::gpu::SettingWord("frame-generation-ui") != "off");
}

void FrameGenerationInitialize(xe::gpu::vulkan::VulkanCommandProcessor& command_processor) {
  FrameGenerationShutdown();
  xe::gpu::g_frame_generation = new xe::gpu::FrameGeneration(command_processor);
}

void FrameGenerationShutdown() {
  delete xe::gpu::g_frame_generation;
  xe::gpu::g_frame_generation = nullptr;
}

}  // namespace warden
