#!/usr/bin/env python3
"""
check_xdvdfs.py - check the native XDVDFS reader against the Python one.

Task 4.4. app/src/main/cpp/warden_xdvdfs.cc is a port of tools/dao_iso.py's Xdvdfs
class. The Python has been reading the real disc correctly since Phase 0, so it is the
reference: if the two disagree, the C++ is wrong.

Compares the full recursive listing - every path, every size, and the partition base
offset - rather than spot-checking a few files. A directory-tree parser that works for
the root and fails on a deeply nested subdirectory is exactly the bug this is for, and
there are 9,452 entries on this disc to disagree about.

Usage:
  python tools/check_xdvdfs.py "Dragon Age - Origins (World) (En,Es,It).iso"
  python tools/check_xdvdfs.py --all          # every .iso in the repo root
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import dao_iso  # noqa: E402

BUILD = ROOT / "build" / "xdvdfs-check"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"


def host_compiler():
    candidate = pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidate = candidate / "LLVM" / "bin" / ("clang-cl.exe" if os.name == "nt" else "clang++")
    if candidate.is_file():
        return candidate
    found = shutil.which("clang-cl") or shutil.which("clang++")
    return pathlib.Path(found) if found else None


def build():
    """Compile the host harness. Returns the executable path."""
    compiler = host_compiler()
    if not compiler:
        raise SystemExit("error: no clang-cl or clang++ found; see docs/TOOLCHAIN.md")
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("xdvdfs_host.exe" if os.name == "nt" else "xdvdfs_host")
    cpp = ROOT / "app" / "src" / "main" / "cpp"
    sources = [cpp / "warden_xdvdfs.cc", cpp / "warden_title.cc",
               ROOT / "tools" / "xdvdfs_host.cc"]

    if compiler.name.startswith("clang-cl"):
        args = [str(compiler), "/std:c++20", "/O2", "/EHsc", "/MT", "/W3",
                "-winsysroot", str(WINSYSROOT),
                f"/I{cpp}", f"/Fe{exe}", f"/Fo{BUILD}\\"]
        args += [str(s) for s in sources]
    else:
        args = [str(compiler), "-std=c++20", "-O2", "-Wall", f"-I{cpp}",
                "-o", str(exe)] + [str(s) for s in sources]

    print(f"  building {exe.name}")
    result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-4000:], file=sys.stderr)
        print(result.stderr[-4000:], file=sys.stderr)
        raise SystemExit(f"error: host harness failed to build (exit {result.returncode})")
    return exe


def native_listing(exe, image):
    result = subprocess.run([str(exe), str(image)], capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        raise SystemExit(f"error: {exe.name} failed on {image}")
    return result.stdout.splitlines()


def python_listing(image):
    iso = dao_iso.Xdvdfs(str(image))
    lines = [f"BASE\t0x{iso.base:X}"]

    # The same identity the app uses to tell Origins from Awakening - and from a
    # different game altogether, which matters because a phone's Download folder is
    # not a curated place.
    xex = dao_iso.parse_xex(iso.read_file("default.xex", limit=64 * 1024))
    if xex and "title_id" in xex:
        version = xex["version"].split(".")
        packed = ((int(version[0]) << 28) | (int(version[1]) << 24)
                  | (int(version[2]) << 8) | int(version[3]))
        disc, _, discs = xex["disc"].partition(" of ")
        lines.append(f"TITLE\t{xex['title_id']}\t{xex['media_id']}"
                     f"\t{packed:08X}\t{disc.strip()}\t{discs.strip()}")
    else:
        lines.append("TITLE\tnone")

    def walk(path):
        for name, _sector, size, attr in iso.listdir(path) or []:
            child = f"{path}/{name}" if path else name
            if attr & dao_iso.ATTR_DIRECTORY:
                lines.append(f"DIR\t{child}")
                walk(child)
            else:
                lines.append(f"{size}\t{child}")

    walk("")
    return lines


def compare(name, native, reference):
    if native == reference:
        files = sum(1 for line in reference if not line.startswith(("DIR\t", "BASE\t")))
        dirs = sum(1 for line in reference if line.startswith("DIR\t"))
        print(f"  [ OK ] {name}")
        print(f"         {len(reference)} lines identical: {files} files, {dirs} directories")
        return True

    print(f"  [FAIL] {name}")
    only_native = [line for line in native if line not in set(reference)]
    only_reference = [line for line in reference if line not in set(native)]
    print(f"         {len(only_native)} lines only from C++, "
          f"{len(only_reference)} only from Python")
    for line in only_native[:10]:
        print(f"           C++ only: {line}")
    for line in only_reference[:10]:
        print(f"           Py  only: {line}")
    return False


def main():
    ap = argparse.ArgumentParser(description="Differential check for the XDVDFS reader")
    ap.add_argument("images", nargs="*", help="disc images to check")
    ap.add_argument("--all", action="store_true", help="check every .iso in the repo root")
    a = ap.parse_args()

    images = [pathlib.Path(i) for i in a.images]
    if a.all or not images:
        images = sorted(ROOT.glob("*.iso"))
    images = [i for i in images if i.is_file()]
    if not images:
        raise SystemExit("error: no disc images found; put your .iso in the repo root")

    exe = build()
    ok = True
    for image in images:
        print(f"\n{image.name}")
        ok = compare(image.name, native_listing(exe, image), python_listing(image)) and ok

    print()
    print("the native reader matches the reference" if ok else "MISMATCH - the C++ reader is wrong")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
