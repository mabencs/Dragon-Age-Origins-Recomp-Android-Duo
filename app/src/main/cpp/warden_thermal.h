// PLAN 10.2: the Thor's temperatures and the GPU's thermal limit, as the game's own process
// reads them, and the heat guard. See warden_thermal.cc.
#pragma once

#include <string>

namespace warden::thermal {

// Every 5 s, from the GPU status line: reads the sensors, steps the heat guard when it is
// on, and returns the line's text - the hottest GPU sensor, the hottest big core, the
// board, and the GPU's clock against its thermal cap (or what could not be read).
std::string Tick();

// The heat guard (Options -> Heat guard, files/thermal-guard "on"; off by default - the
// owner, 2026-10-31): the frame rate held a step lower while the GPU is hot, and at 30
// while Android's battery saver is on.
void SetGuard(bool on);
// Android's battery saver, from GameActivity (it matters only with the guard on).
void SetPowerSave(bool on);

// BUG-117, Heat guard's other options (files/heat-cpu-rest, files/heat-gpu-wait: "on"),
// for any thread without a lock: the game's Sleep(0) spins rest (warden_threads.cc), the
// render thread's GPU fence wait sleeps (warden_game.cc). Read at the start, set live.
bool CpuRest();
bool GpuWait();
void SetCpuRest(bool on);
void SetGpuWait(bool on);

}  // namespace warden::thermal
