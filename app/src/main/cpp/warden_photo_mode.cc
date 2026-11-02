// PLAN 9.20: photo mode, ported from DAORecomp's ui/photo_mode.cpp (the owner picked it,
// 2026-11-01).
//
// The game's mode controller runs one game mode at a time; the 360 build still has the
// development fly camera as mode 6, which hides the HUD and moves the camera with the
// sticks. Photo mode asks the controller for it as the engine's SetGameModeMessage does
// (RequestGameMode(controller, 6, 0, 0)), from the controller's own update, pauses the
// world, and asks for the mode it left to come back.
//
// On top of the fly camera (left stick moves, right stick turns, LB and RB slow and
// speed it up): LT and RT lower and raise it, the D-pad zooms (up and down) and rolls
// (left and right), X changes the speed, Back puts the camera back where photo mode
// started, Y hides the hints, A takes a photo (the app saves the surface to Downloads)
// and B leaves. Those buttons, START included, are photo mode's while it is on: the game
// does not see them. L3 and R3 together, or the options panel's Photo mode row, toggle it.
//
// TU6 addresses, as upstream names them for Title Update 6 (this build's).
#include "warden_photo_mode.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>

#include "warden_camera_fov.h"
#include "xenia/base/logging.h"
#include "warden_game_library.h"

// The camera's SetFov through the field-of-view override, as the game's own call reaches
// it (warden_camera_fov.cc).
extern "C" PPC_FUNC(warden_override_tu6_sub_82683E60);
WARDEN_GAME_FUNC(__imp__tu6_sub_82348010);  // RequestGameMode
WARDEN_GAME_FUNC(__imp__tu6_sub_82285470);  // SetPaused(game, paused, by_user)
WARDEN_GAME_FUNC(__imp__tu6_sub_82370198);  // SetPosition(object, vector, f1 blend, 0)
WARDEN_GAME_FUNC(__imp__tu6_sub_823704B0);  // SetOrientation(object, quaternion)
WARDEN_GAME_FUNC(__imp__tu6_sub_824A48E8);  // the fly camera's update
WARDEN_GAME_FUNC(__imp__tu6_sub_826A09F8);  // the input map's analog actions

