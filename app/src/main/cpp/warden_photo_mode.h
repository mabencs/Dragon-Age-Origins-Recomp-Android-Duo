// PLAN 9.20: photo mode (warden_photo_mode.cc).
#ifndef WARDEN_PHOTO_MODE_H_
#define WARDEN_PHOTO_MODE_H_

#include <cstdint>

#include "ppc_recomp_shared.h"
#include "warden_input.h"

namespace warden::photo {

// On or off at the game's next frame, if it can be on there (exploring or fighting).
// Any thread.
void Toggle();

// The first pad as the game is about to see it (XamInputGetState): L3 and R3 together
// toggle photo mode; while it is on, the buttons it uses are its own, not the game's
// (and after it, until let go).
void FilterPad(PadState& state);

// The mode controller's update (warden_game.cc's hook), before and after the game's own.
void BeforeModeUpdate(PPCContext& ctx, uint8_t* base, uint32_t controller);
void AfterModeUpdate(PPCContext& ctx, uint8_t* base, uint32_t controller);

// For the app's overlay: {on (0/1), field of view (horizontal degrees at 16:9), roll
// (degrees), speed (times the fly camera's), hints shown (0/1), photos asked for so far}.
void State(float out[6]);

}  // namespace warden::photo

#endif  // WARDEN_PHOTO_MODE_H_
