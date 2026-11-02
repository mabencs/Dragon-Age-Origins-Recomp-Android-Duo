#!/usr/bin/env python3
"""
dao_xds.py - decode the Xbox 360 build's .xds textures (the UI atlas pages) to PNG.

An .xds file is the texture data as the GPU reads it, followed by 52 bytes whose last
24 are the Xenos texture fetch constant: the format, the size, whether the data is
tiled, and its byte order. The UI atlases (guiexport.erf, atl_*_dxt5_NN.xds) are DXT1
or DXT5, tiled, with 16-bit words byte-swapped (k_8in16).

Decoding: untile the 4x4 blocks with Xenia's Tiled2D address (texture_address.h), swap
each 16-bit word, then let Pillow decode the linear DXT data through a DDS header.

Usage:
  python tools/dao_xds.py info  <xds>...
  python tools/dao_xds.py png   <xds> <out.png>
  python tools/dao_xds.py cells <xds> <atlas.xml> <out-dir>   one PNG per atlas cell
"""
import argparse
import io
import os
import re
import struct
import sys

from PIL import Image

TRAILER = 52
# Xenos texture formats (xenos.h, TextureFormat) this decoder handles.
FORMATS = {0x12: ("DXT1", 8), 0x13: ("DXT3", 16), 0x14: ("DXT5", 16)}


class Fetch:
    """The texture fetch constant at the end of an .xds file."""

    def __init__(self, data):
        if len(data) < TRAILER:
            raise ValueError("too short for an .xds trailer")
        d = struct.unpack(">6I", data[-24:])
        self.words = d
        self.tiled = bool(d[0] >> 31)
        self.pitch = ((d[0] >> 22) & 0x1FF) * 32
        self.format = d[1] & 0x3F
        self.endian = (d[1] >> 6) & 0x3
        self.width = (d[2] & 0x1FFF) + 1
        self.height = ((d[2] >> 13) & 0x1FFF) + 1
        self.data_size = len(data) - TRAILER

    def describe(self):
        name = FORMATS.get(self.format, ("format 0x%02X" % self.format, 0))[0]
        return "%dx%d %s, %s, endian %d, pitch %d, %d bytes" % (
            self.width, self.height, name, "tiled" if self.tiled else "linear",
            self.endian, self.pitch, self.data_size)


def tiled_2d(x, y, pitch_aligned, bytes_per_block_log2):
    """Xenia's texture_address::Tiled2D: the byte offset of block (x, y)."""
    outer = (((y >> 5) * (pitch_aligned >> 5)) + (x >> 5)) << 6
    inner = (((y >> 1) & 0b111) << 3) | (x & 0b111)
    oib = (outer | inner) << bytes_per_block_log2
    bank = (y >> 4) & 1
    pipe = ((x >> 3) & 0b11) ^ (((y >> 3) & 1) << 1)
    return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oib & 0b1111) | \
        (((oib >> 4) & 1) << 5) | (((oib >> 5) & 0b111) << 8) | ((oib >> 8) << 12)


def swap_words(blob, endian):
    if endian == 1:  # k_8in16
        b = bytearray(blob)
        b[0::2], b[1::2] = blob[1::2], blob[0::2]
        return bytes(b)
    if endian == 2:  # k_8in32
        b = bytearray(len(blob))
        for i in range(4):
            b[i::4] = blob[3 - i::4]
        return bytes(b)
    if endian == 3:  # k_16in32
        b = bytearray(len(blob))
        b[0::4], b[1::4], b[2::4], b[3::4] = blob[2::4], blob[3::4], blob[0::4], blob[1::4]
        return bytes(b)
    return blob


