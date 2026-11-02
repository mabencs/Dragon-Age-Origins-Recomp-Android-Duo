#!/usr/bin/env python3
"""
bootstrap.py - rebuild everything git does not hold, on a fresh clone, in order.

    python tools/bootstrap.py [--iso PATH] [--tu PATH] [--accept-msvc-license]
                              [--check] [--disc-build] [--skip-build]
                              [--validation-layer]

What is gitignored and what this makes of it, each step skipped when its output is
already there (so it is safe to run again on a machine that is half set up):

  1. the disc image and Title Update 7     found in the repo root (or --iso / --tu) and
                                           checked: title 454108C0, media 186A1115, TU7
  2. third_party/                          DAORecomp, XenonRecomp and xenia-canary at the
                                           commits THIRD_PARTY.md pins, with the
                                           submodules each needs; the FidelityFX SDK's
                                           sdk folder (FSR 3.1, PLAN 9.4)
  3. third_party/DAORecomp/game/default.xex       from the disc image
     third_party/DAORecomp/title_update/default.xexp   from the title update
  4. XenonRecomp, patched and built        DAORecomp's patch_recompiler.py, then clang-cl
                                           against the MSVC CRT / Windows SDK sysroot in
                                           toolchain/winsysroot (Microsoft's licence:
                                           --accept-msvc-license accepts it)
  5. recomp/generated/ppc_tu6              the recompiled title (the TU7 build the app
                                           links); --disc-build also makes ppc, the
                                           disc build check_xex.py cross-checks
  6. build/runtime-tree                    Xenia's Vulkan backend grafted onto DAORecomp,
                                           with every recorded edit; the shaders compiled
                                           to SPIR-V; the generated third-party units;
                                           build/fsr3-shaders, FSR 3.1's passes
                                           (tools/build_fsr3_shaders.py); build/lsfg-vk,
                                           lsfg-vk with its edits (tools/prepare_lsfg.py);
                                           build/dao-convert, the PC mod converter's
                                           edited files (tools/assemble_dao_convert.py)
  6b. build/device-toolchain,           the device's compiler (LLVM 19.1.7's clang and lld) and
      build/gamecode-stage                XenonRecomp for Android, staged for the APK - the
                                         app makes the game's code with them
                                         (tools/build_device_toolchain.py,
                                         tools/stage_gamecode_toolchain.py; PLAN 12)
  7. local.properties, the debug key       sdk.dir; app/debug.keystore copied to
                                           ~/.android/debug.keystore if there is none
                                           there (a different key there is reported, not
                                           replaced, and stops the run before Gradle -
                                           docs/SIGNING.md)
     app/src/main/jniLibs/arm64-v8a/       the Khronos validation layer, optional: only
                                           with --validation-layer, from the Vulkan SDK
                                           1.4.363.0 release, checked by SHA-256
  8. app/build/outputs/apk/debug/app-debug.apk   ./gradlew :app:assembleDebug

--check reports every step's state and changes nothing. Needs on PATH or in place:
Python 3, git, CMake and Ninja (host build), LLVM at C:/Program Files/LLVM (or
--llvm-bin), JDK 17 (JAVA_HOME), the Android SDK with platform 36, build-tools 36.1.0,
CMake 3.31.6 and NDK 28.2.13676358 (docs/TOOLCHAIN.md). It never touches the device;
installing is `adb install -r -d app/build/outputs/apk/debug/app-debug.apk` - and never
uninstall the app on the Thor, which deletes the extracted game data.
"""
import argparse
import filecmp
import glob
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

