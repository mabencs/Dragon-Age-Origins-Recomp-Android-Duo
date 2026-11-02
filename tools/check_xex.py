#!/usr/bin/env python3
"""
check_xex.py - check the decrypted, assembled XEX image against the recompiled source.

Task 5.0e. The loader decrypts default.xex with AES-128-CBC and assembles four raw
blocks into a 19 MB image. If any of that is wrong - the key, the IV, the block
arithmetic, the header offsets - the result is not an error but *noise*, and noise is
indistinguishable from code until something tries to run it.

So this checks the image against the only reference that can settle it: the recompiled
source itself, which XenonRecomp wrote from this exact image.

The trick avoids needing a PowerPC disassembler. XenonRecomp emits the original
instruction as a comment above each translated one:

    PPC_FUNC_IMPL(__imp__tu6_sub_82210008) {
        PPC_FUNC_PROLOGUE();
        uint32_t ea{};
        // mflr r12            <- the first instruction, at 0x82210008

Thousands of functions begin with `mflr r12`. Whatever four bytes that encodes to,
**every one of those addresses must contain the same four bytes** - and under a wrong
key they would all differ. Grouping addresses by their instruction text and requiring
agreement within each group therefore tests the whole pipeline, without this script
needing to know a single opcode encoding.

Usage:
  python tools/check_xex.py
  python tools/check_xex.py --image work/default.xex
"""
import argparse
import collections
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import dao_iso  # noqa: E402

GENERATED = None  # set from --build
BUILD = ROOT / "build" / "xex-check"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"
XENON = ROOT / "third_party" / "XenonRecomp"
TINY_AES = XENON / "thirdparty" / "tiny-AES-c"
TINY_SHA1 = XENON / "thirdparty" / "TinySHA1"
MSPACK = XENON / "thirdparty" / "libmspack" / "libmspack" / "mspack"
XENON_UTILS = XENON / "XenonUtils"

FUNCTION = re.compile(r"^PPC_FUNC_IMPL\(__imp__(?:tu6_)?sub_([0-9A-Fa-f]{8})\)")
COMMENT = re.compile(r"^\s*// (.+)$")

# Instructions whose encoding depends on nothing but the mnemonic and its operands.
# A branch is excluded because its encoding holds a *relative* displacement, so two
# `b 0x82a976e0` at different addresses legitimately differ.
BRANCHY = re.compile(r"^(b|ba|bl|bla|bc|bcl|bdnz|bdz|beq|bne|blt|bgt|ble|bge|"
                     r"bso|bns|bdnzf|bdnzt|b[a-z]*\s+0x)")


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
    exe = BUILD / ("xex_host.exe" if os.name == "nt" else "xex_host")
    cpp = ROOT / "app" / "src" / "main" / "cpp"
    sources = [cpp / "warden_xex.cc", ROOT / "tools" / "xex_host.cc",
               TINY_AES / "aes.c"]

    if compiler.name.startswith("clang-cl"):
        args = [str(compiler), "/std:c++20", "/O2", "/EHsc", "/MT", "/W3",
                "-winsysroot", str(WINSYSROOT),
                f"/I{cpp}", f"/I{TINY_AES}", f"/Fe{exe}", f"/Fo{BUILD}\\"]
    else:
        args = [str(compiler), "-std=c++20", "-O2", f"-I{cpp}", f"-I{TINY_AES}",
                "-o", str(exe)]
    args += [str(s) for s in sources]

    print(f"  building {exe.name}")
    result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-4000:], file=sys.stderr)
        print(result.stderr[-4000:], file=sys.stderr)
        raise SystemExit("error: host harness failed to build")
    return exe


