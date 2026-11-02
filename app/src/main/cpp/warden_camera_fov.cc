// PLAN 9.17: the field of view as two options - in play, and in conversations and
// cutscenes - ported from DAORecomp's gpu/camera_fov.cpp (the owner picked it,
// 2026-11-01).
//
// Every frame the game's camera update (tu6_sub_82284BD8) gives the main camera its
// field of view - vertical, in radians - through the camera's SetFov (vtable slot 3,
// tu6_sub_82683E60): 60 degrees in play, each shot's own in conversations. SetFov on the
// main camera inside that update is scaled for the game's mode: by the play setting in
// exploration, combat and the menus over the world, by the conversation one in
// conversations and cutscenes, not at all elsewhere (the title, loading, character
// creation). The tangent of half the angle is scaled, so the picture keeps its
// proportions: 110 widens the game's 91.5 degrees across (at 16:9) to 110, and a
// conversation shot by as much. The settings are horizontal degrees at 16:9, 80 to 120;
// 91, the game's own, leaves it alone.
#include "warden_camera_fov.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ppc_recomp_shared.h"
#include "warden_game_library.h"
#include "xenia/base/logging.h"

WARDEN_GAME_FUNC(__imp__tu6_sub_82284BD8);
WARDEN_GAME_FUNC(__imp__tu6_sub_82683E60);

namespace warden {
namespace {

constexpr int kOriginalFov = 91;
constexpr int kMinFov = 80;
constexpr int kMaxFov = 120;

// The main camera (Title Update 6's address, as upstream names it).
constexpr uint32_t kMainCamera = 0x832E227C;
// The camera update's object: its mode controller at +280, the controller's current
// mode at +120, the mode's number at +96 (upstream's kernel/game_modes.h).
constexpr uint32_t kModeControllerOffset = 280;
constexpr uint32_t kCurrentModeOffset = 120;
constexpr uint32_t kModeNumberOffset = 96;
constexpr int32_t kModeMenu = 3;  // the last of the modes in the world's own camera
constexpr int32_t kModeConversation = 4;
constexpr int32_t kModeCutscene = 5;
constexpr int32_t kModeFlyCam = 6;  // the development fly camera, photo mode's

// What a scaled field of view may be, in radians.
constexpr double kMinScaledFov = 0.01;
constexpr double kMaxScaledFov = 3.0;
constexpr double kPi = 3.14159265358979323846;
// tan(fov / 2) of the game's own horizontal field of view at 16:9: 60 degrees vertical.
const double kOriginalHalfTan = std::tan(kPi / 6.0) * 16.0 / 9.0;

std::atomic<float> g_photo_fov{0.0f};
std::atomic<bool> g_shot{false};
std::atomic<float> g_fov_radians{0.0f};
std::atomic<int> g_gameplay{-1};
std::atomic<int> g_conversation{-1};
thread_local uint32_t t_camera_update = 0;

int ReadSetting(const char* file) {
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  int degrees = kOriginalFov;
  if (FILE* f = dir ? std::fopen((std::string(dir) + "/" + file).c_str(), "r") : nullptr) {
    int value = 0;
    if (std::fscanf(f, "%d", &value) == 1) {
      degrees = value;
    }
    std::fclose(f);
  }
  return std::clamp(degrees, kMinFov, kMaxFov);
}

int Setting(std::atomic<int>& setting, const char* file) {
  int value = setting.load(std::memory_order_relaxed);
  if (value < 0) {
    value = ReadSetting(file);
    setting.store(value, std::memory_order_relaxed);
    XELOGI("camera: field of view {} {}", file, value);
  }
  return value;
}

double FovScale(int degrees) {
  if (degrees == kOriginalFov) {
    return 1.0;
  }
  return std::tan(degrees * kPi / 360.0) / kOriginalHalfTan;
}

int32_t Mode(uint8_t* base, uint32_t camera_update) {
  const uint32_t controller = PPC_LOAD_U32(camera_update + kModeControllerOffset);
  const uint32_t current = controller ? PPC_LOAD_U32(controller + kCurrentModeOffset) : 0;
  return current ? int32_t(PPC_LOAD_U32(current + kModeNumberOffset)) : -1;
}

}  // namespace

bool CameraShot() { return g_shot.load(std::memory_order_relaxed); }

void SetPhotoFov(float radians) { g_photo_fov.store(radians, std::memory_order_relaxed); }

int GameplayFovDegrees() { return Setting(g_gameplay, "fov"); }

float CameraFovRadians() { return g_fov_radians.load(std::memory_order_relaxed); }

void SetCameraFov(const std::string& which, int degrees) {
  degrees = std::clamp(degrees, kMinFov, kMaxFov);
  (which == "fov-conversation" ? g_conversation : g_gameplay)
      .store(degrees, std::memory_order_relaxed);
}

}  // namespace warden

// The camera update: r3 is its object.
WARDEN_OVERRIDE(tu6_sub_82284BD8) {
  const uint32_t before = warden::t_camera_update;
  warden::t_camera_update = ctx.r3.u32;
  __imp__tu6_sub_82284BD8(ctx, base);
  warden::t_camera_update = before;
}

// The camera's SetFov: r3 the camera, f1 the field of view. The mode is read here, not
// as the update starts: the update may change it first.
WARDEN_OVERRIDE(tu6_sub_82683E60) {
  using namespace warden;
  if (t_camera_update && ctx.r3.u32 == PPC_LOAD_U32(kMainCamera)) {
    const int32_t mode = Mode(base, t_camera_update);
    int degrees = kOriginalFov;
    if (mode >= 0 && mode <= kModeMenu) {
      degrees = Setting(g_gameplay, "fov");
    } else if (mode == kModeConversation || mode == kModeCutscene) {
      degrees = Setting(g_conversation, "fov-conversation");
    }
    const float photo = g_photo_fov.load(std::memory_order_relaxed);
    if (mode == kModeFlyCam && photo > 0.0f) {
      ctx.f1.f64 = double(photo);
    } else if (degrees != kOriginalFov) {
      const double scaled = 2.0 * std::atan(std::tan(ctx.f1.f64 / 2.0) * FovScale(degrees));
      // Rounded to single precision: the game keeps it as a float.
      ctx.f1.f64 = double(float(std::clamp(scaled, kMinScaledFov, kMaxScaledFov)));
    }
    // PLAN 9.19: published for depth of field.
    g_shot.store(mode == kModeConversation || mode == kModeCutscene,
                 std::memory_order_relaxed);
    g_fov_radians.store(float(ctx.f1.f64), std::memory_order_relaxed);
  }
  __imp__tu6_sub_82683E60(ctx, base);
}
