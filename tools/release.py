#!/usr/bin/env python3
"""
release.py - keep a release build and prove what is in it (PLAN 11.3).

Run after the release build (`gradlew :app:assembleRelease`, or with -Pwarden.clean for
11.4's second app); it does not build - the game check comes before any build on the
owner's PC, and a person reads its verdict (CLAUDE.md).

  1. The APK, checked: signed, and by the release key (never the committed debug key);
     no game material in it (the pre-commit rule's file types, and the game's other
     formats); no Lossless.dll and nothing of Lossless Scaling's; no disc image or
     title update names; and no path of the build machine (a drive's Users or Repos
     folder) anywhere in it - the native code is built with its paths mapped to short
     stand-ins (app/src/main/cpp/CMakeLists.txt, the owner's ask of 2026-11-02: the
     home folder holds the user's name).
  2. Kept in build/releases/<version>/: the APK, and the native library as built
     (unstripped) with its build id in build-id.txt - a tombstone from that release names
     the build id, and this copy turns its addresses into functions and lines
     (ndk-stack -sym build/releases/<version>).

Usage:
  python tools/release.py            # the release APK
  python tools/release.py --clean    # 11.4's clean copy (built with -Pwarden.clean)
"""
import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APK = ROOT / "app" / "build" / "outputs" / "apk" / "release" / "app-release.apk"
# AGP builds a release's native code as RelWithDebInfo.
LIB_GLOB = "app/build/intermediates/cxx/RelWithDebInfo/*/obj/arm64-v8a/libwarden.so"
BUILD_TOOLS = Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "build-tools" / "36.1.0"

# The pre-commit rule's types and the game's other formats (.gitignore's list).
GAME_TYPES = re.compile(
    r"\.(iso|xex|xexp|erf|rim|gda|tlk|gfx|xds|dds|bik|cif|nxeart|das|met|dazip|rar|7z|"
    r"mmh|msh|phy|mor|ncs|nss|uti|utc|dlg|are|lvl|fsb|xma|xwb|xsb)$", re.I)
# The owner's own files by name - the disc images and title update as they are named on
# this PC and the Thor. (The app's text names Lossless.dll and ".iso" as what the player
# supplies; that is no file of anyone's, so those words are not looked for - a bundled
# Lossless.dll is caught by its file, above, and by its bytes, below.)
NAMES = [b"TU_12K2260_0000004000000", b"Dragon Age - Origins (World)",
         b"Ultimate Edition (USA, Europe) (Disc 2)", b"Awakening (USA, Europe)"]
LOSSLESS = ROOT / "Lossless.dll"
NM = Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "ndk" / "28.2.13676358" /     "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-nm.exe"
# PLAN 12.6: the published APK makes the game's code on the device - these must be in it.
REQUIRED = ["lib/arm64-v8a/libwarden.so", "lib/arm64-v8a/libllvm.so",
            "lib/arm64-v8a/libxenonrecomp.so", "assets/gamecode/toolchain.zip",
            "assets/gamecode/toolchain.version", "assets/gamecode/recipe.json",
            "assets/gamecode/hooks.h", "assets/gamecode/shim.cpp",
            "assets/gamecode/sources.json"]
# A game function, defined: the recompiled code (its C name, or the C++ alias's mangling).
GAME_FUNCTION = re.compile(r"^(__imp__(tu6_)?sub_[0-9A-F]{8}|_Z\d+(tu6_)?sub_[0-9A-F]{8}\w*)$")


def say(text):
    print(text, flush=True)


