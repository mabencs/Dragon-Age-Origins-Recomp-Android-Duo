#!/usr/bin/env python3
"""
reference_build.py - build and install DAORecomp's Windows build, the reference renderer
of PLAN 3.3b and Phase 9, from the same DAORecomp revision as the port.

  python tools/reference_build.py [--dest C:\\Games\\DAORecomp] [--game-ok]

1. A clone of third_party/DAORecomp (the pinned checkout, which stays as fetched) in
   %LOCALAPPDATA%\\DAORecomp-src, with the port's corrections to DAORecomp's recompiler
   configuration applied - tools/recompile.py's ADDITIONS (BUG-051: without them the
   reference build crashes entering the Korcari Wilds, as the port did) - and the
   port's PATCHES to its source (DAO_RUN_SCRIPT, for the areas' debug jumps).
   An incremental rebuild after a patch, without the installer: the installer builds
   its own copy of the sources in %LOCALAPPDATA%\\DAORecomp-build (not the clone), so
   the patch goes into that copy too; then the game check,
   `%LOCALAPPDATA%\\DAORecomp-build\\tools\\ninja\\ninja.exe -C
   %LOCALAPPDATA%\\DAORecomp-build\\build\\installer\\release DAORecomp`, and the
   built `release\\DAORecomp\\DAORecomp.exe` copied over C:\\Games\\DAORecomp's while
   the game is not running.
2. The game check (tools/game_check.py): a build waits for the owner if a game may be
   running (CLAUDE.md).
3. DAORecomp's installer from the clone - the disc image and Title Update 7 from the
   repository root, the tools and the build in %LOCALAPPDATA%\\DAORecomp-build
   (DAO_WORK_DIR), the game in --dest - started detached (a build outlives the shell),
   hidden, at below-normal priority, its output in build/daorecomp-install.log. It ends
   with "installer exit code N"; running this again resumes where it stopped.

The Microsoft headers and SDK the build needs are the ones toolchain/winsysroot already
holds (tools/setup_winsysroot.py, their license accepted there): copied into the work
folder, so the installer neither downloads them nor asks again.
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import recompile  # noqa: E402  (its ADDITIONS)

CHECKOUT = ROOT / "third_party" / "DAORecomp"
LOCAL = pathlib.Path(os.environ["LOCALAPPDATA"])
CLONE = LOCAL / "DAORecomp-src"
WORK = LOCAL / "DAORecomp-build"
ISO = ROOT / "Dragon Age - Origins (World) (En,Es,It).iso"
TU7 = ROOT / "TU_12K2260_0000004000000.00000000001O1"
LOG = ROOT / "build" / "daorecomp-install.log"


def git(*args, cwd=None):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


def make_clone():
    head = git("-C", str(CHECKOUT), "rev-parse", "HEAD")
    if not (CLONE / ".git").is_dir():
        git("clone", "--quiet", "--no-hardlinks", str(CHECKOUT), str(CLONE))
    git("-C", str(CLONE), "fetch", "--quiet", str(CHECKOUT), head)
    git("-C", str(CLONE), "reset", "--quiet", "--hard", head)
    print("clone: %s at %s" % (CLONE, head[:10]))


def apply_additions():
    """recompile.ADDITIONS into the clone's TOML files themselves (the installer reads
    them by their own names)."""
    for config, additions in recompile.ADDITIONS.items():
        toml = CLONE / "config" / (config + ".toml")
        text = toml.read_text(encoding="utf-8")
        rows = "".join("    { address = 0x%08X, size = 0x%X },\n" % f
                       for f in additions["functions"])
        text, n = re.subn(r"^functions = \[\n", lambda m: m.group(0) + rows, text, count=1,
                          flags=re.M)
        assert n == 1, "no 'functions = [' in %s" % toml
        toml.write_text(text, encoding="utf-8", newline="\n")
        if additions["switches"]:
            name = re.search(r'^switch_table_file_path\s*=\s*"([^"]+)"', text, flags=re.M).group(1)
            tables = CLONE / "config" / name
            text = tables.read_text(encoding="utf-8")
            text += "".join("\n[[switch]]\nbase = 0x%08X\nr = %d\nlabels = [\n%s]\n"
                            % (base, r, "".join("    0x%08X,\n" % l for l in labels))
                            for base, r, labels in additions["switches"])
            tables.write_text(text, encoding="utf-8", newline="\n")
        print("ADDITIONS: %d function(s), %d switch table(s) in the clone's %s" %
              (len(additions["functions"]), len(additions["switches"]), config))


# The port's own additions to the reference's source (file, description, find, replace),
# each matching exactly once - as tools/assemble_runtime.py's EDITS; the clone is reset
# to the pinned commit first, so they are applied on every run.
PATCHES = [
    # Session 40 (BUG-085's reference check): DAO_RUN_SCRIPT=<name>[@<seconds>] runs a
    # script - a module's debug script, zz_den_debug say - once the game is playing,
    # as the player's character, without the cheats menu; DAO_INPUT_SCRIPT then drives
    # its dialogue. The same jump as the Thor's areas' tour, in the reference.
    ("DAORecomp/ui/cheats.cpp", "DAO_RUN_SCRIPT: a script run once the game is playing",
     "        const bool playing = game && (mode == kModeExplore || mode == kModeCombat);\n"
     "        const int64_t toggleAt = s_toggleAt.load(std::memory_order_relaxed);\n",
     "        const bool playing = game && (mode == kModeExplore || mode == kModeCombat);\n"
     "        // DAO/Warden (the Android port's reference checks, its BUG-085):\n"
     "        // DAO_RUN_SCRIPT=<name>[@<seconds>] runs the script `name` once, as the\n"
     "        // player's character and without the menu, `seconds` (default 5) after the\n"
     "        // game is first playing - a debug script's menu, which DAO_INPUT_SCRIPT then\n"
     "        // drives.\n"
     "        {\n"
     "            static const char* autoRun = getenv(\"DAO_RUN_SCRIPT\");\n"
     "            static int64_t autoRunPlayingSince = 0;\n"
     "            static bool autoRunDone = false;\n"
     "            if (autoRun && *autoRun && !autoRunDone && playing)\n"
     "            {\n"
     "                if (!autoRunPlayingSince)\n"
     "                    autoRunPlayingSince = Now();\n"
     "                std::string name(autoRun);\n"
     "                double delay = 5.0;\n"
     "                if (const size_t at = name.find('@'); at != std::string::npos)\n"
     "                {\n"
     "                    delay = atof(name.c_str() + at + 1);\n"
     "                    name.resize(at);\n"
     "                }\n"
     "                if (Now() - autoRunPlayingSince > int64_t(delay * 1e9))\n"
     "                {\n"
     "                    autoRunDone = true;\n"
     "                    LOGI(\"Cheats: DAO_RUN_SCRIPT {} after {} s playing\", name, delay);\n"
     "                    RunRequest(base, Request{ name, {}, dao::ui::cheats::Scope::Hero });\n"
     "                }\n"
     "            }\n"
     "        }\n"
     "        const int64_t toggleAt = s_toggleAt.load(std::memory_order_relaxed);\n"),
]


def apply_patches():
    for path, description, find, replace in PATCHES:
        file = CLONE / path
        text = file.read_text(encoding="utf-8")
        if replace in text:
            print("patch already applied: %s" % description)
            continue
        assert text.count(find) == 1, "%s: '%s' must match exactly once" % (path, description)
        file.write_text(text.replace(find, replace), encoding="utf-8", newline="\n")
        print("patched %s: %s" % (path, description))


def winsysroot():
    target = WORK / "tools" / "winsysroot"
    source = ROOT / "toolchain" / "winsysroot"
    stamp = "dao-xwin.txt"
    if (target / stamp).is_file() and (target / stamp).read_text() == (source / stamp).read_text():
        return
    if not (source / stamp).is_file():
        sys.exit("no toolchain/winsysroot - python tools/setup_winsysroot.py --accept-license")
    shutil.copytree(source, target, dirs_exist_ok=True)
    print("winsysroot: copied into %s" % target)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dest", default=r"C:\Games\DAORecomp")
    ap.add_argument("--game-ok", action="store_true", help="the owner said to go ahead")
    a = ap.parse_args()
    make_clone()
    apply_additions()
    apply_patches()
    winsysroot()
    sys.path.insert(0, str(ROOT / "tools"))
    import game_check
    verdict, check = game_check.run()
    print(verdict)
    if verdict != "verdict: no game" and not a.game_ok:
        print(check)
        sys.exit("a game may be running: ask the owner first (then --game-ok)")
    LOG.parent.mkdir(parents=True, exist_ok=True)
    batch = ROOT / "build" / "daorecomp-install.cmd"
    batch.write_text(
        "@echo off\nset DAO_WORK_DIR=%s\npython \"%s\" --iso \"%s\" --title-update \"%s\" "
        "--dest \"%s\"\necho installer exit code %%ERRORLEVEL%%\n"
        % (WORK, CLONE / "install.py", ISO, TU7, a.dest), encoding="utf-8", newline="\r\n")
    # Through WMI: a process of its own, not the shell's child, so it outlives the shell;
    # hidden (ShowWindow 0) and below normal (0x4000), which its children inherit.
    # /s and the outer quotes: cmd strips only those, keeping the paths' own.
    command = 'cmd.exe /s /c ""%s" >> "%s" 2>&1"' % (batch, LOG)
    script = ("$s = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property "
              "@{ShowWindow=[uint16]0; PriorityClass=[uint32]16384}; "
              "$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments "
              "@{CommandLine=$env:DAO_CMD; CurrentDirectory=$env:DAO_DIR; "
              "ProcessStartupInformation=$s}; \"$($r.ReturnValue) $($r.ProcessId)\"")
    env = dict(os.environ, DAO_CMD=command, DAO_DIR=str(ROOT))
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script], env=env,
                         capture_output=True, text=True).stdout.split()
    if not out or out[0] != "0":
        sys.exit("the installer did not start: %s" % " ".join(out))
    print("installer started (pid %s): its output in %s, ending with 'installer exit code N'"
          % (out[1], LOG))
    return 0


if __name__ == "__main__":
    sys.exit(main())
