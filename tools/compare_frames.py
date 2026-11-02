#!/usr/bin/env python3
"""
compare_frames.py - the port's frame beside DAORecomp's (PLAN 3.3b).

  python tools/compare_frames.py <port.png> <reference.png> <out.png> [--gain N]

Both are the guest output - the port's guest-output-N.png (warden_gpu.cc) and the
reference's capture_N.png (tools/dao_reference.py), the presenter's CaptureGuestOutput
on each side - brought to the same size (the smaller of the two). The sheet shows the
port, the reference, and their difference amplified N times (default 4), and prints
the mean difference and the share of pixels that differ by more than 24 of 255 - which
says something only where the scene holds still in both.
"""
import argparse
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("reference")
    ap.add_argument("out")
    ap.add_argument("--gain", type=float, default=4.0)
    a = ap.parse_args()
    port = Image.open(a.port).convert("RGB")
    ref = Image.open(a.reference).convert("RGB")
    w, h = min(port.width, ref.width), min(port.height, ref.height)
    port = port.resize((w, h), Image.LANCZOS) if port.size != (w, h) else port
    ref = ref.resize((w, h), Image.LANCZOS) if ref.size != (w, h) else ref
    diff = ImageChops.difference(port, ref)
    mean = sum(ImageStat.Stat(diff).mean) / 3
    gray = diff.convert("L")
    differing = sum(gray.histogram()[25:]) / float(w * h)
    shown = diff.point(lambda v: min(255, int(v * a.gain)))
    sheet = Image.new("RGB", (w * 3, h + 24), (24, 24, 28))
    for i, (im, label) in enumerate(((port, "port (Vulkan, the Thor)"),
                                     (ref, "reference (DAORecomp, D3D12)"),
                                     (shown, "difference x%g" % a.gain))):
        sheet.paste(im, (i * w, 24))
        ImageDraw.Draw(sheet).text((i * w + 6, 5), label, fill=(230, 220, 160),
                                   font=ImageFont.load_default())
    sheet.save(a.out)
    print("%s: mean difference %.1f of 255, %.1f%% of pixels differ by more than 24" %
          (a.out, mean, differing * 100))
    return 0


if __name__ == "__main__":
    sys.exit(main())
