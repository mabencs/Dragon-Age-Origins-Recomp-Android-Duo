#!/usr/bin/env python3
"""
compile_runtime.py - compile the grafted Xenia runtime, and report what breaks.

Phase 3.1/3.2. `tools/assemble_runtime.py` produces a tree with Xenia's Vulkan
backend grafted onto DAORecomp's Xenos frontend. This compiles it and classifies the
failures, so progress through the graft is measurable rather than a wall of errors.

It compiles only - never links. The goal at this stage is to drive the number of
failing translation units to zero, one cause at a time.

  android   the NDK's aarch64-linux-android33-clang++   (the real target)
  host      clang-cl against the xwin winsysroot        (reference / faster iteration)

Usage:
  python tools/compile_runtime.py                      android, the Vulkan subset
  python tools/compile_runtime.py --target host
  python tools/compile_runtime.py --scope all          every .cc in the tree
  python tools/compile_runtime.py --verbose            show full errors
  python tools/compile_runtime.py --file vulkan_graphics_system.cc
"""
import argparse
import collections
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TREE = ROOT / "build" / "runtime-tree" / "src"
CANARY = ROOT / "third_party" / "xenia-canary"
XENON_TP = ROOT / "third_party" / "XenonRecomp" / "thirdparty"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
OBJ = ROOT / "build" / "runtime-obj"

