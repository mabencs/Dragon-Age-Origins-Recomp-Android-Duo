// The title's six channels made into the two the Thor plays (PLAN 9.12).
//
// What the title puts where (run 367's recording, tools/audio_levels.py): the intro
// movies in the front pair alone; the game in all six - the back pair largely the front
// pair again, 7 dB down (correlation 0.83), the centre in use, the low-frequency channel
// hardly (-43.5 dBFS against the front's -20). Loud: combat peaks past full scale, and
// the menus sit 25 dB below it.
//
// The speakers: ITU-R BS.775's mixdown - each side its front channel, the centre and its
// back channel at -3 dB, the low frequencies left out - scaled by 1 / 2.414 so that
// nothing the title submits clips (run 367's combat peaks at -2.4 dBFS through it). The
// level is the old mixdown's (Xenia's: the centre at -6 dB, the back at 0 dB, / 2.5),
// -17.7 LUFS against -17.9 over the recording; the voices in the centre 3 dB forward.
//
// Headphones: the five speakers the title mixes for, placed where a 5.1 room has them -
// the front pair at 30 degrees either side, the centre ahead, the back pair at 110 - and
// each heard by both ears as a head would hear it (Brown and Duda's spherical head, 1998):
// the far ear later, by Woodworth's interaural delay (up to 0.55 ms for the back pair),
// and in the head's shadow - a first-order shelf from 1.2 kHz, the near ear brighter and
// the far one duller; behind the head a further 3 dB off above 3-4 kHz, the pinna's
// shadow, which is all that tells behind from in front in a model without ears. The
// shelves are normalised so that the five speakers' average is flat: the timbre is the
// mixdown's, only the placing differs. The low frequency channel stays out, as on the
// speakers. Over run 367's recording (tools/audio_levels.py --render): -17.7 LUFS, the
// speakers' level, peaks at -1.7 dBFS; every octave within 1.7 dB of the mixdown's (a
// little more low end, the two ears hearing every speaker); the two ears' signals 0.82
// alike against the mixdown's 0.58 - the image a room's, not the headphones' extremes.

#include "warden_audio_mix.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace warden::audio {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;
constexpr double kHeadRadius = 0.0875;  // metres
constexpr double kSpeedOfSound = 343.0;  // metres per second
constexpr double kMinAlpha = 0.1;        // Brown and Duda's shadow at its deepest...
constexpr double kMinAlphaAngle = 150.0;  // ...this far round from the ear, in degrees

constexpr float kMinus3dB = 0.70710678f;
constexpr float kSpeakerScale = 1.0f / 2.41421356f;  // 1 + 2 x 0.707: never clips
// Headphones: matched to the speakers' mixdown by loudness over run 367's recording
// (tools/audio_levels.py --render): -17.7 LUFS both.
constexpr float kHeadphoneScale = 0.268f;

// Brown and Duda's alpha: the head's high-frequency gain for a source `angle` degrees
// round from the ear - 2 facing it (+6 dB), 0.1 at 150 degrees (-20 dB).
double ShadowAlpha(double angle) {
  return (1.0 + kMinAlpha / 2) +
         (1.0 - kMinAlpha / 2) * std::cos(angle / kMinAlphaAngle * kPi);
}

// The angle between two azimuths, 0 to 180 degrees.
double AngleBetween(double a, double b) {
  double difference = std::fmod(std::fabs(a - b), 360.0);
  return difference > 180.0 ? 360.0 - difference : difference;
}

// The rear shelf's high-frequency gain: -3 dB.
constexpr double kRearGain = 0.70794578;
constexpr double kRearPole = 3000.0;  // Hz; the zero at kRearPole / kRearGain

struct Placement {
  uint32_t channel;
  double azimuth;  // degrees, to the right positive
};
constexpr Placement kPlacements[] = {
    {0, -30.0}, {1, 30.0}, {2, 0.0}, {4, -110.0}, {5, 110.0},
};

}  // namespace

