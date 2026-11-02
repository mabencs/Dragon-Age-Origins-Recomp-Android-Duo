#!/usr/bin/env python3
"""
build_fsr3_shaders.py - FSR 3.1's shaders for Vulkan, as the FidelityFX SDK's
permutation headers: its upscaler (PLAN 9.4) and its frame generation's two components,
frame interpolation and optical flow (PLAN 9.14).

The FidelityFX SDK (third_party/FidelityFX-SDK, v1.1.4) compiles each FSR 3.1 upscaler
pass - src/backends/vk/shaders/fsr3upscaler/*.glsl - in every combination of its six
options, and its host code binds resources by the names in each blob's reflection. Its
own build does both with FidelityFX_SC, a Windows executable shipped in the SDK, which
this project does not run. So this is that step with the NDK's tools, as
tools/build_xenia_shaders.py does for Xenia's shaders: glslc compiles the same sources
with the same defines (the SDK's CMakeCompileFSR3UpscalerShaders.txt), spirv-opt
optimises, and the reflection - each descriptor's name, binding, set and array size, by
kind - is read back from spirv-dis's disassembly.

Also build/fsr3-shaders/FidelityFX/host/ffx_types.h: the SDK's own, its default context
size doubled. A context is an opaque block of a fixed size that the component's private
state must fit; that state holds wide-character names, and wchar_t is 4 bytes on Android
against Windows' 2, so FSR 3.1's no longer fits in 512 KB. The folder comes first on the
include path (app/src/main/cpp/CMakeLists.txt), for the SDK and the app alike, so both
agree on the size.

And build/fsr3-shaders/frameinterpolation/ffx_frameinterpolation.cpp: the SDK's frame
interpolation, its optical flow vector field pass dispatched over the flow's own buffer
(opticalFlowBufferSize) rather than the display size over the block size. The two agree
when the flow runs at the display size, as the SDK's FSR 3 runs it; this port runs it at
the render size (PLAN 9.14, warden_upscale.cc), and the pass then had half its threads
reading outside the flow. The app compiles this copy in place of the SDK's.

Output, in the layout the SDK's blob accessors
(src/backends/shared/blob_accessors/ffx_<component>_shaderblobs.cpp) include:
build/fsr3-shaders/ffx_<component>_<pass>[_wave64][_16bit]_permutations.h. Each header
has its pass's permutation key, an indirection table from the key's values (64 for the
upscaler's six options, 8 for frame interpolation's three, 2 for optical flow's one) to
the distinct SPIR-V blobs, and the blobs with their reflection. In GLSL the wave64 variants
are the same SPIR-V (the backend asks for 64-lane subgroups at pipeline creation), so
their headers refer to the wave32 tables. FFX_HALF=1 (the _16bit headers) compiles the
16-bit arithmetic paths, which the Adreno 740 runs (shaderFloat16).

Usage:
  python tools/build_fsr3_shaders.py           # what is out of date
  python tools/build_fsr3_shaders.py --force
"""
import argparse
import concurrent.futures
import os
import re
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / "third_party" / "FidelityFX-SDK" / "sdk"
SHADERS = SDK / "src" / "backends" / "vk" / "shaders"
PASS_DIR = SHADERS / "fsr3upscaler"
GPU_INCLUDE = SDK / "include" / "FidelityFX" / "gpu"
OUT = ROOT / "build" / "fsr3-shaders"

