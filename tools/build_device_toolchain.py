#!/usr/bin/env python3
"""
build_device_toolchain.py - the compiler the app runs on the device (PLAN 12).

The published APK holds none of the game's code: the app translates it from the player's
own disc on the device (XenonRecomp) and compiles that with this toolchain. It is LLVM
19.1.7 - the major version of the NDK r28c clang the generated code was tested with -
built for Android arm64 as one program, `llvm` (LLVM's multicall driver), holding clang
and lld and nothing else, plus clang's own headers.

    python tools/build_device_toolchain.py [--check] [--jobs N]

Steps, each skipped when its output is there:
  1. third_party/llvm-project      llvmorg-19.1.7, sparse (llvm, clang, lld, cmake,
                                   third-party, libunwind), blob-filtered
  2. build/llvm-host               LLVM's tablegen tools for this computer (clang-cl
                                   against toolchain/winsysroot, as XenonRecomp is built:
                                   tools/cmake/clang-cl-xwin-host.cmake)
  3. build/llvm-android            the Android build: the `llvm-driver` target (bin/llvm)
                                   and `clang-resource-headers`
  4. build/device-toolchain        bin/llvm stripped, lib/clang/19/include
  5. build/xenonrecomp-android     XenonRecomp, patched as the bootstrap patches it, for
                                   Android: build/device-toolchain/bin/xenonrecomp

Needs the Android NDK (ANDROID_NDK_HOME), the SDK's CMake 3.31.6 and its Ninja, LLVM at
C:/Program Files/LLVM (clang-cl, lld-link) and the MSVC sysroot from
tools/setup_winsysroot.py. On the owner's PC, python tools/game_check.py comes first:
this is a long build (CLAUDE.md).
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LLVM_URL = "https://github.com/llvm/llvm-project.git"
LLVM_COMMIT = "cd708029e0b2869e80abe31ddb175f7c35361f90"  # llvmorg-19.1.7
LLVM_SPARSE = ["llvm", "clang", "lld", "cmake", "third-party", "libunwind"]
LLVM_SRC = ROOT / "third_party" / "llvm-project"
HOST_BUILD = ROOT / "build" / "llvm-host"
ANDROID_BUILD = ROOT / "build" / "llvm-android"
OUT = ROOT / "build" / "device-toolchain"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
SDK = Path(os.environ.get("ANDROID_HOME", Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"))
NDK = Path(os.environ.get("ANDROID_NDK_HOME", SDK / "ndk" / "28.2.13676358"))
CMAKE = SDK / "cmake" / "3.31.6" / "bin" / "cmake.exe"
NINJA = SDK / "cmake" / "3.31.6" / "bin" / "ninja.exe"
API = 33

# Tools LLVM's multicall driver takes in by default; only clang and lld are wanted.
LLVM_TOOLS_OFF = ["dsymutil", "llvm-ar", "llvm-cxxfilt", "llvm-debuginfod", "llvm-dwp",
                  "llvm-gsymutil", "llvm-ifs", "llvm-libtool-darwin", "llvm-lipo", "llvm-ml",
                  "llvm-mt", "llvm-nm", "llvm-objcopy", "llvm-objdump", "llvm-profdata",
                  "llvm-rc", "llvm-readobj", "llvm-size", "llvm-symbolizer", "sancov"]
CLANG_TOOLS_OFF = ["clang-installapi", "clang-scan-deps"]
TABLEGENS = ["llvm-min-tblgen", "llvm-tblgen", "clang-tblgen"]


def say(state, text):
    print("  [%-4s] %s" % (state, text), flush=True)


def run(cmd, cwd=None):
    print("    $ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True)


def prefix_maps():
    """The build machine's paths mapped to stand-ins (PLAN 11.3: none in what ships)."""
    home = Path.home().as_posix()
    return " ".join("-ffile-prefix-map=%s=%s" % (path, name) for path, name in
                    [(NDK.as_posix(), "ndk"), (ROOT.as_posix(), "src"), (home, "home")])


