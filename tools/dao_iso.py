#!/usr/bin/env python3
"""
dao_iso.py - Xbox 360 XDVDFS (XGD2/XGD3) disc image reader.

Reads the game partition of an Xbox 360 disc image, lists its contents, and
extracts files. Also parses the XEX2 execution header so you can confirm a
disc's Media ID / Title ID before feeding it to the recompiler.

Verified against:
  Dragon Age - Origins (World) (En,Es,It).iso     media 186A1115  title 454108C0
  Dragon Age - Origins - Awakening (USA, Europe)  media 2E6F7560  title 454108C0

Usage:
  python tools/dao_iso.py info    <iso>
  python tools/dao_iso.py ls      <iso> [path] [-r] [--depth N]
  python tools/dao_iso.py extract <iso> <path-in-iso> <dest-file>
  python tools/dao_iso.py pull    <iso> <dir-in-iso> <dest-dir>
"""
import argparse
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Known game-partition base offsets: raw, ISO9660-ish, XGD2, XGD3 variants.
PARTITION_OFFSETS = (0x0, 0x10000, 0xFD90000, 0x18300000, 0x2080000)
ATTR_DIRECTORY = 0x10
XEX_HEADER_EXECUTION_ID = 0x00040006


class Xdvdfs:
    """Read-only view of the game partition of an Xbox 360 disc image."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.base = self._find_base()
        if self.base is None:
            raise ValueError("no XDVDFS volume found in %s" % path)
        self.f.seek(self.base + 0x10000 + len(MAGIC))
        self.root_sector, self.root_size = struct.unpack("<II", self.f.read(8))

    def _find_base(self):
        for off in PARTITION_OFFSETS:
            try:
                self.f.seek(off + 0x10000)
            except OSError:
                continue
            if self.f.read(len(MAGIC)) == MAGIC:
                return off
        return None

    def read_dir(self, sector, size):
        """Read one directory table. Entries form a binary tree of 4-byte words."""
        self.f.seek(self.base + sector * SECTOR)
        data = self.f.read(size)
        out, stack, seen = [], [0], set()
        while stack:
            p = stack.pop()
            if p in seen or p >= len(data) - 14:
                continue
            seen.add(p)
            left, right, start, fsize, attr, nlen = struct.unpack_from("<HHIIBB", data, p)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            name = data[p + 14:p + 14 + nlen].decode("ascii", "replace")
            if name:
                out.append((name, start, fsize, attr))
            if left:
                stack.append(left * 4)
            if right:
                stack.append(right * 4)
        return sorted(out, key=lambda e: e[0].lower())

    def resolve(self, path):
        """Resolve a '/'-separated path to (name, sector, size, attr), or None."""
        parts = [p for p in path.replace("\\", "/").split("/") if p]
        sector, size = self.root_sector, self.root_size
        entry = ("", sector, size, ATTR_DIRECTORY)
        for i, part in enumerate(parts):
            match = None
            for e in self.read_dir(sector, size):
                if e[0].lower() == part.lower():
                    match = e
                    break
            if match is None:
                return None
            entry = match
            if match[3] & ATTR_DIRECTORY:
                sector, size = match[1], match[2]
            elif i != len(parts) - 1:
                return None
        return entry

    def listdir(self, path=""):
        if path:
            e = self.resolve(path)
        else:
            e = ("", self.root_sector, self.root_size, ATTR_DIRECTORY)
        if e is None or not (e[3] & ATTR_DIRECTORY):
            return None
        return self.read_dir(e[1], e[2])

    def read_file(self, path, limit=None):
        e = self.resolve(path)
        if e is None or e[3] & ATTR_DIRECTORY:
            raise FileNotFoundError(path)
        self.f.seek(self.base + e[1] * SECTOR)
        return self.f.read(min(e[2], limit) if limit else e[2])


def parse_xex(blob):
    """Parse an XEX2 header, returning the execution-ID fields."""
    if blob[:4] != b"XEX2":
        return None
    module_flags, _pe, _res, _sec, opt_count = struct.unpack_from(">IIIII", blob, 4)
    info = {"module_flags": "0x%08X" % module_flags, "optional_headers": opt_count}
    for i in range(opt_count):
        key, val = struct.unpack_from(">II", blob, 0x18 + i * 8)
        if key == XEX_HEADER_EXECUTION_ID and val + 24 <= len(blob):
            media, ver, bver, title = struct.unpack_from(">IIII", blob, val)
            plat, exetype, disc, discs = struct.unpack_from(">BBBB", blob, val + 16)
            info.update(
                media_id="%08X" % media,
                title_id="%08X" % title,
                version=_ver(ver),
                base_version=_ver(bver),
                platform=plat,
                exe_type=exetype,
                disc="%d of %d" % (disc, discs),
            )
    return info


def _ver(v):
    return "%d.%d.%d.%d" % (v >> 28, (v >> 24) & 0xF, (v >> 8) & 0xFFFF, v & 0xFF)


def human(n):
    return "{:,}".format(n)


def cmd_info(a):
    iso = Xdvdfs(a.iso)
    print(os.path.basename(a.iso))
    print("  size                 %s bytes" % human(os.path.getsize(a.iso)))
    print("  game partition base  0x%X" % iso.base)
    info = parse_xex(iso.read_file("default.xex", limit=0x40000))
    if not info:
        print("  default.xex          !! not an XEX2 image")
        return 1
    print("  default.xex:")
    for k in ("media_id", "title_id", "version", "base_version", "disc",
              "platform", "exe_type", "optional_headers"):
        if k in info:
            print("    %-18s %s" % (k, info[k]))
    return 0


def cmd_ls(a):
    iso = Xdvdfs(a.iso)

    def walk(path, depth):
        items = iso.listdir(path)
        if items is None:
            print("  (not a directory: %s)" % path)
            return
        pad = "  " * (depth + 1)
        for name, _start, size, attr in items:
            if attr & ATTR_DIRECTORY:
                print("%s%s/" % (pad, name))
                if a.recursive and depth + 1 < a.depth:
                    walk(("%s/%s" % (path, name)).lstrip("/"), depth + 1)
            else:
                print("%s%-42s %14s" % (pad, name, human(size)))

    print("%s:/%s" % (os.path.basename(a.iso), a.path))
    walk(a.path, 0)
    return 0


def cmd_extract(a):
    iso = Xdvdfs(a.iso)
    data = iso.read_file(a.src)
    parent = os.path.dirname(os.path.abspath(a.dest))
    os.makedirs(parent, exist_ok=True)
    with open(a.dest, "wb") as fh:
        fh.write(data)
    print("extracted %s -> %s (%s bytes)" % (a.src, a.dest, human(len(data))))
    return 0


def cmd_pull(a):
    iso = Xdvdfs(a.iso)
    count = [0, 0]

    def walk(path, dest):
        items = iso.listdir(path)
        if items is None:
            return
        os.makedirs(dest, exist_ok=True)
        for name, _start, size, attr in items:
            sub = ("%s/%s" % (path, name)).lstrip("/")
            if attr & ATTR_DIRECTORY:
                walk(sub, os.path.join(dest, name))
            else:
                with open(os.path.join(dest, name), "wb") as fh:
                    fh.write(iso.read_file(sub))
                count[0] += 1
                count[1] += size
                print("  %s  (%s)" % (sub, human(size)))

    walk(a.src, a.dest)
    print("pulled %d files, %s bytes -> %s" % (count[0], human(count[1]), a.dest))
    return 0


def main():
    ap = argparse.ArgumentParser(description="Xbox 360 XDVDFS disc image reader")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info", help="show disc + XEX identity")
    p.add_argument("iso")
    p.set_defaults(fn=cmd_info)

    p = sub.add_parser("ls", help="list a directory")
    p.add_argument("iso")
    p.add_argument("path", nargs="?", default="")
    p.add_argument("--recursive", "-r", action="store_true")
    p.add_argument("--depth", type=int, default=3)
    p.set_defaults(fn=cmd_ls)

    p = sub.add_parser("extract", help="extract one file")
    p.add_argument("iso")
    p.add_argument("src")
    p.add_argument("dest")
    p.set_defaults(fn=cmd_extract)

    p = sub.add_parser("pull", help="extract a directory tree")
    p.add_argument("iso")
    p.add_argument("src")
    p.add_argument("dest")
    p.set_defaults(fn=cmd_pull)

    a = ap.parse_args()
    try:
        return a.fn(a)
    except (ValueError, FileNotFoundError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
