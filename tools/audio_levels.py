"""audio_levels.py - what the title puts in each of its six channels, and how loud it is.

    python tools/audio_levels.py build/audio367.wav [--window 10] [--from S] [--to S]
    python tools/audio_levels.py build/audio367.wav --render


Reads a six-channel dump - the port's (a file named audio-dump in the app's files
directory, holding seconds: files/audio-dump.wav, warden_audio.cc) or DAORecomp's
(DAO_AUDIO_DUMP) - 32-bit float, interleaved, 48 kHz, XAudio's order: front left, front
right, centre, low frequency, back left, back right. Prints:
  - each window's RMS and peak per channel, in dBFS, and the loudness of the 5.1 itself;
  - over the whole span: the channels' correlations (is the back a copy of the front, is
    the centre the voices) and how much of the time each channel is silent;
  - each candidate stereo mixdown's integrated loudness (ITU-R BS.1770: K-weighted,
    gated), its peak, and how often it would exceed full scale at a given makeup gain.
--render instead builds tools/audio_host.cc with the device's own mixdown
(app/src/main/cpp/warden_audio_mix.cc), runs the recording through it the way the device
does - frames of 256 samples - and compares what the speakers and the headphones get:
loudness per window and over the whole, peaks, the balance across octaves, and how alike
the two ears are.
Numpy only (the K-weighting filters are applied as FFT convolutions of their impulse
responses, which die out within 0.2 s).
"""
import argparse
import math
import os
import pathlib
import struct
import subprocess
import sys

import numpy as np

RATE = 48000
ROOT = pathlib.Path(__file__).resolve().parent.parent
CPP = ROOT / "app" / "src" / "main" / "cpp"
BUILD = ROOT / "build" / "audio-check"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
NAMES = ["FL", "FR", "C", "LFE", "BL", "BR"]
M3DB = 1 / math.sqrt(2)

# Stereo mixdowns: (name, 2x6 matrix) - rows left and right, columns as NAMES.
MIXDOWNS = [
    ("xenia (the port until now)",
     [[1 / 2.5, 0, 0.5 / 2.5, 0, 1 / 2.5, 0], [0, 1 / 2.5, 0.5 / 2.5, 0, 0, 1 / 2.5]]),
    ("itu bs.775, no scaling",
     [[1, 0, M3DB, 0, M3DB, 0], [0, 1, M3DB, 0, 0, M3DB]]),
    ("itu bs.775 / 2.414 (never clips)",
     [[1 / 2.414, 0, M3DB / 2.414, 0, M3DB / 2.414, 0],
      [0, 1 / 2.414, M3DB / 2.414, 0, 0, M3DB / 2.414]]),
    ("sdl3 (DAORecomp on a stereo PC)",
     [[0.294545216, 0, 0.208181650, 0.090909100, 0.251818180, 0.154545456],
      [0, 0.294545216, 0.208181650, 0.090909100, 0.154545456, 0.251818180]]),
    ("android fold (aosp downmix effect)",
     [[1, 0, M3DB, M3DB, 1, 0], [0, 1, M3DB, M3DB, 0, 1]]),
]


def read_wav(path, channels_wanted=6):
    with open(path, "rb") as file:
        data = file.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        sys.exit("%s: not a WAV file" % path)
    at = 12
    channels = fmt = None
    while at + 8 <= len(data):
        chunk, size = data[at:at + 4], struct.unpack_from("<I", data, at + 4)[0]
        if chunk == b"fmt ":
            fmt, channels = struct.unpack_from("<HH", data, at + 8)
        elif chunk == b"data":
            body = data[at + 8:at + 8 + size]
            break
        at += 8 + size + (size & 1)
    else:
        sys.exit("%s: no data chunk" % path)
    if fmt != 3 or channels != channels_wanted:
        sys.exit("%s: format %s, %s channels - expected 32-bit float, %d channels"
                 % (path, fmt, channels, channels_wanted))
    width = 4 * channels
    frames = len(body) // width
    return np.frombuffer(body[:frames * width], dtype="<f4").reshape(
        frames, channels).astype(np.float64)


def biquad_impulse(stages, length=16384):
    """The impulse response of cascaded biquads ((b0, b1, b2), (1, a1, a2))."""
    signal = np.zeros(length)
    signal[0] = 1.0
    for b, a in stages:
        out = np.zeros(length)
        x1 = x2 = y1 = y2 = 0.0
        for n in range(length):
            x = signal[n]
            y = b[0] * x + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
            out[n] = y
            x2, x1, y2, y1 = x1, x, y1, y
        signal = out
    return signal


# BS.1770-4's K-weighting at 48 kHz: the head's shelf, then the RLB high-pass.
K_WEIGHTING = [((1.53512485958697, -2.69169618940638, 1.19839281085285),
                (1.0, -1.69065929318241, 0.73248077421585)),
               ((1.0, -2.0, 1.0), (1.0, -1.99004745483398, 0.99007225036621))]