def fetch(check):
    print("1. LLVM 19.1.7's sources")
    head = None
    if (LLVM_SRC / ".git").exists():
        head = subprocess.run(["git", "-C", LLVM_SRC, "rev-parse", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
    if head == LLVM_COMMIT:
        say("OK", "third_party/llvm-project %s" % LLVM_COMMIT[:12])
        return
    if head:
        raise SystemExit("third_party/llvm-project is at %s, not llvmorg-19.1.7 - move it aside"
                         % head[:12])
    if check:
        say("TODO", "third_party/llvm-project - to fetch")
        return
    LLVM_SRC.mkdir(parents=True, exist_ok=True)
    run(["git", "init", "-q"], cwd=LLVM_SRC)
    run(["git", "remote", "add", "origin", LLVM_URL], cwd=LLVM_SRC)
    run(["git", "fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", LLVM_COMMIT],
        cwd=LLVM_SRC)
    run(["git", "sparse-checkout", "set", "--cone"] + LLVM_SPARSE, cwd=LLVM_SRC)
    run(["git", "-c", "advice.detachedHead=false", "checkout", "-q", "FETCH_HEAD"], cwd=LLVM_SRC)
    say("OK", "fetched")


def host_tools(check, jobs):
    print("2. LLVM's tablegen tools, for this computer")
    exes = [HOST_BUILD / "bin" / (t + ".exe") for t in TABLEGENS]
    if all(e.is_file() for e in exes):
        say("OK", "build/llvm-host/bin: " + ", ".join(TABLEGENS))
        return
    if check:
        say("TODO", "build/llvm-host")
        return
    llvm_bin = Path("C:/Program Files/LLVM/bin")
    if not (llvm_bin / "clang-cl.exe").is_file():
        raise SystemExit("no clang-cl in %s" % llvm_bin)
    if not WINSYSROOT.is_dir():
        raise SystemExit("no toolchain/winsysroot - python tools/setup_winsysroot.py "
                         "--accept-license (Microsoft's licence, as for XenonRecomp)")
    if not (HOST_BUILD / "build.ninja").is_file():
        run([CMAKE, "-G", "Ninja", "-S", LLVM_SRC / "llvm", "-B", HOST_BUILD, "-Wno-dev",
             "-DCMAKE_MAKE_PROGRAM=" + NINJA.as_posix(),
             "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_TOOLCHAIN_FILE=" + (ROOT / "tools" / "cmake" /
                                          "clang-cl-xwin-host.cmake").as_posix(),
             "-DDAO_LLVM_BIN=" + llvm_bin.as_posix(),
             "-DDAO_WINSYSROOT=" + WINSYSROOT.as_posix(),
             "-DLLVM_ENABLE_PROJECTS=clang",
             "-DLLVM_TARGETS_TO_BUILD=AArch64",
             "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF",
             "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_DOCS=OFF",
             "-DCLANG_INCLUDE_TESTS=OFF", "-DCLANG_INCLUDE_DOCS=OFF",
             "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
             "-DLLVM_ENABLE_BINDINGS=OFF",
             # BLAKE3's x86 assembly is MASM, which needs Visual Studio's ml64; its
             # portable C does the same for the tablegen tools.
             "-DLLVM_DISABLE_ASSEMBLY_FILES=ON"])
    run([NINJA, "-C", HOST_BUILD, "-j", str(jobs)] + TABLEGENS)
    say("OK", "built")


def android_build(check, jobs):
    print("3. LLVM for Android: clang and lld in one program")
    driver = ANDROID_BUILD / "bin" / "llvm"
    headers = ANDROID_BUILD / "lib" / "clang" / "19" / "include" / "arm_neon.h"
    if driver.is_file() and headers.is_file():
        say("OK", "build/llvm-android/bin/llvm")
        return
    if check:
        say("TODO", "build/llvm-android")
        return
    if not (ANDROID_BUILD / "build.ninja").is_file():
        flags = "%s -g0" % prefix_maps()
        cmd = [CMAKE, "-G", "Ninja", "-S", LLVM_SRC / "llvm", "-B", ANDROID_BUILD, "-Wno-dev",
               "-DCMAKE_MAKE_PROGRAM=" + NINJA.as_posix(),
               "-DCMAKE_BUILD_TYPE=Release",
               "-DCMAKE_TOOLCHAIN_FILE=" + (NDK / "build" / "cmake" /
                                            "android.toolchain.cmake").as_posix(),
               "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-%d" % API,
               "-DANDROID_STL=c++_static",
               "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_CXX_FLAGS=" + flags,
               "-DLLVM_NATIVE_TOOL_DIR=" + (HOST_BUILD / "bin").as_posix(),
               "-DLLVM_HOST_TRIPLE=aarch64-unknown-linux-android%d" % API,
               "-DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-unknown-linux-android%d" % API,
               "-DLLVM_TARGETS_TO_BUILD=AArch64",
               "-DLLVM_ENABLE_PROJECTS=clang;lld",
               "-DLLVM_TOOL_LLVM_DRIVER_BUILD=ON",
               "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF",
               "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_DOCS=OFF",
               # FileCheck and the like; tablegen is built regardless. (mlgo-utils' test
               # suite would want llvm-objcopy, left out below.)
               "-DLLVM_INCLUDE_UTILS=OFF",
               "-DCLANG_INCLUDE_TESTS=OFF", "-DCLANG_INCLUDE_DOCS=OFF",
               "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF", "-DLLVM_ENABLE_LIBXML2=OFF",
               "-DLLVM_ENABLE_LIBEDIT=OFF",
               "-DLLVM_ENABLE_LIBPFM=OFF", "-DLLVM_ENABLE_CURL=OFF",
               "-DLLVM_ENABLE_HTTPLIB=OFF", "-DLLVM_ENABLE_BINDINGS=OFF",
               "-DLLVM_ENABLE_PLUGINS=OFF", "-DCLANG_PLUGIN_SUPPORT=OFF",
               "-DCLANG_ENABLE_ARCMT=OFF", "-DCLANG_ENABLE_STATIC_ANALYZER=OFF",
               "-DLLVM_PARALLEL_LINK_JOBS=2"]
        cmd += ["-DLLVM_TOOL_%s_BUILD=OFF" % t.upper().replace("-", "_") for t in LLVM_TOOLS_OFF]
        cmd += ["-DCLANG_TOOL_%s_BUILD=OFF" % t.upper().replace("-", "_") for t in CLANG_TOOLS_OFF]
        run(cmd)
    run([NINJA, "-C", ANDROID_BUILD, "-j", str(jobs), "llvm-driver", "clang-resource-headers"])
    say("OK", "built")


def stage(check):
    print("4. build/device-toolchain")
    out_driver = OUT / "bin" / "llvm"
    if out_driver.is_file() and out_driver.stat().st_mtime >= \
            (ANDROID_BUILD / "bin" / "llvm").stat().st_mtime:
        say("OK", "bin/llvm %.1f MB" % (out_driver.stat().st_size / 1e6))
        return
    if check:
        say("TODO", "stage")
        return
    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / "bin").mkdir(parents=True)
    strip = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-strip.exe"
    run([strip, "--strip-all", "-o", out_driver, ANDROID_BUILD / "bin" / "llvm"])
    shutil.copytree(ANDROID_BUILD / "lib" / "clang" / "19" / "include",
                    OUT / "lib" / "clang" / "19" / "include")
    say("OK", "bin/llvm %.1f MB, clang's headers" % (out_driver.stat().st_size / 1e6))


def recompiler(check, jobs):
    """XenonRecomp for the device: the same sources as the computer's (DAORecomp's
    patch_recompiler.py and tools/patch_xenonrecomp.py applied - the bootstrap's step 4),
    built with the NDK. The device runs it on the player's own executable (PLAN 12.4)."""
    print("5. XenonRecomp for Android")
    source = ROOT / "third_party" / "XenonRecomp"
    build = ROOT / "build" / "xenonrecomp-android"
    built = build / "XenonRecomp" / "XenonRecomp"
    out = OUT / "bin" / "xenonrecomp"
    recompiler_cpp = source / "XenonRecomp" / "recompiler.cpp"
    text = recompiler_cpp.read_text(encoding="utf-8", errors="replace") \
        if recompiler_cpp.is_file() else ""
    if "DAO additions" not in text or "DAO/Warden (PLAN 12.4)" not in text:
        raise SystemExit("third_party/XenonRecomp is not patched: run tools/bootstrap.py "
                         "(step 4), or DAORecomp's scripts/patch_recompiler.py and "
                         "tools/patch_xenonrecomp.py")
    if out.is_file() and out.stat().st_mtime >= recompiler_cpp.stat().st_mtime and \
            built.is_file() and out.stat().st_mtime >= built.stat().st_mtime:
        say("OK", "bin/xenonrecomp %.1f MB" % (out.stat().st_size / 1e6))
        return
    if check:
        say("TODO", "XenonRecomp for Android")
        return
    if not (build / "build.ninja").is_file():
        flags = "%s -g0" % prefix_maps()
        run([CMAKE, "-G", "Ninja", "-S", source, "-B", build, "-Wno-dev",
             "-DCMAKE_MAKE_PROGRAM=" + NINJA.as_posix(),
             "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_TOOLCHAIN_FILE=" + (NDK / "build" / "cmake" /
                                          "android.toolchain.cmake").as_posix(),
             "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-%d" % API,
             "-DANDROID_STL=c++_static",
             "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_CXX_FLAGS=" + flags])
    run([NINJA, "-C", build, "-j", str(jobs), "XenonRecomp"])
    (OUT / "bin").mkdir(parents=True, exist_ok=True)
    strip = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-strip.exe"
    run([strip, "--strip-all", "-o", out, built])
    say("OK", "bin/xenonrecomp %.1f MB" % (out.stat().st_size / 1e6))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--check", action="store_true", help="report, change nothing")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 8)
    args = parser.parse_args()
    fetch(args.check)
    host_tools(args.check, args.jobs)
    android_build(args.check, args.jobs)
    stage(args.check)
    recompiler(args.check, args.jobs)


if __name__ == "__main__":
    main()
