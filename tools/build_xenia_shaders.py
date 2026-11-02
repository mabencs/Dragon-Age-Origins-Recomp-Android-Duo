#!/usr/bin/env python3
"""
build_xenia_shaders.py - compile Xenia's own helper shaders to SPIR-V headers.

Phase 3.1b. Xenia's Vulkan backend `#include`s its internal shaders as C headers
containing SPIR-V words - gamma application, texture loads, host depth store, the
presenter's output filters and so on. Those headers are *build artefacts*: upstream
ships only a placeholder, and DAORecomp ships 126 pre-generated DXBC ones because it
is a Direct3D project. Nothing generates the SPIR-V equivalents, so six translation
units in the grafted backend cannot compile until this runs.

DAORecomp vendors Xenia's own `tools_build/compile_shader_spirv.py`, which wants
glslangValidator, spirv-opt and spirv-dis from the Vulkan SDK. We have no Vulkan SDK,
but the Android NDK ships **spirv-opt**, **spirv-dis** and **glslc** - shaderc's
glslang frontend. So this is that pipeline with glslc substituted for
glslangValidator, which only changes how the stage and target are named.

The emitted header format matches upstream exactly (disassembly in an `#if 0` block,
then a `const uint32_t <identifier>[]` array), because the backend's source includes
these by name and indexes them as arrays.

Usage:
  python tools/build_xenia_shaders.py
  python tools/build_xenia_shaders.py --force      ignore up-to-date checks
  python tools/build_xenia_shaders.py --only gamma
"""
import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TREE = ROOT / "build" / "runtime-tree" / "src"

