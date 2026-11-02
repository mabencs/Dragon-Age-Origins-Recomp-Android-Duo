#!/usr/bin/env python3
"""
gamecode_probe.py - time the game code's compile on the device itself (PLAN 12.1).

The published APK makes the game's code on the device: XenonRecomp's C++ (the 211 files
of recomp/generated/ppc_tu6), compiled there with the device toolchain
(tools/build_device_toolchain.py). This pushes that toolchain, the headers, the
generated files and a small driver to /data/local/tmp/gc on the device, compiles every
file there with the flags the app's own build uses for them (app/src/main/cpp/
CMakeLists.txt, warden_guest), and reports the wall time, each file's time and the
largest resident memory one compile reached.

    python tools/gamecode_probe.py [--jobs N] [--no-push]

The generated files are the game's code translated: they go to the owner's own device
only, and are deleted from it at the end (--keep leaves them).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "recomp" / "generated" / "ppc_tu6"
TOOLCHAIN = ROOT / "build" / "device-toolchain"
SDK = Path(os.environ.get("ANDROID_HOME", Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"))
NDK = Path(os.environ.get("ANDROID_NDK_HOME", SDK / "ndk" / "28.2.13676358"))
SYSROOT_INCLUDE = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "sysroot" / "usr" / "include"
SIMDE = ROOT / "third_party" / "XenonRecomp" / "thirdparty" / "simde"
STAGE = ROOT / "build" / "gamecode-probe"
DEVICE = "/data/local/tmp/gc"

# The guest hooks header CMake writes (warden_guest_hooks.h), with relative includes.
HOOKS = """#include <cstdint>
#include "ppc_config.h"
extern "C" void warden_gpu_register_write(uint32_t address, uint32_t value);
#define PPC_MM_STORE_U32(x, y) do { const uint32_t warden_mm_address_ = (x); const uint32_t warden_mm_value_ = (y); PPC_STORE_U32(warden_mm_address_, warden_mm_value_); if ((warden_mm_address_ & 0xFFFF0000u) == 0x7FC80000u) warden_gpu_register_write(warden_mm_address_, warden_mm_value_); } while (0)
#include "ppc_context.h"
#undef PPC_FUNC_PROLOGUE
extern "C" void warden_guest_trace(const char* function);
#define PPC_FUNC_PROLOGUE() warden_guest_trace(__func__); __builtin_assume(((size_t)base & 0x1F) == 0)
"""

# One file: the flags of warden_guest in app/src/main/cpp/CMakeLists.txt as the NDK build
# passes them (compile_commands.json), the paths the device's.
ONE = """#!/system/bin/sh
cd %(dir)s
f=$1
b=${f##*/}
b=${b%%.cpp}
toybox time -v ./tc/bin/llvm clang --target=aarch64-none-linux-android33 \\
  --sysroot=%(dir)s/sysroot -resource-dir=%(dir)s/tc/lib/clang/19 \\
  -Isrc -Isimde -DANDROID -fdata-sections -ffunction-sections -funwind-tables \\
  -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -frtti -fexceptions \\
  -fPIC -include hooks.h -std=c++17 -O2 -g0 -w -c "$f" -o obj/$b.o > log/$b.txt 2>&1
echo "exit $?" >> log/$b.txt
"""

RUN = """#!/system/bin/sh
cd %(dir)s
rm -rf obj log
mkdir -p obj log
start=$(date +%%s)
xargs -P $1 -n 1 sh ./one.sh < list.txt
end=$(date +%%s)
echo "wall $((end - start)) s, $1 jobs" > log/total.txt
"""


def adb(*args, check=True, capture=False):
    cmd = ["adb"] + [str(a) for a in args]
    if capture:
        return subprocess.run(cmd, check=check, capture_output=True, text=True).stdout
    subprocess.run(cmd, check=check)


def pc_order():
    """The generated files, slowest first by this PC's own build (app/.cxx ninja log),
    so the long ones start at once; any the log does not have by size."""
    times = {}
    for log in ROOT.glob("app/.cxx/RelWithDebInfo/*/arm64-v8a/.ninja_log"):
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            parts = line.split("\t")
            if len(parts) >= 4 and "warden_guest" in parts[3]:
                m = re.search(r"(ppc_[a-z_.0-9]+)\.cpp\.o$", parts[3])
                if m:
                    times[m.group(1)] = int(parts[1]) - int(parts[0])
    files = sorted(GEN.glob("*.cpp"))
    return sorted(files, key=lambda f: (-times.get(f.stem, 0), -f.stat().st_size))


def stage():
    if STAGE.exists():
        shutil.rmtree(STAGE)
    (STAGE / "tc" / "bin").mkdir(parents=True)
    shutil.copy2(TOOLCHAIN / "bin" / "llvm", STAGE / "tc" / "bin" / "llvm")
    shutil.copytree(TOOLCHAIN / "lib", STAGE / "tc" / "lib")
    shutil.copytree(SYSROOT_INCLUDE, STAGE / "sysroot" / "usr" / "include")
    shutil.copytree(SIMDE, STAGE / "simde", ignore=shutil.ignore_patterns(".git", "test*"))
    shutil.copytree(GEN, STAGE / "src")
    (STAGE / "hooks.h").write_text(HOOKS, encoding="utf-8", newline="\n")
    (STAGE / "one.sh").write_text(ONE % {"dir": DEVICE}, encoding="utf-8", newline="\n")
    (STAGE / "run.sh").write_text(RUN % {"dir": DEVICE}, encoding="utf-8", newline="\n")
    (STAGE / "list.txt").write_text("".join("src/%s\n" % f.name for f in pc_order()),
                                    encoding="utf-8", newline="\n")


def report():
    out = STAGE / "results"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir()
    adb("pull", DEVICE + "/log", out)
    logs = out / "log"
    total = (logs / "total.txt").read_text().strip()
    rows, failed = [], []
    for f in sorted(logs.glob("*.txt")):
        if f.name == "total.txt":
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        real = re.search(r"Real time \(s\): ([0-9.]+)", text)
        rss = re.search(r"Max RSS \(KiB\): ([0-9]+)", text)
        if "exit 0" not in text:
            failed.append(f.stem)
        rows.append((float(real.group(1)) if real else 0.0,
                     int(rss.group(1)) if rss else 0, f.stem))
    rows.sort(reverse=True)
    print(total)
    print("files %d, failed %d %s" % (len(rows), len(failed), " ".join(failed[:5])))
    print("cpu-seconds (sum of each file's wall time) %.0f" % sum(r[0] for r in rows))
    print("largest memory %.0f MB (%s)" % (max(r[1] for r in rows) / 1024,
                                           max(rows, key=lambda r: r[1])[2]))
    for real, rss, name in rows[:8]:
        print("  %7.1f s  %6.0f MB  %s" % (real, rss / 1024, name))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--no-push", action="store_true", help="the files are there already")
    parser.add_argument("--keep", action="store_true", help="leave the files on the device")
    args = parser.parse_args()
    if not args.no_push:
        stage()
        adb("shell", "rm -rf " + DEVICE)
        started = time.time()
        # The folder pushed to a path that does not exist yet becomes that path.
        adb("push", STAGE, DEVICE)
        print("pushed in %.0f s" % (time.time() - started))
        adb("shell", "chmod 755 %s/tc/bin/llvm" % DEVICE)
    adb("shell", "sh %s/run.sh %d" % (DEVICE, args.jobs))
    report()
    if not args.keep:
        adb("shell", "rm -rf %s/src %s/obj" % (DEVICE, DEVICE))


if __name__ == "__main__":
    sys.exit(main())
