// The options panel's changes while the game runs (PLAN 10.1): the setters with no
// header of their own. Runtime::ApplyOption (warden_runtime.h) calls them; the files they
// mirror stay what the game reads as it starts.

#pragma once

namespace warden {

// warden_game.cc (PLAN 9.8): the engine's dynamic shadows and post-processing, from the
// next frame; switched back on, the engine's own option again.
void SetDynamicShadows(bool on);
void SetPostProcessing(bool on);

// warden_upscale.cc (PLAN 9.21): the game's corner vignette in play, drawn or skipped, from
// the next frame.
void SetVignette(bool on);

}  // namespace warden