NDK_HOME = os.environ.get("ANDROID_NDK_HOME") or str(
    Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk" / "ndk" / "28.2.13676358")

# Each component: its passes, the base arguments and the permutation arguments of its
# CMakeCompile<Component>Shaders.txt - the latter in order, the permutation key's bits, the
# first the lowest (the order its bit fields are declared in, as the SDK's generated keys
# have them) - and its include folder.
COMPONENTS = {
    "fsr3upscaler": {
        "passes": sorted((SHADERS / "fsr3upscaler").glob("*.glsl")),
        "defines": [
            "FFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0",
            "FFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0",
            "FFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1",
            "FFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0",
            "FFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2",
        ],
        "options": [
            "FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE",
            "FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT",
            "FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS",
            "FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS",
            "FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH",
            "FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING",
        ],
    },
    # Every pass but the debug view's, which this port never draws.
    "frameinterpolation": {
        "passes": sorted(src for src in (SHADERS / "frameinterpolation").glob("*.glsl")),
        "defines": [
            "FFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0",
            "FFX_FRAMEINTERPOLATION_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0",
            "FFX_FRAMEINTERPOLATION_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1",
            "FFX_FRAMEINTERPOLATION_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0",
            "FFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2",
        ],
        "options": [
            "FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS",
            "FFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS",
            "FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH",
        ],
    },
    "opticalflow": {
        "passes": [SHADERS / "opticalflow" / ("ffx_opticalflow_%s.glsl" % name) for name in (
            "prepare_luma_pass", "compute_luminance_pyramid_pass",
            "generate_scd_histogram_pass", "compute_scd_divergence_pass",
            "compute_optical_flow_advanced_pass_v5", "filter_optical_flow_pass_v5",
            "scale_optical_flow_advanced_pass_v5")],
        "defines": [],
        "options": ["FFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT"],
    },
}
BASE_DEFINES = ["FFX_GPU=1", "FFX_GLSL=1"]
KINDS = ["constantBuffer", "srvTexture", "uavTexture", "srvBuffer", "uavBuffer", "sampler",
         "rtAccelerationStructure"]
COUNT_FIELDS = {"constantBuffer": "numConstantBuffers", "srvTexture": "numSRVTextures",
                "uavTexture": "numUAVTextures", "srvBuffer": "numSRVBuffers",
                "uavBuffer": "numUAVBuffers", "sampler": "numSamplers",
                "rtAccelerationStructure": "numRTAccelerationStructures"}


def shader_tools():
    for host in ("windows-x86_64", "linux-x86_64", "darwin-x86_64"):
        d = Path(NDK_HOME) / "shader-tools" / host
        if d.is_dir():
            ext = ".exe" if host.startswith("windows") else ""
            tools = (d / f"glslc{ext}", d / f"spirv-opt{ext}", d / f"spirv-dis{ext}")
            if all(t.is_file() for t in tools):
                return tools
    return None


def reflect(disassembly):
    """The descriptors a module binds: {kind: [(name, binding, count, set)]}, by binding."""
    names, decorations, member_decorations = {}, defaultdict(dict), defaultdict(set)
    types, constants, variables = {}, {}, {}
    for line in disassembly.splitlines():
        s = line.strip()
        m = re.match(r'OpName %(\d+) "(.*)"$', s)
        if m:
            names[m.group(1)] = m.group(2)
            continue
        m = re.match(r"OpDecorate %(\d+) (\w+)(?: (\d+))?", s)
        if m:
            decorations[m.group(1)][m.group(2)] = m.group(3)
            continue
        m = re.match(r"OpMemberDecorate %(\d+) \d+ (\w+)", s)
        if m:
            member_decorations[m.group(1)].add(m.group(2))
            continue
        m = re.match(r"%(\d+) = (Op\w+)(.*)$", s)
        if not m:
            continue
        rid, op, rest = m.group(1), m.group(2), m.group(3).split()
        if op == "OpTypePointer":
            types[rid] = ("pointer", rest[0], rest[1].lstrip("%"))
        elif op == "OpTypeImage":
            # sampled type, dim, depth, arrayed, multisampled, sampled (1 sampled, 2 storage)
            types[rid] = ("image", rest[1], rest[5])
        elif op == "OpTypeSampler":
            types[rid] = ("sampler",)
        elif op == "OpTypeSampledImage":
            types[rid] = ("sampledimage", rest[0].lstrip("%"))
        elif op == "OpTypeArray":
            types[rid] = ("array", rest[0].lstrip("%"), rest[1].lstrip("%"))
        elif op == "OpTypeRuntimeArray":
            types[rid] = ("runtimearray", rest[0].lstrip("%"))
        elif op == "OpTypeStruct":
            types[rid] = ("struct",)
        elif op == "OpTypeAccelerationStructureKHR":
            types[rid] = ("accel",)
        elif op == "OpConstant" and len(rest) >= 2 and re.fullmatch(r"-?\d+", rest[1]):
            constants[rid] = int(rest[1])
        elif op == "OpVariable":
            variables[rid] = (rest[0].lstrip("%"), rest[1])

    found = defaultdict(list)
    for rid, (pointer, storage) in variables.items():
        decoration = decorations.get(rid, {})
        if "Binding" not in decoration:
            continue
        binding = int(decoration["Binding"])
        space = int(decoration.get("DescriptorSet") or 0)
        pointee = types[pointer][2]
        count = 1
        while types[pointee][0] in ("array", "runtimearray"):
            if types[pointee][0] == "array":
                count *= constants[types[pointee][2]]
            else:
                count = 0
            pointee = types[pointee][1]
        kind_type = types[pointee]
        if kind_type[0] == "struct":
            block = decorations.get(pointee, {})
            if storage == "Uniform" and "Block" in block:
                kind = "constantBuffer"
            else:
                read_only = ("NonWritable" in decoration or
                             "NonWritable" in member_decorations.get(pointee, set()))
                kind = "srvBuffer" if read_only else "uavBuffer"
        elif kind_type[0] == "image":
            dim, sampled = kind_type[1], kind_type[2]
            if dim == "Buffer":
                kind = "srvBuffer" if sampled == "1" else "uavBuffer"
            else:
                kind = "srvTexture" if sampled == "1" else "uavTexture"
        elif kind_type[0] == "sampledimage":
            kind = "srvTexture"
        elif kind_type[0] == "sampler":
            kind = "sampler"
        elif kind_type[0] == "accel":
            kind = "rtAccelerationStructure"
        else:
            raise ValueError("unknown descriptor type for %s" % names.get(rid, rid))
        found[kind].append((names.get(rid, "unnamed_%s" % rid), binding, count, space))
    return {kind: sorted(found[kind], key=lambda entry: entry[1]) for kind in KINDS}


def compile_one(tools, job):
    glslc, spirv_opt, spirv_dis = tools
    component, src, half, permutation = job
    spec = COMPONENTS[component]
    defines = BASE_DEFINES + spec["defines"] + ["FFX_HALF=%d" % half] + [
        "%s=%d" % (option, (permutation >> bit) & 1)
        for bit, option in enumerate(spec["options"])]
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "raw.spv"
        opt = Path(tmp) / "opt.spv"
        # Vulkan 1.1 (SPIR-V 1.3), as Xenia's shaders: with 1.3 glslang emits the
        # workgroup size as LocalSizeId, which the Adreno 740 mishandles (BUG-039).
        args = [str(glslc), "-fshader-stage=compute", "--target-env=vulkan1.1",
                f"-I{GPU_INCLUDE}", f"-I{GPU_INCLUDE / component}"]
        args += ["-D" + define for define in defines]
        args += ["-o", str(raw), str(src)]
        r = subprocess.run(args, capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            return job, None, None, "glslc: " + (r.stderr or r.stdout).strip()[:800]
        r = subprocess.run([str(spirv_opt), "-O", str(raw), "-o", str(opt)],
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            return job, None, None, "spirv-opt: " + (r.stderr or r.stdout).strip()[:800]
        r = subprocess.run([str(spirv_dis), "--raw-id", str(opt)], capture_output=True,
                           text=True, errors="replace")
        if r.returncode != 0:
            return job, None, None, "spirv-dis: " + (r.stderr or r.stdout).strip()[:800]
        try:
            reflection = reflect(r.stdout)
        except (KeyError, ValueError) as e:
            return job, None, None, "reflection: %s" % e
        return job, opt.read_bytes(), reflection, ""


def c_array(name, values, element="uint32_t"):
    return "static const %s %s[] = { %s };\n" % (element, name, ", ".join(values))


def emit_header(pass_name, variant, permutations, options):
    """permutations: (spirv bytes, reflection) by key index, 2 ** len(options) of them."""
    symbol = "g_" + pass_name + variant
    unique, table = [], []
    for spirv, reflection in permutations:
        for index, (other, _) in enumerate(unique):
            if other == spirv:
                table.append(index)
                break
        else:
            table.append(len(unique))
            unique.append((spirv, reflection))

    out = []
    out.append("// Generated by tools/build_fsr3_shaders.py from the FidelityFX SDK's %s.glsl"
               " (FFX_HALF=%d) - do not edit.\n" % (pass_name, 1 if "16bit" in variant else 0))
    out.append("#pragma once\n#include <cstdint>\n\n")
    out.append("#ifndef WARDEN_FFX_PERMUTATION_INFO\n#define WARDEN_FFX_PERMUTATION_INFO\n")
    out.append("struct WardenFfxPermutationInfo {\n"
               "  const uint32_t blobSize;\n  const unsigned char* blobData;\n")
    for kind in KINDS:
        out.append("  const uint32_t %s;\n  const char** %sNames;\n  const uint32_t* %sBindings;\n"
                   "  const uint32_t* %sCounts;\n  const uint32_t* %sSpaces;\n"
                   % (COUNT_FIELDS[kind], kind, kind, kind, kind))
    out.append("};\n#endif\n\n")
    key = pass_name + "_PermutationKey"
    out.append("#ifndef %s_DEFINED\n#define %s_DEFINED\n" % (key.upper(), key.upper()))
    out.append("typedef union %s {\n  struct {\n" % key)
    for option in options:
        out.append("    uint32_t %s : 1;\n" % option)
    out.append("  };\n  uint32_t index;\n} %s;\n#endif\n\n" % key)

    for number, (spirv, reflection) in enumerate(unique):
        prefix = "%s_%d" % (symbol, number)
        words = struct.unpack("<%dI" % (len(spirv) // 4), spirv)
        out.append("static const uint32_t %s_words[] = {\n" % prefix)
        for i in range(0, len(words), 8):
            out.append("  " + ", ".join("0x%08X" % w for w in words[i:i + 8]) + ",\n")
        out.append("};\n")
        for kind in KINDS:
            entries = reflection[kind]
            if not entries:
                continue
            out.append(c_array("%s_%sNames" % (prefix, kind),
                               ['"%s"' % e[0] for e in entries], "char*"))
            out.append(c_array("%s_%sBindings" % (prefix, kind), [str(e[1]) for e in entries]))
            out.append(c_array("%s_%sCounts" % (prefix, kind), [str(e[2]) for e in entries]))
            out.append(c_array("%s_%sSpaces" % (prefix, kind), [str(e[3]) for e in entries]))
        out.append("\n")

    out.append("static const WardenFfxPermutationInfo %s_PermutationInfo[] = {\n" % symbol)
    for number, (spirv, reflection) in enumerate(unique):
        prefix = "%s_%d" % (symbol, number)
        fields = ["%d" % len(spirv),
                  "reinterpret_cast<const unsigned char*>(%s_words)" % prefix]
        for kind in KINDS:
            entries = reflection[kind]
            if entries:
                fields += ["%d" % len(entries),
                           "const_cast<const char**>(%s_%sNames)" % (prefix, kind),
                           "%s_%sBindings" % (prefix, kind), "%s_%sCounts" % (prefix, kind),
                           "%s_%sSpaces" % (prefix, kind)]
            else:
                fields += ["0", "nullptr", "nullptr", "nullptr", "nullptr"]
        out.append("  { %s },\n" % ", ".join(fields))
    out.append("};\n\n")
    out.append(c_array("%s_IndirectionTable" % symbol, [str(i) for i in table], "int32_t"))
    return "".join(out), len(unique)


def emit_alias(pass_name, variant, target_variant):
    """The wave64 variant: the same SPIR-V in GLSL, so the same tables."""
    return ("// Generated by tools/build_fsr3_shaders.py - do not edit. In GLSL the wave64\n"
            "// variant is the wave32 SPIR-V (the backend sets the subgroup size).\n"
            "#pragma once\n#include \"%s%s_permutations.h\"\n\n"
            "static const auto& g_%s%s_PermutationInfo = g_%s%s_PermutationInfo;\n"
            "static const auto& g_%s%s_IndirectionTable = g_%s%s_IndirectionTable;\n"
            % (pass_name, target_variant, pass_name, variant, pass_name, target_variant,
               pass_name, variant, pass_name, target_variant))


def write_types_header():
    """The SDK's ffx_types.h with its default context size doubled (see above)."""
    original = SDK / "include" / "FidelityFX" / "host" / "ffx_types.h"
    text = original.read_text(encoding="utf-8", errors="replace")
    old = "#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 128)"
    if text.count(old) != 1:
        raise SystemExit("error: ffx_types.h has moved; its context size is not where it was")
    text = text.replace(old, "#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 256)  "
                             "// doubled for 4-byte wchar_t (tools/build_fsr3_shaders.py)")
    target = OUT / "FidelityFX" / "host" / "ffx_types.h"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text(encoding="utf-8") != text:
        target.write_text(text, encoding="utf-8", newline="\n")
        print("FidelityFX/host/ffx_types.h written, its context size doubled")


def write_frame_interpolation_source():
    """The SDK's ffx_frameinterpolation.cpp, its optical flow pass sized by the flow's own
    buffer (see above)."""
    original = SDK / "src" / "components" / "frameinterpolation" / "ffx_frameinterpolation.cpp"
    text = original.read_text(encoding="utf-8", errors="replace")
    old = ("    uint32_t opticalFlowDispatchSizeX = uint32_t(params->displaySize.width / "
           "float(params->opticalFlowBlockSize) + 7) / 8;\n"
           "    uint32_t opticalFlowDispatchSizeY = uint32_t(params->displaySize.height / "
           "float(params->opticalFlowBlockSize) + 7) / 8;\n")
    if text.count(old) != 1:
        raise SystemExit("error: ffx_frameinterpolation.cpp has moved; its optical flow "
                         "dispatch is not where it was")
    text = text.replace(old, (
        "    // Over the flow's own buffer (tools/build_fsr3_shaders.py: the flow at the\n"
        "    // render size; the same as the display size over the block size otherwise).\n"
        "    uint32_t opticalFlowDispatchSizeX = (params->opticalFlowBufferSize.width + 7) / 8;\n"
        "    uint32_t opticalFlowDispatchSizeY = (params->opticalFlowBufferSize.height + 7) / 8;\n"))
    target = OUT / "frameinterpolation" / "ffx_frameinterpolation.cpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text(encoding="utf-8") != text:
        target.write_text(text, encoding="utf-8", newline="\n")
        print("frameinterpolation/ffx_frameinterpolation.cpp written, its optical flow pass "
              "sized by the flow")


def main():
    ap = argparse.ArgumentParser(description="FSR 3.1's shaders as SDK headers")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--jobs", type=int, default=0)
    a = ap.parse_args()

    if not PASS_DIR.is_dir():
        print("error: no FidelityFX SDK at %s (THIRD_PARTY.md)" % SDK, file=sys.stderr)
        return 1
    tools = shader_tools()
    if not tools:
        print("error: NDK shader tools not found under %s/shader-tools" % NDK_HOME,
              file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    write_types_header()
    write_frame_interpolation_source()
    includes = list(GPU_INCLUDE.rglob("*.h"))
    newest_input = max([p.stat().st_mtime for p in includes] +
                       [Path(__file__).stat().st_mtime])
    passes = [(component, src) for component, spec in COMPONENTS.items()
              for src in spec["passes"]]
    todo = []
    for component, src in passes:
        name = src.stem
        header = OUT / ("%s_permutations.h" % name)
        if (not a.force and header.exists() and
                header.stat().st_mtime >= max(newest_input, src.stat().st_mtime)):
            continue
        todo.append((component, src))
    print("%d of %d passes to compile (every permutation, 32-bit and 16-bit)"
          % (len(todo), len(passes)))
    if not todo:
        return 0

    jobs = [(component, src, half, permutation) for component, src in todo
            for half in (0, 1)
            for permutation in range(2 ** len(COMPONENTS[component]["options"]))]
    results = {}
    failed = []
    workers = a.jobs or (os.cpu_count() or 4)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for i, (job, spirv, reflection, error) in enumerate(
                pool.map(lambda j: compile_one(tools, j), jobs), 1):
            if error:
                failed.append((job, error))
            else:
                results[job] = (spirv, reflection)
            if i % 128 == 0 or i == len(jobs):
                print("  [%d/%d] %d failed" % (i, len(jobs), len(failed)))
    if failed:
        for (component, src, half, permutation), error in failed[:6]:
            print("  %s FFX_HALF=%d permutation %d: %s" % (src.name, half, permutation, error))
        return 1

    for component, src in todo:
        name = src.stem
        options = COMPONENTS[component]["options"]
        count = 2 ** len(options)
        for half, variant in ((0, ""), (1, "_16bit")):
            text, distinct = emit_header(
                name, variant, [results[(component, src, half, p)] for p in range(count)],
                options)
            (OUT / ("%s%s_permutations.h" % (name, variant))).write_text(
                text, encoding="utf-8", newline="\n")
            print("  %s%s: %d distinct of %d" % (name, variant, distinct, count))
        (OUT / ("%s_wave64_permutations.h" % name)).write_text(
            emit_alias(name, "_wave64", ""), encoding="utf-8", newline="\n")
        (OUT / ("%s_wave64_16bit_permutations.h" % name)).write_text(
            emit_alias(name, "_wave64_16bit", "_16bit"), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
