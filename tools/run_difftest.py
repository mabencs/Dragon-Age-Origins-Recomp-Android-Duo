#!/usr/bin/env python3
"""
run_difftest.py - build the differential test for both architectures, run both,
and report which recompiled functions disagree.

This is the Phase 2.3 correctness check. Compiling for ARM64 proves the code is
accepted; this proves it *behaves* the same. It runs the generated harness from
tools/gen_difftest.py on the host and on the handheld over adb, then diffs the
per-function context hashes.

Prerequisites:
  python tools/recompile.py tu
  python tools/compile_check.py --target both     (produces the objects to link)
  python tools/gen_difftest.py

Usage:
  python tools/run_difftest.py                  build, run both, compare
  python tools/run_difftest.py --host-only
  python tools/run_difftest.py --no-build
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "recomp" / "generated" / "ppc_tu6"
SIMDE = ROOT / "third_party" / "XenonRecomp" / "thirdparty" / "simde"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
DT = ROOT / "build" / "difftest"
OBJS = ROOT / "build" / "compile-check"
REMOTE = "/data/local/tmp/difftest"

NDK_HOME = os.environ.get("ANDROID_NDK_HOME") or str(
    Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk" / "28.2.13676358")
ANDROID_API = 33

PROGRAM_FILES = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
CLANG_CL = PROGRAM_FILES / "LLVM" / "bin" / "clang-cl.exe"
LLD_LINK = PROGRAM_FILES / "LLVM" / "bin" / "lld-link.exe"


def ndk_tool(name):
    for host in ("windows-x86_64", "linux-x86_64", "darwin-x86_64"):
        bindir = Path(NDK_HOME) / "toolchains" / "llvm" / "prebuilt" / host / "bin"
        for cand in (bindir / (name + ".cmd"), bindir / name, bindir / (name + ".exe")):
            if cand.is_file():
                return cand
    return None


def run(cmd, label):
    started = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        print(f"  {label} FAILED (exit {proc.returncode})")
        out = (proc.stdout or "") + (proc.stderr or "")
        for line in out.splitlines()[:18]:
            print(f"    {line[:170]}")
        return False
    print(f"  {label} ok ({time.time() - started:.1f}s)")
    return True


def build_host():
    print("host build (x86_64-pc-windows-msvc)")
    if not CLANG_CL.is_file():
        print("  clang-cl not found; skipping")
        return None
    objdir = DT / "host"
    objdir.mkdir(parents=True, exist_ok=True)
    for name in ("difftest", "difftest_stubs"):
        cmd = [str(CLANG_CL), "/c", "/std:c++17", "/EHsc", "/O2", "/W0", "/MT",
               "/arch:AVX",                      # see compile_check.py: roundevenf
               "-winsysroot", str(WINSYSROOT),
               "/I", str(GEN), "/I", str(SIMDE),
               f"/Fo{objdir / (name + '.obj')}", str(DT / f"{name}.cpp")]
        if not run(cmd, f"compile {name}"):
            return None

    exe = DT / "difftest_host.exe"
    rsp = DT / "host_link.rsp"
    lines = [f'/out:"{exe}"', f'/winsysroot:"{WINSYSROOT}"', "/manifest:no", "/subsystem:console",
             f'"{objdir / "difftest.obj"}"', f'"{objdir / "difftest_stubs.obj"}"']
    generated = sorted((OBJS / "host" / "ppc_tu6").glob("*.obj"))
    if not generated:
        print("  no host objects; run: python tools/compile_check.py --target host")
        return None
    lines += [f'"{p}"' for p in generated]
    rsp.write_text("\n".join(lines) + "\n", encoding="ascii")
    if not run([str(LLD_LINK), f"@{rsp}"], f"link ({len(generated)} objects)"):
        return None
    return exe


def build_android():
    print("android build (aarch64-linux-android%d)" % ANDROID_API)
    cxx = ndk_tool(f"aarch64-linux-android{ANDROID_API}-clang++")
    if not cxx:
        print(f"  NDK compiler not found under {NDK_HOME}; skipping")
        return None
    objdir = DT / "android"
    objdir.mkdir(parents=True, exist_ok=True)
    for name in ("difftest", "difftest_stubs"):
        cmd = [str(cxx), "-c", "-std=c++17", "-O2", "-w", "-fPIC",
               "-I", str(GEN), "-I", str(SIMDE),
               "-o", str(objdir / f"{name}.o"), str(DT / f"{name}.cpp")]
        if not run(cmd, f"compile {name}"):
            return None

    binary = DT / "difftest_android"
    generated = sorted((OBJS / "android" / "ppc_tu6").glob("*.o"))
    if not generated:
        print("  no android objects; run: python tools/compile_check.py --target android")
        return None
    rsp = DT / "android_link.rsp"
    # The GNU-style driver treats a backslash in a response file as an escape
    # character, so Windows paths must be written with forward slashes.
    rsp.write_text("\n".join(
        ["-static", "-o", binary.as_posix(),
         (objdir / "difftest.o").as_posix(), (objdir / "difftest_stubs.o").as_posix()]
        + [p.as_posix() for p in generated]) + "\n", encoding="ascii")
    if not run([str(cxx), f"@{rsp}"], f"link ({len(generated)} objects)"):
        return None
    return binary


def adb(*args, **kw):
    env = dict(os.environ, MSYS2_ARG_CONV_EXCL="*")
    return subprocess.run(["adb", *args], capture_output=True, text=True,
                          errors="replace", env=env, **kw)


def run_on_device(binary, verbose):
    print("running on device")
    if not shutil.which("adb"):
        print("  adb not on PATH; skipping")
        return None
    devices = adb("devices").stdout.splitlines()[1:]
    if not any("\tdevice" in d for d in devices):
        print("  no device in 'device' state; skipping")
        return None
    if adb("push", str(binary), REMOTE).returncode != 0:
        print("  push failed")
        return None
    adb("shell", f"chmod 755 {REMOTE}")
    args = f"{REMOTE}" + (" -v" if verbose else "")
    proc = adb("shell", args)
    adb("shell", f"rm -f {REMOTE}")
    if proc.returncode != 0:
        print(f"  run failed (exit {proc.returncode})")
        print(proc.stdout[-2000:])
        return None
    return proc.stdout.replace("\r\n", "\n")


def run_locally(exe, verbose):
    print("running on host")
    cmd = [str(exe)] + (["-v"] if verbose else [])
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        print(f"  exit {proc.returncode}")
        print(proc.stdout[-2000:])
        return None
    return proc.stdout.replace("\r\n", "\n")


def parse(output):
    meta, hashes, total = [], {}, None
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("#"):
            meta.append(line)
        elif line.startswith("TOTAL "):
            total = line.split()[1]
        elif " " in line:
            name, _, h = line.rpartition(" ")
            hashes[name] = h
    return meta, hashes, total


def compare(host_out, droid_out):
    hm, hh, ht = parse(host_out)
    dm, dh, dt = parse(droid_out)
    print()
    print("=" * 70)
    print("comparison")
    print("=" * 70)
    for line in hm:
        print(f"  host    {line}")
    for line in dm:
        print(f"  android {line}")
    print()

    # Struct layout must agree or nothing else is meaningful.
    def sizes(meta):
        return [m for m in meta if "sizeof" in m]
    if sizes(hm) != sizes(dm):
        print("  !! STRUCT LAYOUT DIFFERS between architectures")
        print("     everything below is suspect until that is resolved")
    else:
        print("  struct layout            identical")

    print(f"  host TOTAL               {ht}")
    print(f"  android TOTAL            {dt}")
    if ht and ht == dt:
        print()
        print("  RESULT: IDENTICAL - every function produced the same context on both")
        print("          architectures.")
        return True

    if not hh or not dh:
        print()
        print("  TOTAL differs. Re-run with -v to get per-function hashes.")
        return False

    names = [n for n in hh if n in dh]
    bad = [n for n in names if hh[n] != dh[n]]
    print(f"  functions compared       {len(names)}")
    print(f"  functions DISAGREEING    {len(bad)}")
    if bad:
        print()
        print("  first disagreements:")
        for n in bad[:25]:
            print(f"    {n:<26} host {hh[n]}  android {dh[n]}")
        if len(bad) > 25:
            print(f"    ... and {len(bad) - 25} more")
    missing = [n for n in hh if n not in dh]
    if missing:
        print(f"  present only on host     {len(missing)}")
    return not bad


def main():
    ap = argparse.ArgumentParser(description="Differential test: x86-64 vs ARM64")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--host-only", action="store_true")
    ap.add_argument("--quiet", action="store_true",
                    help="aggregate hash only (default compares per function)")
    a = ap.parse_args()
    verbose = not a.quiet

    if not (DT / "difftest.cpp").is_file():
        print("error: no harness. run: python tools/gen_difftest.py", file=sys.stderr)
        return 1

    host_exe = DT / "difftest_host.exe"
    droid_bin = DT / "difftest_android"
    if not a.no_build:
        host_exe = build_host()
        if not host_exe:
            return 1
        if not a.host_only:
            droid_bin = build_android()
            if not droid_bin:
                return 1
        print()

    host_out = run_locally(host_exe, verbose)
    if host_out is None:
        return 1
    if a.host_only:
        meta, hashes, total = parse(host_out)
        for m in meta:
            print(f"  {m}")
        print(f"  cases {len(hashes)}  TOTAL {total}")
        return 0

    droid_out = run_on_device(droid_bin, verbose)
    if droid_out is None:
        return 1

    (DT / "host.txt").write_text(host_out, encoding="utf-8", newline="\n")
    (DT / "android.txt").write_text(droid_out, encoding="utf-8", newline="\n")
    ok = compare(host_out, droid_out)
    print()
    print(f"  raw output saved to {(DT / 'host.txt').relative_to(ROOT)} "
          f"and {(DT / 'android.txt').relative_to(ROOT)}")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