def build_id(path):
    """The ELF's GNU build id, hex, from its notes."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        return None
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    for i in range(shnum):
        base = shoff + i * shentsize
        kind, = struct.unpack_from("<I", data, base + 4)
        if kind != 7:  # SHT_NOTE
            continue
        offset, size = struct.unpack_from("<QQ", data, base + 0x18)
        at = offset
        while at < offset + size:
            namesz, descsz, note = struct.unpack_from("<III", data, at)
            name_at = at + 12
            desc_at = name_at + ((namesz + 3) & ~3)
            if note == 3 and data[name_at:name_at + namesz].rstrip(b"\0") == b"GNU":
                return data[desc_at:desc_at + descsz].hex()
            at = desc_at + ((descsz + 3) & ~3)
    return None


def signer(apk):
    """(verified, the signer's SHA-256) from apksigner."""
    tool = BUILD_TOOLS / ("apksigner.bat" if sys.platform == "win32" else "apksigner")
    result = subprocess.run([str(tool), "verify", "--print-certs", str(apk)],
                            capture_output=True, text=True)
    digest = re.search(r"certificate SHA-256 digest: ([0-9a-f]+)", result.stdout)
    return result.returncode == 0, digest.group(1) if digest else None


def badging(apk):
    """(package, version name) from aapt2."""
    tool = BUILD_TOOLS / ("aapt2.exe" if sys.platform == "win32" else "aapt2")
    out = subprocess.run([str(tool), "dump", "badging", str(apk)], capture_output=True,
                         text=True).stdout
    package = re.search(r"package: name='([^']+)'", out)
    version = re.search(r"versionName='([^']+)'", out)
    return package.group(1) if package else None, version.group(1) if version else "unknown"


def debug_signer():
    keystore = ROOT / "app" / "debug.keystore"
    result = subprocess.run(["keytool", "-list", "-v", "-keystore", str(keystore),
                             "-storepass", "android"], capture_output=True, text=True)
    digest = re.search(r"SHA256: ([0-9A-F:]+)", result.stdout)
    return digest.group(1).replace(":", "").lower() if digest else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--clean", action="store_true",
                        help="11.4's clean copy (the release built with -Pwarden.clean)")
    args = parser.parse_args()
    if not APK.is_file():
        sys.exit("no %s - build it first (gradlew :app:assembleRelease)" % APK.relative_to(ROOT))
    problems = []

    verified, digest = signer(APK)
    debug = debug_signer()
    if not verified:
        problems.append("the APK is not signed (no release key - app/build.gradle.kts)")
    elif digest == debug:
        problems.append("the APK is signed with the committed debug key")
    say("signer: %s%s" % (digest or "none", " (verified)" if verified else ""))

    with zipfile.ZipFile(APK) as apk:
        entries = apk.infolist()
        for entry in entries:
            if GAME_TYPES.search(entry.filename):
                problems.append("game material type: %s" % entry.filename)
            if "lossless" in entry.filename.lower():
                problems.append("Lossless Scaling's file: %s" % entry.filename)
        # Lossless.dll's bytes anywhere (a 4 KiB piece from its middle), when this PC has it.
        lossless = None
        if LOSSLESS.is_file():
            whole = LOSSLESS.read_bytes()
            lossless = whole[len(whole) // 2:len(whole) // 2 + 4096]
        build_paths = 0
        for entry in entries:
            if entry.filename.startswith("assets/licences/"):
                continue
            data = apk.read(entry)
            for name in NAMES:
                if name in data:
                    problems.append("%r in %s" % (name.decode(errors="replace"),
                                                  entry.filename))
            if lossless and lossless in data:
                problems.append("Lossless.dll's bytes in %s" % entry.filename)
            found = len(re.findall(rb"[A-Za-z]:[/\\]+(?:Users|Repos)[/\\]", data))
            if found:
                problems.append("%d build machine path(s) in %s" % (found, entry.filename))
            build_paths += found
        # PLAN 12.6: what makes the game's code there, and none of the game's code here.
        names = {entry.filename for entry in entries}
        for required in REQUIRED:
            if required not in names:
                problems.append("missing %s (tools/stage_gamecode_toolchain.py)" % required)
        if any(name.endswith("libdaogame.so") for name in names):
            problems.append("the game's code (libdaogame.so) is in the APK")
        if "assets/gamecode/toolchain.zip" in names:
            import io
            with zipfile.ZipFile(io.BytesIO(apk.read("assets/gamecode/toolchain.zip"))) as inner:
                for item in inner.namelist():
                    if GAME_TYPES.search(item):
                        problems.append("game material type in toolchain.zip: %s" % item)
                    data = inner.read(item)
                    if any(name in data for name in NAMES):
                        problems.append("a disc's file name in toolchain.zip: %s" % item)
                    if re.search(rb"[A-Za-z]:[/\\]+(?:Users|Repos)[/\\]", data):
                        problems.append("a build machine path in toolchain.zip: %s" % item)
        import tempfile
        with tempfile.TemporaryDirectory() as scratch:
            for name in sorted(n for n in names if n.endswith(".so")):
                lib = Path(scratch) / Path(name).name
                lib.write_bytes(apk.read(name))
                symbols = subprocess.run([str(NM), "--defined-only", "-j", str(lib)],
                                         capture_output=True, text=True).stdout.split()
                symbols += subprocess.run([str(NM), "-D", "--defined-only", "-j", str(lib)],
                                          capture_output=True, text=True).stdout.split()
                game = sorted({s for s in symbols if GAME_FUNCTION.match(s)})
                if game:
                    problems.append("%d game functions defined in %s (%s ...)" %
                                    (len(game), name, game[0]))
                say("%s: %.1f MB, no game function defined" % (name, lib.stat().st_size / 1e6)
                    if not game else "%s: GAME CODE" % name)
    package, version = badging(APK)
    clean = package == "prod.daorecompandroid.duo.clean"
    say("package %s, version %s" % (package, version))
    if args.clean != clean:
        problems.append("the APK is %s the clean copy, but --clean was %s" %
                        ("" if clean else "not", "given" if args.clean else "not given"))
    say("entries: %d, %.1f MB; build machine paths: %d" %
        (len(entries), APK.stat().st_size / 1e6, build_paths))

    libs = sorted(ROOT.glob(LIB_GLOB), key=lambda p: p.stat().st_mtime)
    if not libs:
        problems.append("no unstripped libwarden.so (%s)" % LIB_GLOB)
    if problems:
        for problem in problems:
            say("PROBLEM: " + problem)
        return 1

    keep = ROOT / "build" / "releases" / version
    keep.mkdir(parents=True, exist_ok=True)
    lib = libs[-1]
    shutil.copy2(APK, keep / ("dao-recomp-android-%s.apk" % version))
    shutil.copy2(lib, keep / "libwarden.so")
    ident = build_id(lib)
    sha = hashlib.sha256(APK.read_bytes()).hexdigest()
    (keep / "build-id.txt").write_text(
        "libwarden.so build id: %s\nAPK SHA-256: %s\nsigner SHA-256: %s\n" % (ident, sha, digest))
    say("kept in %s: the APK (SHA-256 %s), libwarden.so (build id %s)" %
        (keep.relative_to(ROOT), sha[:16], ident))
    say("release: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