def linear_blocks(data, fetch):
    """The DXT blocks in row order, untiled and in the PC's byte order."""
    name, block = FORMATS[fetch.format]
    bw, bh = (fetch.width + 3) // 4, (fetch.height + 3) // 4
    pitch_blocks = max(fetch.pitch // 4, (bw + 31) & ~31)
    log2 = block.bit_length() - 1
    out = bytearray(bw * bh * block)
    for y in range(bh):
        for x in range(bw):
            src = tiled_2d(x, y, pitch_blocks, log2) if fetch.tiled else (y * pitch_blocks + x) * block
            dst = (y * bw + x) * block
            out[dst:dst + block] = data[src:src + block]
    return swap_words(bytes(out), fetch.endian), name


def decode(path):
    data = open(path, "rb").read()
    fetch = Fetch(data)
    if fetch.format not in FORMATS:
        raise ValueError("%s: %s - not a DXT format this decoder reads" % (path, fetch.describe()))
    blocks, fourcc = linear_blocks(data, fetch)
    # "DDS ", then DDS_HEADER: size, flags (caps, height, width, pixel format, linear
    # size), height, width, linear size, depth, mip count, 11 reserved; the pixel
    # format: size, flags (FourCC), FourCC, bit count, four masks; caps 1-4, reserved.
    header = struct.pack("<4s7I44s2I4s5I5I", b"DDS ", 124, 0x81007, fetch.height,
                         fetch.width, len(blocks), 0, 0, b"\0" * 44, 32, 0x4,
                         fourcc.encode(), 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    assert len(header) == 128
    image = Image.open(io.BytesIO(header + blocks))
    image.load()
    return image.convert("RGBA"), fetch


def atlas_pages(xml_path):
    """{page name: [(cell name, x, y, w, h)]} of an atlas XML, in pixels of each page.
    Page names are lower case without their extension (atl_shared_dxt5_00); one XML can
    describe several pages."""
    text = open(xml_path, "r", encoding="utf-8", errors="replace").read()
    pages = {}
    for block in re.finditer(r'<AtlasTexture\s+Name="([^"]+)"(.*?)</AtlasTexture>', text, re.S):
        page = block.group(1).rsplit(".", 1)[0].lower()
        cells = pages.setdefault(page, [])
        # Attributes in any order (some atlases add OffsetAndScale_V2).
        for element in re.finditer(r"<SourceTexture\b([^>]*)/?>", block.group(2)):
            attrs = dict(re.findall(r'(\w+)="([^"]*)"', element.group(1)))
            ox, oy, _sx, _sy = (float(v) for v in attrs["OffsetAndScale"].split())
            aw, ah = (int(v) for v in attrs["AtlasSize"].split())
            w, h = (int(v) for v in attrs["OriginalImageSize"].split())
            cells.append((attrs["Name"], round(ox * aw), round(oy * ah), w, h))
    return pages


def atlas_cells(xml_path, xds_path):
    """The cells of the page an .xds file holds: (name, x, y, w, h)."""
    page = os.path.basename(xds_path).rsplit(".", 1)[0].lower()
    return atlas_pages(xml_path).get(page, [])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("xds", nargs="+")
    p = sub.add_parser("png")
    p.add_argument("xds")
    p.add_argument("out")
    p = sub.add_parser("cells")
    p.add_argument("xds")
    p.add_argument("xml")
    p.add_argument("out_dir")
    a = ap.parse_args()
    if a.cmd == "info":
        for path in a.xds:
            print("%-32s %s" % (os.path.basename(path), Fetch(open(path, "rb").read()).describe()))
    elif a.cmd == "png":
        image, fetch = decode(a.xds)
        image.save(a.out)
        print("%s -> %s" % (fetch.describe(), a.out))
    elif a.cmd == "cells":
        image, _ = decode(a.xds)
        os.makedirs(a.out_dir, exist_ok=True)
        for name, x, y, w, h in atlas_cells(a.xml, a.xds):
            image.crop((x, y, x + w, y + h)).save(os.path.join(a.out_dir, name.rsplit(".", 1)[0] + ".png"))
        print("cells -> %s" % a.out_dir)


if __name__ == "__main__":
    sys.exit(main())
