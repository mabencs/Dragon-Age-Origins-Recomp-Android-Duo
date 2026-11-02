#include "warden_input.h"

#include <android/keycodes.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "xenia/base/logging.h"

namespace warden {

namespace {

// Android key code to 360 button bit.
//
// Android's key codes are named by *label*, not by position - KEYCODE_BUTTON_A is
// whichever button the driver's key layout calls "A". For the overwhelming majority of
// pads that is the Xbox arrangement (A bottom, B right, X left, Y top) because
// Android's generic HID gamepad layout maps usages that way, and a pad with Nintendo
// labels then reports its physically-bottom button as BUTTON_A regardless of what is
// printed on it.
//
// That is exactly what the glyph system needs and it is why this table is positional:
// bit kPadA always means "the bottom face button". Phase 7 decides what to draw there
// from the vendor and product ids, and anything that turns out to report differently
// gets a per-device quirk keyed on the descriptor - not a change here.
struct KeyMapping {
  int32_t android_key_code;
  uint16_t pad_button;
};

constexpr KeyMapping kKeyMap[] = {
    {AKEYCODE_BUTTON_A, kPadA},
    {AKEYCODE_BUTTON_B, kPadB},
    {AKEYCODE_BUTTON_X, kPadX},
    {AKEYCODE_BUTTON_Y, kPadY},
    {AKEYCODE_BUTTON_L1, kPadLeftShoulder},
    {AKEYCODE_BUTTON_R1, kPadRightShoulder},
    {AKEYCODE_BUTTON_THUMBL, kPadLeftThumb},
    {AKEYCODE_BUTTON_THUMBR, kPadRightThumb},
    {AKEYCODE_BUTTON_START, kPadStart},
    {AKEYCODE_BUTTON_SELECT, kPadBack},
    {AKEYCODE_BUTTON_MODE, kPadGuide},
    // Some pads deliver the d-pad as key events, others as the hat axes, and a few as
    // both. Handled in both places; setting a bit that is already set is harmless.
    {AKEYCODE_DPAD_UP, kPadDpadUp},
    {AKEYCODE_DPAD_DOWN, kPadDpadDown},
    {AKEYCODE_DPAD_LEFT, kPadDpadLeft},
    {AKEYCODE_DPAD_RIGHT, kPadDpadRight},
    // Android's own conventions for pads without a dedicated Start/Back: BUTTON_START
    // is often absent and MENU arrives instead. Mapped so such a pad can still pause.
    {AKEYCODE_MENU, kPadStart},
    {AKEYCODE_BACK, kPadBack},
    // L2/R2 as digital buttons, for pads with no analog triggers. The threshold value
    // is what the 360 considered "pressed", so a digital trigger reads as fully
    // pressed rather than as some invented midpoint.
    {AKEYCODE_BUTTON_L2, 0},
    {AKEYCODE_BUTTON_R2, 0},
};

// Android reports sticks as -1..1 and the 360 used int16. The Y axes are inverted
// between them: Android's Y grows downwards, the 360's thumb_ly grows upwards.
int16_t AxisToThumb(float value, bool invert) {
  float clamped = std::clamp(value, -1.0f, 1.0f);
  if (invert) {
    clamped = -clamped;
  }
  // Scaled by 32767 and not 32768, so that full deflection is exactly INT16_MAX in
  // both directions rather than overflowing at one end.
  return int16_t(std::lround(clamped * 32767.0f));
}

uint8_t TriggerToByte(float value) {
  return uint8_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

void AppendButtons(std::string& out, uint16_t buttons) {
  struct Named {
    uint16_t bit;
    const char* name;
  };
  static const Named kNames[] = {
      {kPadDpadUp, "up"},          {kPadDpadDown, "down"},
      {kPadDpadLeft, "left"},      {kPadDpadRight, "right"},
      {kPadStart, "start"},        {kPadBack, "back"},
      {kPadLeftThumb, "LS"},       {kPadRightThumb, "RS"},
      {kPadLeftShoulder, "LB"},    {kPadRightShoulder, "RB"},
      {kPadGuide, "guide"},        {kPadA, "A"},
      {kPadB, "B"},                {kPadX, "X"},
      {kPadY, "Y"},
  };
  bool first = true;
  for (const Named& named : kNames) {
    if (buttons & named.bit) {
      if (!first) {
        out += " ";
      }
      out += named.name;
      first = false;
    }
  }
  if (first) {
    out += "none";
  }
}

}  // namespace

Input& Input::Get() {
  static Input instance;
  return instance;
}

void Input::PadAttached(int slot, int32_t device_id, int32_t vendor_id,
                        int32_t product_id, std::string name, std::string descriptor,
                        bool has_analog_triggers, bool has_hat) {
  if (slot < 0 || size_t(slot) >= kMaxPads) {
    return;
  }
  PadInfo& info = infos_[size_t(slot)];
  info = PadInfo{};
  info.connected = true;
  info.device_id = device_id;
  info.vendor_id = vendor_id;
  info.product_id = product_id;
  info.name = std::move(name);
  info.descriptor = std::move(descriptor);
  info.has_analog_triggers = has_analog_triggers;
  info.has_hat = has_hat;
  states_[size_t(slot)] = PadState{};
  XELOGI("pad {} attached: {} (vendor 0x{:04X} product 0x{:04X})", slot, info.name,
         uint32_t(vendor_id), uint32_t(product_id));
}

void Input::PadDetached(int slot) {
  if (slot < 0 || size_t(slot) >= kMaxPads) {
    return;
  }
  // The state is cleared, not left at its last value: a disconnected controller must
  // not read as holding whatever it was holding when the cable came out.
  states_[size_t(slot)] = PadState{};
  infos_[size_t(slot)].connected = false;
  XELOGI("pad {} detached", slot);
}

bool Input::PadKey(int slot, int32_t android_key_code, bool down) {
  if (slot < 0 || size_t(slot) >= kMaxPads) {
    return false;
  }
  PadState& state = states_[size_t(slot)];
  PadInfo& info = infos_[size_t(slot)];

  for (const KeyMapping& mapping : kKeyMap) {
    if (mapping.android_key_code != android_key_code) {
      continue;
    }
    ++info.key_events;
    if (mapping.pad_button) {
      if (down) {
        state.buttons |= mapping.pad_button;
        info.buttons_ever |= mapping.pad_button;
      } else {
        state.buttons &= uint16_t(~mapping.pad_button);
      }
    } else if (android_key_code == AKEYCODE_BUTTON_L2) {
      state.left_trigger = down ? 255 : 0;
    } else if (android_key_code == AKEYCODE_BUTTON_R2) {
      state.right_trigger = down ? 255 : 0;
    }
    return true;
  }
  // Not a pad button. Returning false lets the system have it - on a handheld the
  // volume keys arrive as key events from the same device.
  return false;
}

void Input::PadAxes(int slot, float left_x, float left_y, float right_x, float right_y,
                    float left_trigger, float right_trigger, float hat_x, float hat_y) {
  if (slot < 0 || size_t(slot) >= kMaxPads) {
    return;
  }
  PadState& state = states_[size_t(slot)];
  PadInfo& info = infos_[size_t(slot)];
  ++info.axis_events;

  state.thumb_lx = AxisToThumb(left_x, false);
  state.thumb_ly = AxisToThumb(left_y, true);
  state.thumb_rx = AxisToThumb(right_x, false);
  state.thumb_ry = AxisToThumb(right_y, true);

  // Only overwrite the triggers from axes when the pad actually has analog ones;
  // otherwise the digital L2/R2 key events set them and a 0 here would cancel them
  // on the next stick movement.
  if (info.has_analog_triggers) {
    state.left_trigger = TriggerToByte(left_trigger);
    state.right_trigger = TriggerToByte(right_trigger);
  }

  // The hat is a d-pad reported as two axes at -1, 0 or 1. The threshold is 0.5 rather
  // than an exact comparison because some drivers report intermediate values while a
  // diagonal is being released.
  constexpr uint16_t kHatMask =
      kPadDpadUp | kPadDpadDown | kPadDpadLeft | kPadDpadRight;
  if (info.has_hat) {
    uint16_t hat_buttons = 0;
    if (hat_y <= -0.5f) {
      hat_buttons |= kPadDpadUp;
    } else if (hat_y >= 0.5f) {
      hat_buttons |= kPadDpadDown;
    }
    if (hat_x <= -0.5f) {
      hat_buttons |= kPadDpadLeft;
    } else if (hat_x >= 0.5f) {
      hat_buttons |= kPadDpadRight;
    }
    state.buttons = uint16_t((state.buttons & ~kHatMask) | hat_buttons);
    info.buttons_ever |= hat_buttons;
  }
}

std::string Input::SelfTest() {
  // Expected bit per key code, written out independently of kKeyMap rather than
  // derived from it - a test that reads the table it is testing proves only that the
  // table equals itself.
  struct Expectation {
    int32_t key_code;
    const char* label;
    uint16_t expected_button;
    uint8_t expected_left_trigger;
    uint8_t expected_right_trigger;
  };
  static const Expectation kExpected[] = {
      {AKEYCODE_BUTTON_A, "BUTTON_A -> A (bottom)", kPadA, 0, 0},
      {AKEYCODE_BUTTON_B, "BUTTON_B -> B (right)", kPadB, 0, 0},
      {AKEYCODE_BUTTON_X, "BUTTON_X -> X (left)", kPadX, 0, 0},
      {AKEYCODE_BUTTON_Y, "BUTTON_Y -> Y (top)", kPadY, 0, 0},
      {AKEYCODE_BUTTON_L1, "BUTTON_L1 -> LB", kPadLeftShoulder, 0, 0},
      {AKEYCODE_BUTTON_R1, "BUTTON_R1 -> RB", kPadRightShoulder, 0, 0},
      {AKEYCODE_BUTTON_THUMBL, "BUTTON_THUMBL -> LS", kPadLeftThumb, 0, 0},
      {AKEYCODE_BUTTON_THUMBR, "BUTTON_THUMBR -> RS", kPadRightThumb, 0, 0},
      {AKEYCODE_BUTTON_START, "BUTTON_START -> start", kPadStart, 0, 0},
      {AKEYCODE_BUTTON_SELECT, "BUTTON_SELECT -> back", kPadBack, 0, 0},
      {AKEYCODE_BUTTON_MODE, "BUTTON_MODE -> guide", kPadGuide, 0, 0},
      {AKEYCODE_DPAD_UP, "DPAD_UP -> up", kPadDpadUp, 0, 0},
      {AKEYCODE_DPAD_DOWN, "DPAD_DOWN -> down", kPadDpadDown, 0, 0},
      {AKEYCODE_DPAD_LEFT, "DPAD_LEFT -> left", kPadDpadLeft, 0, 0},
      {AKEYCODE_DPAD_RIGHT, "DPAD_RIGHT -> right", kPadDpadRight, 0, 0},
      {AKEYCODE_MENU, "MENU -> start", kPadStart, 0, 0},
      {AKEYCODE_BACK, "BACK -> back", kPadBack, 0, 0},
      {AKEYCODE_BUTTON_L2, "BUTTON_L2 -> left trigger 255", 0, 255, 0},
      {AKEYCODE_BUTTON_R2, "BUTTON_R2 -> right trigger 255", 0, 0, 255},
  };

  Input test;
  test.PadAttached(0, 1, 0, 0, "self-test", "self-test", false, true);

  std::string out = "INPUT MAPPING SELF-TEST\n";
  char line[160];
  size_t failures = 0;
  for (const Expectation& expectation : kExpected) {
    test.states_[0] = PadState{};
    const bool consumed = test.PadKey(0, expectation.key_code, true);
    const PadState& state = test.states_[0];
    const bool ok = consumed && state.buttons == expectation.expected_button &&
                    state.left_trigger == expectation.expected_left_trigger &&
                    state.right_trigger == expectation.expected_right_trigger;
    if (!ok) {
      ++failures;
      std::snprintf(line, sizeof(line),
                    "  FAIL %-34s consumed=%d buttons=0x%04X lt=%u rt=%u\n",
                    expectation.label, int(consumed), state.buttons,
                    state.left_trigger, state.right_trigger);
      out += line;
    }
    // Release must clear it again. A button that latches on is worse than one that
    // never registers, because the game would act as though it were held forever.
    test.PadKey(0, expectation.key_code, false);
    if (expectation.expected_button && test.states_[0].buttons != 0) {
      ++failures;
      std::snprintf(line, sizeof(line), "  FAIL %-34s did not clear on release\n",
                    expectation.label);
      out += line;
    }
  }

  // An unmapped code must be rejected, not swallowed: on a handheld the volume keys
  // arrive as key events from the same device, and consuming them would make them
  // dead while the game runs.
  if (test.PadKey(0, AKEYCODE_VOLUME_UP, true)) {
    ++failures;
    out += "  FAIL VOLUME_UP was consumed as a pad button\n";
  }

  const size_t checks = sizeof(kExpected) / sizeof(kExpected[0]) + 1;
  std::snprintf(line, sizeof(line), "  %-34s %zu/%zu checks pass\n",
                failures ? "RESULT: FAILURES" : "RESULT: ok",
                checks - failures, checks);
  out += line;
  if (failures) {
    XELOGE("input mapping self-test: {} failures", failures);
  } else {
    XELOGI("input mapping self-test: {} checks pass", checks);
  }
  return out;
}

std::string Input::Report() const {
  // Leads with the self-test, so the capture records whether the table is right
  // alongside what hardware was found.
  std::string out = SelfTest();
  out += "CONTROLLERS\n";
  char line[512];
  bool any = false;
  for (size_t slot = 0; slot < kMaxPads; ++slot) {
    const PadInfo& info = infos_[slot];
    if (!info.connected) {
      continue;
    }
    any = true;
    std::snprintf(line, sizeof(line),
                  "  slot %zu  vendor 0x%04X  product 0x%04X  %s\n", slot,
                  uint32_t(info.vendor_id), uint32_t(info.product_id),
                  info.name.c_str());
    out += line;
    std::snprintf(line, sizeof(line), "    %-30s %s\n", "descriptor",
                  info.descriptor.c_str());
    out += line;
    std::snprintf(line, sizeof(line), "    %-30s %s\n", "analog triggers",
                  info.has_analog_triggers ? "yes" : "no");
    out += line;
    std::snprintf(line, sizeof(line), "    %-30s %s\n", "hat axes",
                  info.has_hat ? "yes" : "no");
    out += line;
    std::snprintf(line, sizeof(line), "    %-30s %u key, %u axis\n", "events seen",
                  info.key_events, info.axis_events);
    out += line;

    const PadState& state = states_[slot];
    out += "    held                           ";
    AppendButtons(out, state.buttons);
    out += "\n";
    out += "    pressed since attach           ";
    AppendButtons(out, info.buttons_ever);
    out += "\n";
    std::snprintf(line, sizeof(line),
                  "    %-30s L %d,%d  R %d,%d  LT %u  RT %u\n", "axes",
                  state.thumb_lx, state.thumb_ly, state.thumb_rx, state.thumb_ry,
                  state.left_trigger, state.right_trigger);
    out += line;
  }
  if (!any) {
    out += "  no gamepads seen\n";
  }
  return out;
}

// ---- PLAN 10.17: the on-screen controller ----

namespace {

struct TouchPad {
  std::mutex mutex;
  bool active = false;
  PadState state;
};

TouchPad& Touch() {
  static TouchPad* touch = new TouchPad();
  return *touch;
}

int16_t StickValue(float value) {
  return int16_t(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
}

uint8_t TriggerValue(float value) {
  return uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

}  // namespace

void SetTouchPad(bool active, uint16_t buttons, float left_x, float left_y, float right_x,
                 float right_y, float left_trigger, float right_trigger) {
  TouchPad& touch = Touch();
  std::lock_guard<std::mutex> lock(touch.mutex);
  if (active != touch.active) {
    XELOGI("pad: the on-screen controller {}", active ? "shown" : "hidden");
  }
  touch.active = active;
  touch.state.buttons = active ? buttons : 0;
  touch.state.thumb_lx = active ? StickValue(left_x) : 0;
  touch.state.thumb_ly = active ? StickValue(left_y) : 0;
  touch.state.thumb_rx = active ? StickValue(right_x) : 0;
  touch.state.thumb_ry = active ? StickValue(right_y) : 0;
  touch.state.left_trigger = active ? TriggerValue(left_trigger) : 0;
  touch.state.right_trigger = active ? TriggerValue(right_trigger) : 0;
}

bool ApplyTouchPad(PadState& state) {
  TouchPad& touch = Touch();
  std::lock_guard<std::mutex> lock(touch.mutex);
  if (!touch.active) {
    return false;
  }
  const PadState& t = touch.state;
  state.buttons |= t.buttons;
  if (t.thumb_lx || t.thumb_ly) {
    state.thumb_lx = t.thumb_lx;
    state.thumb_ly = t.thumb_ly;
  }
  if (t.thumb_rx || t.thumb_ry) {
    state.thumb_rx = t.thumb_rx;
    state.thumb_ry = t.thumb_ry;
  }
  state.left_trigger = std::max(state.left_trigger, t.left_trigger);
  state.right_trigger = std::max(state.right_trigger, t.right_trigger);
  return true;
}

}  // namespace warden
