// Frame pacing (PLAN 4.7) over VK_GOOGLE_display_timing: when each frame reached the
// panel, read back from the presentation engine, and - when pacing is on - each frame held
// in the compositor until the refresh that keeps an even cadence. See
// warden_frame_pacing.cc.
//
// The presenter's hooks (recorded edits in vulkan_presenter.cc) have C linkage, so the
// edits declare them without including this header:
//   const void* warden_present_timing(VkDevice, VkSwapchainKHR, bool display_timing);
//       just before vkQueuePresentKHR: the VkPresentTimesInfoGOOGLE to chain into the
//       present, or null without the extension.
//   void warden_presented(VkDevice, VkSwapchainKHR);
//       just after it: the present recorded (warden_frame_times) and the panel's times
//       read back.
#ifndef WARDEN_FRAME_PACING_H_
#define WARDEN_FRAME_PACING_H_

#include <string>

namespace warden {

// "mode .., refresh .. ms, cadence n, lead .. ms | off panel: .. | paced panel: .." -
// since the previous call, per mode: the panel's intervals (warden_frame_times'
// DescribeIntervals, in refreshes), present-to-panel latency, how many refreshes earlier
// the frames were ready, frames never shown, and for the paced mode the late frames and
// the grid's moves. Empty before the first present.
std::string FramePacingSummary();

// The frame rate changed (warden_frame_rate.h), or is first known: the panel's pacing
// follows it - each frame held for its place on the title's timeline at the title's own
// 30, shown as soon as it is finished above it - unless files/frame-pacing chose a mode.
void FramePacingFollowFrameRate();

// PLAN 9.14: whether frame generation presents its pairs (a generated frame and the real
// one, presented at once by dao_frame_pacer_android.cc's thread, which names each
// present's role through warden_present_role): meanwhile each present's desired time is
// on the pairs' timeline, the real frame half a title frame after the generated one,
// instead of the modes' - the paced timeline expects one present a title frame.
void FramePacingFrameGeneration(bool on);

}  // namespace warden

#endif  // WARDEN_FRAME_PACING_H_
