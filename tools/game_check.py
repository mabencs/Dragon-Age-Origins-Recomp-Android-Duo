"""game_check.py - is the owner playing a game on this PC? (CLAUDE.md: before any build,
and before every start of DAORecomp's Windows build.)

    python tools/game_check.py

Looks four ways, as the owner's rule asks: processes under a game library; what the game
launchers started; the heaviest users of the GPU's 3D engine and of dedicated GPU memory
(a game keeps its memory when alt-tabbed); and windowed processes. Prints what it found
and a last line "verdict: no game" or "verdict: possible game: <names>". Launchers open
with nothing launched are not games, nor is DAORecomp itself (it is reported, as
"reference"); nor are the processes named in tools/game_check.local.txt (one name a line,
without .exe - gitignored: which games a builder lets run during a build is their own
business). Deciding is still the caller's: the verdict names candidates, a person judges
them.

Python rather than PowerShell (it was game_check.ps1 until session 35): this PC's
execution policy refuses unsigned scripts, and the check should never need that policy
loosened. Processes and windows come from the Windows API through ctypes, the GPU's
counters from typeperf; nothing here changes anything.

    import game_check; verdict, report = game_check.run()   # for the other tools
"""
import csv
import ctypes
import io
import re
import subprocess
import sys
from ctypes import wintypes

LIBRARIES = ["\\steamapps\\common\\", "\\Epic Games\\", "\\XboxGames\\", "\\GOG Games\\",
             "\\GOG Galaxy\\Games\\", "\\Riot Games\\", "\\EA Games\\",
             "\\Ubisoft Game Launcher\\games\\", "\\Rockstar Games\\", "C:\\Games\\"]
LAUNCHERS = ["Battle.net", "Agent", "steam", "EpicGamesLauncher", "EADesktop", "GalaxyClient",
             "UbisoftConnect", "upc", "RiotClientServices", "RockstarLauncher"]
# Launchers' own helpers, overlays and services: not games.
HELPERS = ["steamwebhelper", "steamservice", "Battle.net", "BlizzardBrowser", "Agent",
           "EpicWebHelper", "EpicOnlineServicesUserHelper", "CrashReporter", "GameOverlayUI",
           "gameoverlayui64", "UplayWebCore", "EABackgroundService", "EALocalHostSvc",
           "GalaxyClient Helper", "RiotClientCrashHandler", "conhost", "cmd", "powershell"]
# Local and gitignored, tools/game_check.local.txt: a process name a line (no .exe; #
# starts a comment) - a game that may run during a build; "everyday: <name>" - one of the
# builder's own everyday programs, listed but never counted as a game.
EXEMPT = []
LOCAL_EVERYDAY = []
_LOCAL = __import__("pathlib").Path(__file__).with_name("game_check.local.txt")
if _LOCAL.is_file():
    for _line in _LOCAL.read_text(encoding="utf-8", errors="replace").splitlines():
        _name = _line.split("#", 1)[0].strip()
        _list = EXEMPT
        if _name.lower().startswith("everyday:"):
            _name, _list = _name[len("everyday:"):].strip(), LOCAL_EVERYDAY
        if _name:
            _list.append(_name[:-4] if _name.lower().endswith(".exe") else _name)
SYSTEM = ["dwm", "System", "Idle", "explorer", "csrss", "svchost", "ShellExperienceHost",
          "SearchHost", "StartMenuExperienceHost", "TextInputHost", "ApplicationFrameHost"]
# Everyday programs and the like, never games: listed, never counted. Their GPU memory
# counts can be absurd (a browser has read 147 GB "dedicated"). A builder's own others are
# "everyday:" lines in tools/game_check.local.txt.
EVERYDAY = ["firefox", "chrome", "msedge", "opera", "brave", "Discord", "Code", "VSCodium",
            "electron", "Spotify", "Teams", "ms-teams", "Slack", "Zoom", "obs64", "PowerToys",
            "PowerToys.QuickAccess", "PowerToys.Settings", "7zFM", "Notepad", "SystemSettings",
            "clang-cl", "lld-link", "java", "python", "ninja", "cmake", "adb", "node",
            "typeperf"] + LOCAL_EVERYDAY


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_void_p),
                ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", wintypes.DWORD), ("szExeFile", ctypes.c_wchar * 260)]