def build_patcher():
    """XenonRecomp's own XexPatcher, which is what produced ppc_tu6."""
    compiler = host_compiler()
    if not compiler:
        raise SystemExit("error: no clang-cl or clang++ found; see docs/TOOLCHAIN.md")
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / ("xexpatch_host.exe" if os.name == "nt" else "xexpatch_host")
    sources = [XENON_UTILS / "xex_patcher.cpp",
               XENON_UTILS / "memory_mapped_file.cpp",
               ROOT / "tools" / "xexpatch_host.cc",
               TINY_AES / "aes.c",
               MSPACK / "lzxd.c",
               MSPACK / "system.c"]
    includes = [XENON_UTILS, TINY_AES, TINY_SHA1, MSPACK, MSPACK.parent]

    if compiler.name.startswith("clang-cl"):
        # NOMINMAX: the Windows headers define min and max as macros, which breaks
        # every std::min / std::max in xex_patcher.cpp.
        args = [str(compiler), "/std:c++20", "/O2", "/EHsc", "/MT", "/W0", "/DNOMINMAX",
                "-winsysroot", str(WINSYSROOT), f"/Fe{exe}", f"/Fo{BUILD}\\"]
        args += [f"/I{i}" for i in includes]
    else:
        args = [str(compiler), "-std=c++20", "-O2", "-w", "-DNOMINMAX", "-o", str(exe)]
        args += [f"-I{i}" for i in includes]
    args += [str(s) for s in sources]

    print(f"  building {exe.name}")
    result = subprocess.run(args, cwd=BUILD, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-6000:], file=sys.stderr)
        print(result.stderr[-6000:], file=sys.stderr)
        raise SystemExit("error: the patcher harness failed to build")
    return exe


