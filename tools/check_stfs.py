#!/usr/bin/env python3
"""
check_stfs.py - check the native STFS reader against DAORecomp's Python one.

Task 5.0f. app/src/main/cpp/warden_stfs.cc is a port of
third_party/DAORecomp/scripts/stfs.py, which is the reference both of DAORecomp's own
implementations claim to track and the one that produced the known-good
work/tu7_extracted. So the Python is the authority: if the two disagree, the C++ is
wrong.

Compares the file list, every size, **and every byte of every file**. Contents matter
here in a way they did not for the disc listing: an STFS file's blocks are threaded
through hash tables, and a fragmented file is read by following a chain one block at a
time. Getting the chain subtly wrong gives a file of exactly the right length holding
some of the wrong blocks - which a size comparison cannot see.

Usage:
  python tools/check_stfs.py
  python tools/check_stfs.py TU_12K2260_0000004000000.00000000001O1
"""
import argparse
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "third_party" / "DAORecomp" / "scripts"))

BUILD = ROOT / "build" / "stfs-check"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"

# Known-good, from tools/recompile.py: the sha1 of the title update's executable patch
# identifies which update a package is.
TU_XEXP_SHA1S = {"4141b608e44c4ed35eb4e24ba2c05844a2f57a54": 6,
                 "dd5b493f4fe2fb4c99e6ef7ed6923ddddeee8291": 7}


def host_compiler():
    candidate = pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidate = candidate / "LLVM" / "bin" / ("clang-cl.exe" if os.name == "nt" else "clang++")
    if candidate.is_file():
        return candidate
    found = shutil.which("clang-cl") or shutil.which("clang++")
    return pathlib.Path(found) if found else None


def build():
    compiler = host_compiler()
    if not compiler:
        raise SystemExit("error: no clang-cl or clang++ found; see docs/TOOLCHAIN.md")
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("stfs_host.exe" if os.name == "nt" else "stfs_host")
    cpp = ROOT / "app" / "src" / "main" / "cpp"
    sources = [cpp / "warden_stfs.cc", ROOT / "tools" / "stfs_host.cc"]

    if compiler.name.startswith("clang-cl"):
        args = [str(compiler), "/std:c++20", "/O2", "/EHsc", "/MT", "/W3",
                "-winsysroot", str(WINSYSROOT),
                f"/I{cpp}", f"/Fe{exe}", f"/Fo{BUILD}\\"]
    else:
        args = [str(compiler), "-std=c++20", "-O2", f"-I{cpp}", "-o", str(exe)]
    args += [str(s) for s in sources]

    print(f"  building {exe.name}")
    result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-5000:], file=sys.stderr)
        print(result.stderr[-5000:], file=sys.stderr)
        raise SystemExit("error: host harness failed to build")
    return exe


def native(exe, package, out_dir):
    result = subprocess.run([str(exe), str(package), str(out_dir)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
        raise SystemExit(f"error: the native reader failed on {package.name}")
    header = {}
    files = []
    for line in result.stdout.splitlines():
        parts = line.split("\t")
        if parts[0] == "FILE" and len(parts) == 3:
            files.append((parts[2], int(parts[1])))
        elif len(parts) == 2:
            header[parts[0]] = parts[1]
    return header, files


def reference(package, out_dir):
    import stfs
    with stfs.Package(str(package)) as pkg:
        header = {"MAGIC": pkg.magic, "TITLE": f"{pkg.title_id:08X}",
                  "MEDIA": f"{pkg.media_id:08X}", "TYPE": f"{pkg.content_type:08X}"}
        files = [(path, length) for path, length, _, _ in pkg.files]
        pkg.extract(str(out_dir))
    return header, files


def digest(path):
    h = hashlib.sha1()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compare(package):
    print(f"\n{package.name}")
    exe = build()
    with tempfile.TemporaryDirectory(prefix="stfs-check-") as temp:
        temp = pathlib.Path(temp)
        native_dir = temp / "native"
        native_dir.mkdir()
        # The Python's extract() insists the destination does not exist.
        reference_dir = temp / "reference"

        native_header, native_files = native(exe, package, native_dir)
        reference_header, reference_files = reference(package, reference_dir)

        ok = True
        if native_header != reference_header:
            print(f"  [FAIL] header differs")
            print(f"         C++    {native_header}")
            print(f"         Python {reference_header}")
            ok = False
        else:
            print(f"  [ OK ] header: {native_header['MAGIC']} "
                  f"title {native_header['TITLE']} media {native_header['MEDIA']} "
                  f"type {native_header['TYPE']}")

        if native_files != reference_files:
            print(f"  [FAIL] file list differs "
                  f"({len(native_files)} vs {len(reference_files)} entries)")
            only_native = set(native_files) - set(reference_files)
            only_reference = set(reference_files) - set(native_files)
            for path, size in sorted(only_native)[:8]:
                print(f"         C++ only:    {size:>10}  {path}")
            for path, size in sorted(only_reference)[:8]:
                print(f"         Python only: {size:>10}  {path}")
            return False
        total = sum(size for _path, size in reference_files)
        print(f"  [ OK ] file list: {len(reference_files)} files, {total:,} bytes, "
              f"all paths and sizes identical")

        # Contents. The harness flattens '/' to '_' so it needs no directory support.
        mismatched = []
        for path, _size in reference_files:
            native_path = native_dir / path.replace("/", "_")
            reference_path = reference_dir / pathlib.Path(path)
            if not native_path.is_file():
                mismatched.append((path, "missing from the C++ extraction"))
                continue
            if digest(native_path) != digest(reference_path):
                mismatched.append((path, "contents differ"))
        if mismatched:
            print(f"  [FAIL] {len(mismatched)} files differ in content")
            for path, why in mismatched[:10]:
                print(f"         {path}: {why}")
            ok = False
        else:
            print(f"  [ OK ] contents: every one of {len(reference_files)} files "
                  f"byte-identical")

        # Which update this is - reported, not judged. This script verifies the
        # reader against its reference; whether a given package is one the project
        # can use is verify_assets.py's job, and failing here for a package that
        # read perfectly would be the wrong signal entirely.
        xexp = reference_dir / "default.xexp"
        if xexp.is_file():
            sha1 = digest(xexp)
            update = TU_XEXP_SHA1S.get(sha1)
            if update:
                print(f"         default.xexp is Title Update {update} "
                      f"({xexp.stat().st_size:,} bytes)")
            else:
                print(f"         default.xexp sha1 {sha1}")
                print(f"         not Title Update 6 or 7 - this package is not one "
                      f"this project can use")
        return ok


def main():
    ap = argparse.ArgumentParser(description="Differential check for the STFS reader")
    ap.add_argument("packages", nargs="*", help="packages to check")
    a = ap.parse_args()

    packages = [pathlib.Path(p) for p in a.packages]
    if not packages:
        packages = sorted(ROOT.glob("TU_*"))
    packages = [p for p in packages if p.is_file()]
    if not packages:
        raise SystemExit("error: no TU_* packages in the repo root")

    results = [compare(p) for p in packages]
    print()
    print("the native reader matches the reference" if all(results)
          else "MISMATCH - the C++ reader is wrong")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
