#!/usr/bin/env python3
"""
setup_winsysroot.py - fetch the MSVC C runtime and Windows SDK without Visual Studio.

Building the host recompiler on Windows needs the MSVC CRT headers/libraries and the
Windows SDK. We already have LLVM (clang-cl, lld-link, llvm-lib, llvm-rc), which is
what DAORecomp accepts in place of Visual Studio, so only the Microsoft headers and
import libraries are missing.

This mirrors what DAORecomp's own installer does: download xwin and have it "splat"
the CRT and SDK into a winsysroot directory that clang-cl can be pointed at with
/winsysroot.

  LICENSE: those headers and libraries are Microsoft's, under
  https://go.microsoft.com/fwlink/?LinkId=2086102
  Running this accepts that license, exactly as installing Visual Studio would.
  Pass --accept-license to confirm.

Versions are pinned to the same ones DAORecomp uses, so our build matches theirs.

Usage:
  python tools/setup_winsysroot.py --accept-license
  python tools/setup_winsysroot.py --check          # report state, change nothing
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

# Pinned to DAORecomp installer/deps.py so our toolchain matches upstream's.
XWIN_VERSION = "0.10.0"
XWIN_URL = (f"https://github.com/Jake-Shadle/xwin/releases/download/{XWIN_VERSION}"
            f"/xwin-{XWIN_VERSION}-x86_64-pc-windows-msvc.tar.gz")
XWIN_SHA256 = "96e83665ef5c1406aabd0be603a8d08e67e604f36fda630b6807ee948a614e26"
XWIN_SIZE = 3074443
XWIN_TOP_DIR = f"xwin-{XWIN_VERSION}-x86_64-pc-windows-msvc"

MSVC_CRT_VERSION = "14.44.17.14"
WINDOWS_SDK_VERSION = "10.0.26100"
MSVC_LICENSE_URL = "https://go.microsoft.com/fwlink/?LinkId=2086102"

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN = ROOT / "toolchain"
XWIN_DIR = TOOLCHAIN / "xwin"
WINSYSROOT = TOOLCHAIN / "winsysroot"
STAMP = WINSYSROOT / "dao-xwin.txt"


def stamp_text():
    return f"xwin {XWIN_VERSION}, CRT {MSVC_CRT_VERSION}, SDK {WINDOWS_SDK_VERSION}\n"


def ready():
    return STAMP.exists() and STAMP.read_text() == stamp_text()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest):
    print(f"  downloading {url}")
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(url, timeout=120) as r, open(tmp, "wb") as fh:
        shutil.copyfileobj(r, fh)
    tmp.replace(dest)
    return dest


def ensure_xwin():
    exe = XWIN_DIR / "xwin.exe"
    if exe.is_file():
        print(f"  xwin already present: {exe}")
        return exe
    XWIN_DIR.mkdir(parents=True, exist_ok=True)
    archive = XWIN_DIR / "xwin.tar.gz"
    if not archive.is_file():
        download(XWIN_URL, archive)

    size = archive.stat().st_size
    if size != XWIN_SIZE:
        raise SystemExit(f"error: xwin download is {size} bytes, expected {XWIN_SIZE}")
    digest = sha256(archive)
    if digest != XWIN_SHA256:
        raise SystemExit(f"error: xwin sha256 mismatch\n  got      {digest}\n  expected {XWIN_SHA256}")
    print(f"  sha256 verified: {digest}")

    with tarfile.open(archive) as tf:
        # Python 3.12+ requires an explicit extraction filter.
        try:
            tf.extractall(XWIN_DIR, filter="data")
        except TypeError:
            tf.extractall(XWIN_DIR)
    inner = XWIN_DIR / XWIN_TOP_DIR / "xwin.exe"
    if inner.is_file():
        inner.replace(exe)
    if not exe.is_file():
        raise SystemExit(f"error: xwin.exe not found after extracting {archive}")
    archive.unlink(missing_ok=True)
    print(f"  extracted: {exe}")
    return exe


def splat(exe):
    cache = XWIN_DIR / "cache"
    args = [
        str(exe), "--accept-license",
        "--cache-dir", str(cache),
        "--log-level", "warn",
        "--crt-version", MSVC_CRT_VERSION,
        "--sdk-version", WINDOWS_SDK_VERSION,
        "--http-retry", "3",
        "--arch", "x86_64",
        "--variant", "desktop",
        "splat",
        "--use-winsysroot-style",
        "--preserve-ms-arch-notation",
        "--output", str(WINSYSROOT),
    ]
    if os.name == "nt":
        # Case-insensitive filesystem; symlinks would need elevated privileges.
        args.append("--disable-symlinks")

    print(f"  splatting CRT {MSVC_CRT_VERSION} + SDK {WINDOWS_SDK_VERSION} (about 85 MB)")
    result = subprocess.run(args)
    shutil.rmtree(cache, ignore_errors=True)
    if result.returncode != 0:
        raise SystemExit(f"error: xwin splat failed (exit {result.returncode})")

    STAMP.write_text(stamp_text())


def report():
    print("winsysroot state")
    print(f"  path      {WINSYSROOT}")
    print(f"  ready     {'yes' if ready() else 'no'}")
    print(f"  pinned    {stamp_text().strip()}")
    if WINSYSROOT.is_dir():
        for sub in ("VC/Tools/MSVC", "Windows Kits/10/Include", "Windows Kits/10/Lib"):
            p = WINSYSROOT / sub
            mark = "yes" if p.exists() else "no"
            print(f"  {sub:<28} {mark}")
    return 0 if ready() else 1


def main():
    ap = argparse.ArgumentParser(description="Fetch the MSVC CRT + Windows SDK via xwin")
    ap.add_argument("--accept-license", action="store_true",
                    help=f"accept Microsoft's license ({MSVC_LICENSE_URL})")
    ap.add_argument("--check", action="store_true", help="report state and exit")
    a = ap.parse_args()

    if a.check:
        return report()

    if os.name != "nt":
        print("note: this is only needed when building the host tools on Windows.")

    if ready():
        print(f"already set up: {stamp_text().strip()}")
        print(f"  {WINSYSROOT}")
        return 0

    if not a.accept_license:
        print("The MSVC C runtime headers/libraries and the Windows SDK are Microsoft's,")
        print(f"licensed under {MSVC_LICENSE_URL}")
        print("Re-run with --accept-license to download them.")
        return 2

    print(f"accepting {MSVC_LICENSE_URL}")
    TOOLCHAIN.mkdir(parents=True, exist_ok=True)
    exe = ensure_xwin()
    splat(exe)
    print()
    return report()


if __name__ == "__main__":
    sys.exit(main())