def fft_filter(signal, impulse):
    """signal (frames x channels) convolved with impulse, same length."""
    n = signal.shape[0] + impulse.shape[0] - 1
    size = 1 << (n - 1).bit_length()
    spectrum = np.fft.rfft(impulse, size)
    out = np.fft.irfft(np.fft.rfft(signal, size, axis=0) * spectrum[:, None], size, axis=0)
    return out[:signal.shape[0]]


def loudness(weighted, gains):
    """Integrated loudness (LUFS) of K-weighted channels with BS.1770's channel gains;
    -inf if everything is gated out."""
    block, step = int(0.4 * RATE), int(0.1 * RATE)
    if weighted.shape[0] < block:
        return float("-inf")
    squares = weighted ** 2
    cumulative = np.vstack([np.zeros((1, squares.shape[1])), np.cumsum(squares, axis=0)])
    starts = np.arange(0, weighted.shape[0] - block + 1, step)
    means = (cumulative[starts + block] - cumulative[starts]) / block
    power = means @ np.asarray(gains, dtype=np.float64)
    with np.errstate(divide="ignore"):
        levels = -0.691 + 10 * np.log10(power)
    kept = power[levels > -70]
    if kept.size == 0:
        return float("-inf")
    relative = -0.691 + 10 * math.log10(kept.mean()) - 10
    with np.errstate(divide="ignore"):
        kept = kept[-0.691 + 10 * np.log10(kept) > relative]
    return -0.691 + 10 * math.log10(kept.mean()) if kept.size else float("-inf")


def db(value):
    return 20 * math.log10(value) if value > 0 else float("-inf")


def host_compiler():
    candidate = pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidate = candidate / "LLVM" / "bin" / ("clang-cl.exe" if os.name == "nt" else "clang++")
    if candidate.is_file():
        return candidate
    import shutil
    found = shutil.which("clang-cl") or shutil.which("clang++")
    return pathlib.Path(found) if found else None


def build_host():
    compiler = host_compiler()
    if not compiler:
        sys.exit("error: no clang-cl or clang++ found; see docs/TOOLCHAIN.md")
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("audio_host.exe" if os.name == "nt" else "audio_host")
    sources = [CPP / "warden_audio_mix.cc", ROOT / "tools" / "audio_host.cc"]
    if compiler.name.startswith("clang-cl"):
        args = [str(compiler), "/std:c++20", "/O2", "/EHsc", "/MT", "/W3",
                "-winsysroot", str(WINSYSROOT), f"/I{CPP}", f"/Fe{exe}", f"/Fo{BUILD}\\"]
    else:
        args = [str(compiler), "-std=c++20", "-O2", f"-I{CPP}", "-o", str(exe)]
    args += [str(s) for s in sources]
    result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-4000:], result.stderr[-4000:], file=sys.stderr)
        sys.exit("error: audio_host failed to build")
    return exe


def octave_balance(stereo, reference):
    """Per octave band, the power of `stereo` against `reference` in dB (both ears)."""
    size = 1 << 15
    count = min(stereo.shape[0], reference.shape[0]) // size
    edges = [62.5 * 2 ** k for k in range(9)]  # 62.5 Hz to 16 kHz
    freqs = np.fft.rfftfreq(size, 1 / RATE)
    totals = np.zeros((2, len(edges) - 1))
    window = np.hanning(size)[:, None]
    for block in range(count):
        for which, signal in enumerate((stereo, reference)):
            part = signal[block * size:(block + 1) * size] * window
            power = (np.abs(np.fft.rfft(part, axis=0)) ** 2).sum(axis=1)
            for band in range(len(edges) - 1):
                mask = (freqs >= edges[band]) & (freqs < edges[band + 1])
                totals[which, band] += power[mask].sum()
    return [(edges[b], edges[b + 1], 10 * math.log10(totals[0, b] / totals[1, b]))
            for b in range(len(edges) - 1) if totals[1, b] > 0 and totals[0, b] > 0]


