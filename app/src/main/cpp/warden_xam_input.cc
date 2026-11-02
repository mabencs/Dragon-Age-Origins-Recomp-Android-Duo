// The title's controller queries, answered from the pad state the Java side keeps
// (ControllerManager -> warden_input.cc, task 4.6).
//
// XamInputGetState is the title's whole view of the controller: it polls it every
// frame and compares the packet number to see whether anything changed. The stub's
// ERROR_FUNCTION_FAILED said "no controller, and something is wrong"; the handheld's
// built-in pad is attached from the first frame, so the honest answer is its state.
// An empty slot gets ERROR_DEVICE_NOT_CONNECTED, which is what the console says.
//
// Layouts are XInput's, big-endian (Xenia's hid/input.h): X_INPUT_STATE is a packet
// number then X_INPUT_GAMEPAD - buttons u16, two trigger bytes, four s16 thumb axes -
// 16 bytes; X_INPUT_CAPABILITIES is type, sub-type, flags u16, a gamepad of the
// supported bits and a vibration pair, 20 bytes.

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "ppc_recomp_shared.h"
#include "warden_deck.h"
#include "warden_input.h"
#include "warden_photo_mode.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"

namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorDeviceNotConnected = 0x48F;
constexpr uint32_t kUserIndexAny = 0xFF;
constexpr uint32_t kInputFlagAnyUser = 0x40000000;
constexpr uint8_t kDevTypeGamepad = 1;
constexpr uint8_t kDevSubTypeGamepad = 1;

// The packet number advances when the state changes, as XInput's does: a title that
// compares it sees a change exactly when there is one.
struct SlotPacket {
  std::mutex mutex;
  warden::PadState last;
  uint32_t number = 0;
  bool connected_logged = false;
};
std::array<SlotPacket, warden::Input::kMaxPads> g_packets;

bool SameState(const warden::PadState& a, const warden::PadState& b) {
  return a.buttons == b.buttons && a.left_trigger == b.left_trigger &&
         a.right_trigger == b.right_trigger && a.thumb_lx == b.thumb_lx &&
         a.thumb_ly == b.thumb_ly && a.thumb_rx == b.thumb_rx && a.thumb_ry == b.thumb_ry;
}

// Which pad a user index and flags name. XUSER_INDEX_ANY and the any-user flag mean
// the first one; there is one pad on this device anyway.
uint32_t SlotFor(uint32_t user_index, uint32_t flags) {
  if (user_index == kUserIndexAny || (flags & kInputFlagAnyUser)) {
    return 0;
  }
  return user_index;
}

// A pad press from a file, for the device loop without hands on the device:
// `files/inject-pad` holds `<buttons in hex> [<delay s> [<hold s> [<lt> <rt> [<lx> <ly>
// [<rx> <ry>]]]]]` - the X_INPUT button mask (START 0010, BACK 0020, A 1000, B 2000,
// X 4000, Y 8000, the d-pad 0001/0002/0004/0008, the shoulders 0100/0200) pressed in
// slot 0 from `delay` seconds after the file is first seen, for `hold` seconds (default
// 1); then, optionally, the triggers (0-255) and the sticks (-32768..32767, up and right
// positive), held the same while. While the file exists slot 0 counts as a connected
// pad; a real pad's buttons are OR-ed with the injected ones, its triggers take the
// larger value, and an injected stick that is not centred replaces the real one.
// Rewriting the file (a new modification time) presses again. Triggers and sticks came
// in session 32: the King's Camp's forced tutorials want LT, and combat the sticks.
struct Press {
  uint16_t buttons = 0;
  double delay = 0;
  double hold = 1;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
};
struct Injection {
  std::mutex mutex;
  std::chrono::steady_clock::time_point checked;
  std::chrono::steady_clock::time_point seen;
  time_t mtime = 0;
  bool present = false;
  std::array<Press, 16> presses;  // one per line of the file, in order
  size_t count = 0;
};
Injection g_injection;

