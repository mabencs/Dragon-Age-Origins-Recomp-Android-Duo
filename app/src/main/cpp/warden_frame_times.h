// Frame times for the performance baseline (PLAN 6.7): when the presenter queues frames
// for the panel and when the title finishes them, summarised for the GPU status line
// every few seconds. When the frames reached the panel is warden_frame_pacing's (4.7).
#ifndef WARDEN_FRAME_TIMES_H_
#define WARDEN_FRAME_TIMES_H_

#include <cstdint>
#include <string>
#include <vector>

namespace warden {

// A frame queued for the panel: vkQueuePresentKHR has returned (warden_frame_pacing's
// hook). The presenting thread. The frame reaches the panel at a later refresh.
void RecordPresent(int64_t present_nanos);

// The display's refresh period, from Choreographer's refresh-rate callback.
void SetDisplayRefreshPeriod(int64_t period_nanos);

// The title finished a frame (VdSwap). The title's render thread.
void RecordGuestSwap();

// The median of the title's last 16 frame intervals, or 0 before it has made two frames.
int64_t RecentGuestFrameInterval();

// The mean of the title's last 64 frame intervals, leaving out pauses (more than four times
// the median - its three- and four-vblank frames count), or 0 before it has made two
// frames: its true rate, which at its 30 fps cap is a little under 30 (33.42 ms at the
// King's Camp, run 280).
int64_t RecentGuestMeanInterval();

// The oldest of the title's swaps not yet presented, taken: when it was made, or 0 when
// there is none. The presenter presents once per swap; warden_frame_pacing takes one per
// present to time the swap's way to the panel.
int64_t TakeOldestGuestSwap();

// "present: ... ; title: ..." - the intervals since the previous call: count, mean,
// percentiles, the worst, the 1% low, and how many refreshes (or 60 Hz vblanks, for the
// title) each frame took.
std::string FrameTimesSummary();

// "<what>: N frames, interval ms mean .. p50 .. p95 .. p99 .. max .., 1% low .. fps, in
// <unit_name> (.. ms): 3x.. 4x.." - the histogram only with a unit.
std::string DescribeIntervals(const char* what, std::vector<int64_t> intervals, int64_t unit,
                              const char* unit_name);

}  // namespace warden

#endif  // WARDEN_FRAME_TIMES_H_
