#!/usr/bin/env python3
"""
sample_screen.py - report what is actually on the handheld's screen.

Phase 4.2a. "The presenter reported a swapchain and painted a frame" and "something
appeared on the panel" are different claims, and the first was true while the second
was false: a `setBackgroundColor` on the SurfaceView drew over the surface, so the
renderer presented into a layer nothing could see. Nothing in the logs said so.

So the bring-up capture samples pixels. No image library: PNG with filtering is about
forty lines to decode, and adding a dependency to the device loop for this would be a
poor trade.

Usage:
  adb exec-out screencap -p > shot.png
  python tools/appbringup/sample_screen.py shot.png
"""
import pathlib
import struct
import sys
import zlib

# Sampled away from the bring-up overlay, which sits in the top-left corner and grows
# downwards as the report gets longer. Sampling into it reads the overlay's own colour
# - warden_iron at 75% over whatever is behind it - which is evidence about the overlay
# and not about the surface. The right-hand columns stay clear of it at any report
# length, which is why the overlay is pinned and WRAP_CONTENT rather than full width.
SAMPLE_FRACTIONS = (0.5, 0.75, 0.99)
COLUMN_FRACTIONS = (0.8, 0.9, 0.97)


def decode(path):
    """Return (width, height, channels, rows) with rows as un-filtered bytes."""
    data = pathlib.Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\x08"[:8] and data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"error: {path} is not a PNG")

    pos = 8
    width = height = None
    bit_depth = color_type = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        chunk = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if chunk == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", body[:10])
        elif chunk == b"IDAT":
            idat += body
        elif chunk == b"IEND":
            break
        pos += 12 + length

    if width is None:
        raise SystemExit("error: no IHDR")
    if bit_depth != 8:
        raise SystemExit(f"error: only 8-bit PNGs are handled, got {bit_depth}")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    if not channels or color_type == 3:
        raise SystemExit(f"error: colour type {color_type} not handled")

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    for y in range(height):
        offset = y * (stride + 1)
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        # PNG filters, per the spec. Paeth is the only one that needs thought.
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + up) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                predictor = left + up - up_left
                deltas = (abs(predictor - left), abs(predictor - up),
                          abs(predictor - up_left))
                if deltas[0] <= deltas[1] and deltas[0] <= deltas[2]:
                    chosen = left
                elif deltas[1] <= deltas[2]:
                    chosen = up
                else:
                    chosen = up_left
                line[i] = (line[i] + chosen) & 0xFF
        rows.append(bytes(line))
        previous = line
    return width, height, channels, rows


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    width, height, channels, rows = decode(sys.argv[1])
    print(f"  {'screenshot':<34} {width}x{height}")

    samples = []
    for fraction in SAMPLE_FRACTIONS:
        y = min(height - 1, int(height * fraction))
        row = rows[y]
        pixels = []
        for column_fraction in COLUMN_FRACTIONS:
            x = min(width - 1, int(width * column_fraction))
            pixel = tuple(row[x * channels:x * channels + 3])
            pixels.append(pixel)
            samples.append(pixel)
        rendered = "  ".join("#%02X%02X%02X" % p for p in pixels)
        print(f"  row {y:<30} {rendered}")

    # The specific colour that means "the renderer is presenting into a hidden
    # layer": the app's own windowBackground, warden_iron.
    window_background = (0x14, 0x11, 0x0F)
    if all(pixel == window_background for pixel in samples):
        print(f"  {'VERDICT':<34} window background only - surface NOT visible")
        return 1
    if all(sum(pixel) <= 12 for pixel in samples):
        print(f"  {'VERDICT':<34} black - the presented letterbox, surface visible")
        return 0
    print(f"  {'VERDICT':<34} surface visible, non-black content")
    return 0


if __name__ == "__main__":
    sys.exit(main())