# THIRD_PARTY.md - the pins. Submodules are at the commits each superproject records.
# A fifth entry, where there is one, is the folders a sparse checkout takes.
REPOS = [
    ("DAORecomp", "https://github.com/96dot/DAORecomp.git",
     "eb2360a435da409082a0ed82acdfb0f4b7b3fbab", []),
    ("XenonRecomp", "https://github.com/hedge-dev/XenonRecomp.git",
     "ddd128bcca99fe8bfbb99bea583c972351fa6ace",
     ["thirdparty/fmt", "thirdparty/libmspack", "thirdparty/simde", "thirdparty/tiny-AES-c",
      "thirdparty/tomlplusplus", "thirdparty/xxHash"]),
    ("xenia-canary", "https://github.com/xenia-canary/xenia-canary.git",
     "1c81b867473806b2845a1946b2d185c9d888a319",
     ["third_party/Vulkan-Headers", "third_party/VulkanMemoryAllocator",
      "third_party/glslang", "third_party/SPIRV-Tools", "third_party/FidelityFX-CAS",
      "third_party/FidelityFX-FSR", "third_party/fmt", "third_party/cxxopts",
      "third_party/date", "third_party/disruptorplus", "third_party/tomlplusplus",
      "third_party/utfcpp", "third_party/xxhash", "third_party/snappy",
      # has207's fork with the xmaframes decoder: the XMA decoder (task 4.5).
      "third_party/FFmpeg"]),
    # FSR 3.1 (PLAN 9.4): the SDK's v1.1.4 tag, its sdk folder only.
    ("FidelityFX-SDK", "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git",
     "c6efa6bf7f2027b3ec94f28578bb5965eabb9e55", [], ["sdk"]),
    # Frame generation (PLAN 9.14): lsfg-vk v2.0.0-dev, GPL-3.0 - its shared Vulkan layer and
    # its backend only (THIRD_PARTY.md: the newest version its licence lets us change).
    ("lsfg-vk", "https://github.com/PancakeTAS/lsfg-vk.git",
     "0213be9ff93121283ad46a27119f40ece0e6f301", [], ["lsfg-vk-common", "lsfg-vk-backend"]),
    # .7z mod imports (PLAN 10.19): 7-Zip's tag 26.05, its C folder only - the 7z decoder,
    # public domain.
    ("7zip", "https://github.com/ip7z/7zip.git",
     "b88c6bc09339de32f7d18bbab900686b244f42ba", [], ["C"]),
    # .rar mod imports (PLAN 10.19, 11.3): libarchive v3.8.11, BSD 2-Clause - its library
    # and its Android configuration only. (UnRAR, used before, carries a restriction the
    # GPL-3.0 the app is under cannot take.)
    ("libarchive", "https://github.com/libarchive/libarchive.git",
     "03036ffa409a2aa1c5bbad35be2202641a040fda", [], ["libarchive", "contrib/android"]),
]

GAME_XEX_SHA1 = "532c0ec164f44bc8784cc0e8f6f67f72c1ed3751"
TU7_XEXP_SHA1 = "dd5b493f4fe2fb4c99e6ef7ed6923ddddeee8291"
REQUIRED_TITLE_ID = "454108C0"
REQUIRED_MEDIA_ID = "186A1115"
SDK_PIECES = ["platforms/android-36", "build-tools/36.1.0", "cmake/3.31.6",
              "ndk/28.2.13676358"]

# The Khronos validation layer (optional, debug APK only - docs/TESTING.md).
LAYER_VERSION = "1.4.363.0"
LAYER_NAME = "libVkLayer_khronos_validation.so"
LAYER_URL = ("https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/"
             "vulkan-sdk-%s/android-binaries-%s.zip" % (LAYER_VERSION, LAYER_VERSION))
LAYER_ZIP_SHA256 = "9a3b8e32cb3330406dd4f3651594201635baee4528da8e2a5300e4f55e1a9cc7"
LAYER_SHA256 = "db7447027e28db95584e9fde8a57cc1f28c2dd09defcbe2f2b76ee2bbc65bf09"

DAO = ROOT / "third_party" / "DAORecomp"
XEX = DAO / "game" / "default.xex"
XEXP = DAO / "title_update" / "default.xexp"
RECOMPILER = ROOT / "build" / "xenonrecomp" / "XenonRecomp" / "XenonRecomp.exe"
WINSYSROOT = ROOT / "toolchain" / "winsysroot"


class Stop(Exception):
    pass


def say(state, text):
    print("  [%-4s] %s" % (state, text))


def run(cmd, cwd=ROOT, check=True):
    shown = " ".join(str(c) for c in cmd)
    print("         $ " + shown)
    result = subprocess.run([str(c) for c in cmd], cwd=str(cwd))
    if check and result.returncode != 0:
        raise Stop("failed (exit %d): %s" % (result.returncode, shown))
    return result.returncode


def output(cmd, cwd=ROOT):
    result = subprocess.run([str(c) for c in cmd], cwd=str(cwd), capture_output=True,
                            text=True)
    return result.returncode, (result.stdout or "").strip()