namespace warden::photo {
namespace {

constexpr int32_t kModeExplore = 0;
constexpr int32_t kModeCombat = 1;
constexpr int32_t kModeFlyCam = 6;
constexpr uint32_t kCurrentModeOffset = 120;
constexpr uint32_t kModeNumberOffset = 96;
// The game (a pointer): its Paused byte at +232.
constexpr uint32_t kGame = 0x832E20E8;
constexpr uint32_t kPausedOffset = 232;
constexpr uint32_t kMainCamera = 0x832E227C;
// The fly camera mode's camera object at +92: its position (x, y, z, w; z up) at +112,
// its orientation (a quaternion, +y ahead) at +144.
constexpr uint32_t kFlyCamObjectOffset = 92;
constexpr uint32_t kPositionOffset = 112;
constexpr uint32_t kOrientationOffset = 144;
// The fly camera's input actions: the left stick's two axes.
constexpr uint32_t kActionMoveX = 43;
constexpr uint32_t kActionMoveY = 44;
// Height from the triggers in metres a second: the fly camera's 6, 0.18 with LB, 132
// with RB.
constexpr double kHeightSpeed = 6.0, kHeightSpeedSlow = 0.18, kHeightSpeedFast = 132.0;
constexpr int kTriggerDeadZone = 30;
constexpr double kMaxStep = 0.1;
constexpr double kPi = 3.14159265358979323846;
// The speeds X steps through.
constexpr double kSpeeds[] = {0.25, 0.5, 1.0, 2.0, 4.0};
// The field of view's steps and range (horizontal degrees at 16:9), the roll's.
constexpr double kFovStep = 5.0, kFovMin = 20.0, kFovMax = 120.0;
constexpr double kRollStep = 5.0, kRollMax = 45.0;

constexpr uint16_t kPhotoButtons = kPadA | kPadB | kPadX | kPadY | kPadStart | kPadBack |
                                   kPadDpadUp | kPadDpadDown | kPadDpadLeft | kPadDpadRight |
                                   kPadLeftThumb | kPadRightThumb;

enum class Mode { kOff, kEntering, kOn, kLeaving };

std::atomic<int64_t> g_toggle_at{0};
std::atomic<bool> g_takes_buttons{false};
std::atomic<uint16_t> g_held_over{0};
std::atomic<bool> g_on{false};
std::atomic<double> g_fov{91.0};
std::atomic<double> g_roll{0.0};
std::atomic<int> g_speed_index{2};
std::atomic<bool> g_hints{true};
std::atomic<int> g_photos{0};
std::atomic<bool> g_reset{false};
std::atomic<uint8_t> g_left_trigger{0}, g_right_trigger{0};
std::atomic<uint16_t> g_buttons{0};
uint16_t g_previous_buttons = 0;  // the input thread's

// The mode controller's thread.
Mode g_mode = Mode::kOff;
int32_t g_return_mode = kModeExplore;
bool g_paused_here = false;
int64_t g_left_at = 0;
// The game thread's (the fly camera's update).
struct Pose {
  uint32_t position[4];
  uint32_t orientation[4];
};
std::optional<Pose> g_start;
double g_applied_roll = 0.0;
thread_local bool t_in_fly_cam = false;

int64_t Now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
             .count() |
         1;
}

int32_t CurrentMode(uint8_t* base, uint32_t controller) {
  const uint32_t current = PPC_LOAD_U32(controller + kCurrentModeOffset);
  return current ? int32_t(PPC_LOAD_U32(current + kModeNumberOffset)) : -1;
}

// A guest call from a hook: the context copied, the stack well below the hook's.
PPCContext CallContext(const PPCContext& ctx) {
  PPCContext call = ctx;
  call.r1.u64 = ctx.r1.u32 - 512;
  return call;
}

void RequestMode(PPCContext& ctx, uint8_t* base, uint32_t controller, int32_t mode) {
  PPCContext call = CallContext(ctx);
  call.r3.u64 = controller;
  call.r4.u64 = uint32_t(mode);
  call.r5.u64 = 0;
  call.r6.u64 = 0;
  __imp__tu6_sub_82348010(call, base);
}

void SetPaused(PPCContext& ctx, uint8_t* base, uint32_t game, bool paused) {
  PPCContext call = CallContext(ctx);
  call.r3.u64 = game;
  call.r4.u64 = paused ? 1 : 0;
  call.r5.u64 = 0;
  __imp__tu6_sub_82285470(call, base);
}

void Finish(PPCContext& ctx, uint8_t* base, uint32_t game) {
  if (g_paused_here && game && PPC_LOAD_U8(game + kPausedOffset)) {
    SetPaused(ctx, base, game, false);
  }
  g_paused_here = false;
  g_takes_buttons = false;
  g_on = false;
  SetPhotoFov(0.0f);
  g_mode = Mode::kOff;
}

double LoadFloat(uint8_t* base, uint32_t address) {
  const uint32_t bits = PPC_LOAD_U32(address);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint32_t FloatBits(double value) {
  const float f = float(value);
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  return bits;
}

// A vector for SetPosition or SetOrientation, 16-byte aligned, below the hook's stack.
uint32_t StoreVector(PPCContext& ctx, uint8_t* base, const uint32_t (&words)[4]) {
  const uint32_t vector = (ctx.r1.u32 - 32) & ~0xFu;
  for (uint32_t i = 0; i < 4; ++i) {
    PPC_STORE_U32(vector + i * 4, words[i]);
  }
  return vector;
}

void SetPosition(PPCContext& ctx, uint8_t* base, uint32_t object,
                 const uint32_t (&position)[4]) {
  PPCContext call = CallContext(ctx);
  call.r3.u64 = object;
  call.r4.u64 = StoreVector(ctx, base, position);
  call.r5.u64 = 0;
  call.f1.f64 = 0.0;  // at once, no blend
  __imp__tu6_sub_82370198(call, base);
}

void SetOrientation(PPCContext& ctx, uint8_t* base, uint32_t object,
                    const uint32_t (&orientation)[4]) {
  PPCContext call = CallContext(ctx);
  call.r3.u64 = object;
  call.r4.u64 = StoreVector(ctx, base, orientation);
  __imp__tu6_sub_823704B0(call, base);
}

// Vertical radians from horizontal degrees at 16:9.
double VerticalFov(double degrees) {
  return 2.0 * std::atan(std::tan(degrees * kPi / 360.0) * 9.0 / 16.0);
}

}  // namespace

void Toggle() { g_toggle_at.store(Now(), std::memory_order_relaxed); }

void FilterPad(PadState& state) {
  const uint16_t buttons = state.buttons;
  const uint16_t pressed = buttons & ~g_previous_buttons;
  g_previous_buttons = buttons;
  g_left_trigger.store(state.left_trigger, std::memory_order_relaxed);
  g_right_trigger.store(state.right_trigger, std::memory_order_relaxed);
  g_buttons.store(buttons, std::memory_order_relaxed);
  // L3 and R3 together: on or off.
  const uint16_t thumbs = kPadLeftThumb | kPadRightThumb;
  if ((buttons & thumbs) == thumbs && (pressed & thumbs)) {
    Toggle();
  }
  if (g_takes_buttons.load(std::memory_order_relaxed)) {
    if (g_on.load(std::memory_order_relaxed)) {
      if (pressed & kPadA) {
        g_photos.fetch_add(1, std::memory_order_relaxed);
      }
      if (pressed & (kPadB | kPadStart)) {
        Toggle();
      }
      if (pressed & kPadY) {
        g_hints.store(!g_hints.load());
      }
      if (pressed & kPadX) {
        g_speed_index.store((g_speed_index.load() + 1) % int(std::size(kSpeeds)));
      }
      if (pressed & kPadBack) {
        g_reset.store(true);
        g_roll.store(0.0);
      }
      if (pressed & (kPadDpadUp | kPadDpadDown)) {
        const double step = (pressed & kPadDpadDown) ? kFovStep : -kFovStep;
        g_fov.store(std::clamp(g_fov.load() + step, kFovMin, kFovMax));
      }
      if (pressed & (kPadDpadLeft | kPadDpadRight)) {
        const double step = (pressed & kPadDpadRight) ? kRollStep : -kRollStep;
        g_roll.store(std::clamp(g_roll.load() + step, -kRollMax, kRollMax));
      }
    }
    g_held_over.store(buttons & kPhotoButtons, std::memory_order_relaxed);
    state.buttons = buttons & ~kPhotoButtons;
    return;
  }
  // Each button still down from photo mode stays hidden until let go.
  const uint16_t held = g_held_over.fetch_and(buttons, std::memory_order_relaxed) & buttons;
  state.buttons = buttons & ~held;
}

void BeforeModeUpdate(PPCContext& ctx, uint8_t* base, uint32_t controller) {
  const uint32_t game = PPC_LOAD_U32(kGame);
  const int64_t toggle_at = g_toggle_at.load(std::memory_order_relaxed);
  if (!toggle_at || !game) {
    return;
  }
  g_toggle_at = 0;
  const int32_t mode = CurrentMode(base, controller);
  if (Now() - toggle_at > 1'000'000'000) {
    // Asked for long ago (the controller did not update meanwhile: loading).
    return;
  }
  if (g_mode == Mode::kOn || (g_mode == Mode::kLeaving && Now() - g_left_at > 500'000'000)) {
    XELOGI("Photo mode: off (back to mode {})", g_return_mode);
    RequestMode(ctx, base, controller, g_return_mode);
    g_mode = Mode::kLeaving;
    g_left_at = Now();
  } else if (g_mode == Mode::kOff && (mode == kModeExplore || mode == kModeCombat)) {
    g_return_mode = mode;
    g_paused_here = !PPC_LOAD_U8(game + kPausedOffset);
    XELOGI("Photo mode: on (from mode {}{})", mode, g_paused_here ? "" : ", already paused");
    g_fov.store(double(GameplayFovDegrees()));
    g_roll.store(0.0);
    g_speed_index.store(2);
    g_hints.store(true);
    RequestMode(ctx, base, controller, kModeFlyCam);
    g_takes_buttons = true;
    g_start.reset();
    g_applied_roll = 0.0;
    g_mode = Mode::kEntering;
  } else if (g_mode == Mode::kOff) {
    XELOGI("Photo mode: not available in mode {}", mode);
  }
}

void AfterModeUpdate(PPCContext& ctx, uint8_t* base, uint32_t controller) {
  const uint32_t game = PPC_LOAD_U32(kGame);
  const int32_t mode = CurrentMode(base, controller);
  switch (g_mode) {
    case Mode::kEntering:
      if (mode == kModeFlyCam) {
        g_mode = Mode::kOn;
        g_on = true;
        if (g_paused_here && game) {
          SetPaused(ctx, base, game, true);
        }
      } else {
        XELOGW("Photo mode: the game stayed in mode {}", mode);
        Finish(ctx, base, game);
      }
      break;
    case Mode::kOn:
    case Mode::kLeaving:
      if (mode != kModeFlyCam) {
        if (g_mode == Mode::kOn) {
          XELOGI("Photo mode: ended by the game (mode {})", mode);
        }
        Finish(ctx, base, game);
      }
      break;
    case Mode::kOff:
      break;
  }
}

void State(float out[6]) {
  out[0] = g_on.load() ? 1.0f : 0.0f;
  out[1] = float(g_fov.load());
  out[2] = float(g_roll.load());
  out[3] = float(kSpeeds[g_speed_index.load() % int(std::size(kSpeeds))]);
  out[4] = g_hints.load() ? 1.0f : 0.0f;
  out[5] = float(g_photos.load());
}

}  // namespace warden::photo