NDK_HOME = os.environ.get("ANDROID_NDK_HOME") or str(
    Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk" / "28.2.13676358")
ANDROID_API = 33

# Xenia includes most dependencies as "third_party/<name>/...", resolved from the
# repository root, so the Canary checkout itself is an include root. glslang is the
# exception: the SPIR-V translator includes it as "glslang/Include/...", which needs
# glslang's own directory on the path.
INCLUDES = [TREE, CANARY,
            CANARY / "third_party" / "Vulkan-Headers" / "include",
            CANARY / "third_party" / "glslang",
            # Xenia includes fmt by its full path, but fmt's own sources include
            # each other as <fmt/...>, so its include/ dir is needed too.
            CANARY / "third_party" / "fmt" / "include"]

# Third-party translation units that have to be compiled for the link to resolve.
# Header-only libraries still need one unit somewhere, and these are the ones Xenia
# relies on another file to provide. tools/prepare_deps.py generates the rest.
THIRD_PARTY_SOURCES = [
    (CANARY / "third_party" / "fmt" / "src" / "format.cc", "fmt::vformat"),
    (CANARY / "third_party" / "snappy" / "snappy.cc", "snappy, for GPU traces"),
    (CANARY / "third_party" / "snappy" / "snappy-sinksource.cc", "snappy"),
    (CANARY / "third_party" / "snappy" / "snappy-stubs-internal.cc", "snappy"),
    (ROOT / "build" / "deps-gen" / "stb_impl.cc", "stb image, for png_utils"),
]

# glslang's SPIR-V assembler. Xenia's SPIR-V shader translator calls spv::Builder
# directly, which is 57 of the symbols the full-scope link asks for - a missing
# library rather than anything to do with porting.
#
# All of SPIRV/*.cpp except GlslangToSpv.cpp, which is the rule Xenia's own
# third_party/CMakeLists.txt uses (it builds a `glslang-spirv` target with exactly
# that filter). GlslangToSpv is the GLSL front end's output stage and would drag in
# the whole compiler; Xenia only wants the assembler.
_GLSLANG_SPIRV = CANARY / "third_party" / "glslang" / "SPIRV"
if _GLSLANG_SPIRV.is_dir():
    for _src in sorted(_GLSLANG_SPIRV.glob("*.cpp")):
        if _src.name != "GlslangToSpv.cpp":
            THIRD_PARTY_SOURCES.append((_src, "glslang SPIR-V assembler"))

# The Vulkan work. Everything else in the tree is DAORecomp's existing frontend,
# which already builds in their project.
VULKAN_SCOPE = ["xenia/gpu/vulkan", "xenia/ui/vulkan"]
# Just enough to bring up a Vulkan device and report what it supports. The
# full runtime also needs host services DAORecomp implements outside the
# vendored tree, so this scope exists to get Xenia's Vulkan code running on
# the handheld without waiting for Phase 4.
DEVICE_SCOPE = ["xenia/ui/vulkan", "xenia/ui", "xenia/base"]
VULKAN_GLOBS = [("xenia/gpu", "spirv_shader_translator*.cc"),
                ("xenia/gpu", "spirv_builder.cc"),
                ("xenia/gpu", "spirv_shader.cc")]

# ---------------------------------------------------------------- source selection
#
# Phase 3.1c. The assembled tree carries every platform Xenia supports and both GPU
# backends. An Android build must pick from it; compiling the lot is what produced 76
# failures that looked like porting problems and were not.
#
# Two kinds of exclusion, kept apart deliberately: files that are *wrong* for this
# target and can never build, and files that simply are not needed yet.

# Wrong for Android. These will never be part of this port.
EXCLUDE_PLATFORM = [
    (re.compile(r"_win\.cc$"), "Windows-only"),
    (re.compile(r"_mac\.cc$"), "macOS-only"),
    (re.compile(r"_amd64\.cc$"), "x86-64 code generation"),
    (re.compile(r"/gpu/d3d12/"), "Direct3D 12 GPU backend"),
    # The DXBC shader translator belongs to the Direct3D path; this build uses
    # Xenia's SPIR-V translator instead. It also drags in third_party/dxbc for a
    # checksum routine that nothing on this target needs.
    (re.compile(r"/gpu/dxbc"), "DXBC shader translator; SPIR-V is used here"),
    (re.compile(r"/ui/d3d12/"), "Direct3D 12 UI backend"),
    # Xenia sets XE_PLATFORM_LINUX on Android as well as on desktop Linux, so the
    # desktop windowing files are reachable and pull in SDL2, GTK, GLib, X11 and xcb.
    (re.compile(r"_gnulinux\.cc$"), "desktop GNU/Linux (SDL2, xcb)"),
    (re.compile(r"_gtk\.cc$"), "GTK desktop windowing"),
    (re.compile(r"windowed_app_main_posix\.cc$"), "POSIX desktop entry point"),
    # Both of these define main(). The runtime is linked as a library and our own
    # entry point comes from the Android app via JNI, so neither belongs here.
    (re.compile(r"console_app_main_(posix|android)\.cc$"),
     "console entry point; the app provides main() via JNI"),
    # Xenia's CPU backend detects ARM64 instruction-set features with xbyak_aarch64.
    # This port uses static recompilation rather than Xenia's JIT, and nothing else
    # in the tree references what this file exports.
    (re.compile(r"platform_arm64\.cc$"), "Xenia CPU-backend ARM64 feature detection"),
    # DAORecomp's frame pacer (frame generation's presentation) is built on Windows
    # waitable timers (CreateWaitableTimerExW / WaitForMultipleObjects).
    # app/src/main/cpp/dao_frame_pacer_android.cc is its port (PLAN 9.14).
    (re.compile(r"/ui/dao_frame_pacer\.cc$"), "Windows waitable-timer frame pacer; dao_frame_pacer_android.cc ports it"),
]

# Not needed yet. Each line says what would bring it back.
EXCLUDE_DEFERRED = [
    (re.compile(r"_demo\.cc$|window_demo"), "demo programs; never shipped"),
    # The whole window layer, not just the ImGui parts. xe::ui::Window needs ImGui,
    # and window_android.cc subclasses it, so they go together. The Vulkan presenter
    # needs only Surface (vulkan_presenter.h references Window not at all), so a
    # device-and-presenter build links without any of this. Phase 4 supplies a real
    # surface from an Android SurfaceView.
    # menu_item.cc's factory is implemented per platform (menu_item_win/_gtk) and
    # Android has no menu bar; windowed_app_context_android.cc drives Xenia's own
    # Android window, which the app shell replaces in Phase 4.
    (re.compile(r"/ui/menu_item\.cc$|/ui/windowed_app_context_android\.cc$"),
     "Xenia's menu and Android window context - the app shell replaces these"),
    # window.cc is IN as of Phase 4.2a - the presenter requires a Window, and the only
    # thing keeping window.cc out was two unused ImGui includes (assemble_runtime.py,
    # "drop window.cc's unused ImGui includes").
    #
    # window_android.cc stays out, and not for a build reason: AndroidWindow casts
    # app_context() to AndroidWindowedAppContext and calls GetActivityWindow /
    # SetActivityWindow on it, so it is bound to Xenia's own Java activity. This app
    # owns its activity, so it implements Window itself - app/src/main/cpp/
    # warden_window.cc. The ImGui overlay proper is still Phase 10.
    (re.compile(r"/ui/imgui_|/ui/window_android\.cc$"),
     "ImGui overlay (Phase 10), and Xenia's own Android window - the app shell replaces it"),
]


def excluded(path):
    """Return (reason, kind) if this source is not part of the Android build."""
    rel = path.as_posix()
    for pattern, reason in EXCLUDE_PLATFORM:
        if pattern.search(rel):
            return reason, "platform"
    for pattern, reason in EXCLUDE_DEFERRED:
        if pattern.search(rel):
            return reason, "deferred"
    return None, None

# The file part must tolerate a Windows drive letter, which itself contains a colon.
ERROR_LINE = re.compile(
    r"^(?P<file>(?:[A-Za-z]:)?[^:]+):(?P<line>\d+):(?P<col>\d+):\s*(?:fatal\s+)?error:\s*(?P<msg>.*)$",
    re.M)
MISSING_INC = re.compile(r"'([^']+)' file not found")


def ndk_tool(name):
    for host in ("windows-x86_64", "linux-x86_64", "darwin-x86_64"):
        d = Path(NDK_HOME) / "toolchains" / "llvm" / "prebuilt" / host / "bin"
        for cand in (d / (name + ".cmd"), d / name, d / (name + ".exe")):
            if cand.is_file():
                return cand
    return None


def host_tool():
    p = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin" / "clang-cl.exe"
    return p if p.is_file() else (shutil.which("clang-cl") and Path(shutil.which("clang-cl")))


def android_cmd(src, obj):
    cc = ndk_tool(f"aarch64-linux-android{ANDROID_API}-clang++")
    # Xenia uses exceptions and RTTI; disabling either does not build.
    args = [str(cc), "-c", "-std=c++20", "-O1", "-fPIC",
            "-fexceptions", "-frtti",
            # No platform defines needed: xenia/base/platform.h detects __ANDROID__
            # and sets XE_PLATFORM_ANDROID, and vulkan_api.h then defines
            # VK_USE_PLATFORM_ANDROID_KHR from that.
            # Keep the diagnostics short: we want causes, not a transcript.
            "-ferror-limit=4", "-fno-caret-diagnostics", "-fno-color-diagnostics"]
    for inc in INCLUDES:
        args += ["-I", str(inc)]
    args += ["-o", str(obj), str(src)]
    return args, cc


def host_cmd(src, obj):
    cl = host_tool()
    args = [str(cl), "/c", "/std:c++20", "/O1", "/W0", "/MT", "/arch:AVX", "/EHsc",
            "-winsysroot", str(WINSYSROOT),
            "-ferror-limit=4", "-fno-caret-diagnostics"]
    for inc in INCLUDES:
        args += ["/I", str(inc)]
    args += [f"/Fo{obj}", str(src)]
    return args, cl


TARGETS = {"android": android_cmd, "host": host_cmd}


def sources(scope, only, apply_selection=True):
    """Sources for this scope, plus the exclusions that were applied."""
    out = []
    if scope == "device":
        for d in DEVICE_SCOPE:
            # Flat glob for xenia/ui: recursing would pull in ui/d3d12 too.
            pattern = (TREE / d).glob if d == "xenia/ui" else (TREE / d).rglob
            out += sorted(pattern("*.cc"))
        out = sorted(set(out))
    elif scope in ("all", "android"):
        out = sorted(TREE.rglob("*.cc"))
    else:
        for d in VULKAN_SCOPE:
            out += sorted((TREE / d).glob("*.cc"))
        for d, pat in VULKAN_GLOBS:
            out += sorted((TREE / d).glob(pat))

    if scope in ("android", "device"):
        for path, _why in THIRD_PARTY_SOURCES:
            if path.is_file():
                out.append(path)

    skipped = []
    if apply_selection:
        kept = []
        for p in out:
            reason, kind = excluded(p)
            if reason:
                skipped.append((p, reason, kind))
            else:
                kept.append(p)
        out = kept

    if only:
        out = [p for p in out if only in p.name]
    return out, skipped


def object_path(target, src):
    """Stable, collision-free object name derived from the source's path.

    Naming by a loop index meant a second run with a different scope wrote new
    objects beside the old ones, and the link then saw the same translation unit
    twice. Deriving the name from the path makes re-runs overwrite in place.
    """
    try:
        rel = src.relative_to(TREE).as_posix()
    except ValueError:
        # Third-party sources live outside the assembled tree.
        rel = "thirdparty_" + src.name
    rel = rel.replace("/", "_")
    ext = ".obj" if target == "host" else ".o"
    return OBJ / target / (rel.rsplit(".", 1)[0] + ext)


def compile_one(target, src, idx):
    obj = object_path(target, src)
    obj.parent.mkdir(parents=True, exist_ok=True)
    args, _tool = TARGETS[target](src, obj)
    proc = subprocess.run(args, capture_output=True, text=True, errors="replace")
    return src, proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def classify(output):
    """Group an error blob into a cause we can act on."""
    missing = MISSING_INC.findall(output)
    if missing:
        return ("missing include", missing[0])
    for m in ERROR_LINE.finditer(output):
        msg = m.group("msg").strip()
        # Collapse identifiers so similar errors group together.
        key = re.sub(r"'[^']*'", "'X'", msg)[:90]
        return ("compile error", key)
    if "error" in output.lower():
        return ("other", output.strip().splitlines()[0][:90] if output.strip() else "?")
    return ("unknown", "?")


def main():
    ap = argparse.ArgumentParser(description="Compile the grafted Xenia runtime")
    ap.add_argument("--target", default="android", choices=["android", "host"])
    ap.add_argument("--scope", default="vulkan", choices=["vulkan", "device", "android", "all"],
                    help="vulkan = the graft only; android = whole tree with "
                         "platform selection applied; all = no selection (diagnostic)")
    ap.add_argument("--file", help="only sources whose name contains this")
    ap.add_argument("--verbose", action="store_true", help="print full error text")
    ap.add_argument("--jobs", type=int, default=0)
    a = ap.parse_args()

    if not TREE.is_dir():
        print("error: no assembled tree. run: python tools/assemble_runtime.py", file=sys.stderr)
        return 1
    _args, tool = TARGETS[a.target](Path("x.cc"), Path("x.o"))
    if not tool:
        print(f"error: no {a.target} compiler found", file=sys.stderr)
        return 1

    # "all" deliberately skips selection so the raw state is still inspectable.
    srcs, skipped = sources(a.scope, a.file, apply_selection=(a.scope != "all"))
    if not srcs:
        print("error: no sources matched", file=sys.stderr)
        return 1

    print(f"target   {a.target}  ({tool})")
    print(f"scope    {a.scope}  ({len(srcs)} translation units)")
    print(f"includes {', '.join(str(i.relative_to(ROOT)) for i in INCLUDES)}")
    if skipped:
        groups = collections.defaultdict(list)
        for path, reason, kind in skipped:
            groups[(kind, reason)].append(path.name)
        plat = sum(len(v) for (k, _r), v in groups.items() if k == "platform")
        defer = sum(len(v) for (k, _r), v in groups.items() if k == "deferred")
        print(f"excluded {len(skipped)} ({plat} wrong for this platform, {defer} deferred)")
        for (kind, reason), names in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            tag = "platform" if kind == "platform" else "deferred"
            print(f"  [{tag}] {len(names):>3}  {reason}")
    print()

    jobs = a.jobs or (os.cpu_count() or 4)
    started = time.time()
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futs = [pool.submit(compile_one, a.target, s, i) for i, s in enumerate(srcs)]
        for fut in concurrent.futures.as_completed(futs):
            results.append(fut.result())
    elapsed = time.time() - started

    ok = [r for r in results if r[1] == 0]
    bad = [r for r in results if r[1] != 0]
    print(f"compiled {len(ok)}/{len(results)} ok   ({elapsed:.1f}s, {jobs} jobs)")

    if not bad:
        print()
        print("  the whole scope compiles.")
        return 0

    causes = collections.defaultdict(list)
    for src, _code, out in bad:
        causes[classify(out)].append(src.name)

    print()
    print("=" * 74)
    print(f"FAILING: {len(bad)} translation units, {len(causes)} distinct causes")
    print("=" * 74)
    for (kind, key), files in sorted(causes.items(), key=lambda kv: -len(kv[1])):
        print(f"\n  [{kind}] {key}")
        print(f"    {len(files)} file(s): {', '.join(sorted(files)[:6])}"
              + (" ..." if len(files) > 6 else ""))

    if a.verbose:
        print()
        print("=" * 74)
        print("FULL OUTPUT (first 3 failures)")
        print("=" * 74)
        for src, code, out in bad[:3]:
            print(f"\n--- {src.name} (exit {code}) ---")
            for line in out.splitlines()[:25]:
                print(f"  {line[:180]}")
    else:
        print()
        print("  re-run with --verbose for full errors, or --file <name> to focus on one")
    return 2


if __name__ == "__main__":
    sys.exit(main())