def digest(path, h):
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha1(path):
    return digest(path, hashlib.sha1())


def sha256(path):
    return digest(path, hashlib.sha256())


# ------------------------------------------------------------------ 1. game files
def find_assets(args):
    from dao_iso import Xdvdfs, parse_xex
    from verify_assets import read_stfs, tu_number

    iso = Path(args.iso) if args.iso else None
    if not iso:
        for path in sorted(ROOT.glob("*.iso")) + sorted(ROOT.glob("*.ISO")):
            try:
                xex = parse_xex(Xdvdfs(str(path)).read_file("default.xex"))
            except Exception:  # not a disc this reader understands
                continue
            if xex.get("title_id") == REQUIRED_TITLE_ID and \
                    xex.get("media_id") == REQUIRED_MEDIA_ID:
                iso = path
                break
    tu = Path(args.tu) if args.tu else None
    if not tu:
        for path in sorted(ROOT.glob("TU_*")):
            meta = read_stfs(str(path))
            if meta and meta["title_id"] == REQUIRED_TITLE_ID and \
                    meta["media_id"] == REQUIRED_MEDIA_ID and \
                    tu_number(meta["display_name"]) == 7:
                tu = path
                break
    print("1. the game files")
    if iso and iso.is_file():
        say("OK", "disc image  %s" % iso.name)
    else:
        say("FAIL", "no disc image of title %s, media %s in the repo root (or --iso)"
            % (REQUIRED_TITLE_ID, REQUIRED_MEDIA_ID))
    if tu and tu.is_file():
        say("OK", "title update %s" % tu.name)
    else:
        say("FAIL", "no Title Update 7 package (TU_12K2260_...1O1) in the repo root (or --tu)")
    return iso, tu


# ------------------------------------------------------------------ 2. third_party
def fetch_repos(check):
    print("2. third_party at the pinned commits")
    for name, url, commit, submodules, *sparse in REPOS:
        path = ROOT / "third_party" / name
        if (path / ".git").exists():
            code, head = output(["git", "-C", path, "rev-parse", "HEAD"])
            if head != commit:
                raise Stop("third_party/%s is at %s, not the pinned %s - move it aside or "
                           "check out the pin" % (name, head[:12], commit[:12]))
            say("OK", "%s %s" % (name, commit[:12]))
        elif check:
            say("TODO", "%s - to fetch at %s" % (name, commit[:12]))
            continue
        else:
            say("GET", "%s at %s" % (name, commit[:12]))
            path.mkdir(parents=True, exist_ok=True)
            run(["git", "init", "-q"], cwd=path)
            run(["git", "remote", "add", "origin", url], cwd=path)
            if sparse:
                # Only these folders, and their blobs only: the rest of the repository
                # (samples, prebuilt binaries) is never fetched.
                run(["git", "fetch", "-q", "--depth", "1", "--filter=blob:none", "origin",
                     commit], cwd=path)
                run(["git", "sparse-checkout", "set", "--cone"] + sparse[0], cwd=path)
            else:
                run(["git", "fetch", "-q", "--depth", "1", "origin", commit], cwd=path)
            run(["git", "checkout", "-q", "FETCH_HEAD"], cwd=path)
        for sub in submodules:
            sub_path = path / sub
            populated = sub_path.is_dir() and any(sub_path.iterdir())
            if populated:
                continue
            if check:
                say("TODO", "%s/%s - submodule to fetch" % (name, sub))
                continue
            say("GET", "%s/%s" % (name, sub))
            if run(["git", "submodule", "update", "--init", "--depth", "1", sub], cwd=path,
                   check=False) != 0:
                # A server that will not serve the recorded commit shallowly.
                run(["git", "submodule", "update", "--init", sub], cwd=path)