Mixer::Mixer() {
  // The head's shelves first, unnormalised, to find the five speakers' average power.
  double alpha[5][2];
  double power = 0.0;
  for (uint32_t s = 0; s < kSources; ++s) {
    const Placement& place = kPlacements[s];
    for (uint32_t ear = 0; ear < 2; ++ear) {
      const double ear_azimuth = ear == 0 ? -90.0 : 90.0;
      alpha[s][ear] = ShadowAlpha(AngleBetween(place.azimuth, ear_azimuth));
      const double rear = std::fabs(place.azimuth) > 90.0 ? kRearGain : 1.0;
      power += alpha[s][ear] * alpha[s][ear] * rear * rear;
    }
  }
  // At low frequencies every path passes 1; at high ones alpha. Normalised by this, the
  // average over the five speakers and two ears is 1 at high frequencies too.
  const double alpha_flat = std::sqrt(power / (2.0 * kSources));
  // Bilinear transform, prewarped at the shelf's corner (2 c / a rad/s, about 1.2 kHz).
  const double head_k = 1.0 / std::tan((kSpeedOfSound / kHeadRadius) / kRate);
  const double rear_kp = 1.0 / std::tan(kPi * kRearPole / kRate);
  const double rear_kz = 1.0 / std::tan(kPi * (kRearPole / kRearGain) / kRate);

  for (uint32_t s = 0; s < kSources; ++s) {
    Source& source = sources_[s];
    std::memset(&source, 0, sizeof(source));
    const Placement& place = kPlacements[s];
    source.channel = place.channel;
    source.rear = std::fabs(place.azimuth) > 90.0;
    // H(s) = (1 + s / wz) / (1 + s / wp): unity below, kRearGain above.
    const double rear_a0 = 1.0 + rear_kp;
    source.r_b0 = float((1.0 + rear_kz) / rear_a0);
    source.r_b1 = float((1.0 - rear_kz) / rear_a0);
    source.r_a1 = float((1.0 - rear_kp) / rear_a0);
    // Woodworth: the far ear's delay, from the source's angle off the median plane.
    const double lateral = std::asin(std::fabs(std::sin(place.azimuth * kPi / 180.0)));
    const double itd = kHeadRadius / kSpeedOfSound * (lateral + std::sin(lateral));
    const uint32_t far_delay = uint32_t(std::lround(itd * kRate));
    for (uint32_t ear = 0; ear < 2; ++ear) {
      Path& path = source.ear[ear];
      const bool far = (ear == 0 && place.azimuth > 0.0) || (ear == 1 && place.azimuth < 0.0);
      path.delay = far ? far_delay : 0;
      // H(s) = (1 + alpha s / 2w0) / (1 + alpha_flat s / 2w0).
      const double a0 = 1.0 + alpha_flat * head_k;
      path.b0 = float((1.0 + alpha[s][ear] * head_k) / a0);
      path.b1 = float((1.0 - alpha[s][ear] * head_k) / a0);
      path.a1 = float((1.0 - alpha_flat * head_k) / a0);
    }
  }
}

void Mixer::Reset() {
  for (Source& source : sources_) {
    source.r_x1 = source.r_y1 = 0.0f;
    std::memset(source.history, 0, sizeof(source.history));
    for (Path& path : source.ear) {
      path.x1 = path.y1 = 0.0f;
    }
  }
}

void Mixer::Speakers(const float* channels, uint32_t samples, float* stereo) {
  const float* front_left = channels;
  const float* front_right = channels + samples;
  const float* centre = channels + 2 * samples;
  const float* back_left = channels + 4 * samples;
  const float* back_right = channels + 5 * samples;
  for (uint32_t i = 0; i < samples; ++i) {
    const float shared = centre[i] * kMinus3dB;
    stereo[2 * i] = (front_left[i] + shared + back_left[i] * kMinus3dB) * kSpeakerScale;
    stereo[2 * i + 1] = (front_right[i] + shared + back_right[i] * kMinus3dB) * kSpeakerScale;
  }
}

