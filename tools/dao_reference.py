#!/usr/bin/env python3
"""
dao_reference.py - run DAORecomp's Windows build, the reference renderer (PLAN 3.3b),
without disturbing the owner.

Every start of DAORecomp goes through here, whatever it is for (CLAUDE.md, the owner's
rule of 2026-10-27):
  - it refuses while a game is running - tools/game_check.py's verdict, as before a
    build - so the owner can be asked first;
  - the game's window opens behind the active one with the focus kept
    (DAO_BACKGROUND=1), without its launcher (--play), windowed at 1280x720, without
    sound (DAO_AUDIO=null), at below-normal priority;
  - the game gets a test copy of userdata/config.toml (DAORecomp's
    scripts/test/set_config.py: the launcher hidden, the intro logos skipped, and the
    overrides), and the owner's copy is put back afterwards, byte for byte.

Usage:
  python tools/dao_reference.py run <name> <seconds> [--input <file or inline script>]
                                [--capture <s>] [--set section.key=value ...] [--game-ok]
  python tools/dao_reference.py check

`run` keeps the game's captures (capture_N.png, the guest output every --capture
seconds, default 2), dao.log and xenia.log in build/reference/<name>/. --input is
DAORecomp's DAO_INPUT_SCRIPT, a scripted gamepad: `<time s> <BUTTONS> [hold s]` and
`every <period s> <BUTTONS> [hold s] [from <s>] [until <s>]`, joined with newlines or
`;` (third_party/DAORecomp/scripts/test/README.md). --game-ok: the owner said to go
ahead although the check found a game. `check` runs the game check alone.
The install is C:\\Games\\DAORecomp, or DAO_REFERENCE.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
INSTALL = pathlib.Path(os.environ.get("DAO_REFERENCE", r"C:\Games\DAORecomp"))
EXE = INSTALL / "DAORecomp.exe"
USERDATA = INSTALL / "userdata"
CONFIG = USERDATA / "config.toml"
BACKUP = USERDATA / "config.toml.refbak"
SET_CONFIG = ROOT / "third_party" / "DAORecomp" / "scripts" / "test" / "set_config.py"
OUT = ROOT / "build" / "reference"
BELOW_NORMAL_PRIORITY_CLASS = 0x4000
# Every run's own settings, before the caller's: a window, never the whole screen.
WINDOWED = ["video.fullscreen=false", "video.window_width=1280", "video.window_height=720"]


def game_check():
    """(verdict line, the whole report) from tools/game_check.py."""
    sys.path.insert(0, str(ROOT / "tools"))
    import game_check as check
    return check.run()


def foreground_pid():
    """The process that owns the foreground window (Windows), 0 if none."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    window = user32.GetForegroundWindow()
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
    return pid.value


def reference_running():
    result = subprocess.run(["tasklist", "/FI", "IMAGENAME eq DAORecomp.exe", "/NH"],
                            capture_output=True, text=True)
    return "DAORecomp.exe" in result.stdout


def restore_config(existed):
    """The owner's config.toml back, byte for byte; a run that found none leaves none."""
    if existed:
        shutil.copyfile(BACKUP, CONFIG)
        if CONFIG.read_bytes() != BACKUP.read_bytes():
            print("WARNING: config.toml not restored; the owner's copy is %s" % BACKUP)
            return
        BACKUP.unlink()
    else:
        CONFIG.unlink(missing_ok=True)
        BACKUP.unlink(missing_ok=True)
    print("settings restored")


def run(args):
    if not EXE.is_file():
        sys.exit("no %s - install DAORecomp first (PLAN 3.3b)" % EXE)
    if reference_running():
        sys.exit("DAORecomp is already running: not starting another")
    verdict, report = game_check()
    print(verdict)
    if verdict != "verdict: no game" and not args.game_ok:
        print(report)
        sys.exit("a game may be running: ask the owner first (then --game-ok)")
    if BACKUP.exists():
        sys.exit("%s exists: an earlier run did not finish - put it back as config.toml first"
                 % BACKUP)

    USERDATA.mkdir(parents=True, exist_ok=True)
    existed = CONFIG.exists()
    if not existed:
        CONFIG.write_text("", encoding="utf-8")
    shutil.copyfile(CONFIG, BACKUP)
    try:
        subprocess.run([sys.executable, "-I", str(SET_CONFIG), str(BACKUP), str(CONFIG)] +
                       WINDOWED + args.set, check=True)
        for old in USERDATA.glob("capture_*.png"):
            old.unlink()
        env = dict(os.environ)
        env.update(DAO_BACKGROUND="1", DAO_AUDIO="null", DAO_SKIP_LAUNCHER="1")
        if args.capture > 0:
            env["DAO_CAPTURE_EVERY"] = str(args.capture)
        if args.input:
            env["DAO_INPUT_SCRIPT"] = args.input
        out = OUT / args.name
        out.mkdir(parents=True, exist_ok=True)
        # An earlier run of that name: its captures would mix with these.
        for old in list(out.glob("capture_*.png")) + [out / n for n in ("dao.log", "xenia.log")]:
            if old.exists():
                old.unlink()
        with open(out / "stdout.txt", "wb") as stdout:
            game = subprocess.Popen([str(EXE), "--play"], cwd=str(INSTALL), env=env,
                                    stdout=stdout, stderr=subprocess.STDOUT,
                                    creationflags=BELOW_NORMAL_PRIORITY_CLASS)
            print("%s started in the background (pid %d) for %d s" %
                  (time.strftime("%H:%M:%S"), game.pid, args.seconds))
            # Once a second: whose window is in front - never the game's, the owner's
            # rule; a run that took the focus says so.
            started = time.monotonic()
            in_front = []
            while game.poll() is None and time.monotonic() - started < args.seconds:
                if foreground_pid() == game.pid:
                    in_front.append(round(time.monotonic() - started))
                time.sleep(1)
            if game.poll() is None:
                subprocess.run(["taskkill", "/PID", str(game.pid), "/T", "/F"],
                               capture_output=True)
                game.wait()
            else:
                print("it ended by itself (exit %d)" % game.returncode)
            if in_front:
                print("WARNING: the game's window was in front at %s s" % in_front[:10])
            else:
                print("its window stayed behind the owner's the whole run")
        captures = sorted(USERDATA.glob("capture_*.png"),
                          key=lambda p: int(p.stem.split("_")[1]))
        for capture in captures:
            shutil.move(str(capture), str(out / capture.name))
        for log in ("dao.log", "xenia.log"):
            if (USERDATA / log).is_file():
                shutil.copyfile(USERDATA / log, out / log)
    finally:
        restore_config(existed)
    swaps = 0
    problems = []
    if (out / "dao.log").is_file():
        for line in (out / "dao.log").read_text(encoding="utf-8", errors="replace").splitlines():
            swaps += "VdSwap #" in line
            if any(word in line for word in ("abort", "Abort", "device lost", "DEVICE_REMOVED")):
                problems.append(line.strip())
    print("%s: %d captures, %d swap lines in dao.log" % (out, len(captures), swaps))
    for line in problems[:10]:
        print("  " + line)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("name")
    r.add_argument("seconds", type=int)
    r.add_argument("--input", default="")
    r.add_argument("--capture", type=int, default=2)
    r.add_argument("--set", action="append", default=[])
    r.add_argument("--game-ok", action="store_true")
    sub.add_parser("check")
    a = ap.parse_args()
    if a.cmd == "check":
        verdict, report = game_check()
        print(report)
        return 0 if verdict == "verdict: no game" else 3
    run(a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