# ------------------------------------------------------------------ 3. executables
def extract_executables(iso, tu, check):
    print("3. the executable and the title update's patch")
    if XEX.is_file() and sha1(XEX) == GAME_XEX_SHA1:
        say("OK", "default.xex %s" % GAME_XEX_SHA1[:12])
    elif check:
        say("TODO", "default.xex - to extract from the disc image")
    else:
        if not iso:
            raise Stop("need the disc image for default.xex")
        run([sys.executable, "tools/dao_iso.py", "extract", iso, "default.xex", XEX])
        if sha1(XEX) != GAME_XEX_SHA1:
            raise Stop("default.xex has the wrong SHA-1 - not the supported disc revision")
        say("OK", "default.xex extracted")
    if XEXP.is_file() and sha1(XEXP) == TU7_XEXP_SHA1:
        say("OK", "default.xexp (Title Update 7) %s" % TU7_XEXP_SHA1[:12])
    elif check:
        say("TODO", "default.xexp - to extract from the title update")
    else:
        if not tu:
            raise Stop("need Title Update 7 for default.xexp")
        # stfs.py extracts into a folder that must not exist yet; an earlier extraction
        # is used as it is.
        work = ROOT / "work" / "tu7"
        if not (work / "default.xexp").is_file():
            if work.exists():
                shutil.rmtree(work)
            work.parent.mkdir(parents=True, exist_ok=True)
            run([sys.executable, "-I", DAO / "scripts" / "stfs.py", "extract", tu, work])
        XEXP.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(work / "default.xexp", XEXP)
        if sha1(XEXP) != TU7_XEXP_SHA1:
            raise Stop("default.xexp has the wrong SHA-1 - not Title Update 7")
        say("OK", "default.xexp extracted")


# ------------------------------------------------------------------ 4. XenonRecomp
def build_recompiler(args, check):
    print("4. XenonRecomp")
    # PLAN 2.4: our edits (tools/patch_xenonrecomp.py) after DAORecomp's - a recompiler
    # built before them is built again, and the title must then be recompiled.
    recompiler_cpp = ROOT / "third_party" / "XenonRecomp" / "XenonRecomp" / "recompiler.cpp"
    ours = recompiler_cpp.is_file() and "DAO/Warden (PLAN 2.4)" in recompiler_cpp.read_text(
        encoding="utf-8", errors="replace")
    if RECOMPILER.is_file() and ours:
        say("OK", "built: %s" % RECOMPILER.relative_to(ROOT))
        return
    if check:
        say("TODO", "to patch with DAORecomp's patch_recompiler.py and "
                    "tools/patch_xenonrecomp.py, and build")
        return
    if not RECOMPILER.is_file():
        run([sys.executable, "-I", DAO / "scripts" / "patch_recompiler.py", recompiler_cpp])
    run([sys.executable, "tools/patch_xenonrecomp.py"])
    code, state = output([sys.executable, "tools/setup_winsysroot.py", "--check"])
    if code != 0 or " no" in state:
        if not args.accept_msvc_license:
            raise Stop("the MSVC CRT and Windows SDK sysroot is missing. It is Microsoft's, "
                       "under their licence (https://go.microsoft.com/fwlink/?LinkId=2086102); "
                       "run again with --accept-msvc-license to fetch it")
        run([sys.executable, "tools/setup_winsysroot.py", "--accept-license"])
    llvm = Path(args.llvm_bin)
    if not (llvm / "clang-cl.exe").is_file():
        raise Stop("no clang-cl in %s - install LLVM or pass --llvm-bin" % llvm)
    if not shutil.which("ninja"):
        raise Stop("no ninja on PATH - install Ninja (winget install Ninja-build.Ninja)")
    # CMake reads a relative toolchain path against the build and source trees, not the
    # working directory, so every path here is absolute.
    build = ROOT / "build" / "xenonrecomp"
    run(["cmake", "-G", "Ninja", "-S", (ROOT / "third_party" / "XenonRecomp").as_posix(),
         "-B", build.as_posix(), "-Wno-dev", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_TOOLCHAIN_FILE=" + (DAO / "cmake" / "clang-cl-xwin.cmake").as_posix(),
         "-DDAO_LLVM_BIN=" + llvm.as_posix(),
         "-DDAO_WINSYSROOT=" + WINSYSROOT.as_posix()])
    run(["ninja", "-C", build.as_posix(), "XenonRecomp"])
    say("OK", "built")