NDK_HOME = os.environ.get("ANDROID_NDK_HOME") or str(
    Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk" / "28.2.13676358")

# Xenia names a shader's stage in its filename: foo.cs.xesl is a compute shader.
# glslc spells the stages differently from glslangValidator.
STAGES = {"vs": "vertex", "hs": "tesscontrol", "ds": "tesseval",
          "gs": "geometry", "ps": "fragment", "cs": "compute"}

# .xesl is Xenia's own cross-language shader source; it is compiled by wrapping it
# in a GLSL translation unit that includes it.
XESL_WRAPPER = (
    "#version 460\n"
    "#extension GL_EXT_control_flow_attributes : require\n"
    "#extension GL_EXT_samplerless_texture_functions : require\n"
    "#extension GL_GOOGLE_include_directive : require\n"
    '#include "%s"\n'
)


# The app's own shaders (app/src/main/cpp/shaders/*.glsl, named like Xenia's by stage),
# compiled into the tree's ui bytecode folder beside Xenia's. PLAN 9.7's FXAA includes
# NVIDIA's FXAA 3.11 header from Xenia's third_party, through a copy patched for
# Vulkan's separate texture and sampler: GLSL constructs a combined sampler only at a
# texture function's call, so FxaaTex becomes a placeholder int and the fetches name the
# shader's own texture and sampler, which it defines as XE_FXAA_SOURCE.
APP_SHADERS = ROOT / "app" / "src" / "main" / "cpp" / "shaders"
# PLAN 9.2's HDR10 variants of Xenia's presenter passes (warden_hdr_*.ps.glsl) include
# the passes' own .xesli, untouched, from the tree's ui shader folder.
UI_SHADERS = TREE / "xenia" / "ui" / "shaders"
FXAA_HEADER = ROOT / "third_party" / "xenia-canary" / "third_party" / "fxaa" / "FXAA3_11.h"
APP_INCLUDE = TREE.parent / "warden-shader-include"
FXAA_PATCHES = [
    ("    #define FxaaSat(x) clamp(x, 0.0, 1.0)\n    #define FxaaTex sampler2D\n",
     "    #define FxaaSat(x) clamp(x, 0.0, 1.0)\n    #define FxaaTex int\n"),
    ("    #define FxaaTexTop(t, p) textureLod(t, p, 0.0)\n"
     "    #define FxaaTexOff(t, p, o, r) textureLodOffset(t, p, 0.0, o)\n"
     "    #if (FXAA_GATHER4_ALPHA == 1)\n"
     "        // use #extension GL_ARB_gpu_shader5 : enable\n"
     "        #define FxaaTexAlpha4(t, p) textureGather(t, p, 3)\n"
     "        #define FxaaTexOffAlpha4(t, p, o) textureGatherOffset(t, p, o, 3)\n"
     "        #define FxaaTexGreen4(t, p) textureGather(t, p, 1)\n"
     "        #define FxaaTexOffGreen4(t, p, o) textureGatherOffset(t, p, o, 1)\n",
     "    #define FxaaTexTop(t, p) textureLod(XE_FXAA_SOURCE, p, 0.0)\n"
     "    #define FxaaTexOff(t, p, o, r) textureLodOffset(XE_FXAA_SOURCE, p, 0.0, o)\n"
     "    #if (FXAA_GATHER4_ALPHA == 1)\n"
     "        #define FxaaTexAlpha4(t, p) textureGather(XE_FXAA_SOURCE, p, 3)\n"
     "        #define FxaaTexOffAlpha4(t, p, o) textureGatherOffset(XE_FXAA_SOURCE, p, o, 3)\n"
     "        #define FxaaTexGreen4(t, p) textureGather(XE_FXAA_SOURCE, p, 1)\n"
     "        #define FxaaTexOffGreen4(t, p, o) textureGatherOffset(XE_FXAA_SOURCE, p, o, 1)\n"),
]


def app_include_dir():
    """The patched FXAA header's folder, rewritten when the original is newer."""
    APP_INCLUDE.mkdir(parents=True, exist_ok=True)
    patched = APP_INCLUDE / FXAA_HEADER.name
    if FXAA_HEADER.is_file() and (not patched.exists() or
                                  patched.stat().st_mtime < FXAA_HEADER.stat().st_mtime):
        header = FXAA_HEADER.read_text(encoding="utf-8", errors="replace")
        for old, new in FXAA_PATCHES:
            if header.count(old) != 1:
                raise SystemExit("error: FXAA3_11.h has moved; the patch for "
                                 + old.strip().splitlines()[-1] + " does not apply")
            header = header.replace(old, new)
        patched.write_text(header, encoding="utf-8", newline="\n")
    return APP_INCLUDE


def shader_tools():
    for host in ("windows-x86_64", "linux-x86_64", "darwin-x86_64"):
        d = Path(NDK_HOME) / "shader-tools" / host
        if d.is_dir():
            ext = ".exe" if host.startswith("windows") else ""
            tools = (d / f"glslc{ext}", d / f"spirv-opt{ext}", d / f"spirv-dis{ext}")
            if all(t.is_file() for t in tools):
                return tools
    return None


def jobs(only, force):
    out = []
    for sub in ("gpu", "ui"):
        shader_dir = TREE / "xenia" / sub / "shaders"
        if not shader_dir.is_dir():
            continue
        out_dir = shader_dir / "bytecode" / "vulkan_spirv"
        includes = list(shader_dir.glob("*.xesli")) + list(shader_dir.glob("*.glsli"))
        newest_include = max((p.stat().st_mtime for p in includes), default=0)
        # For Vulkan take the .glsl variants, not the .hlsl ones.
        srcs = sorted(list(shader_dir.glob("*.xesl")) + list(shader_dir.glob("*.glsl")))
        for src in srcs:
            ident = src.name.rsplit(".", 1)[0].replace(".", "_")
            if ident[-2:] not in STAGES:
                continue
            if only and only not in src.name:
                continue
            dst = out_dir / f"{ident}.h"
            if not force and dst.exists() and \
                    dst.stat().st_mtime >= max(src.stat().st_mtime, newest_include):
                continue
            out.append((src, dst, ident))
    if APP_SHADERS.is_dir():
        out_dir = TREE / "xenia" / "ui" / "shaders" / "bytecode" / "vulkan_spirv"
        app_includes = list(APP_SHADERS.glob("*.glsli")) + list(UI_SHADERS.glob("*.xesli"))
        newest_app_include = max((p.stat().st_mtime for p in app_includes), default=0)
        for src in sorted(APP_SHADERS.glob("*.glsl")):
            ident = src.name.rsplit(".", 1)[0].replace(".", "_")
            if ident[-2:] not in STAGES or (only and only not in src.name):
                continue
            dst = out_dir / f"{ident}.h"
            newest = max(src.stat().st_mtime, newest_app_include,
                         FXAA_HEADER.stat().st_mtime if FXAA_HEADER.is_file() else 0)
            if not force and dst.exists() and dst.stat().st_mtime >= newest:
                continue
            out.append((src, dst, ident))
    return out


def emit_header(dst, ident, spv_bytes, disassembly):
    with open(dst, "w", encoding="utf-8", newline="\n") as out:
        out.write("// Generated with `xb buildshaders`.\n#if 0\n")
        if disassembly:
            out.write(disassembly)
            if not disassembly.endswith("\n"):
                out.write("\n")
        out.write("#endif\n\nconst uint32_t %s[] = {" % ident)
        if len(spv_bytes) % 4:
            raise ValueError("SPIR-V binary is misaligned")
        for index in range(len(spv_bytes) // 4):
            word = struct.unpack_from("<I", spv_bytes, index * 4)[0]
            out.write("\n    " if index % 6 == 0 else " ")
            out.write("0x%08X," % word)
        out.write("\n};\n")


def compile_one(tools, job):
    glslc, spirv_opt, spirv_dis = tools
    src, dst, ident = job
    stage = STAGES[ident[-2:]]
    dst.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "raw.spv"
        opt = Path(tmp) / "opt.spv"

        # Vulkan 1.1 (SPIR-V 1.3), not 1.3: targeting 1.3 makes glslang emit the
        # workgroup size as SPIR-V 1.6's OpExecutionModeId LocalSizeId, and the
        # Adreno 740 driver (Qualcomm build of 2023-12-27, Vulkan 1.3.128) runs those
        # compute shaders with the wrong workgroup size - a 1280x720 resolve copied
        # one row per workgroup row, texture loads converted fragments (BUG-039).
        # SPIR-V 1.3 declares it with the plain LocalSize execution mode.
        args = [str(glslc), "-fshader-stage=" + stage, "--target-env=vulkan1.1",
                "-DSHADING_LANGUAGE_GLSL_XE=1", f"-I{src.parent}",
                "-o", str(raw)]
        if src.parent == APP_SHADERS:
            args[-2:-2] = [f"-I{app_include_dir()}", f"-I{UI_SHADERS}"]
        if src.suffix == ".xesl":
            # Fed on stdin wrapped in a GLSL unit that includes the .xesl.
            args += ["-x", "glsl", "-"]
            stdin = XESL_WRAPPER % src.name
        else:
            args += [str(src)]
            stdin = None

        r = subprocess.run(args, input=stdin, text=True, capture_output=True, errors="replace")
        if r.returncode != 0:
            return src.name, False, "glslc: " + (r.stderr or r.stdout).strip()[:500]

        # Upstream also passes --canonicalize-ids, which the NDK's spirv-opt does not
        # recognise. It only renumbers result ids for reproducible output, so dropping
        # it changes nothing about the generated code.
        r = subprocess.run([str(spirv_opt), "-O", "-O", str(raw), "-o", str(opt)],
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            return src.name, False, "spirv-opt: " + (r.stderr or r.stdout).strip()[:500]

        r = subprocess.run([str(spirv_dis), str(opt)], capture_output=True, text=True,
                           errors="replace")
        disassembly = r.stdout if r.returncode == 0 else ""

        try:
            emit_header(dst, ident, opt.read_bytes(), disassembly)
        except ValueError as e:
            return src.name, False, str(e)
    return src.name, True, ""


def main():
    ap = argparse.ArgumentParser(description="Compile Xenia's shaders to SPIR-V headers")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", help="only sources whose name contains this")
    ap.add_argument("--jobs", type=int, default=0)
    a = ap.parse_args()

    if not TREE.is_dir():
        print("error: no assembled tree. run: python tools/assemble_runtime.py", file=sys.stderr)
        return 1
    tools = shader_tools()
    if not tools:
        print(f"error: NDK shader tools not found under {NDK_HOME}/shader-tools", file=sys.stderr)
        print("       need glslc, spirv-opt and spirv-dis", file=sys.stderr)
        return 1
    print("tools")
    for t in tools:
        print(f"  {t}")

    todo = jobs(a.only, a.force)
    print(f"\n{len(todo)} shader(s) to compile")
    if not todo:
        print("  everything is up to date")
        return 0

    workers = a.jobs or (os.cpu_count() or 4)
    ok, failed = 0, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = [pool.submit(compile_one, tools, j) for j in todo]
        for i, fut in enumerate(concurrent.futures.as_completed(futs), 1):
            name, good, err = fut.result()
            if good:
                ok += 1
            else:
                failed.append((name, err))
            if i % 25 == 0 or i == len(todo):
                print(f"  [{i}/{len(todo)}] {ok} ok, {len(failed)} failed")

    if failed:
        print(f"\n{len(failed)} failed:")
        for name, err in failed[:10]:
            print(f"  --- {name}")
            for line in err.splitlines()[:4]:
                print(f"      {line[:150]}")
        if len(failed) > 10:
            print(f"  ... and {len(failed) - 10} more")
    print(f"\n{ok}/{len(todo)} shaders compiled")
    return 0 if not failed else 2


if __name__ == "__main__":
    sys.exit(main())
