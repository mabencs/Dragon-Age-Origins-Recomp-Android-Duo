#!/usr/bin/env python3
"""
verify_assets.py - Check that local disc images and title updates are the
editions this port actually needs, before you spend an hour recompiling.

The recompiler is built against one specific disc revision. A different
regional press has a different Media ID, a different executable layout, and
the generated function database will not match it. This script tells you
straight away whether what you have is usable.

Requirements for this project:
  Base game   Title ID 454108C0, Media ID 186A1115  (the En/Es/It "World" press)
  Title update  TU7 preferred (TU6 also acceptable), matching Media ID 186A1115

Usage:
  python tools/verify_assets.py [directory]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dao_iso import Xdvdfs, parse_xex  # noqa: E402

REQUIRED_TITLE_ID = "454108C0"
REQUIRED_MEDIA_ID = "186A1115"
PREFERRED_TU = (6, 7)

STFS_MAGICS = (b"LIVE", b"PIRS", b"CON ")
OFF_CONTENT_TYPE = 0x344
OFF_MEDIA_ID = 0x354
OFF_TITLE_ID = 0x360
OFF_DISPLAY_NAME = 0x411

OK = "[ OK ]"
WARN = "[WARN]"
BAD = "[FAIL]"


def read_stfs(path):
    """Parse the identifying fields out of an Xbox 360 STFS container."""
    with open(path, "rb") as fh:
        head = fh.read(0x1000)
    if len(head) < 0x500 or head[:4] not in STFS_MAGICS:
        return None
    name = head[OFF_DISPLAY_NAME:OFF_DISPLAY_NAME + 0x100]
    name = name.decode("utf-16-be", "ignore").split("\x00")[0].strip()
    return {
        "magic": head[:4].decode("ascii", "replace").strip(),
        "content_type": "%08X" % struct.unpack_from(">I", head, OFF_CONTENT_TYPE)[0],
        "media_id": "%08X" % struct.unpack_from(">I", head, OFF_MEDIA_ID)[0],
        "title_id": "%08X" % struct.unpack_from(">I", head, OFF_TITLE_ID)[0],
        "display_name": name,
    }


def tu_number(display_name):
    """Pull the title-update number out of a display name like '... Title Update #7'."""
    if "#" in display_name:
        tail = display_name.rsplit("#", 1)[1].strip()
        digits = ""
        for ch in tail:
            if ch.isdigit():
                digits += ch
            else:
                break
        if digits:
            return int(digits)
    return None


def looks_like_stfs(path):
    try:
        with open(path, "rb") as fh:
            return fh.read(4) in STFS_MAGICS
    except OSError:
        return False


def check_iso(path, findings):
    print("\nDisc image: %s" % os.path.basename(path))
    print("  size %s bytes" % "{:,}".format(os.path.getsize(path)))
    try:
        iso = Xdvdfs(path)
    except ValueError as e:
        print("  %s %s" % (BAD, e))
        findings.append((BAD, os.path.basename(path), "not a readable Xbox 360 disc image"))
        return
    print("  game partition base 0x%X" % iso.base)
    try:
        info = parse_xex(iso.read_file("default.xex", limit=0x40000))
    except FileNotFoundError:
        print("  %s no default.xex in the game partition" % BAD)
        findings.append((BAD, os.path.basename(path), "no default.xex"))
        return
    if not info:
        print("  %s default.xex is not an XEX2 image" % BAD)
        findings.append((BAD, os.path.basename(path), "default.xex not XEX2"))
        return

    print("  title id  %s" % info.get("title_id"))
    print("  media id  %s" % info.get("media_id"))
    print("  version   %s (base %s)" % (info.get("version"), info.get("base_version")))

    title_ok = info.get("title_id") == REQUIRED_TITLE_ID
    media_ok = info.get("media_id") == REQUIRED_MEDIA_ID
    label = os.path.basename(path)

    if title_ok and media_ok:
        print("  %s Supported base game. This is the disc the recompiler targets." % OK)
        findings.append((OK, label, "base game, media %s - use this" % REQUIRED_MEDIA_ID))
    elif title_ok:
        print("  %s Dragon Age: Origins, but media id %s != required %s."
              % (WARN, info.get("media_id"), REQUIRED_MEDIA_ID))
        print("         A different press or the Awakening disc. The recompiler")
        print("         config will not match this executable. Archive it for now.")
        findings.append((WARN, label, "media %s - not the targeted press" % info.get("media_id")))
    else:
        print("  %s Not Dragon Age: Origins (title id %s)." % (BAD, info.get("title_id")))
        findings.append((BAD, label, "wrong title id %s" % info.get("title_id")))


def check_tu(path, findings):
    meta = read_stfs(path)
    label = os.path.basename(path)
    print("\nTitle update: %s" % label)
    print("  size %s bytes" % "{:,}".format(os.path.getsize(path)))
    if not meta:
        print("  %s not a recognisable STFS container" % BAD)
        findings.append((BAD, label, "not an STFS container"))
        return
    print("  container %s  content type %s" % (meta["magic"], meta["content_type"]))
    print("  name      %s" % meta["display_name"])
    print("  title id  %s" % meta["title_id"])
    print("  media id  %s" % meta["media_id"])

    num = tu_number(meta["display_name"])
    if meta["title_id"] != REQUIRED_TITLE_ID:
        print("  %s belongs to another game (title id %s)" % (BAD, meta["title_id"]))
        findings.append((BAD, label, "wrong title id"))
        return
    if meta["media_id"] != REQUIRED_MEDIA_ID:
        print("  %s media id %s does not match the supported disc"
              % (WARN, meta["media_id"]))
        findings.append((WARN, label, "media mismatch"))
        return
    if num in PREFERRED_TU:
        print("  %s Title Update %d - supported, and the one to build against." % (OK, num))
        findings.append((OK, label, "TU%d - use this" % num))
    elif num is not None:
        print("  %s Title Update %d - valid but not needed; TU6 or TU7 is what the"
              % (WARN, num))
        print("         port builds against. Keep it, but do not build with it.")
        findings.append((WARN, label, "TU%d - not required" % num))
    else:
        print("  %s could not read a title update number from the name" % WARN)
        findings.append((WARN, label, "unknown TU number"))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    if not os.path.isdir(root):
        print("error: %s is not a directory" % root, file=sys.stderr)
        return 2

    isos, tus = [], []
    for name in sorted(os.listdir(root)):
        full = os.path.join(root, name)
        if not os.path.isfile(full):
            continue
        if name.lower().endswith(".iso"):
            isos.append(full)
        elif name.upper().startswith("TU_") or looks_like_stfs(full):
            tus.append(full)

    print("=" * 72)
    print("Asset verification - %s" % os.path.abspath(root))
    print("Need: title %s, media %s, plus TU6 or TU7"
          % (REQUIRED_TITLE_ID, REQUIRED_MEDIA_ID))
    print("=" * 72)

    findings = []
    if not isos:
        print("\nNo .iso files found.")
    for p in isos:
        check_iso(p, findings)
    for p in tus:
        check_tu(p, findings)

    print("\n" + "=" * 72)
    print("Summary")
    print("=" * 72)
    for status, label, note in findings:
        print("  %s %-54s %s" % (status, label[:54], note))

    usable_disc = any(s == OK and "base game" in n for s, _l, n in findings)
    usable_tu = any(s == OK and n.startswith("TU") for s, _l, n in findings)
    print()
    if usable_disc and usable_tu:
        print("  Ready: you have a supported disc and a supported title update.")
        return 0
    if usable_disc:
        print("  Partial: supported disc found, but no TU6/TU7. The disc-only build")
        print("  will work; DLC content needs a title update.")
        return 0
    print("  Blocked: no supported disc image found. Nothing can be built yet.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