# ------------------------------------------------------------------ 5. recompile
def recompile(args, check):
    print("5. the recompiled title")
    builds = [("tu", "ppc_tu6")] + ([("disc", "ppc")] if args.disc_build else [])
    for build, folder in builds:
        out = ROOT / "recomp" / "generated" / folder
        sources = list(out.glob("ppc_recomp.*.cpp")) if out.is_dir() else []
        # Older than the recompiler (built again, PLAN 2.4): made again.
        stale = bool(sources) and RECOMPILER.is_file() and \
            max(f.stat().st_mtime for f in sources) < RECOMPILER.stat().st_mtime
        if sources and not stale:
            say("OK", "recomp/generated/%s" % folder)
        elif check:
            say("TODO", "recomp/generated/%s - python tools/recompile.py %s" % (folder, build))
        else:
            run([sys.executable, "tools/recompile.py", build])


# ------------------------------------------------------------------ 6. runtime
def runtime(check):
    print("6. the runtime tree, its shaders and generated units")
    tree = ROOT / "build" / "runtime-tree"
    if tree.is_dir():
        code, _ = output([sys.executable, "tools/assemble_runtime.py", "--check"])
        if code != 0:
            raise Stop("build/runtime-tree does not match tools/assemble_runtime.py - "
                       "python tools/assemble_runtime.py --check says why")
        say("OK", "build/runtime-tree, every recorded edit applies")
    elif check:
        say("TODO", "build/runtime-tree - python tools/assemble_runtime.py")
    else:
        run([sys.executable, "tools/assemble_runtime.py"])
    if check:
        say("....", "shaders and generated units are checked by building")
        return
    run([sys.executable, "tools/build_xenia_shaders.py"])
    run([sys.executable, "tools/build_fsr3_shaders.py"])
    run([sys.executable, "tools/prepare_lsfg.py"])
    run([sys.executable, "tools/assemble_dao_convert.py"])
    # PLAN 11.5: the licences the app's About page shows, from the pinned sources.
    run([sys.executable, "tools/gather_licences.py"])
    run([sys.executable, "tools/prepare_deps.py"])


# ------------------------------------------------------------------ 7. local setup
def device_toolchain(check):
    """PLAN 12: what the app makes the game's code with on the device - LLVM 19.1.7's clang
    and lld and XenonRecomp, built for Android, and their headers and link inputs staged
    for the APK (about 25 minutes the first time; each part skipped when it is there)."""
    print("6b. the device toolchain (the app makes the game's code with it)")
    run([sys.executable, "tools/build_device_toolchain.py"] + (["--check"] if check else []))
    if not check:
        run([sys.executable, "tools/stage_gamecode_toolchain.py"])


def sdk_dir():
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(env) and Path(os.environ[env]).is_dir():
            return Path(os.environ[env])
    local = os.environ.get("LOCALAPPDATA")
    if local and (Path(local) / "Android" / "Sdk").is_dir():
        return Path(local) / "Android" / "Sdk"
    return None


def local_setup(check):
    print("7. local.properties, the SDK and the debug key")
    props = ROOT / "local.properties"
    sdk = sdk_dir()
    if props.is_file():
        say("OK", "local.properties")
    elif not sdk:
        say("FAIL", "no Android SDK found (ANDROID_HOME, or %LOCALAPPDATA%\\Android\\Sdk)")
    elif check:
        say("TODO", "local.properties - sdk.dir=%s" % sdk)
    else:
        escaped = str(sdk).replace("\\", "\\\\").replace(":", "\\:")
        props.write_text("sdk.dir=%s\n" % escaped, encoding="ascii")
        say("OK", "local.properties written (sdk.dir=%s)" % sdk)
    if sdk:
        missing = [p for p in SDK_PIECES if not (sdk / p).is_dir()]
        if missing:
            say("FAIL", "SDK pieces missing: %s - android sdk install %s"
                % (", ".join(missing), " ".join(missing)))
        else:
            say("OK", "SDK: %s" % ", ".join(SDK_PIECES))
    if not os.environ.get("JAVA_HOME"):
        say("WARN", "JAVA_HOME is not set in this shell (JDK 17 - docs/TOOLCHAIN.md)")
    key = Path.home() / ".android" / "debug.keystore"
    ours = ROOT / "app" / "debug.keystore"
    if key.is_file() and filecmp.cmp(key, ours, shallow=False):
        say("OK", "~/.android/debug.keystore is the project's key")
    elif key.is_file():
        say("WARN", "~/.android/debug.keystore is a different key: the APK will not install "
            "over the one on the Thor. docs/SIGNING.md - copy app/debug.keystore there "
            "(after keeping the old one if anything else uses it)")
    elif check:
        say("TODO", "~/.android/debug.keystore - to copy from app/debug.keystore")
    else:
        key.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ours, key)
        say("OK", "~/.android/debug.keystore copied from app/debug.keystore")


