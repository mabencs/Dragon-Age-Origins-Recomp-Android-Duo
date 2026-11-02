// The XAudio render driver and the sound output it feeds (warden_audio.cc).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace warden {

// Pauses the sound output while the app is in the background, and resumes it. The
// title's audio clock keeps running either way: the engine's state advances in its
// render callback, so the render worker falls back to the console's schedule.
void SetAudioOutputPaused(bool paused);

// The output devices that are headphones (Android's ids, AudioDeviceInfo.getId), from
// GameActivity's audio device callback: while the stream plays on one of them, and the
// setting asks for it, the output gets the headphone surround (PLAN 9.12).
void SetHeadphoneDevices(const std::vector<int32_t>& device_ids);

// Settings -> Headphones (PLAN 9.12) changed in the options panel (10.1): the headphone
// surround, or the speakers' mixdown, from the next frame.
void SetHeadphoneSurround(bool surround);

// Options -> Volume boost (the owner, 2026-11-02): "off", or the decibels the output is
// raised by ("6", "12", "18"), its peaks limited (warden_audio_mix.h's Boost). From the
// next frame.
void SetVolumeBoost(const std::string& value);

}  // namespace warden
