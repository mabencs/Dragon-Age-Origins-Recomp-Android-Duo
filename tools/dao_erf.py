#!/usr/bin/env python3
"""
dao_erf.py - Dragon Age: Origins ERF V2.1 archive reader (Xbox 360 build).

The Xbox 360 disc stores game content in ERF archives. Unlike the PC release
(which uses ERF V2.0 with UTF-16 names), the 360 build uses "ERF V2.1" with
little-endian fields and 32-byte ASCII names. Entries may be zlib-compressed.

Layout:
  0x00  char[8]   "ERF V2.1"
  0x08  byte[8]   reserved / zero
  0x10  uint32    file count          (little-endian)
  0x14  uint32    reserved
  0x18  uint16    build day / misc
  0x1C  uint32    0xFFFF0000
  0x20  entries[] 44 bytes each:
          char[32] name (ASCII, NUL padded)
          uint32   offset
          uint32   packed length
          uint32   unpacked length     (!= packed  =>  zlib stream)

Usage:
  python tools/dao_erf.py ls      <erf> [--filter TEXT]
  python tools/dao_erf.py extract <erf> <dest-dir> [--filter TEXT]
  python tools/dao_erf.py atlas   <erf>
"""
import argparse
import os
import re
import struct
import sys
import zlib

HEADER_SIZE = 0x20
ENTRY_SIZE = 44
NAME_SIZE = 32
SIGNATURE = b"ERF V2.1"
# ERF V2.0 (the DLC packages): the signature in UTF-16, 72-byte entries.
SIGNATURE_V20 = "ERF V2.0".encode("utf-16-le")
ENTRY_SIZE_V20 = 72
NAME_SIZE_V20 = 64


class Erf:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        head = self.f.read(HEADER_SIZE)
        if head[:16] == SIGNATURE_V20:
            # The downloadable content's packages (PLAN 10.10, the Ultimate Edition's
            # disc 2 and the Awakening disc's add-in) hold ERF V2.0 archives, the PC
            # release's layout: a UTF-16 signature, little-endian fields, 72-byte
            # entries of a 32-character UTF-16 name, offset and length, nothing
            # compressed.
            self.version = "2.0"
            self.count = struct.unpack_from("<I", head, 0x10)[0]
            self.f.seek(HEADER_SIZE)
            index = self.f.read(self.count * ENTRY_SIZE_V20)
            self.entries = []
            for i in range(self.count):
                p = i * ENTRY_SIZE_V20
                raw = index[p:p + NAME_SIZE_V20].decode("utf-16-le", "replace")
                name = raw.split("\x00")[0]
                offset, length = struct.unpack_from("<II", index, p + NAME_SIZE_V20)
                self.entries.append((name, offset, length, length))
            return
        if head[:8] != SIGNATURE:
            raise ValueError("%s: not an ERF V2.1 archive (got %r)" % (path, head[:8]))
        self.version = "2.1"
        self.count = struct.unpack_from("<I", head, 0x10)[0]
        self.f.seek(HEADER_SIZE)
        index = self.f.read(self.count * ENTRY_SIZE)
        self.entries = []
        for i in range(self.count):
            p = i * ENTRY_SIZE
            name = index[p:p + NAME_SIZE].split(b"\x00")[0].decode("ascii", "replace")
            offset, packed, unpacked = struct.unpack_from("<III", index, p + NAME_SIZE)
            self.entries.append((name, offset, packed, unpacked))

    def read(self, entry):
        _name, offset, packed, unpacked = entry
        self.f.seek(offset)
        blob = self.f.read(packed)
        if packed == unpacked:
            return blob
        try:
            return zlib.decompress(blob)
        except zlib.error:
            # Some entries are raw deflate without a zlib wrapper.
            return zlib.decompressobj(-zlib.MAX_WBITS).decompress(blob)

    def select(self, needle=None):
        if not needle:
            return list(self.entries)
        n = needle.lower()
        return [e for e in self.entries if n in e[0].lower()]


def human(n):
    return "{:,}".format(n)


def cmd_ls(a):
    erf = Erf(a.erf)
    rows = sorted(erf.select(a.filter), key=lambda e: e[0].lower())
    print("%s - %d entries (%d shown)" % (os.path.basename(a.erf), erf.count, len(rows)))
    for name, _off, packed, unpacked in rows:
        kind = "zlib" if packed != unpacked else "raw "
        print("  %-36s %s %12s -> %12s" % (name, kind, human(packed), human(unpacked)))
    return 0


def cmd_extract(a):
    erf = Erf(a.erf)
    rows = erf.select(a.filter)
    os.makedirs(a.dest, exist_ok=True)
    total = 0
    for entry in rows:
        data = erf.read(entry)
        with open(os.path.join(a.dest, entry[0]), "wb") as fh:
            fh.write(data)
        total += len(data)
        print("  %-36s %12s bytes" % (entry[0], human(len(data))))
    print("extracted %d files, %s bytes -> %s" % (len(rows), human(total), a.dest))
    return 0


ATLAS_RE = re.compile(
    r'Name="([^"]+)"[^>]*?OffsetAndScale="([^"]+)"[^>]*?'
    r'AtlasSize="(\d+) (\d+)"[^>]*?OriginalImageSize="(\d+) (\d+)"'
)


def cmd_atlas(a):
    """Dump every atlas layout in the archive: cell name, UV rect, pixel size.

    This is how the glyph system locates button-prompt art: the UI is Scaleform
    GFx, its bitmaps are atlased, and each atlas ships a plain-XML descriptor.
    """
    erf = Erf(a.erf)
    for entry in sorted(erf.select("_dat.xml"), key=lambda e: e[0].lower()):
        xml = erf.read(entry).decode("utf-8", "replace")
        page = re.search(r'AtlasTexture Name="([^"]+)"', xml)
        print("== %s  (page %s)" % (entry[0], page.group(1) if page else "?"))
        for m in ATLAS_RE.finditer(xml):
            name, uv, aw, ah, ow, oh = m.groups()
            print("   %-40s %-36s %sx%s on %sx%s" % (name, uv, ow, oh, aw, ah))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Dragon Age: Origins ERF V2.1 reader")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("ls", help="list archive contents")
    p.add_argument("erf")
    p.add_argument("--filter")
    p.set_defaults(fn=cmd_ls)

    p = sub.add_parser("extract", help="extract (and decompress) entries")
    p.add_argument("erf")
    p.add_argument("dest")
    p.add_argument("--filter")
    p.set_defaults(fn=cmd_extract)

    p = sub.add_parser("atlas", help="dump atlas layouts (glyph hunting)")
    p.add_argument("erf")
    p.set_defaults(fn=cmd_atlas)

    a = ap.parse_args()
    try:
        return a.fn(a)
    except (ValueError, FileNotFoundError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
