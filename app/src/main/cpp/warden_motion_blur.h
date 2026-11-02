// PLAN 9.18: motion blur's level (warden_upscale.cc, with FSR 3.1's motion vectors).
#ifndef WARDEN_MOTION_BLUR_H_
#define WARDEN_MOTION_BLUR_H_

namespace warden {

// 0 off, 1 low, 2 high - from files/motion-blur as the upscaler starts, then live.
void SetMotionBlur(int level);

// PLAN 9.19: depth of field in conversations and cutscenes - from files/dof as the
// upscaler starts, then live.
void SetDepthOfField(bool on);

}  // namespace warden

#endif  // WARDEN_MOTION_BLUR_H_
