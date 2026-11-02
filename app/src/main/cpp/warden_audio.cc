// The XAudio render driver: the title's audio engine's clock (BUG-039), and the sound
// output it feeds (task 4.5).
//
// The title's statically linked XAudio (under FMOD, and under Bink for movies with
// sound) registers one render callback. On the console the audio hardware calls it
// every 256 samples at 48 kHz - every 5.333 ms - and the callback mixes one frame and
// hands it back through XAudioSubmitRenderDriverFrame. Everything that waits on audio
// time waits on that callback: voices ending, streams refilling, and a movie's
// playback clock. With the registration failing (the stub's E_NOTIMPL) the intro
// movies opened, read their headers, and never advanced - the black frames of
// BUG-039 were an undecoded movie drawn faithfully.
//
// So a worker calls each registered callback, as DAORecomp does (DAORecomp/apu/
// audio.cpp) and as Xenia's AudioSystem does. The frames the title submits - 6
// channels x 256 samples, big-endian floats, channel-major - are made into stereo
// (warden_audio_mix.cc: the speakers' mixdown, or on headphones the headphone surround,
// PLAN 9.12) and played through an AAudio stream. While the stream is consuming, the worker makes
// a frame only when the stream's queue has room, so the title's audio clock follows
// the device's - as Xenia lets its host driver's free slots hold the pump back. When
// the stream is not consuming (opening, disconnected), the worker keeps the console's
// schedule on the steady clock instead, because the engine's state advances in the
// callback: a movie must not stop for want of a speaker. In the background the worker
// holds with the rest of the guest (warden_suspend.cc, BUG-057) - until session 35 it
// ran on there, and the title's sound went on unheard.

#include "warden_audio.h"

#include <aaudio/AAudio.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ppc_recomp_shared.h"
#include "warden_audio_mix.h"
#include "warden_guest.h"
#include "warden_guest_heap.h"
#include "warden_suspend.h"
#include "warden_threads.h"
#include "xenia/base/logging.h"

namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorInvalidArg = 0x80070057;    // E_INVALIDARG
constexpr uint32_t kErrorOutOfMemory = 0x8007000E;   // E_OUTOFMEMORY

// Driver handles are 0x4155xxxx ("AU"), the low 16 bits the client index - Xenia's
// convention, which the title only passes back.
constexpr uint32_t kDriverTag = 0x41550000;
constexpr uint32_t kMaxClients = 8;
// Xenia's default X_AUDIO_FLAGS: Dolby Digital | Dolby Pro Logic.
constexpr uint32_t kSpeakerConfig = 0x00010001;
constexpr uint32_t kFrameChannels = 6;
constexpr uint32_t kFrameChannelSamples = 256;
constexpr uint32_t kFrameSamples = kFrameChannels * kFrameChannelSamples;
constexpr int32_t kSampleRate = 48000;

struct Client {
  bool in_use = false;
  uint32_t callback = 0;
  // The callback is called with the address of a guest cell holding the argument the
  // title registered, not with the argument itself (Xenia, DAORecomp).
  uint32_t argument_cell = 0;
};

std::mutex g_mutex;
std::array<Client, kMaxClients> g_clients;
uint32_t g_cells = 0;  // a page of argument cells, one per client

std::atomic<uint64_t> g_callbacks{0};
std::atomic<uint64_t> g_frames{0};
std::atomic<uint64_t> g_silent_frames{0};
std::atomic<uint32_t> g_peak_milli{0};  // loudest sample since the last report, x1000
std::atomic<uint32_t> g_out_peak_milli{0};  // the same of the output: mixed, boosted