// The fly camera's update: r3 the mode, f1 the frame's time in seconds.
WARDEN_OVERRIDE(tu6_sub_824A48E8) {
  using namespace warden::photo;
  const uint32_t mode = ctx.r3.u32;
  const double seconds = std::clamp(ctx.f1.f64, 0.0, kMaxStep);
  const bool on = g_on.load(std::memory_order_relaxed);
  t_in_fly_cam = on;
  __imp__tu6_sub_824A48E8(ctx, base);
  t_in_fly_cam = false;
  const uint32_t object = on ? PPC_LOAD_U32(mode + kFlyCamObjectOffset) : 0;
  if (!object) {
    return;
  }
  if (!g_start) {
    Pose pose;
    for (uint32_t i = 0; i < 4; ++i) {
      pose.position[i] = PPC_LOAD_U32(object + kPositionOffset + i * 4);
      pose.orientation[i] = PPC_LOAD_U32(object + kOrientationOffset + i * 4);
    }
    g_start = pose;
  }
  if (g_reset.exchange(false)) {
    SetPosition(ctx, base, object, g_start->position);
    SetOrientation(ctx, base, object, g_start->orientation);
    g_applied_roll = 0.0;
  }
  // Up and down with the triggers.
  const auto amount = [](uint8_t trigger) {
    return trigger > kTriggerDeadZone ? (trigger - kTriggerDeadZone) / (255.0 - kTriggerDeadZone)
                                      : 0.0;
  };
  const double rise = amount(g_right_trigger.load()) - amount(g_left_trigger.load());
  const double speed = kSpeeds[g_speed_index.load() % int(std::size(kSpeeds))];
  if (rise != 0.0) {
    const uint16_t buttons = g_buttons.load();
    const double metres = (buttons & warden::kPadRightShoulder)  ? kHeightSpeedFast
                          : (buttons & warden::kPadLeftShoulder) ? kHeightSpeedSlow
                                                                 : kHeightSpeed;
    uint32_t position[4];
    for (uint32_t i = 0; i < 4; ++i) {
      position[i] = PPC_LOAD_U32(object + kPositionOffset + i * 4);
    }
    const double z = LoadFloat(base, object + kPositionOffset + 8) + rise * metres * speed * seconds;
    position[2] = FloatBits(z);
    SetPosition(ctx, base, object, position);
  }
  // Roll: about its own forward axis (+y), by what the setting changed since applied.
  const double roll = g_roll.load() - g_applied_roll;
  if (std::abs(roll) > 1e-4) {
    double q[4];
    for (uint32_t i = 0; i < 4; ++i) {
      q[i] = LoadFloat(base, object + kOrientationOffset + i * 4);
    }
    const double half = roll * kPi / 360.0;
    const double r[4] = {0.0, std::sin(half), 0.0, std::cos(half)};
    const double turned[4] = {q[3] * r[0] + q[0] * r[3] + q[1] * r[2] - q[2] * r[1],
                              q[3] * r[1] - q[0] * r[2] + q[1] * r[3] + q[2] * r[0],
                              q[3] * r[2] + q[0] * r[1] - q[1] * r[0] + q[2] * r[3],
                              q[3] * r[3] - q[0] * r[0] - q[1] * r[1] - q[2] * r[2]};
    const double norm = std::sqrt(turned[0] * turned[0] + turned[1] * turned[1] +
                                  turned[2] * turned[2] + turned[3] * turned[3]);
    uint32_t orientation[4];
    for (uint32_t i = 0; i < 4; ++i) {
      orientation[i] = FloatBits(turned[i] / norm);
    }
    SetOrientation(ctx, base, object, orientation);
    g_applied_roll = g_roll.load();
  }
  // The field of view: set here (the fly camera does not), and kept by the camera
  // update's SetFov in this mode (warden_camera_fov.cc).
  const double fov = VerticalFov(g_fov.load());
  warden::SetPhotoFov(float(fov));
  if (const uint32_t camera = PPC_LOAD_U32(kMainCamera)) {
    PPCContext call = CallContext(ctx);
    call.r3.u64 = camera;
    call.f1.f64 = double(float(fov));
    warden_override_tu6_sub_82683E60(call, base);
  }
}

// The input map's analog actions (r4 points to the action's number, the value in f1):
// the left stick scaled by the speed in the fly camera.
WARDEN_OVERRIDE(tu6_sub_826A09F8) {
  using namespace warden::photo;
  const uint32_t action = t_in_fly_cam ? PPC_LOAD_U32(ctx.r4.u32) : 0;
  __imp__tu6_sub_826A09F8(ctx, base);
  if (action == kActionMoveX || action == kActionMoveY) {
    ctx.f1.f64 *= kSpeeds[g_speed_index.load() % int(std::size(kSpeeds))];
  }
}
