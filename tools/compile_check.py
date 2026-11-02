#!/usr/bin/env python3
"""
compile_check.py - compile the recompiled game code and report what breaks.

Phase 1.5 is the host build: prove the generated C++ is valid before adding any
other variable. Phase 2 is the same code for arm64-v8a, which is the real question
for this project, since the generated code reaches SIMD through SIMD Everywhere
(the translation units include <x86/sse.h> and <x86/avx.h>) and simde is what has
to map those onto NEON.

Compiling only, never linking: the goal is to find code the compiler rejects, not
to produce a runnable binary.

  host     clang-cl against the xwin winsysroot, x86_64-pc-windows-msvc
  android  the NDK's aarch64-linux-android33-clang++

Usage:
  python tools/compile_check.py                     host, the tu build
  python tools/compile_check.py --target android
  python tools/compile_check.py --target both
  python tools/compile_check.py --build disc
  python tools/compile_check.py --target android --limit 10
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERATED = ROOT / "recomp" / "generated"
SIMDE = ROOT / "third_party" / "XenonRecomp" / "thirdparty" / "simde"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
OUT = ROOT / "build" / "compile-check"

NDK_HOME = os.environ.get("ANDROID_NDK_HOME") or str(
    Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk" / "28.2.13676358")
ANDROID_API = 33


def host_compiler():
    cl = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "clang-cl.exe"
    return cl if cl.is_file() else (shutil.which("clang-cl") and Path(shutil.which("clang-cl")))


def android_compiler():
    for host in ("windows-x86_64", "linux-x86_64", "darwin-x86_64"):
        bindir = Path(NDK_HOME) / "toolchains" / "llvm" / "prebuilt" / host / "bin"
        for name in (f"aarch64-linux-android{ANDROID_API}-clang++.cmd",
                     f"aarch64-linux-android{ANDROID_API}-clang++"):
            p = bindir / name
            if p.is_file():
                return p
    return None


def host_command(src, obj, gen):
    # /arch:AVX is required, not an optimisation. simde reaches rounding through
    # __builtin_roundevenf; with SSE4.1 available Clang lowers that to a ROUNDPS
    # instruction, and without it emits a call to roundevenf, which the MSVC CRT
    # does not provide and the link then fails on. DAORecomp requires an AVX CPU
    # (2011+) for the same reason, and XenonRecomp's own tests build with
    # -march=sandybridge.
    return [str(host_compiler()), "/c", "/std:c++17", "/EHsc", "/O2", "/W0",
            "/arch:AVX",
            "-winsysroot", str(WINSYSROOT),
            "/I", str(gen), "/I", str(SIMDE),
            f"/Fo{obj}", str(src)]


def android_command(src, obj, gen):
    # No equivalent flag is needed: aarch64 has FRINTN natively, so Clang lowers
    # __builtin_roundevenf to a single instruction with no libcall.
    return [str(android_compiler()), "-c", "-std=c++17", "-O2", "-w",
            "-fPIC",
            "-I", str(gen), "-I", str(SIMDE),
            "-o", str(obj), str(src)]


TARGETS = {
    "host": {"compiler": host_compiler, "command": host_command, "ext": ".obj",
             "label": "x86_64-pc-windows-msvc (clang-cl + xwin)"},
    "android": {"compiler": android_compiler, "command": android_command, "ext": ".o",
                "label": f"aarch64-linux-android{ANDROID_API} (NDK)"},
}

FIRST_ERROR = re.compile(r"error|fatal", re.I)


def compile_one(spec, src, outdir, gen):
    obj = outdir / (src.stem + spec["ext"])
    cmd = spec["command"](src, obj, gen)
    started = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return {
        "src": src,
        "ok": proc.returncode == 0,
        "code": proc.returncode,
        "seconds": time.time() - started,
        "size": obj.stat().st_size if obj.is_file() else 0,
        "output": (proc.stdout or "") + (proc.stderr or ""),
    }


def run_target(name, build, limit, jobs):
    spec = TARGETS[name]
    compiler = spec["compiler"]()
    gen = GENERATED / build
    print()
    print("=" * 70)
    print(f"target: {name}  -  {spec['label']}")
    print("=" * 70)
    if not compiler:
        print("  compiler not found; skipping.")
        if name == "android":
            print(f"  looked under {NDK_HOME}")
        return None
    print(f"  compiler  {compiler}")
    if not gen.is_dir():
        print(f"  no generated code at {gen.relative_to(ROOT)}")
        print("  run: python tools/recompile.py " + ("disc" if build == "ppc" else "tu"))
        return None

    sources = sorted(gen.glob("ppc_recomp.*.cpp"), key=lambda p: int(p.stem.split(".")[1]))
    mapping = gen / "ppc_func_mapping.cpp"
    if mapping.is_file():
        sources.append(mapping)
    if limit:
        sources = sources[:limit]
    print(f"  sources   {len(sources)} translation units")

    outdir = OUT / name / build
    outdir.mkdir(parents=True, exist_ok=True)

    started = time.time()
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(compile_one, spec, s, outdir, gen) for s in sources]
        done = 0
        for fut in concurrent.futures.as_completed(futures):
            r = fut.result()
            results.append(r)
            done += 1
            if not r["ok"] or done % 25 == 0 or done == len(sources):
                flag = "" if r["ok"] else "  <-- FAILED"
                print(f"  [{done:>3}/{len(sources)}] {r['src'].name}{flag}")
    elapsed = time.time() - started

    failed = [r for r in results if not r["ok"]]
    total_size = sum(r["size"] for r in results)
    print()
    print(f"  compiled  {len(results) - len(failed)}/{len(results)} ok")
    print(f"  objects   {total_size:,} bytes")
    print(f"  elapsed   {elapsed:.1f}s wall ({sum(r['seconds'] for r in results):.0f}s cpu, {jobs} jobs)")

    if failed:
        print(f"  FAILURES  {len(failed)}")
        for r in failed[:5]:
            print(f"    --- {r['src'].name} (exit {r['code']}) ---")
            lines = [l for l in r["output"].splitlines() if FIRST_ERROR.search(l)]
            for line in (lines or r["output"].splitlines())[:6]:
                print(f"      {line.strip()[:160]}")
        if len(failed) > 5:
            print(f"    ... and {len(failed) - 5} more")
    return not failed


def main():
    ap = argparse.ArgumentParser(description="Compile the recompiled game code")
    ap.add_argument("--target", default="host", choices=["host", "android", "both"])
    ap.add_argument("--build", default="ppc_tu6", choices=["ppc", "ppc_tu6"],
                    help="which generated build (default: ppc_tu6, the Title Update build)")
    ap.add_argument("--limit", type=int, default=0, help="only the first N translation units")
    ap.add_argument("--jobs", type=int, default=0, help="parallel compiles (default: cpu count)")
    a = ap.parse_args()

    jobs = a.jobs or (os.cpu_count() or 4)
    names = ["host", "android"] if a.target == "both" else [a.target]

    outcomes = {}
    for name in names:
        outcomes[name] = run_target(name, a.build, a.limit, jobs)

    print()
    print("=" * 70)
    print("summary")
    print("=" * 70)
    for name, ok in outcomes.items():
        state = "PASS" if ok else ("SKIPPED" if ok is None else "FAIL")
        print(f"  {name:<10} {state}")
    return 0 if all(v is not False for v in outcomes.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
