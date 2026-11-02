// PLAN 9.14: frame generation between the game's frames - see warden_frame_generation.cc.
// Its hooks in the swap are WardenUpscaler's (vulkan_command_processor.h, the graft):
// FrameGenerationReady and OnGuestOutput.
#ifndef WARDEN_FRAME_GENERATION_H_
#define WARDEN_FRAME_GENERATION_H_

namespace xe::gpu::vulkan {
class VulkanCommandProcessor;
}  // namespace xe::gpu::vulkan

namespace warden {

// With the upscaler's (WardenUpscaler::Initialize and Shutdown, with Xenia's context):
// the setting read; the generator itself is made at the first guest output it can take.
void FrameGenerationInitialize(xe::gpu::vulkan::VulkanCommandProcessor& command_processor);
void FrameGenerationShutdown();

}  // namespace warden

#endif  // WARDEN_FRAME_GENERATION_H_