def render(args):
    exe = build_host()
    speakers_path, headphones_path = BUILD / "speakers.wav", BUILD / "headphones.wav"
    result = subprocess.run([str(exe), args.dump, str(speakers_path), str(headphones_path)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("audio_host: " + result.stderr.strip())
    print(result.stdout.strip())
    outputs = [("speakers", read_wav(speakers_path, 2)), ("headphones", read_wav(headphones_path, 2))]
    impulse = biquad_impulse(K_WEIGHTING)
    weighted = [fft_filter(signal, impulse) for _, signal in outputs]
    print("\nper window: loudness LUFS (peak dBFS)")
    print("%7s  %-20s %-20s" % ("from s", "speakers", "headphones"))
    step = int(args.window * RATE)
    for start in range(0, outputs[0][1].shape[0], step):
        cells = []
        for (_, signal), k in zip(outputs, weighted):
            part = signal[start:start + step]
            cells.append("%6.1f (%6.1f)" % (loudness(k[start:start + step], [1.0, 1.0]),
                                            db(float(np.max(np.abs(part))))))
        print("%7.0f  %-20s %-20s" % (start / RATE, cells[0], cells[1]))
    print("\nover the recording:")
    for (name, signal), k in zip(outputs, weighted):
        left, right = signal[:, 0], signal[:, 1]
        correlation = float(np.corrcoef(left, right)[0, 1])
        over = float(np.mean(np.abs(signal) > 1.0)) * 100
        print("  %-10s %6.1f LUFS  peak %6.1f dBFS  %.4f%% of samples past full scale  "
              "left-right correlation %.2f" % (name, loudness(k, [1.0, 1.0]),
                                               db(float(np.max(np.abs(signal)))), over,
                                               correlation))
    print("\nheadphones against speakers, per octave (dB):")
    for low, high, value in octave_balance(outputs[1][1], outputs[0][1]):
        print("  %6.0f-%-6.0f Hz  %+5.1f" % (low, high, value))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump")
    parser.add_argument("--window", type=float, default=10.0, help="seconds per row")
    parser.add_argument("--from", dest="start", type=float, default=0.0)
    parser.add_argument("--to", dest="end", type=float, default=None)
    parser.add_argument("--target", type=float, default=-18.0,
                        help="LUFS a makeup gain is worked out for (default -18)")
    parser.add_argument("--render", action="store_true",
                        help="run the recording through the device's mixdown and compare "
                             "the speakers' output with the headphones'")
    args = parser.parse_args()
    if args.render:
        render(args)
        return

    audio = read_wav(args.dump)
    first = int(args.start * RATE)
    last = audio.shape[0] if args.end is None else min(audio.shape[0], int(args.end * RATE))
    audio = audio[first:last]
    audio[~np.isfinite(audio)] = 0.0
    print("%s: %.1f s from %.1f s, 6 channels" % (args.dump, audio.shape[0] / RATE, args.start))

    impulse = biquad_impulse(K_WEIGHTING)
    weighted = fft_filter(audio, impulse)
    gains_51 = [1.0, 1.0, 1.0, 0.0, 1.41, 1.41]

    print("\nper window: RMS dBFS (peak dBFS) per channel; the 5.1's loudness")
    print("%7s  " % "from s" + "  ".join("%-13s" % name for name in NAMES) + "  LUFS")
    step = int(args.window * RATE)
    for start in range(0, audio.shape[0], step):
        part = audio[start:start + step]
        cells = []
        for channel in range(6):
            rms = math.sqrt(float(np.mean(part[:, channel] ** 2)))
            peak = float(np.max(np.abs(part[:, channel])))
            cells.append("%5.1f (%5.1f)" % (db(rms), db(peak)) if peak > 0 else "    silent   ")
        level = loudness(weighted[start:start + step], gains_51)
        print("%7.0f  " % (args.start + start / RATE) + "  ".join(cells) + "  %5.1f" % level)

    print("\nover the span:")
    active = np.abs(audio) > 1e-6
    for channel in range(6):
        column = audio[:, channel]
        print("  %-3s  RMS %6.1f dBFS  peak %6.1f dBFS  sounding %5.1f%% of the time"
              % (NAMES[channel], db(math.sqrt(float(np.mean(column ** 2)))),
                 db(float(np.max(np.abs(column)))), 100 * float(np.mean(active[:, channel]))))
    std = audio.std(axis=0)
    usable = std > 0
    print("  correlations (1: the same signal; 0: unrelated):")
    print("        " + "".join("%7s" % name for name in NAMES))
    for row in range(6):
        cells = []
        for column in range(6):
            if usable[row] and usable[column]:
                value = float(np.mean((audio[:, row] - audio[:, row].mean()) *
                                      (audio[:, column] - audio[:, column].mean()))
                              / (std[row] * std[column]))
                cells.append("%7.2f" % value)
            else:
                cells.append("%7s" % "-")
        print("  %-5s " % NAMES[row] + "".join(cells))
    print("  the 5.1 as played on five speakers: %.1f LUFS" % loudness(weighted, gains_51))

    print("\nstereo mixdowns: integrated loudness, peak, and at the makeup gain to %.0f LUFS "
          "the share of samples past full scale" % args.target)
    for name, matrix in MIXDOWNS:
        matrix = np.asarray(matrix)
        stereo = audio @ matrix.T
        level = loudness(weighted @ matrix.T, [1.0, 1.0])
        peak = float(np.max(np.abs(stereo)))
        makeup = args.target - level
        over = float(np.mean(np.abs(stereo) * 10 ** (makeup / 20) > 1.0)) * 100
        print("  %-36s %6.1f LUFS  peak %6.1f dBFS  makeup %+5.1f dB -> %6.3f%% past full "
              "scale" % (name, level, db(peak), makeup, over))


if __name__ == "__main__":
    main()