void Mixer::Headphones(const float* channels, uint32_t samples, float* stereo) {
  for (uint32_t i = 0; i < samples; ++i) {
    float left = 0.0f;
    float right = 0.0f;
    const uint32_t at = write_ & (kHistory - 1);
    for (Source& source : sources_) {
      float x = channels[source.channel * samples + i];
      if (source.rear) {
        const float y = source.r_b0 * x + source.r_b1 * source.r_x1 - source.r_a1 * source.r_y1;
        source.r_x1 = x;
        source.r_y1 = y;
        x = y;
      }
      source.history[at] = x;
      for (uint32_t ear = 0; ear < 2; ++ear) {
        Path& path = source.ear[ear];
        const float in = source.history[(write_ - path.delay) & (kHistory - 1)];
        const float out = path.b0 * in + path.b1 * path.x1 - path.a1 * path.y1;
        path.x1 = in;
        path.y1 = out;
        (ear == 0 ? left : right) += out;
      }
    }
    ++write_;
    stereo[2 * i] = left * kHeadphoneScale;
    stereo[2 * i + 1] = right * kHeadphoneScale;
  }
  // Denormals: a filter memory left decaying in silence slows every sample after it.
  for (Source& source : sources_) {
    if (std::fabs(source.r_y1) < 1e-20f) source.r_y1 = 0.0f;
    for (Path& path : source.ear) {
      if (std::fabs(path.y1) < 1e-20f) path.y1 = 0.0f;
    }
  }
}

// ---- the volume boost's limiter ----
//
// Each sample's needed gain (the ceiling over its louder side, at most 1) is released -
// it may fall at once but rises back at the release's pace - then the least over the
// next kLook samples is taken, and that averaged over kLook: the average of a window's
// least is never above the gain a peak inside it needs, so the peak, delayed kLook - 1
// samples to meet it, is always under the ceiling, and the gain glides down to it over
// 1.3 ms rather than stepping.
namespace {
constexpr float kCeiling = 0.944f;  // -0.5 dBFS
const float kReleaseStep = float(1.0 - std::exp(-1.0 / (0.2 * kRate)));  // 200 ms
}  // namespace

Boost::Boost() { Reset(); }

void Boost::Reset() {
  release_ = 1.0f;
  for (uint32_t i = 0; i < kLook; ++i) {
    needed_[i] = 1.0f;
    smoothed_[i] = 1.0f;
  }
  sum_ = kLook;
  std::memset(delay_, 0, sizeof(delay_));
  at_ = 0;
}

void Boost::Process(float* stereo, uint32_t samples, float gain) {
  if (gain <= 1.0f) {
    if (active_) {
      Reset();
      active_ = false;
    }
    return;
  }
  active_ = true;
  for (uint32_t i = 0; i < samples; ++i) {
    const float left = stereo[2 * i] * gain;
    const float right = stereo[2 * i + 1] * gain;
    const float peak = std::max(std::fabs(left), std::fabs(right));
    const float needed = peak > kCeiling ? kCeiling / peak : 1.0f;
    release_ = std::min(needed, release_ + (1.0f - release_) * kReleaseStep);
    needed_[at_] = release_;
    float least = 1.0f;
    for (uint32_t k = 0; k < kLook; ++k) {
      least = std::min(least, needed_[k]);
    }
    sum_ += least - smoothed_[at_];
    smoothed_[at_] = least;
    const float applied = float(sum_ / kLook);
    delay_[2 * at_] = left;
    delay_[2 * at_ + 1] = right;
    // The oldest held sample: kLook - 1 behind the one just written.
    const uint32_t oldest = (at_ + 1) % kLook;
    stereo[2 * i] = delay_[2 * oldest] * applied;
    stereo[2 * i + 1] = delay_[2 * oldest + 1] * applied;
    at_ = oldest;
  }
  if (release_ > 1.0f - 1e-6f) release_ = 1.0f;
}

}  // namespace warden::audio
