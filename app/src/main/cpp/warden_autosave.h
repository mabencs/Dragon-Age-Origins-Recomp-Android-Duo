// PLAN 9.13: an autosave as the game is put away. warden_game.cc asks the game for it on
// its update thread, warden_xam.cc sees an autosave written, and Runtime::SetPaused holds
// the guest only after it (or after a few seconds) - Android freezes a put-away app some
// seconds after it is cached, and a save takes 0.6 s (runs 374-375).

#pragma once

#include <cstdint>

namespace warden {

// Whether an autosave is asked for as the game is put away: files/autosave-on-pause
// holding "on" (off without it, the owner's default), or the options panel's choice.
bool AutosaveOnPauseEnabled();
void SetAutosaveOnPause(bool on);

// Asks for that autosave on the update thread's next frame. Only while exploring - the
// game's own autosaves never come in combat, conversations, cutscenes or loads.
// Returns false, asking nothing, when switched off or not exploring.
// forced (PLAN 11.1, a bug report's fresh save): whatever the option says.
bool RequestAutosave(bool forced = false);

// Autosave containers (`<character>_N_a!0`) written and closed, ever (warden_xam.cc).
uint64_t AutosavesWritten();

}  // namespace warden