bool InjectedState(warden::PadState& state, bool& connected) {
  const char* dir = std::getenv("WARDEN_FILES_DIR");
  if (!dir) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(g_injection.mutex);
  if (now - g_injection.checked > std::chrono::milliseconds(250)) {
    g_injection.checked = now;
    struct stat info;
    const std::string path = std::string(dir) + "/inject-pad";
    if (stat(path.c_str(), &info) != 0) {
      g_injection.present = false;
    } else if (!g_injection.present || info.st_mtime != g_injection.mtime) {
      g_injection.present = true;
      g_injection.mtime = info.st_mtime;
      g_injection.seen = now;
      g_injection.count = 0;
      if (FILE* file = fopen(path.c_str(), "r")) {
        char line[128];
        while (g_injection.count < g_injection.presses.size() && fgets(line, sizeof(line), file)) {
          Press press;
          unsigned buttons = 0;
          int lt = 0, rt = 0, lx = 0, ly = 0, rx = 0, ry = 0;
          if (sscanf(line, "%x %lf %lf %d %d %d %d %d %d", &buttons, &press.delay, &press.hold,
                     &lt, &rt, &lx, &ly, &rx, &ry) >= 1) {
            press.buttons = uint16_t(buttons);
            press.left_trigger = uint8_t(std::clamp(lt, 0, 255));
            press.right_trigger = uint8_t(std::clamp(rt, 0, 255));
            press.thumb_lx = int16_t(std::clamp(lx, -32768, 32767));
            press.thumb_ly = int16_t(std::clamp(ly, -32768, 32767));
            press.thumb_rx = int16_t(std::clamp(rx, -32768, 32767));
            press.thumb_ry = int16_t(std::clamp(ry, -32768, 32767));
            g_injection.presses[g_injection.count++] = press;
          }
        }
        fclose(file);
      }
      std::string plan;
      for (size_t i = 0; i < g_injection.count; ++i) {
        const Press& press = g_injection.presses[i];
        plan += fmt::format("{}{:04X} at {} s for {} s", i ? ", " : "", press.buttons, press.delay,
                            press.hold);
        if (press.left_trigger || press.right_trigger || press.thumb_lx || press.thumb_ly ||
            press.thumb_rx || press.thumb_ry) {
          plan += fmt::format(" (triggers {} {}, sticks {} {} {} {})", press.left_trigger,
                              press.right_trigger, press.thumb_lx, press.thumb_ly,
                              press.thumb_rx, press.thumb_ry);
        }
      }
      XELOGI("inject-pad: {} presses: {}", g_injection.count, plan);
    }
  }
  if (!g_injection.present) {
    return false;
  }
  connected = true;
  const double since = std::chrono::duration<double>(now - g_injection.seen).count();
  for (size_t i = 0; i < g_injection.count; ++i) {
    const Press& press = g_injection.presses[i];
    if (since >= press.delay && since < press.delay + press.hold) {
      state.buttons |= press.buttons;
      state.left_trigger = std::max(state.left_trigger, press.left_trigger);
      state.right_trigger = std::max(state.right_trigger, press.right_trigger);
      if (press.thumb_lx || press.thumb_ly) {
        state.thumb_lx = press.thumb_lx;
        state.thumb_ly = press.thumb_ly;
      }
      if (press.thumb_rx || press.thumb_ry) {
        state.thumb_rx = press.thumb_rx;
        state.thumb_ry = press.thumb_ry;
      }
    }
  }
  return true;
}

void WriteGamepad(uint8_t* base, uint32_t address, const warden::PadState& state) {
  (void)base;
  PPC_STORE_U16(address + 0, state.buttons);
  PPC_STORE_U8(address + 2, state.left_trigger);
  PPC_STORE_U8(address + 3, state.right_trigger);
  PPC_STORE_U16(address + 4, uint16_t(state.thumb_lx));
  PPC_STORE_U16(address + 6, uint16_t(state.thumb_ly));
  PPC_STORE_U16(address + 8, uint16_t(state.thumb_rx));
  PPC_STORE_U16(address + 10, uint16_t(state.thumb_ry));
}

}  // namespace

