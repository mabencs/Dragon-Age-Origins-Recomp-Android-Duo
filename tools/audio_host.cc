// audio_host - the device's mixdown (app/src/main/cpp/warden_audio_mix.cc) run over a
// six-channel recording on the computer. tools/audio_levels.py --render builds this and
// measures what it writes.
//
//   audio_host <six-channel float WAV> <speakers WAV> <headphones WAV>
//
// The recording is the port's (files/audio-dump -> files/audio-dump.wav) or DAORecomp's
// (DAO_AUDIO_DUMP): 32-bit float, interleaved, XAudio's channel order. It is fed through
// in the title's own frames of 256 samples, as the device does; both outputs are 32-bit
// float stereo.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "warden_audio_mix.h"

namespace {

constexpr uint32_t kFrame = 256;

bool ReadDump(const char* path, std::vector<float>& samples) {
  FILE* file = std::fopen(path, "rb");
  if (!file) {
    return false;
  }
  std::vector<uint8_t> data;
  uint8_t buffer[1 << 16];
  size_t got;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    data.insert(data.end(), buffer, buffer + got);
  }
  std::fclose(file);
  if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) || std::memcmp(&data[8], "WAVE", 4)) {
    return false;
  }
  size_t at = 12;
  uint16_t format = 0, channels = 0;
  while (at + 8 <= data.size()) {
    uint32_t size;
    std::memcpy(&size, &data[at + 4], 4);
    if (!std::memcmp(&data[at], "fmt ", 4)) {
      std::memcpy(&format, &data[at + 8], 2);
      std::memcpy(&channels, &data[at + 10], 2);
    } else if (!std::memcmp(&data[at], "data", 4)) {
      if (format != 3 || channels != warden::audio::kTitleChannels) {
        std::fprintf(stderr, "%s: format %u, %u channels - expected float, 6\n", path,
                     unsigned(format), unsigned(channels));
        return false;
      }
      const size_t bytes = std::min<size_t>(size, data.size() - at - 8);
      samples.resize(bytes / 4);
      std::memcpy(samples.data(), &data[at + 8], samples.size() * 4);
      return true;
    }
    at += 8 + size + (size & 1);
  }
  return false;
}

bool WriteStereo(const char* path, const std::vector<float>& samples) {
  FILE* file = std::fopen(path, "wb");
  if (!file) {
    return false;
  }
  const uint32_t data_bytes = uint32_t(samples.size() * 4);
  struct {
    char riff[4];
    uint32_t riff_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format, channels;
    uint32_t rate, byte_rate;
    uint16_t block_align, bits;
    char data[4];
    uint32_t data_size;
  } header = {{'R', 'I', 'F', 'F'}, 36 + data_bytes, {'W', 'A', 'V', 'E'},
              {'f', 'm', 't', ' '}, 16, 3, 2, 48000, 48000 * 2 * 4, 8, 32,
              {'d', 'a', 't', 'a'}, data_bytes};
  std::fwrite(&header, sizeof(header), 1, file);
  std::fwrite(samples.data(), 4, samples.size(), file);
  std::fclose(file);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: audio_host <six-channel WAV> <speakers WAV> <headphones WAV>\n");
    return 2;
  }
  std::vector<float> interleaved;
  if (!ReadDump(argv[1], interleaved)) {
    std::fprintf(stderr, "%s: cannot read a six-channel float WAV\n", argv[1]);
    return 1;
  }
  const uint32_t channels = warden::audio::kTitleChannels;
  const size_t frames = interleaved.size() / channels / kFrame;
  std::vector<float> speakers(frames * kFrame * 2), headphones(frames * kFrame * 2);
  warden::audio::Mixer mixer;
  float planar[warden::audio::kTitleChannels * kFrame];
  for (size_t f = 0; f < frames; ++f) {
    const float* in = &interleaved[f * kFrame * channels];
    for (uint32_t i = 0; i < kFrame; ++i) {
      for (uint32_t c = 0; c < channels; ++c) {
        planar[c * kFrame + i] = in[i * channels + c];
      }
    }
    mixer.Speakers(planar, kFrame, &speakers[f * kFrame * 2]);
    mixer.Headphones(planar, kFrame, &headphones[f * kFrame * 2]);
  }
  if (!WriteStereo(argv[2], speakers) || !WriteStereo(argv[3], headphones)) {
    std::fprintf(stderr, "cannot write the outputs\n");
    return 1;
  }
  std::printf("%zu frames of %u samples: %s, %s\n", frames, kFrame, argv[2], argv[3]);
  return 0;
}
