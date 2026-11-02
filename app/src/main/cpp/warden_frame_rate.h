// The frame rate (PLAN 9.1, 10.1): the title's own 30, or 45, 60 or 120. See
// warden_frame_rate.cc.
#ifndef WARDEN_FRAME_RATE_H_
#define WARDEN_FRAME_RATE_H_

namespace warden {

// 30 is the title's own: its 3D scenes flip every second vblank. 60 flips at every
// vblank; 45 and 120 flip each frame at once, the title's render thread held to the rate
// (FrameSwapped, FrameKicked). Anything else is 60, the default. Taken at the next swap,
// so the options panel changes it while the game runs.
void SetFrameRate(int frames_per_second);
// The rate on the screen: as set, unless the heat guard holds it lower.
int FrameRate();
// The rate as the player set it (Options -> Frame rate).
int ChosenFrameRate();
// PLAN 10.2: the heat guard's limit (warden_thermal.cc, an option, off by default) - 30,
// 45 or 60 hold the rate on the screen there while the chosen one is higher; 0 lifts it.
void SetFrameRateCap(int frames_per_second);
// The game's own (PLAN 9.14): the rate on the screen divided by frame generation's
// multiplier (2x, 3x, 4x) while it makes frames between the game's.
double TitleFrameRate();

// The title's render thread: its VdSwap, then its write of the ring buffer's write
// pointer that hands the frame to the GPU. At 45 and 120 the thread is held there to the
// rate.
void FrameSwapped();
void FrameKicked();

}  // namespace warden

#endif  // WARDEN_FRAME_RATE_H_