def processes():
    """{pid: (parent pid, name without .exe, full path or None)}."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.OpenProcess.restype = wintypes.HANDLE
    snapshot = kernel32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    found = {}
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    more = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
    while more:
        name = re.sub(r"\.exe$", "", entry.szExeFile, flags=re.I)
        path = None
        handle = kernel32.OpenProcess(0x1000, False, entry.th32ProcessID)  # limited query
        if handle:
            buffer = ctypes.create_unicode_buffer(1024)
            size = wintypes.DWORD(len(buffer))
            if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                path = buffer.value
            kernel32.CloseHandle(handle)
        found[int(entry.th32ProcessID)] = (int(entry.th32ParentProcessID), name, path)
        more = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    kernel32.CloseHandle(snapshot)
    return found


def windows():
    """[(pid, title)] of the visible top-level windows that have a title."""
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(window, _):
        if user32.IsWindowVisible(window):
            length = user32.GetWindowTextLengthW(window)
            if length:
                title = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(window, title, length + 1)
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
                found.append((pid.value, title.value))
        return True

    user32.EnumWindows(each, 0)
    return found


def counter_by_pid(path):
    """{pid: summed value} of a GPU counter over its instances, from one typeperf sample."""
    result = subprocess.run(["typeperf", path, "-sc", "1"], capture_output=True, text=True)
    rows = [row for row in csv.reader(io.StringIO(result.stdout)) if len(row) > 1]
    if len(rows) < 2:
        return {}
    header, values = rows[0], rows[1]
    totals = {}
    for name, value in zip(header[1:], values[1:]):
        match = re.search(r"pid_(\d+)_", name)
        try:
            number = float(value)
        except ValueError:
            continue
        if match:
            pid = int(match.group(1))
            totals[pid] = totals.get(pid, 0.0) + number
    return totals


def run():
    """(verdict line, the whole report)."""
    procs = processes()
    lines = []
    candidates = []

    def name_of(pid):
        return procs[pid][1] if pid in procs else "pid %d" % pid

    def consider(name, why):
        if name in EXEMPT:
            lines.append("  (%s - exempt)" % name)
            return
        if name == "DAORecomp":
            lines.append("  DAORecomp (reference) running")
            return
        if name in HELPERS or name in SYSTEM or name in EVERYDAY:
            return
        if name not in candidates:
            candidates.append(name)
        lines.append("  %s - %s" % (name, why))

    lines.append("game libraries:")
    for pid, (_, name, path) in sorted(procs.items()):
        if path and any(library.lower() in path.lower() for library in LIBRARIES):
            consider(name, path)

    lines.append("started by a launcher:")
    launcher_ids = {pid for pid, (_, name, _) in procs.items() if name in LAUNCHERS}
    for pid, (parent, name, _) in sorted(procs.items()):
        if parent in launcher_ids:
            consider(name, "child of " + name_of(parent))

    lines.append("GPU 3D engine (busiest, %):")
    load = counter_by_pid(r"\GPU Engine(*engtype_3D)\Utilization Percentage")
    for pid, value in sorted(load.items(), key=lambda item: -item[1])[:5]:
        lines.append("  %-28s %6.1f" % (name_of(pid), value))
        if value >= 20:
            consider(name_of(pid), "%.0f%% of the 3D engine" % value)

    lines.append("dedicated GPU memory (largest, MB):")
    held = counter_by_pid(r"\GPU Process Memory(*)\Dedicated Usage")
    for pid, value in sorted(held.items(), key=lambda item: -item[1])[:5]:
        megabytes = value / (1 << 20)
        lines.append("  %-28s %8.0f" % (name_of(pid), megabytes))
        if megabytes >= 1500 and name_of(pid) != "dwm":
            consider(name_of(pid), "%.0f MB of GPU memory" % megabytes)

    lines.append("windows:")
    seen = set()
    for pid, title in windows():
        if (pid, title) not in seen:
            seen.add((pid, title))
            lines.append("  %-24s %s" % (name_of(pid), title))

    verdict = ("verdict: no game" if not candidates
               else "verdict: possible game: " + ", ".join(candidates))
    lines.append(verdict)
    return verdict, "\n".join(lines)


if __name__ == "__main__":
    if sys.platform != "win32":
        sys.exit("game_check.py looks at Windows processes; on another machine there is no "
                 "game to check for (CLAUDE.md's rule is for the owner's gaming PC)")
    # A window title can hold what the console's code page cannot (an emoji stopped the
    # check before its verdict, session 36): replaced, never fatal.
    sys.stdout.reconfigure(errors="replace")
    print(run()[1])
