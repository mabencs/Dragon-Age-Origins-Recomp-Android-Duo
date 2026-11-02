#!/usr/bin/env python3
"""
make_test_texture_mod.py - a PC-style texture mod for testing the converter (PLAN 10.13).

Writes <out>/<name>/mod.toml and packages/core/override/<texture>.dds (and its half- and
quarter-size copies, <texture>l2 and <texture>l3, as the game names its distant levels):
solid-colour A8R8G8B8 textures with full mip chains, the kind a PC texture pack holds.
Push the folder to <app storage>/mods/ on the Thor, choose the mod on the launcher's Mods
page, then "Convert for this version". The default is Duncan's body, magenta: he stands
next to the hero where the ostagar route (tools/route.sh) lands at the King's Camp.

Usage:
  python tools/make_test_texture_mod.py build/test-mod
  python tools/make_test_texture_mod.py build/test-mod --texture pm_bdy_duna_0d --size 256 --rgb ff00ff
"""
import argparse
import struct
from pathlib import Path


def dds(size, bgra):
    levels = size.bit_length()
    # DDSD_CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT | MIPMAPCOUNT
    head = struct.pack("<4s7I44x", b"DDS ", 124, 0x2100F, size, size, size * 4, 0, levels)
    # DDPF_RGB | ALPHAPIXELS, 32 bits, A8R8G8B8's masks
    head += struct.pack("<2I4s5I", 32, 0x41, bytes(4), 32,
                        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    # DDSCAPS_TEXTURE | COMPLEX | MIPMAP
    head += struct.pack("<4I4x", 0x401008, 0, 0, 0)
    body = bytearray()
    s = size
    while s >= 1:
        body += bgra * (s * s)
        s //= 2
    return head + bytes(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("out", type=Path)
    parser.add_argument("--name", default="ConvertTest")
    parser.add_argument("--texture", default="pm_bdy_duna_0d")
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--rgb", default="ff00ff")
    args = parser.parse_args()
    r, g, b = bytes.fromhex(args.rgb)
    bgra = bytes([b, g, r, 0xFF])
    mod = args.out / args.name
    override = mod / "packages" / "core" / "override"
    override.mkdir(parents=True, exist_ok=True)
    (mod / "mod.toml").write_text(f'name = "{args.name}"\nversion = "1"\n', encoding="utf-8")
    for suffix, size in (("", args.size), ("l2", args.size // 2), ("l3", args.size // 4)):
        path = override / f"{args.texture}{suffix}.dds"
        path.write_bytes(dds(size, bgra))
        print(f"  {path}  {size}x{size}")


if __name__ == "__main__":
    main()