def validation_layer(args, check):
    """The Khronos validation layer, optional: it ships in the debug APK when it is in
    jniLibs, and docs/TESTING.md says how to turn it on. Fetched only when asked."""
    layer = ROOT / "app" / "src" / "main" / "jniLibs" / "arm64-v8a" / LAYER_NAME
    if layer.is_file() and sha256(layer) == LAYER_SHA256:
        say("OK", "the validation layer (Vulkan SDK %s) in jniLibs" % LAYER_VERSION)
        return
    if layer.is_file():
        say("WARN", "%s is not the %s build this project used" % (LAYER_NAME, LAYER_VERSION))
        return
    if check or not args.validation_layer:
        say("----", "the validation layer is optional - --validation-layer fetches it")
        return
    import io
    import urllib.request
    import zipfile
    print("         fetching %s" % LAYER_URL)
    with urllib.request.urlopen(LAYER_URL) as response:
        archive = response.read()
    if hashlib.sha256(archive).hexdigest() != LAYER_ZIP_SHA256:
        raise Stop("the validation layer archive is not the one this project used")
    member = "android-binaries-%s/arm64-v8a/%s" % (LAYER_VERSION, LAYER_NAME)
    data = zipfile.ZipFile(io.BytesIO(archive)).read(member)
    if hashlib.sha256(data).hexdigest() != LAYER_SHA256:
        raise Stop("the validation layer in the archive is not the one this project used")
    layer.parent.mkdir(parents=True, exist_ok=True)
    layer.write_bytes(data)
    say("OK", "the validation layer, in %s" % layer.parent.relative_to(ROOT))


# ------------------------------------------------------------------ 8. the APK
def build_apk(args, check):
    print("8. the APK")
    apk = ROOT / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    if check or args.skip_build:
        say("OK" if apk.is_file() else "TODO",
            "app-debug.apk" + ("" if apk.is_file() else " - ./gradlew :app:assembleDebug"))
        return
    key = Path.home() / ".android" / "debug.keystore"
    if key.is_file() and not filecmp.cmp(key, ROOT / "app" / "debug.keystore", shallow=False):
        raise Stop("~/.android/debug.keystore is not the project's key, so the APK would not "
                   "install over the app on the Thor (and uninstalling it deletes the game "
                   "data). Keep that file elsewhere if anything else uses it, copy "
                   "app/debug.keystore in its place (docs/SIGNING.md section 3), run again")
    gradlew = ROOT / ("gradlew.bat" if os.name == "nt" else "gradlew")
    run([gradlew, ":app:assembleDebug"])
    say("OK", "%s (%d MB)" % (apk.relative_to(ROOT), apk.stat().st_size >> 20))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--iso", help="the disc image, if not in the repo root")
    parser.add_argument("--tu", help="the Title Update 7 package, if not in the repo root")
    parser.add_argument("--accept-msvc-license", action="store_true",
                        help="accept Microsoft's licence for the MSVC CRT / Windows SDK "
                             "sysroot XenonRecomp is built against")
    parser.add_argument("--llvm-bin", default="C:/Program Files/LLVM/bin")
    parser.add_argument("--check", action="store_true", help="report, change nothing")
    parser.add_argument("--disc-build", action="store_true",
                        help="also recompile the disc build (for check_xex.py)")
    parser.add_argument("--skip-build", action="store_true", help="stop before Gradle")
    parser.add_argument("--validation-layer", action="store_true",
                        help="also fetch the Khronos validation layer into jniLibs "
                             "(optional, for Vulkan debugging)")
    args = parser.parse_args()
    try:
        iso, tu = find_assets(args)
        fetch_repos(args.check)
        extract_executables(iso, tu, args.check)
        build_recompiler(args, args.check)
        recompile(args, args.check)
        runtime(args.check)
        device_toolchain(args.check)
        local_setup(args.check)
        validation_layer(args, args.check)
        build_apk(args, args.check)
    except Stop as stop:
        print("\nstopped: %s" % stop)
        return 1
    print("\n%s" % ("checked - nothing changed" if args.check else
                    "done. Install: adb install -r -d app/build/outputs/apk/debug/app-debug.apk"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
