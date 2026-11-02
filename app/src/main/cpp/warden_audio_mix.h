// The title's six channels made into the two the Thor plays (PLAN 9.12): a mixdown for
// its speakers, and for headphones the five speakers the title mixes for, placed around
// the listener (warden_audio_mix.cc). Plain C++ without Android, so that
// tools/audio_host.cc runs the same code over a recording on the computer
// (tools/audio_levels.py --render).

#pragma once

#include <cstdint>

namespace warden::audio {

// XAudio's order: front left, front right, centre, low frequency, back left, back right.
constexpr uint32_t kTitleChannels = 6;

class Mixer {
 public:
  Mixer();

  // `channels` holds kTitleChannels runs of `samples` floats (channel-major, as the
  // title submits them); `stereo` receives `samples` left-right pairs.
  void Speakers(const float* channels, uint32_t samples, float* stereo);
  void Headphones(const float* channels, uint32_t samples, float* stereo);

  // Forgets the headphone path's delay lines and filter memories - for a route change,
  // so that nothing from before it is heard after.
  void Reset();

 private:
  static constexpr uint32_t kSources = 5;   // the five speakers; the LFE is not one
  static constexpr uint32_t kHistory = 64;  // a power of two above the longest delay

  struct Path {      // one source to one ear
    uint32_t delay;  // samples: the far ear hears the source later
    float b0, b1, a1;  // the head's shadow (or the near ear's gain), a first-order shelf
    float x1, y1;      // its memory
  };
  struct Source {
    uint32_t channel;  // in the title's order
    bool rear;
    float r_b0, r_b1, r_a1, r_x1, r_y1;  // behind the head: a further high shelf
    float history[kHistory];
    Path ear[2];  // left, right
  };

  Source sources_[kSources];
  uint32_t write_ = 0;
};

// Options -> Volume boost (the owner, 2026-11-02: "very low even with maxed device
// volume" - the mix leaves the headroom combat's peaks need, so exploring peaks near
// -20 dBFS): the stereo made louder by `gain`, and a lookahead limiter holding every
// sample under a ceiling just below full scale - the quiet and middle of the game only
// louder, the loudest peaks turned down for the moment they last (1.3 ms ahead, a
// 200 ms release). Off, nothing touches the samples.
class Boost {
 public:
  Boost();

  // `stereo`: `samples` left-right pairs, in place. `gain` linear, 1 = off.
  void Process(float* stereo, uint32_t samples, float gain);

  // Forgets the limiter's memory (off, or a route change).
  void Reset();

 private:
  static constexpr uint32_t kLook = 64;  // samples ahead: 1.3 ms at 48 kHz

  float release_ = 1.0f;     // the gain the limiter was recovering to
  float needed_[kLook];      // each sample's gain, released
  float smoothed_[kLook];    // the window's least, for the moving average
  double sum_ = kLook;       // the moving average's sum
  float delay_[2 * kLook];   // the samples, kLook - 1 behind
  uint32_t at_ = 0;
  bool active_ = false;
};

}  // namespace warden::audio