// DWORD XamInputGetState(DWORD user_index, DWORD flags, X_INPUT_STATE* state)
PPC_FUNC(__imp__XamInputGetState) {
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  const uint32_t state_address = ctx.r5.u32;
  if (state_address) {
    std::memset(base + state_address, 0, 16);
  }
  const uint32_t slot = SlotFor(user_index, flags);
  warden::Input& input = warden::Input::Get();
  bool connected = slot < warden::Input::kMaxPads && input.connected(slot);
  warden::PadState state;
  if (connected) {
    state = input.state(slot);
  }
  if (slot == 0) {
    InjectedState(state, connected);
    // PLAN 8.8: a tap on the deck's quickbar, as the slot's own button press.
    if (warden::deck::ApplyQuickslot(state.buttons, state.right_trigger)) {
      connected = true;
    }
    // PLAN 10.17: the on-screen controller, merged into the first pad.
    if (warden::ApplyTouchPad(state)) {
      connected = true;
    }
    // PLAN 9.20: L3 and R3 toggle photo mode; its buttons are its own while it is on.
    warden::photo::FilterPad(state);
  }
  if (!connected) {
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 2) {
      XELOGI("[{}] XamInputGetState(user {}, flags {:08X}) -> no controller in slot {}",
             warden::CurrentGuestThreadName(), user_index, flags, slot);
    }
    ctx.r3.u64 = kErrorDeviceNotConnected;
    return;
  }
  SlotPacket& packet = g_packets[slot];
  uint32_t number;
  bool first;
  {
    std::lock_guard<std::mutex> lock(packet.mutex);
    if (!packet.connected_logged || !SameState(state, packet.last)) {
      ++packet.number;
      packet.last = state;
    }
    first = !packet.connected_logged;
    packet.connected_logged = true;
    number = packet.number;
  }
  if (first) {
    XELOGI("[{}] XamInputGetState(user {}, flags {:08X}) -> the pad in slot {}, polled "
           "from here on", warden::CurrentGuestThreadName(), user_index, flags, slot);
  }
  if (state_address) {
    PPC_STORE_U32(state_address, number);
    WriteGamepad(base, state_address + 4, state);
  }
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamInputGetCapabilities(DWORD user_index, DWORD flags, X_INPUT_CAPABILITIES* caps)
PPC_FUNC(__imp__XamInputGetCapabilities) {
  const uint32_t user_index = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  const uint32_t caps = ctx.r5.u32;
  if (caps) {
    std::memset(base + caps, 0, 20);
  }
  const uint32_t slot = SlotFor(user_index, flags);
  bool connected = slot < warden::Input::kMaxPads && warden::Input::Get().connected(slot);
  if (slot == 0) {
    warden::PadState ignored;
    InjectedState(ignored, connected);
    // PLAN 10.17: the on-screen controller is a pad in the first slot.
    if (warden::ApplyTouchPad(ignored)) {
      connected = true;
    }
  }
  if (!connected) {
    ctx.r3.u64 = kErrorDeviceNotConnected;
    return;
  }
  if (caps) {
    PPC_STORE_U8(caps + 0, kDevTypeGamepad);
    PPC_STORE_U8(caps + 1, kDevSubTypeGamepad);
    PPC_STORE_U16(caps + 2, 0);  // no voice, no wireless
    // The gamepad fields say which inputs exist: all of them.
    warden::PadState everything;
    everything.buttons = 0xFFFF;
    everything.left_trigger = 0xFF;
    everything.right_trigger = 0xFF;
    everything.thumb_lx = everything.thumb_ly = -1;
    everything.thumb_rx = everything.thumb_ry = -1;
    WriteGamepad(base, caps + 4, everything);
    PPC_STORE_U16(caps + 16, 0xFFFF);  // vibration: both motors
    PPC_STORE_U16(caps + 18, 0xFFFF);
  }
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 2) {
    XELOGI("[{}] XamInputGetCapabilities(user {}, flags {:08X}) -> a gamepad in slot {}",
           warden::CurrentGuestThreadName(), user_index, flags, slot);
  }
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XamInputSetState(DWORD user_index, DWORD flags, X_INPUT_VIBRATION* vibration)
//
// Accepted and dropped: rumble is a later task (the handheld has a vibrator; nothing
// drives it yet). Success, because a pad is there.
PPC_FUNC(__imp__XamInputSetState) {
  (void)base;
  const uint32_t slot = SlotFor(ctx.r3.u32, ctx.r4.u32);
  if (slot >= warden::Input::kMaxPads || !warden::Input::Get().connected(slot)) {
    ctx.r3.u64 = kErrorDeviceNotConnected;
    return;
  }
  ctx.r3.u64 = kErrorSuccess;
}
