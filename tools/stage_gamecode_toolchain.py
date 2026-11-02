#!/usr/bin/env python3
"""
stage_gamecode_toolchain.py - what the APK carries to make the game's code (PLAN 12.5).

The published app makes the game's code on the device from the player's own disc: it runs
XenonRecomp on the disc's executable and compiles the result. This stages what that
needs, for Gradle to package (app/build.gradle.kts reads build/gamecode-stage/):

  jniLibs/arm64-v8a/libllvm.so         clang and lld (tools/build_device_toolchain.py's
  jniLibs/arm64-v8a/libxenonrecomp.so  bin/llvm and bin/xenonrecomp) - executables named
                                       as libraries: Android installs the APK's libraries
                                       where an app may run a program from
  assets/gamecode/toolchain.zip        the headers and link inputs:
      sysroot/usr/include/             the NDK's C and C++ headers (API 33)
      lib/                             crtbegin_so.o, crtend_so.o, the libc, libm and libdl
                                       stubs, compiler-rt's builtins, libunwind
      clang/include/                   clang's own headers (the device clang's)
      simde/                           SIMD Everywhere's headers (the generated code's x86
                                       vector operations on ARM)
      xenonrecomp/ppc_context.h        XenonRecomp's context header, patched as the
                                       computer's (the generated code is made with it)
      daorecomp/                       DAORecomp's configuration for the TU build: the
                                       function list and switch tables (MIT)
      VERSION                          a hash of all of it - the app extracts the zip again
                                       when it changes

None of it is the game's. The zip is written deterministically (sorted, fixed times), so
the same inputs give the same APK.

    python tools/stage_gamecode_toolchain.py [--check]
"""
import argparse
import hashlib
import os
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN = ROOT / "build" / "device-toolchain"
STAGE = ROOT / "build" / "gamecode-stage"
SDK = Path(os.environ.get("ANDROID_HOME", Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"))
NDK = Path(os.environ.get("ANDROID_NDK_HOME", SDK / "ndk" / "28.2.13676358"))
PREBUILT = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64"
SYSROOT = PREBUILT / "sysroot"
API = 33
XENON = ROOT / "third_party" / "XenonRecomp"
DAO = ROOT / "third_party" / "DAORecomp"


def entries():
    """(name in the zip, source file), sorted by name."""
    found = []

    def tree(prefix, folder, suffixes=None, skip=()):
        for path in sorted(folder.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(folder).as_posix()
            if any(part in skip for part in relative.split("/")):
                continue
            if suffixes and path.suffix not in suffixes:
                continue
            found.append((prefix + relative, path))

    tree("sysroot/usr/include/", SYSROOT / "usr" / "include")
    lib = SYSROOT / "usr" / "lib" / "aarch64-linux-android" / str(API)
    for name in ["crtbegin_so.o", "crtend_so.o", "libc.so", "libm.so", "libdl.so"]:
        found.append(("lib/" + name, lib / name))
    found.append(("lib/libclang_rt.builtins-aarch64-android.a",
                  PREBUILT / "lib" / "clang" / "19" / "lib" / "linux" /
                  "libclang_rt.builtins-aarch64-android.a"))
    found.append(("lib/libunwind.a",
                  PREBUILT / "lib" / "clang" / "19" / "lib" / "linux" / "aarch64" / "libunwind.a"))
    tree("clang/include/", TOOLCHAIN / "lib" / "clang" / "19" / "include")
    tree("simde/", XENON / "thirdparty" / "simde", suffixes={".h"}, skip={"test", ".git"})
    found.append(("xenonrecomp/ppc_context.h", XENON / "XenonUtils" / "ppc_context.h"))
    for name in ["DAO_tu6.toml", "DAO_tu6_switch_tables.toml"]:
        found.append(("daorecomp/" + name, DAO / "config" / name))
    missing = [str(source) for _, source in found if not source.is_file()]
    if missing:
        raise SystemExit("missing: %s\n(tools/bootstrap.py, then tools/build_device_toolchain.py)"
                         % ", ".join(missing[:5]))
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--check", action="store_true", help="report, change nothing")
    args = parser.parse_args()
    files = entries()
    digest = hashlib.sha256()
    for name, source in files:
        digest.update(name.encode("utf-8") + b"\0")
        digest.update(source.read_bytes())
    for program in ["llvm", "xenonrecomp"]:
        digest.update((TOOLCHAIN / "bin" / program).read_bytes())
    version = digest.hexdigest()[:16]
    zip_path = STAGE / "assets" / "gamecode" / "toolchain.zip"
    version_path = STAGE / "assets" / "gamecode" / "toolchain.version"
    if version_path.is_file() and version_path.read_text().strip() == version and zip_path.is_file():
        print("staged already: toolchain %s (%d files)" % (version, len(files)))
        return 0
    if args.check:
        print("to stage: toolchain %s (%d files)" % (version, len(files)))
        return 1
    libs = STAGE / "jniLibs" / "arm64-v8a"
    libs.mkdir(parents=True, exist_ok=True)
    shutil.copy2(TOOLCHAIN / "bin" / "llvm", libs / "libllvm.so")
    shutil.copy2(TOOLCHAIN / "bin" / "xenonrecomp", libs / "libxenonrecomp.so")
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    part = zip_path.with_suffix(".zip.part")
    with zipfile.ZipFile(part, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, source in files:
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, source.read_bytes())
        info = zipfile.ZipInfo("VERSION", date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, version + "\n")
    part.replace(zip_path)
    version_path.write_text(version + "\n", encoding="utf-8")
    print("staged toolchain %s: %d files, %.1f MB zipped; libllvm.so %.1f MB, "
          "libxenonrecomp.so %.1f MB" % (version, len(files), zip_path.stat().st_size / 1e6,
                                         (libs / "libllvm.so").stat().st_size / 1e6,
                                         (libs / "libxenonrecomp.so").stat().st_size / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
