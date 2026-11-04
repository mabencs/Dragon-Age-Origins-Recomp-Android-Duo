#!/usr/bin/env python3
"""
push_gamecode.py - put the game's code built here on the device (PLAN 12.2).

The game's code is libdaogame.so, never in the APK: the published app makes it on the
device from the player's disc (PLAN 12.5), and for development the `daogame` target
builds the same library here (Gradle builds it with the app, and leaves it out of the
APK). This copies the built one into the app's files - <files>/gamecode/libdaogame.so,
read-only, as the app keeps the one it makes - where the runtime loads it at boot.

    python tools/push_gamecode.py [--release] [--clean]

Only when it changed: the copy on the device is compared by its SHA-256 first. The
library holds the game's code translated - it goes to the owner's own device only.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def library(variant):
    found = sorted(ROOT.glob("app/build/intermediates/cxx/%s/*/obj/arm64-v8a/libdaogame.so"
                             % variant), key=lambda p: p.stat().st_mtime)
    if not found:
        raise SystemExit("no libdaogame.so built for %s - build the app first (Gradle builds "
                         "the daogame target with it)" % variant)
    return found[-1]


def adb(*args, capture=False):
    cmd = ["adb"] + [str(a) for a in args]
    if capture:
        # Read only: a missing file is an empty answer, not a failure.
        return subprocess.run(cmd, check=False, capture_output=True, text=True).stdout
    subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("--release", action="store_true", help="the release build's, to prod.")
    parser.add_argument("--clean", action="store_true", help="11.4's clean copy (.clean)")
    args = parser.parse_args()
    variant = "RelWithDebInfo" if args.release else "Debug"
    package = "prod.daorecompandroid.duo" if args.release else "dev.daorecompandroid.duo"
    if args.clean:
        package += ".clean"
    path = library(variant)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    there = adb("shell", "run-as %s sh -c 'sha256sum files/gamecode/libdaogame.so 2>/dev/null'"
                % package, capture=True).split()
    same = bool(there) and there[0] == digest
    if same:
        # The manifest still written: the library may have come another way, its
        # manifest left as the last made (the launcher then asks to build again).
        print("the device has this libdaogame.so already (%s)" % digest[:12])
    else:
        staging = "/sdcard/warden-libdaogame.so"
        adb("push", path, staging)
        adb("shell", "run-as %s sh -c 'mkdir -p files/gamecode && chmod 700 files/gamecode && "
                     "cp %s files/gamecode/libdaogame.so.part && "
                     "chmod 0444 files/gamecode/libdaogame.so.part && "
                     "mv -f files/gamecode/libdaogame.so.part files/gamecode/libdaogame.so'"
            % (package, staging))
        adb("shell", "rm -f " + staging)
    # What it was made from, as the app writes it (GameCode.java): without it the app
    # would make the game's code again itself (PLAN 12.5). The computer's is TU7's.
    abi = json.loads((ROOT / "app" / "src" / "main" / "assets" / "gamecode" /
                      "recipe.json").read_text(encoding="utf-8"))["abi"]
    manifest = json.dumps({"abi": abi, "tu": 7, "made": "computer"})
    # As a file: the JSON's quotes did not survive adb's shell, run-as's and sh -c's
    # (the manifest was never written - an echo to the screen).
    with tempfile.TemporaryDirectory() as temp:
        local = Path(temp) / "manifest.json"
        local.write_text(manifest + "\n", encoding="utf-8")
        staging = "/sdcard/warden-manifest.json"
        adb("push", local, staging)
        adb("shell", "run-as %s sh -c 'cp %s files/gamecode/manifest.json'"
            % (package, staging))
        adb("shell", "rm -f " + staging)
    if not same:
        print("pushed %s (%.1f MB, %s) to %s files/gamecode/" % (
            path.relative_to(ROOT), path.stat().st_size / 1e6, digest[:12], package))
    print("manifest: ABI %s" % abi)
    return 0


if __name__ == "__main__":
    sys.exit(main())
