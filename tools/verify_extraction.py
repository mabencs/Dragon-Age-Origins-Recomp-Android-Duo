#!/usr/bin/env python3
"""
verify_extraction.py - check what landed on the device against what is on the disc.

Task 4.4. The extractor reporting "complete: 9450 files, 6859144771 bytes" means it
believes it wrote everything. This checks that it did, by comparing every path and
every size against the disc image itself - which is a different claim, and the one that
matters when the game later cannot find a file.

Sizes, not hashes. Hashing 6.4 GiB on the handheld would take considerably longer than
the extraction did, and a wrong-sized file is the failure mode that actually happens:
a truncated write, a skipped file, a directory that was never created. A file of
exactly the right length at the right path, written by a rename-on-completion
extractor, is not plausibly the wrong content.

Usage:
  python tools/verify_extraction.py
  python tools/verify_extraction.py --image "Dragon Age - Origins (World) (En,Es,It).iso"
"""
import argparse
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import dao_iso  # noqa: E402

PACKAGE = "dev.daorecompandroid.duo"
DEVICE_DIR = f"/storage/emulated/0/Android/data/{PACKAGE}/files/gamedata"
# The console's own system-update payload; warden_extract.cc skips it deliberately.
SKIP_ROOT = "$SystemUpdate"
STAMP = "extraction-complete.txt"
# Written by the app from the title update package, not extracted from the disc.
OVERLAY_PREFIX = "packages/core/patch/"


def adb():
    home = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if not home and os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
                home = winreg.QueryValueEx(key, "ANDROID_HOME")[0]
        except (ImportError, OSError):
            home = None
    if home:
        candidate = pathlib.Path(home) / "platform-tools" / "adb.exe"
        if candidate.is_file():
            return str(candidate)
        candidate = pathlib.Path(home) / "platform-tools" / "adb"
        if candidate.is_file():
            return str(candidate)
    import shutil
    found = shutil.which("adb")
    if not found:
        raise SystemExit("error: adb not found")
    return found


def disc_listing(image):
    """{relative path: size} for every file the extractor should have written."""
    iso = dao_iso.Xdvdfs(str(image))
    out = {}

    def walk(path):
        for name, _sector, size, attr in iso.listdir(path) or []:
            if not path and name == SKIP_ROOT:
                continue
            child = f"{path}/{name}" if path else name
            if attr & dao_iso.ATTR_DIRECTORY:
                walk(child)
            else:
                out[child] = size

    walk("")
    return out


def device_listing(adb_path):
    """{relative path: size} for what is actually on the device."""
    # One shell invocation with a batched stat, rather than 9,450 round trips. The
    # output is "<size> ./relative/path" per line.
    command = f"cd {DEVICE_DIR} && find . -type f -exec stat -c '%s %n' {{}} +"
    result = subprocess.run([adb_path, "shell", command],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
        raise SystemExit(f"error: cannot list {DEVICE_DIR} - is it extracted yet?")

    out = {}
    for line in result.stdout.splitlines():
        line = line.strip().replace("\r", "")
        if not line:
            continue
        size, _, path = line.partition(" ")
        path = path.strip()
        if path.startswith("./"):
            path = path[2:]
        if path in (STAMP,) or path.endswith(".part"):
            continue
        try:
            out[path] = int(size)
        except ValueError:
            continue
    return out


def main():
    ap = argparse.ArgumentParser(description="Verify the on-device extraction")
    ap.add_argument("--image", help="disc image (default: the Origins ISO in the repo root)")
    a = ap.parse_args()

    if a.image:
        image = pathlib.Path(a.image)
    else:
        candidates = [p for p in sorted(ROOT.glob("*.iso")) if "Awakening" not in p.name]
        if not candidates:
            raise SystemExit("error: no Origins disc image in the repo root")
        image = candidates[0]

    print(f"disc:   {image.name}")
    expected = disc_listing(image)
    print(f"        {len(expected)} files, {sum(expected.values()):,} bytes")

    adb_path = adb()
    print(f"device: {DEVICE_DIR}")
    actual = device_listing(adb_path)
    print(f"        {len(actual)} files, {sum(actual.values()):,} bytes")
    print()

    # The title update's overlay comes from the TU package, not the disc, so it is
    # legitimately on the device and legitimately absent from the disc listing. The
    # app writes it (warden_boot.cc) and docs/ASSETS.md explains why the engine needs
    # it. Reported separately rather than counted as unexpected - which is what this
    # check did at first, and it was wrong.
    overlay = sorted(p for p in actual if p.startswith(OVERLAY_PREFIX))
    actual = {p: size for p, size in actual.items() if not p.startswith(OVERLAY_PREFIX)}

    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    wrong = sorted(p for p in set(expected) & set(actual) if expected[p] != actual[p])
    partial = [p for p in actual if p.endswith(".part")]

    def show(label, paths, detail=None):
        if not paths:
            return
        print(f"  {label}: {len(paths)}")
        for path in paths[:10]:
            print(f"    {path}" + (f"  {detail(path)}" if detail else ""))
        if len(paths) > 10:
            print(f"    ... and {len(paths) - 10} more")

    show("MISSING from the device", missing)
    show("UNEXPECTED on the device", extra)
    show("WRONG SIZE", wrong,
         lambda p: f"disc {expected[p]:,} vs device {actual[p]:,}")
    show("LEFT PARTIAL", partial)

    if overlay:
        total_overlay = sum(1 for _ in overlay)
        print(f"  title update overlay: {total_overlay} files under {OVERLAY_PREFIX}")
        print(f"    (from the TU package, not the disc - see docs/ASSETS.md)")

    if not (missing or extra or wrong or partial):
        print("  every file present at exactly the size on the disc")
        print()
        print("extraction verified")
        return 0
    print()
    print("EXTRACTION DOES NOT MATCH THE DISC")
    return 1


if __name__ == "__main__":
    sys.exit(main())