def apply_patch(base_xex, xexp, destination):
    exe = build_patcher()
    result = subprocess.run([str(exe), str(base_xex), str(xexp), str(destination)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
        raise SystemExit("error: applying the title update failed")
    print(f"  {result.stdout.strip()}")
    return destination


def extract_xexp(destination):
    """Pull default.xexp out of the TU7 package, so this needs only the raw files."""
    sys.path.insert(0, str(ROOT / "third_party" / "DAORecomp" / "scripts"))
    import stfs
    for package in sorted(ROOT.glob("TU_*")):
        try:
            with stfs.Package(str(package)) as pkg:
                for path, length, start, contiguous in pkg.files:
                    if path != "default.xexp":
                        continue
                    import hashlib
                    import io
                    buffer = io.BytesIO()
                    pkg.read_file(start, length, contiguous, buffer)
                    data = buffer.getvalue()
                    sha1 = hashlib.sha1(data).hexdigest()
                    # Only Title Update 7; TU2 is also in the repo root and is not
                    # usable (verify_assets.py says so).
                    if sha1 == "dd5b493f4fe2fb4c99e6ef7ed6923ddddeee8291":
                        print(f"  reading default.xexp (TU7) from {package.name}")
                        destination.write_bytes(data)
                        return destination
        except (stfs.Error, OSError):
            continue
    raise SystemExit("error: no Title Update 7 package in the repo root")


def extract_xex(destination):
    """Pull default.xex off the disc image, so this needs nothing but the ISO."""
    candidates = [p for p in sorted(ROOT.glob("*.iso")) if "Awakening" not in p.name]
    if not candidates:
        raise SystemExit("error: no Origins disc image in the repo root")
    iso = dao_iso.Xdvdfs(str(candidates[0]))
    print(f"  reading default.xex from {candidates[0].name}")
    destination.write_bytes(iso.read_file("default.xex"))
    return destination


def first_instructions():
    """{guest address: first instruction text} from the recompiled source."""
    out = {}
    for source in sorted(GENERATED.glob("ppc_recomp.*.cpp")):
        address = None
        with open(source, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = FUNCTION.match(line)
                if match:
                    address = int(match.group(1), 16)
                    continue
                if address is None:
                    continue
                comment = COMMENT.match(line)
                if comment:
                    out[address] = comment.group(1).strip()
                    address = None
    return out


def main():
    ap = argparse.ArgumentParser(description="Verify the decrypted XEX image")
    ap.add_argument("--image", help="default.xex (default: read from the ISO)")
    # Which recompilation to compare against. ppc is the base disc executable;
    # ppc_tu6 is the Title Update build, whose code came from the *patched* image and
    # therefore does not match the disc's default.xex.
    ap.add_argument("--build", default="ppc", choices=["ppc", "ppc_tu6"])
    # ppc_tu6 was recompiled from the *patched* image, so comparing against it means
    # applying the title update first - which is itself the thing being tested.
    ap.add_argument("--patch", nargs="?", const="auto",
                    help="apply a title update's default.xexp first "
                         "('auto' reads TU7 from the TU_* package)")
    a = ap.parse_args()

    if a.build == "ppc_tu6" and not a.patch:
        print("  note: --build ppc_tu6 without --patch will not match; the TU7 build\n"
              "        was recompiled from the patched image.")

    global GENERATED
    GENERATED = ROOT / "recomp" / "generated" / a.build
    print(f"  comparing against recomp/generated/{a.build}")

    BUILD.mkdir(parents=True, exist_ok=True)
    xex = pathlib.Path(a.image) if a.image else extract_xex(BUILD / "default.xex")
    if a.patch:
        xexp = (extract_xexp(BUILD / "default.xexp") if a.patch == "auto"
                else pathlib.Path(a.patch))
        xex = apply_patch(xex, xexp, BUILD / "default_patched.xex")

    exe = build()
    image_path = BUILD / "image.bin"

    result = subprocess.run([str(exe), str(xex), str(image_path)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr.strip(), file=sys.stderr)
        raise SystemExit("error: the loader failed")
    info = dict(line.split() for line in result.stdout.split("\n") if line.strip())
    base = int(info["base"], 16)
    size = int(info["size"])
    print(f"  loaded: base {info['base']}, {size:,} bytes, entry {info['entry']}, "
          f"enc {info['encryption']} comp {info['compression']}, "
          f"{info['blocks']} blocks")

    image = image_path.read_bytes()
    if len(image) != size:
        raise SystemExit(f"error: image is {len(image)} bytes, loader said {size}")

    print("  reading first instructions from the recompiled source")
    instructions = first_instructions()
    print(f"  {len(instructions):,} functions with a first instruction")

    groups = collections.defaultdict(list)
    for address, text in instructions.items():
        if BRANCHY.match(text):
            # A relative branch encodes its displacement, so two identical-looking
            # branches at different addresses hold different bytes. Correctly so.
            continue
        offset = address - base
        if 0 <= offset <= len(image) - 4:
            groups[text].append(offset)

    checked = agreed = 0
    failures = []
    for text, offsets in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        if len(offsets) < 2:
            continue
        words = {image[o:o + 4] for o in offsets}
        checked += 1
        if len(words) == 1:
            agreed += 1
        else:
            failures.append((text, len(offsets), len(words)))

    print()
    print(f"  instruction groups with 2+ sites: {checked:,}")
    print(f"  groups where every site agrees:   {agreed:,}")
    if failures:
        print(f"  DISAGREEING GROUPS: {len(failures)}")
        for text, sites, distinct in failures[:12]:
            print(f"    {text:<40} {sites} sites, {distinct} distinct encodings")

    # The largest group is the real evidence: thousands of sites agreeing on one
    # encoding cannot happen by chance from a wrong key.
    biggest = max(groups.items(), key=lambda kv: len(kv[1])) if groups else None
    if biggest:
        text, offsets = biggest
        word = image[offsets[0]:offsets[0] + 4]
        print()
        print(f"  largest group: {text!r} at {len(offsets):,} sites")
        print(f"    all encode to {word.hex().upper()}" if len({image[o:o+4] for o in offsets}) == 1
              else "    DISAGREE")

    ok = not failures and agreed > 0
    print()
    print("the image matches the recompiled source" if ok
          else "IMAGE DOES NOT MATCH - decryption or assembly is wrong")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
