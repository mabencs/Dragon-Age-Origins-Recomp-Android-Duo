#!/usr/bin/env python3
"""recent_draws.py - decode the draws logged before a resolve (BUG-052).

With files/hack-resolve-dump holding a destination pitch (RB_COPY_DEST_PITCH, hex, as the
"GPU: resolve to" lines print it), every resolve with that pitch is dumped and logs the
draws since the previous one (up to the last 64): a "GPU: drawn before the resolve to"
line per draw, then its shader constants. This prints them per resolve - the vertex and
pixel shader hashes, the vertex count, the render target's pitch and EDRAM bases, the
depth test (zE = enabled, W = writes, f = function), up to six textures (address, format,
size) and the vertex shader constants read / zero vectors / non-finite.

    python tools/recent_draws.py build/run<N>.log [--constants] [--pitch 160]

--constants also prints the pixel shader constants listed (as floats); --pitch keeps
only the draws into render targets of that pitch (the portraits' is 160).
"""
import re
import struct
import sys

DRAW = re.compile(
    r"(\d\d:\d\d:\d\d\.\d+) .*GPU: drawn before the resolve to ([0-9A-F]{8}): VS (\w+) PS (\w+) "
    r"prim (\d+) count (\d+) surface ([0-9A-F]{8}) color ([0-9A-F]{8}) depth ([0-9A-F]{8}) "
    r"dc ([0-9A-F]{8}) mask ([0-9A-F]+) tex (.*)$")
CONSTANTS = re.compile(
    r"GPU: drawn before the resolve to [0-9A-F]{8}: VS constants (\d+) read, (\d+) zero, "
    r"(\d+) non-finite, sum ([0-9A-F]+); PS constants (\d+) read, (\d+) zero:(.*)$")
TRIPLE = re.compile(r"\[(\w+) (\w+) (\w+)\]")
CONSTANT = re.compile(r"c(\d+)=\(([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8})\)")
FORMATS = {2: "8", 4: "565", 6: "8888", 7: "2_10_10_10", 10: "8_8", 15: "4444", 18: "DXT1",
           19: "DXT2_3", 20: "DXT4_5", 26: "16_16_16_16F", 49: "DXN"}


def texture(d0, d1, d2):
    d0, d1, d2 = int(d0, 16), int(d1, 16), int(d2, 16)
    if not d0 and not d1:
        return None
    fmt = d1 & 0x3F
    return "@%08X %s %dx%d" % ((d1 >> 12) << 12, FORMATS.get(fmt, "fmt%d" % fmt),
                               (d2 & 0x1FFF) + 1, ((d2 >> 13) & 0x1FFF) + 1)


def as_float(bits):
    return struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    show_constants = "--constants" in args
    pitch_filter = None
    if "--pitch" in args:
        pitch_filter = int(args[args.index("--pitch") + 1])
    path = args[0]
    current, keep = None, False
    for raw in open(path, encoding="utf-8", errors="replace"):
        m = DRAW.search(raw)
        if m:
            (t, dest, vs, ps, prim, count, surface, color, depth, dc, mask, texs) = m.groups()
            if (dest, t[:8]) != current:
                print("\n== before the resolve to %s (%s)" % (dest, t))
                current = (dest, t[:8])
            s, c, d, z = (int(surface, 16), int(color, 16), int(depth, 16), int(dc, 16))
            keep = pitch_filter is None or (s & 0x3FFF) == pitch_filter
            if keep:
                textures = [x for x in (texture(*tr) for tr in TRIPLE.findall(texs)) if x]
                print("  VS %s PS %s prim %s n %5s pitch %4d msaa %d cbase %4d dbase %4d z%s%s f%d mask %s | %s" % (
                    vs, ps, prim, count, s & 0x3FFF, (s >> 16) & 3, c & 0xFFF, d & 0xFFF,
                    "E" if z & 2 else "-", "W" if z & 4 else "-", (z >> 4) & 7, mask,
                    " | ".join(textures) or "-"))
            continue
        k = CONSTANTS.search(raw)
        if k and keep:
            print("      VS constants %s read, %s zero, %s non-finite, sum %s; PS constants %s read, %s zero" % k.groups()[:6])
            if show_constants:
                for cm in CONSTANT.finditer(k.group(7)):
                    print("        c%-3s (%s)" % (cm.group(1), " ".join(
                        "%9.4g" % as_float(cm.group(i)) for i in range(2, 6))))


if __name__ == "__main__":
    main()
