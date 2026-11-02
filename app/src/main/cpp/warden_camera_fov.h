// PLAN 9.17: the field of view options (warden_camera_fov.cc).
#ifndef WARDEN_CAMERA_FOV_H_
#define WARDEN_CAMERA_FOV_H_

#include <string>

namespace warden {

// "fov" (in play) or "fov-conversation" (conversations and cutscenes): horizontal
// degrees at 16:9, 80-120; 91 is the game's own. Taken at the next frame.
void SetCameraFov(const std::string& which, int degrees);

// PLAN 9.19: whether the game shows a shot of its own (a conversation or a cutscene),
// and the main camera's vertical field of view in radians as last given (0: none yet) -
// for depth of field (warden_upscale.cc). Any thread.
bool CameraShot();
float CameraFovRadians();

// PLAN 9.20: photo mode's field of view (vertical radians; 0 none), kept in the fly
// camera's mode; and the play setting it starts from (horizontal degrees at 16:9).
void SetPhotoFov(float radians);
int GameplayFovDegrees();

}  // namespace warden

#endif  // WARDEN_CAMERA_FOV_H_
