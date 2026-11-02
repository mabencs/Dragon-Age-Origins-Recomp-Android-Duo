// Android gamepads to Xbox 360 gamepad state. Task 4.6.
//
// The recompiled game reads X_INPUT_GAMEPAD, so that is what this produces - the same
// 12-byte layout and the same button bits as xenia/hid/input.h, with the deadzones and
// the trigger threshold the 360 used. Byte order is deliberately *not* handled here:
// the guest is big-endian and the conversion belongs at the kernel shim that hands the
// struct to guest code (Phase 5), not spread through the input layer.
//
// What this layer does not decide: which glyph to draw. Mapping an Android key code to
// a 360 button bit is positional and fixed; choosing whether that bit should be drawn
// as "A" or "B" depends on the controller the player is holding, which is Phase 7's
// job. The vendor and product ids it will need are recorded here.
#ifndef WARDEN_INPUT_H_
#define WARDEN_INPUT_H_

#include <array>
#include <cstdint>
#include <string>

namespace warden {

// From xenia/hid/input.h. Repeated rather than included because xenia/hid is not part
// of the grafted tree - the graft is the GPU backend. If these ever disagree with
// upstream, these are wrong.
enum : uint16_t {
  kPadDpadUp = 0x0001,
  kPadDpadDown = 0x0002,
  kPadDpadLeft = 0x0004,
  kPadDpadRight = 0x0008,
  kPadStart = 0x0010,
  kPadBack = 0x0020,
  kPadLeftThumb = 0x0040,
  kPadRightThumb = 0x0080,
  kPadLeftShoulder = 0x0100,
  kPadRightShoulder = 0x0200,
  kPadGuide = 0x0400,
  kPadA = 0x1000,
  kPadB = 0x2000,
  kPadX = 0x4000,
  kPadY = 0x8000,
};

// Also from xenia/hid/input.h: the console's own thresholds, not invented ones.
constexpr int32_t kLeftThumbDeadzone = 7849;
constexpr int32_t kRightThumbDeadzone = 8689;
constexpr uint8_t kTriggerThreshold = 30;

// The 360 gamepad state, host-endian.
struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
};

struct PadInfo {
  bool connected = false;
  int32_t device_id = -1;
  int32_t vendor_id = 0;
  int32_t product_id = 0;
  std::string name;
  // Stable across reconnection, unlike device_id - what per-controller settings
  // should be keyed on.
  std::string descriptor;
  bool has_analog_triggers = false;
  bool has_hat = false;
  // Counted so the report can show that events are arriving even when nothing is
  // held at the moment it is read.
  uint32_t key_events = 0;
  uint32_t axis_events = 0;
  // The buttons pressed at any point since attach. With no hands on the device this
  // is the only way an automated check can show the mapping works.
  uint16_t buttons_ever = 0;
};

class Input {
 public:
  static constexpr size_t kMaxPads = 4;

  static Input& Get();

  void PadAttached(int slot, int32_t device_id, int32_t vendor_id, int32_t product_id,
                   std::string name, std::string descriptor, bool has_analog_triggers,
                   bool has_hat);
  void PadDetached(int slot);

  // Returns false if the key code is not one the 360 pad has, so that Java can let
  // the system handle it - the volume keys, for instance, arrive as key events from
  // the same device on handhelds.
  bool PadKey(int slot, int32_t android_key_code, bool down);
  void PadAxes(int slot, float left_x, float left_y, float right_x, float right_y,
               float left_trigger, float right_trigger, float hat_x, float hat_y);

  const PadState& state(size_t slot) const { return states_[slot]; }
  // Whether a pad is attached in the slot - what XamInputGetState answers with
  // ERROR_DEVICE_NOT_CONNECTED when it is not.
  bool connected(size_t slot) const { return slot < kMaxPads && infos_[slot].connected; }
  std::string Report() const;

  // Drives every mapped Android key code through the table and checks the bit it
  // produces, on a private instance so nothing real is disturbed.
  //
  // Worth having because the mapping table is the part most likely to be wrong and
  // the hardest part to test by hand: verifying it on the device means pressing
  // nineteen buttons and reading an overlay, and a transposed pair of bits - A and B,
  // say - would look like a working controller right up until someone tried to
  // confirm a dialogue.
  static std::string SelfTest();

 private:
  Input() = default;

  std::array<PadState, kMaxPads> states_{};
  std::array<PadInfo, kMaxPads> infos_{};
};

// PLAN 10.17, the on-screen controller (TouchControls.java): its state in the 360's
// terms - the button bits above, the sticks -1..1 with up positive, the triggers 0..1.
// Inactive, it is ignored.
void SetTouchPad(bool active, uint16_t buttons, float left_x, float left_y, float right_x,
                 float right_y, float left_trigger, float right_trigger);
// Merged into the first pad as the game polls it: the buttons OR-ed in, a stick the
// touch is moving taking the place of the pad's, the triggers the larger. Returns true
// while the on-screen controller is shown (the first pad then counts as connected).
bool ApplyTouchPad(PadState& state);

}  // namespace warden

#endif  // WARDEN_INPUT_H_