int64_t MonotonicNanos() {
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return int64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

// ---------------------------------------------------------------- the output queue
//
// Stereo frames, mixed down as they are submitted, on a ring with one producer (the
// render worker, inside the title's callback; serialised by a mutex in case another
// thread ever submits) and one consumer (AAudio's data callback, which takes no lock).
// While the stream consumes, the worker keeps g_target_frames queued: two of the
// device's bursts and a frame, so that a burst never finds the queue short. The Thor
// gives a shared stream with 960-frame (20 ms) bursts, so 9 frames, 48 ms; a fixed 4
// had 8% of the output as silence (run 207).
constexpr uint32_t kRingFrames = 32;
std::atomic<uint32_t> g_target_frames{4};

struct StereoFrame {
  float samples[kFrameChannelSamples * 2];  // interleaved left, right
};

std::array<StereoFrame, kRingFrames> g_ring;
std::atomic<uint32_t> g_ring_write{0};  // frames queued, ever
std::atomic<uint32_t> g_ring_read{0};   // frames played to the end, ever
uint32_t g_read_offset = 0;             // samples played of the oldest frame (consumer's)
std::mutex g_producer_mutex;

std::atomic<int64_t> g_last_consumed{0};    // MonotonicNanos() of the last data callback
std::atomic<uint64_t> g_played{0};          // samples per channel the device has taken
std::atomic<uint64_t> g_underrun{0};        // of those, silence for want of a frame
std::atomic<uint64_t> g_dropped{0};         // frames submitted to a full queue

uint32_t RingFill() {
  return g_ring_write.load(std::memory_order_acquire) -
         g_ring_read.load(std::memory_order_acquire);
}

// The stream is taking frames: its callback ran in the last 50 ms.
bool OutputConsuming() {
  const int64_t last = g_last_consumed.load(std::memory_order_acquire);
  return last && MonotonicNanos() - last < 50000000;
}

float LoadFloat(const uint8_t* base, uint32_t address) {
  uint32_t bits;
  std::memcpy(&bits, base + address, sizeof(bits));
  bits = __builtin_bswap32(bits);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// ---------------------------------------------------------------- the dump
//
// A file named audio-dump in the app's files directory, holding a number of seconds
// (none: 120), records the frames as the title submits them - all six channels,
// before the mixdown - to audio-dump.wav beside it, as DAORecomp's DAO_AUDIO_DUMP does:
// 32-bit float, interleaved, 48 kHz. For measuring what the title puts where
// (tools/audio_levels.py); the header is brought up to date every second, so a run
// ended at any point leaves a file that opens.
std::mutex g_dump_mutex;
FILE* g_dump = nullptr;
uint32_t g_dump_frames = 0;
uint32_t g_dump_limit = 0;  // frames; 0 until the switch has been looked at
bool g_dump_checked = false;

void WriteDumpHeader() {
  const uint32_t data_bytes = g_dump_frames * kFrameSamples * 4;
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
              {'f', 'm', 't', ' '}, 16, 3, uint16_t(kFrameChannels), uint32_t(kSampleRate),
              uint32_t(kSampleRate) * kFrameChannels * 4, uint16_t(kFrameChannels * 4), 32,
              {'d', 'a', 't', 'a'}, data_bytes};
  fseek(g_dump, 0, SEEK_SET);
  fwrite(&header, sizeof(header), 1, g_dump);
  fseek(g_dump, 0, SEEK_END);
}

void DumpFrame(const uint8_t* base, uint32_t samples) {
  std::lock_guard<std::mutex> lock(g_dump_mutex);
  if (!g_dump_checked) {
    g_dump_checked = true;
    const char* dir = getenv("WARDEN_FILES_DIR");
    if (!dir) {
      return;
    }
    const std::string folder(dir);
    FILE* control = fopen((folder + "/audio-dump").c_str(), "r");
    if (!control) {
      return;
    }
    int seconds = 0;
    if (fscanf(control, "%d", &seconds) != 1 || seconds <= 0) {
      seconds = 120;
    }
    fclose(control);
    g_dump = fopen((folder + "/audio-dump.wav").c_str(), "wb");
    if (!g_dump) {
      XELOGE("XAudio: cannot write {}/audio-dump.wav", folder);
      return;
    }
    g_dump_limit = uint32_t(seconds) * uint32_t(kSampleRate) / kFrameChannelSamples;
    WriteDumpHeader();
    XELOGI("XAudio: recording the title's first {} s of frames, all six channels, to "
           "audio-dump.wav", seconds);
  }
  if (!g_dump) {
    return;
  }
  float interleaved[kFrameSamples];
  for (uint32_t i = 0; i < kFrameChannelSamples; ++i) {
    for (uint32_t channel = 0; channel < kFrameChannels; ++channel) {
      interleaved[i * kFrameChannels + channel] =
          LoadFloat(base, samples + 4 * (channel * kFrameChannelSamples + i));
    }
  }
  fwrite(interleaved, sizeof(interleaved), 1, g_dump);
  ++g_dump_frames;
  if (g_dump_frames % 188 == 0 || g_dump_frames >= g_dump_limit) {
    WriteDumpHeader();
    fflush(g_dump);
  }
  if (g_dump_frames >= g_dump_limit) {
    fclose(g_dump);
    g_dump = nullptr;
    XELOGI("XAudio: audio-dump.wav complete, {} frames", g_dump_frames);
  }
}

// ---------------------------------------------------------------- the route
//
// Which mix the output gets (PLAN 9.12, warden_audio_mix.cc): the speakers' mixdown, or
// - with Settings -> Headphones -> Surround, read as the title starts (files/headphones
// holding "surround") - the headphone surround while the stream plays on headphones.
// Android says which output devices are headphones (GameActivity's audio device
// callback -> SetHeadphoneDevices); the stream says which device it plays on
// (AAudioStream_getDeviceId, after each open - plugging headphones in or out
// disconnects the stream, and it is opened again on the new device).
std::mutex g_route_mutex;
std::vector<int32_t> g_headphone_devices;
int32_t g_stream_device = 0;
bool g_surround_wanted = false;
bool g_force_headphones = false;  // files/audio-force-headphones: a test marker
std::atomic<bool> g_headphone_mix{false};

// Logs when the mix changes, and at every open of the stream (`opened`).
void UpdateRouteLocked(bool opened) {
  const bool on_headphones =
      g_force_headphones || (g_stream_device > 0 &&
                             std::find(g_headphone_devices.begin(), g_headphone_devices.end(),
                                       g_stream_device) != g_headphone_devices.end());
  const bool mix = g_surround_wanted && on_headphones;
  if (g_headphone_mix.exchange(mix) == mix && !opened) {
    return;
  }
  XELOGI("XAudio output: device {} is {}{} - {}", g_stream_device,
         on_headphones ? "headphones" : "not headphones",
         g_force_headphones ? " (audio-force-headphones)" : "",
         mix ? "the headphone surround" : "the speakers' mixdown");
}

// Options -> Volume boost: "off", or the decibels ("6", "12", "18").
std::atomic<float> g_boost_gain{1.0f};

void ApplyVolumeBoost(const std::string& value) {
  const int db = value == "off" ? 0 : std::atoi(value.c_str());
  const float gain = db > 0 && db <= 24 ? std::pow(10.0f, float(db) / 20.0f) : 1.0f;
  g_boost_gain.store(gain, std::memory_order_relaxed);
  XELOGI("XAudio output: volume boost {}", gain > 1.0f ? fmt::format("+{} dB, limited", db)
                                                       : std::string("off"));
}

void ReadRouteSettings() {
  const char* dir = getenv("WARDEN_FILES_DIR");
  if (!dir) {
    return;
  }
  const std::string folder(dir);
  std::lock_guard<std::mutex> lock(g_route_mutex);
  if (FILE* file = fopen((folder + "/headphones").c_str(), "r")) {
    char text[16] = {};
    g_surround_wanted = fscanf(file, "%15s", text) == 1 && strcmp(text, "surround") == 0;
    fclose(file);
  }
  if (FILE* file = fopen((folder + "/audio-force-headphones").c_str(), "r")) {
    g_force_headphones = true;
    fclose(file);
  }
  if (FILE* file = fopen((folder + "/volume-boost").c_str(), "r")) {
    char text[16] = {};
    if (fscanf(file, "%15s", text) == 1) {
      ApplyVolumeBoost(text);
    }
    fclose(file);
  }
  XELOGI("XAudio output: headphones get {}{}",
         g_surround_wanted ? "the headphone surround" : "the speakers' mixdown",
         g_force_headphones ? "; the speaker counts as headphones (audio-force-headphones)"
                            : "");
}

// The producer's mixer: its headphone path keeps a few samples of history and its
// filters' memories from frame to frame, so it is used under g_producer_mutex alone.
warden::audio::Mixer g_mixer;
bool g_mixed_for_headphones = false;
// Options -> Volume boost: the gain (linear; 1 = off) and its limiter, after the mix.
warden::audio::Boost g_boost;

// BUG-109: Android's media volume where the device does not apply it to this track
// (SetMediaVolume); the second is the mixing thread's, where the last frame ended.
std::atomic<float> g_media_gain{1.0f};
float g_media_gain_applied = 1.0f;
std::atomic<uint64_t> g_mix_nanos{0};   // spent making frames into stereo
std::atomic<uint64_t> g_mix_frames{0};

// A frame of the title's six channels, made into the output's two and queued.
void QueueFrame(const uint8_t* base, uint32_t samples) {
  std::lock_guard<std::mutex> lock(g_producer_mutex);
  const uint32_t write = g_ring_write.load(std::memory_order_relaxed);
  if (write - g_ring_read.load(std::memory_order_acquire) >= kRingFrames) {
    g_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // Channel-major, as submitted: front left, front right, centre, low frequency, back
  // left, back right (XAudio's order, Microsoft's default mapping).
  float planar[kFrameSamples];
  for (uint32_t i = 0; i < kFrameSamples; ++i) {
    const float value = LoadFloat(base, samples + 4 * i);
    planar[i] = std::isfinite(value) ? value : 0.0f;
  }
  StereoFrame& frame = g_ring[write % kRingFrames];
  const int64_t began = MonotonicNanos();
  const bool headphones = g_headphone_mix.load(std::memory_order_relaxed);
  if (headphones) {
    if (!g_mixed_for_headphones) {
      g_mixer.Reset();
    }
    g_mixer.Headphones(planar, kFrameChannelSamples, frame.samples);
  } else {
    g_mixer.Speakers(planar, kFrameChannelSamples, frame.samples);
  }
  g_mixed_for_headphones = headphones;
  g_boost.Process(frame.samples, kFrameChannelSamples,
                  g_boost_gain.load(std::memory_order_relaxed));
  // BUG-109: the media volume, last as Android's own would be - across the frame, so that a
  // step does not click.
  const float media_gain = g_media_gain.load(std::memory_order_relaxed);
  if (media_gain != g_media_gain_applied || media_gain != 1.0f) {
    const float step = (media_gain - g_media_gain_applied) / float(kFrameChannelSamples);
    float gain = g_media_gain_applied;
    for (uint32_t i = 0; i < kFrameChannelSamples; ++i) {
      gain += step;
      frame.samples[2 * i] *= gain;
      frame.samples[2 * i + 1] *= gain;
    }
    g_media_gain_applied = media_gain;
  }
  float out_peak = 0.0f;
  for (uint32_t i = 0; i < 2 * kFrameChannelSamples; ++i) {
    out_peak = std::fmax(out_peak, std::fabs(frame.samples[i]));
  }
  const uint32_t out_milli = uint32_t(out_peak * 1000.0f);
  uint32_t out_seen = g_out_peak_milli.load(std::memory_order_relaxed);
  while (out_milli > out_seen &&
         !g_out_peak_milli.compare_exchange_weak(out_seen, out_milli,
                                                 std::memory_order_relaxed)) {
  }
  g_mix_nanos.fetch_add(uint64_t(MonotonicNanos() - began), std::memory_order_relaxed);
  g_mix_frames.fetch_add(1, std::memory_order_relaxed);
  g_ring_write.store(write + 1, std::memory_order_release);
}

// ---------------------------------------------------------------- the AAudio stream

std::mutex g_stream_mutex;
AAudioStream* g_stream = nullptr;
bool g_output_paused = false;           // the app is in the background
int32_t g_stream_channels = 2;
aaudio_format_t g_stream_format = AAUDIO_FORMAT_PCM_FLOAT;
std::atomic<uint32_t> g_reopens{0};

// The device asks for `frames` samples per channel; they come off the ring, and
// silence stands in for a frame that is not there yet.
aaudio_data_callback_result_t OnAudioReady(AAudioStream*, void*, void* audio, int32_t frames) {
  const int32_t channels = g_stream_channels;
  const bool float_out = g_stream_format == AAUDIO_FORMAT_PCM_FLOAT;
  uint32_t read = g_ring_read.load(std::memory_order_relaxed);
  int32_t done = 0;
  while (done < frames) {
    if (read == g_ring_write.load(std::memory_order_acquire)) {
      break;
    }
    const StereoFrame& frame = g_ring[read % kRingFrames];
    const int32_t take = std::min<int32_t>(frames - done,
                                           int32_t(kFrameChannelSamples - g_read_offset));
    for (int32_t i = 0; i < take; ++i) {
      const float left = frame.samples[2 * (g_read_offset + i)];
      const float right = frame.samples[2 * (g_read_offset + i) + 1];
      for (int32_t c = 0; c < channels; ++c) {
        const float value = channels == 1 ? (left + right) * 0.5f
                            : c == 0       ? left
                            : c == 1       ? right
                                           : 0.0f;
        const size_t at = size_t(done + i) * size_t(channels) + size_t(c);
        if (float_out) {
          static_cast<float*>(audio)[at] = value;
        } else {
          static_cast<int16_t*>(audio)[at] =
              int16_t(std::lrintf(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
        }
      }
    }
    done += take;
    g_read_offset += uint32_t(take);
    if (g_read_offset == kFrameChannelSamples) {
      g_read_offset = 0;
      ++read;
      g_ring_read.store(read, std::memory_order_release);
    }
  }
  if (done < frames) {
    const size_t bytes = (float_out ? sizeof(float) : sizeof(int16_t)) * size_t(channels);
    std::memset(static_cast<uint8_t*>(audio) + size_t(done) * bytes, 0,
                size_t(frames - done) * bytes);
    g_underrun.fetch_add(uint64_t(frames - done), std::memory_order_relaxed);
  }
  g_played.fetch_add(uint64_t(frames), std::memory_order_relaxed);
  g_last_consumed.store(MonotonicNanos(), std::memory_order_release);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void OpenStreamLocked();

void CloseStreamLocked() {
  if (g_stream) {
    AAudioStream_requestStop(g_stream);
    AAudioStream_close(g_stream);
    g_stream = nullptr;
  }
  g_last_consumed.store(0, std::memory_order_release);
}

// A stream that fails (the output device changed, a headset was plugged in) is closed
// and opened again on another thread: AAudio forbids both inside its own callback.
void OnAudioError(AAudioStream* stream, void*, aaudio_result_t error) {
  XELOGW("XAudio output: the stream stopped ({}); opening it again",
         AAudio_convertResultToText(error));
  std::thread([stream] {
    std::lock_guard<std::mutex> lock(g_stream_mutex);
    if (g_stream != stream) {
      return;
    }
    CloseStreamLocked();
    g_reopens.fetch_add(1, std::memory_order_relaxed);
    if (!g_output_paused) {
      OpenStreamLocked();
    }
  }).detach();
}

void OpenStreamLocked() {
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK) {
    XELOGE("XAudio output: no stream builder ({})", AAudio_convertResultToText(result));
    return;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setSampleRate(builder, kSampleRate);
  AAudioStreamBuilder_setChannelCount(builder, 2);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  // Exclusive if the device allows it; AAudio opens a shared stream otherwise.
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
  AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
  AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
  AAudioStreamBuilder_setDataCallback(builder, OnAudioReady, nullptr);
  AAudioStreamBuilder_setErrorCallback(builder, OnAudioError, nullptr);
  AAudioStream* stream = nullptr;
  result = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (result != AAUDIO_OK || !stream) {
    XELOGE("XAudio output: cannot open a stream ({}); the title's clock stays on the "
           "steady clock", AAudio_convertResultToText(result));
    return;
  }
  g_stream_channels = AAudioStream_getChannelCount(stream);
  g_stream_format = AAudioStream_getFormat(stream);
  if (g_stream_format != AAUDIO_FORMAT_PCM_FLOAT && g_stream_format != AAUDIO_FORMAT_PCM_I16) {
    XELOGE("XAudio output: the stream's sample format {} is neither float nor 16-bit",
           g_stream_format);
    AAudioStream_close(stream);
    return;
  }
  // Two bursts of buffering, the low-latency default, rather than the whole capacity;
  // and the queue in front of it two bursts and a frame deep.
  const int32_t burst = AAudioStream_getFramesPerBurst(stream);
  AAudioStream_setBufferSizeInFrames(stream, burst * 2);
  g_target_frames.store(std::clamp<uint32_t>(
      uint32_t((2 * burst + int32_t(kFrameChannelSamples) - 1) / int32_t(kFrameChannelSamples)) +
          1,
      4, kRingFrames - 2));
  result = AAudioStream_requestStart(stream);
  if (result != AAUDIO_OK) {
    XELOGE("XAudio output: the stream would not start ({})",
           AAudio_convertResultToText(result));
    AAudioStream_close(stream);
    return;
  }
  g_stream = stream;
  {
    std::lock_guard<std::mutex> lock(g_route_mutex);
    g_stream_device = AAudioStream_getDeviceId(stream);
    UpdateRouteLocked(true);
  }
  XELOGI("XAudio output: AAudio, {} Hz, {} channel(s), {}, burst {} frames, buffer {} of "
         "{}, {} latency, {} mode; {} frames queued ahead of it",
         AAudioStream_getSampleRate(stream), g_stream_channels,
         g_stream_format == AAUDIO_FORMAT_PCM_FLOAT ? "float" : "16-bit",
         AAudioStream_getFramesPerBurst(stream), AAudioStream_getBufferSizeInFrames(stream),
         AAudioStream_getBufferCapacityInFrames(stream),
         AAudioStream_getPerformanceMode(stream) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
             ? "low"
             : "normal",
         AAudioStream_getSharingMode(stream) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive"
                                                                              : "shared",
         g_target_frames.load());
}

// ---------------------------------------------------------------- the render worker

uint32_t ClientIndex(uint32_t driver) {
  if ((driver & 0xFFFF0000u) != kDriverTag) {
    return kMaxClients;
  }
  const uint32_t index = driver & 0xFFFFu;
  return index < kMaxClients ? index : kMaxClients;
}

// Frame n is due at origin + n * 256 / 48000 s; 256 / 48000 s = 16,000,000 / 3 ns.
// From the origin, not the previous wake, so no drift accumulates. While the stream is
// consuming the schedule runs 0.5% fast (15,920,000 / 3 ns), so that it is always a
// little ahead of the device and the queue's back-pressure, not the steady clock,
// decides the pace: the two clocks differ by some tens of parts per million, and a
// schedule behind the device would starve it a frame at a time.
std::chrono::nanoseconds FrameOffset(uint64_t frame, bool device) {
  return std::chrono::nanoseconds(frame * (device ? 15920000ull : 16000000ull) / 3);
}

void RunCallbacks(uint8_t* base, PPCContext* ctx) {
  std::array<Client, kMaxClients> clients;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    clients = g_clients;
  }
  for (const Client& client : clients) {
    if (!client.in_use) {
      continue;
    }
    PPCFunc* function = PPC_LOOKUP_FUNC(base, client.callback);
    if (!function) {
      static std::atomic<bool> said{false};
      if (!said.exchange(true)) {
        XELOGE("XAudio: render callback {:08X} is not a translated function",
               client.callback);
      }
      continue;
    }
    ctx->r3.u64 = client.argument_cell;
    function(*ctx, base);
    g_callbacks.fetch_add(1, std::memory_order_relaxed);
  }
}

void WorkerMain() {
  if (!warden::AttachGuestThreadForHost("XAudio worker")) {
    XELOGE("XAudio: the render worker could not attach to the guest; no audio clock");
    return;
  }
  warden::NameCurrentGuestThread("XAudio worker");
  uint8_t* base = warden::GuestMemoryBase();
  PPCContext* ctx = warden::CurrentGuestContext();
  using Clock = std::chrono::steady_clock;
  // Behind by more than this, the schedule restarts rather than bursting callbacks.
  constexpr auto kMaxLag = std::chrono::milliseconds(20);
  Clock::time_point origin = Clock::now();
  Clock::time_point next_report = origin + std::chrono::seconds(5);
  uint64_t frame = 0;
  uint32_t restarts = 0;
  uint64_t held = 0;  // callbacks delayed for room in the queue
  bool device = false;
  for (;;) {
    // Held while the app is in the background (BUG-057); the schedule then starts again
    // from the resumption rather than catching up on the time away. The first callback
    // after it waits for the title's mixer, itself just let go, so a schedule restarted
    // then is expected, not a worker falling behind (runs 319-321 logged one each time).
    const bool resumed = warden::GuestSuspendPoint("XAudio worker");
    if (resumed) {
      origin = Clock::now();
      frame = 0;
    }
    // The steady clock is always the schedule, so a callback that submits nothing - the
    // title skips one when its mixer is not done - can never make the worker spin.
    const bool consuming = OutputConsuming();
    if (consuming != device) {
      device = consuming;
      origin = Clock::now();
      frame = 0;
    }
    std::this_thread::sleep_until(origin + FrameOffset(frame, device));
    const uint32_t target = g_target_frames.load(std::memory_order_relaxed);
    if (device && RingFill() >= target) {
      // The device is behind the schedule: wait for room, and move the schedule with
      // the wait, so the title's audio clock follows the device's.
      const Clock::time_point give_up = Clock::now() + std::chrono::milliseconds(50);
      while (OutputConsuming() && RingFill() >= target && Clock::now() < give_up) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      origin = Clock::now() - FrameOffset(frame, device);
      ++held;
    }
    RunCallbacks(base, ctx);
    ++frame;
    const Clock::time_point now = Clock::now();
    if (now - (origin + FrameOffset(frame, device)) > kMaxLag) {
      if (!resumed) {
        ++restarts;
        if (restarts <= 5 || (restarts & (restarts - 1)) == 0) {
          XELOGW("XAudio: the render worker fell behind; schedule restarted (#{})", restarts);
        }
      }
      origin = now;
      frame = 0;
    }
    if (now >= next_report) {
      next_report = now + std::chrono::seconds(5);
      XELOGI("XAudio: {} callbacks ({} held for the device), {} frames submitted ({} silent, "
             "{} dropped), peak {:.3f} in the last 5 s (out {:.3f}); output {:.1f} s played, {:.1f} s of "
             "it silence for want of a frame, {} queued, {}; {} {:.1f} us a frame",
             g_callbacks.load(), held, g_frames.load(), g_silent_frames.load(),
             g_dropped.load(), g_peak_milli.exchange(0) / 1000.0, g_out_peak_milli.exchange(0) / 1000.0,
             g_played.load() / double(kSampleRate), g_underrun.load() / double(kSampleRate),
             RingFill(), OutputConsuming() ? "consuming" : "not consuming",
             g_headphone_mix.load() ? "headphone surround" : "speakers' mixdown",
             g_mix_frames.load() ? g_mix_nanos.load() / 1000.0 / g_mix_frames.load() : 0.0);
    }
  }
}

void EnsureWorker() {
  static std::once_flag once;
  std::call_once(once, [] {
    ReadRouteSettings();
    std::thread(WorkerMain).detach();
    XELOGI("XAudio: render worker started (48 kHz, 256-sample frames)");
    std::lock_guard<std::mutex> lock(g_stream_mutex);
    if (!g_output_paused) {
      OpenStreamLocked();
    }
  });
}

}  // namespace

namespace warden {

void SetAudioOutputPaused(bool paused) {
  std::lock_guard<std::mutex> lock(g_stream_mutex);
  if (g_output_paused == paused) {
    return;
  }
  g_output_paused = paused;
  if (paused) {
    if (g_stream) {
      AAudioStream_requestPause(g_stream);
      g_last_consumed.store(0, std::memory_order_release);
      XELOGI("XAudio output: paused");
    }
  } else if (g_stream) {
    AAudioStream_requestStart(g_stream);
    XELOGI("XAudio output: resumed");
  } else if (g_callbacks.load()) {
    OpenStreamLocked();
  }
}

void SetHeadphoneDevices(const std::vector<int32_t>& device_ids) {
  std::lock_guard<std::mutex> lock(g_route_mutex);
  g_headphone_devices = device_ids;
  UpdateRouteLocked(false);
}

void SetMediaVolume(float gain) {
  const float clamped = std::fmin(std::fmax(gain, 0.0f), 1.0f);
  if (g_media_gain.exchange(clamped, std::memory_order_relaxed) != clamped) {
    XELOGI("XAudio output: media volume, gain {:.4f}", clamped);
  }
}

void SetVolumeBoost(const std::string& value) { ApplyVolumeBoost(value); }

void SetHeadphoneSurround(bool surround) {
  std::lock_guard<std::mutex> lock(g_route_mutex);
  g_surround_wanted = surround;
  XELOGI("XAudio output: headphones get {}",
         surround ? "the headphone surround" : "the speakers' mixdown");
  UpdateRouteLocked(false);
}

}  // namespace warden

// DWORD XAudioGetSpeakerConfig(DWORD* config)
PPC_FUNC(__imp__XAudioGetSpeakerConfig) {
  if (!ctx.r3.u32) {
    ctx.r3.u64 = kErrorInvalidArg;
    return;
  }
  PPC_STORE_U32(ctx.r3.u32, kSpeakerConfig);
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XAudioGetVoiceCategoryVolumeChangeMask(void* driver, DWORD* mask) - none changed.
PPC_FUNC(__imp__XAudioGetVoiceCategoryVolumeChangeMask) {
  if (ctx.r4.u32) {
    PPC_STORE_U32(ctx.r4.u32, 0);
  }
  ctx.r3.u64 = kErrorSuccess;
}

// DWORD XAudioGetVoiceCategoryVolume(DWORD category, float* volume) - full volume.
PPC_FUNC(__imp__XAudioGetVoiceCategoryVolume) {
  if (ctx.r4.u32) {
    const float one = 1.0f;
    uint32_t bits;
    std::memcpy(&bits, &one, sizeof(bits));
    PPC_STORE_U32(ctx.r4.u32, bits);
  }
  ctx.r3.u64 = kErrorSuccess;
}

// HRESULT XAudioRegisterRenderDriverClient(const DWORD* callback_and_argument,
//                                          DWORD* driver)
//
// callback_and_argument is { callback, argument }.
PPC_FUNC(__imp__XAudioRegisterRenderDriverClient) {
  const uint32_t pair = ctx.r3.u32;
  const uint32_t driver_out = ctx.r4.u32;
  if (!pair || !driver_out) {
    ctx.r3.u64 = kErrorInvalidArg;
    return;
  }
  const uint32_t callback = PPC_LOAD_U32(pair + 0);
  const uint32_t argument = PPC_LOAD_U32(pair + 4);
  if (!callback) {
    ctx.r3.u64 = kErrorInvalidArg;
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_cells) {
    uint32_t got = 0;
    if (warden::AllocateGuestMemory(0, 0x1000, 0x1000, g_cells, got) != 0 || !g_cells) {
      g_cells = 0;
      ctx.r3.u64 = kErrorOutOfMemory;
      return;
    }
    std::memset(base + g_cells, 0, 0x1000);
  }
  for (uint32_t i = 0; i < kMaxClients; ++i) {
    if (g_clients[i].in_use) {
      continue;
    }
    const uint32_t cell = g_cells + 16 * i;
    PPC_STORE_U32(cell, argument);
    g_clients[i] = Client{true, callback, cell};
    PPC_STORE_U32(driver_out, kDriverTag | i);
    XELOGI("[{}] XAudioRegisterRenderDriverClient(callback {:08X}, argument {:08X}) -> "
           "client {}", warden::CurrentGuestThreadName(), callback, argument, i);
    EnsureWorker();
    ctx.r3.u64 = kErrorSuccess;
    return;
  }
  XELOGW("XAudioRegisterRenderDriverClient: all {} client slots are in use", kMaxClients);
  ctx.r3.u64 = kErrorOutOfMemory;
}

// HRESULT XAudioUnregisterRenderDriverClient(void* driver)
PPC_FUNC(__imp__XAudioUnregisterRenderDriverClient) {
  (void)base;
  const uint32_t index = ClientIndex(ctx.r3.u32);
  if (index == kMaxClients) {
    ctx.r3.u64 = kErrorInvalidArg;
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_clients[index] = Client{};
  XELOGI("XAudioUnregisterRenderDriverClient(client {})", index);
  ctx.r3.u64 = kErrorSuccess;
}

// HRESULT XAudioSubmitRenderDriverFrame(void* driver, const float* samples)
//
// 6 x 256 big-endian floats, channel-major: measured, mixed down to stereo and queued
// for the output.
PPC_FUNC(__imp__XAudioSubmitRenderDriverFrame) {
  const uint32_t index = ClientIndex(ctx.r3.u32);
  const uint32_t samples = ctx.r4.u32;
  if (index == kMaxClients || !samples) {
    ctx.r3.u64 = kErrorInvalidArg;
    return;
  }
  float peak = 0.0f;
  for (uint32_t i = 0; i < kFrameSamples; ++i) {
    const float value = std::fabs(LoadFloat(base, samples + 4 * i));
    if (value > peak && std::isfinite(value)) {
      peak = value;
    }
  }
  g_frames.fetch_add(1, std::memory_order_relaxed);
  if (peak == 0.0f) {
    g_silent_frames.fetch_add(1, std::memory_order_relaxed);
  }
  const uint32_t peak_milli = uint32_t(std::fmin(peak, 1000.0f) * 1000.0f);
  uint32_t seen = g_peak_milli.load(std::memory_order_relaxed);
  while (peak_milli > seen &&
         !g_peak_milli.compare_exchange_weak(seen, peak_milli, std::memory_order_relaxed)) {
  }
  DumpFrame(base, samples);
  QueueFrame(base, samples);
  ctx.r3.u64 = kErrorSuccess;
}
