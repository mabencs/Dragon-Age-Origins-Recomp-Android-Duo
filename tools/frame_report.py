"""The performance baseline's report (PLAN 6.7, 4.7) for one run.

    python tools/frame_report.py <run>

Reads build/run<run>.log - the `frames:` and `pacing:` lines the GPU status reporter
writes every 5 s (app/src/main/cpp/warden_frame_times.cc, warden_frame_pacing.cc) - from
the second explore mode on (the `ostagar` route passes through a test area first),
skipping the arrival's first two windows. Per stream: the frame count, the mean interval,
the worst, the histogram in refreshes (or 60 Hz vblanks for the title) with the share of
frames off the main bucket, and the 1% low. With `pacing:` lines, the panel's own
intervals per pacing mode, the present-to-panel latency, how early frames were ready, the
frames never shown and the paced grid's moves; and the `frames:` streams again per mode,
from the windows spent wholly in one. Then, if build/run<run>-power.txt exists
(tools/sample_power.sh), the power draw over the samples taken in game (the GPU at 90%
busy or more), the hours a full battery would last at it, and the temperatures."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
run = sys.argv[1]
lines = (ROOT / "build" / ("run%s.log" % run)).read_text(encoding="utf-8", errors="replace").splitlines()

explores, start = 0, False
windows = []  # [frames text, pacing text or None]
for line in lines:
    if "game: mode is now 0 (explore)" in line:
        explores += 1
        start = start or explores == 2
    if not start:
        continue
    if "frames: " in line:
        windows.append([line.split("frames: ", 1)[1], None])
    elif "pacing: " in line and windows and windows[-1][1] is None:
        windows[-1][1] = line.split("pacing: ", 1)[1]
windows = windows[2:]

# "display" in runs 277-278: the presents.
STREAM = re.compile(r"(display|present|title|panel): (\d+) frames, interval ms mean ([\d.]+) p50 [\d.]+ "
                    r"p95 [\d.]+ p99 [\d.]+ max ([\d.]+), 1% low [\d.]+ fps(?:, in [^(]+\(([\d.]+) ms\):(.*))?")


def add(totals, name, text):
    m = STREAM.match(text)
    if not m:
        return
    _, n, mean, worst, unit, hist = m.groups()
    t = totals.setdefault(name, {"n": 0, "sum": 0.0, "max": 0.0, "unit": unit, "hist": {}})
    t["n"] += int(n)
    t["sum"] += int(n) * float(mean)
    t["max"] = max(t["max"], float(worst))
    for k, c in re.findall(r" (\d+)\+?x(\d+)", hist or ""):
        t["hist"][int(k)] = t["hist"].get(int(k), 0) + int(c)


def report(name, t, windows_count):
    print("%s: %d frames over %d windows, mean interval %.2f ms (%.1f fps), worst %.1f ms" % (
        name, t["n"], windows_count, t["sum"] / t["n"], 1000.0 * t["n"] / t["sum"], t["max"]))
    if not t["hist"]:
        return
    unit = float(t["unit"])
    total = sum(t["hist"].values())
    main = max(t["hist"], key=t["hist"].get)
    print("  in %.2f ms units: %s; %.1f%% of frames off %dx" % (
        unit, " ".join("%dx:%d (%.1f%%)" % (k, c, 100.0 * c / total) for k, c in sorted(t["hist"].items())),
        100.0 * (total - t["hist"][main]) / total, main))
    need = max(1, total // 100)  # the slowest 1% of frames, from the histogram
    acc, weighted = 0, 0.0
    for k in sorted(t["hist"], reverse=True):
        take = min(t["hist"][k], need - acc)
        acc += take
        weighted += take * k * unit
        if acc >= need:
            break
    print("  1%% low: %.1f fps (the slowest %d frames average %.1f ms)" % (
        1000.0 * need / weighted, need, weighted / need))


totals = {}
for frames, _ in windows:
    for part in frames.split("; "):
        add(totals, part.split(":", 1)[0], part)
for name, t in totals.items():
    report(name, t, len(windows))

if any(p for _, p in windows):
    per_mode = {}   # mode -> totals of the frames streams, windows wholly in that mode
    panel = {}      # mode -> totals of the panel stream
    extra = {}      # mode -> counters
    mode_windows = {}
    periods = []
    for frames, pacing in windows:
        if not pacing:
            continue
        parts = pacing.split(" | ")
        pm = re.search(r"\(measured ([\d.]+)\)", parts[0])
        if pm and float(pm.group(1)) > 0:
            periods.append(float(pm.group(1)))
        m = re.match(r"mode (\w+) for (\d+) s", parts[0])
        if m and int(m.group(2)) >= 5:
            mode_windows[m.group(1)] = mode_windows.get(m.group(1), 0) + 1
            for part in frames.split("; "):
                add(per_mode.setdefault(m.group(1), {}), part.split(":", 1)[0], part)
        for part in parts[1:]:
            mode, rest = part.split(" ", 1)
            fields = rest.split("; ")
            add(panel.setdefault(mode, {}), "panel", fields[0])
            e = extra.setdefault(mode, {"p50": [], "p95": [], "max": 0.0, "ready": [0, 0, 0, 0],
                                        "lost": 0, "late": 0, "held": 0, "early": 0, "ahead": 0,
                                        "earlier": 0, "later": 0, "new": 0,
                                        "s50": [], "s95": [], "smax": 0.0,
                                        "a50": [], "a95": [], "amax": 0.0,
                                        "slots": [0, 0, 0]})
            for f in fields[1:]:
                lm = re.match(r"latency ms p50 ([\d.]+) p95 ([\d.]+) max ([\d.]+)", f)
                if lm:
                    e["p50"].append(float(lm.group(1)))
                    e["p95"].append(float(lm.group(2)))
                    e["max"] = max(e["max"], float(lm.group(3)))
                am = re.match(r"acquire ms p50 ([\d.]+) p95 ([\d.]+) max ([\d.]+)", f)
                if am:
                    e["a50"].append(float(am.group(1)))
                    e["a95"].append(float(am.group(2)))
                    e["amax"] = max(e["amax"], float(am.group(3)))
                sm = re.match(r"swap to panel ms p50 ([\d.]+) p95 ([\d.]+) max ([\d.]+)", f)
                if sm:
                    e["s50"].append(float(sm.group(1)))
                    e["s95"].append(float(sm.group(2)))
                    e["smax"] = max(e["smax"], float(sm.group(3)))
                rm = re.match(r"ready earlier 0:(\d+) 1:(\d+) 2:(\d+) 3\+:(\d+)", f)
                if rm:
                    e["ready"] = [a + int(b) for a, b in zip(e["ready"], rm.groups())]
                for key in ("lost", "late", "held", "early", "ahead"):
                    km = re.match(key + r" (\d+)", f)
                    if km:
                        e[key] += int(km.group(1))
                gm = re.match(r"grid earlier (\d+) later (\d+) new (\d+)", f)  # runs 280-283
                if gm:
                    e["earlier"] += int(gm.group(1))
                    e["later"] += int(gm.group(2))
                    e["new"] += int(gm.group(3))
                pm2 = re.match(r"pushed (\d+)", f)
                if pm2:
                    e["later"] += int(pm2.group(1))
                lm2 = re.match(r"slots 3x(\d+) 4x(\d+) 5x(\d+)", f)
                if lm2:
                    e["slots"] = [a + int(b) for a, b in zip(e["slots"], lm2.groups())]
                nm = re.match(r"new (\d+)", f)
                if nm:
                    e["new"] += int(nm.group(1))
    if periods:
        periods.sort()
        print("the panel's period, measured from its own times: median %.3f ms (%.2f Hz), windows %.3f-%.3f" % (
            periods[len(periods) // 2], 1000.0 / periods[len(periods) // 2], periods[0], periods[-1]))
    for mode in sorted(panel):
        print("== %s (the panel's own times)" % mode)
        if "panel" in panel[mode]:
            report("panel", panel[mode]["panel"], len(windows))
        e = extra[mode]
        if e["p50"]:
            p50 = sorted(e["p50"])[len(e["p50"]) // 2]
            p95 = sorted(e["p95"])[len(e["p95"]) // 2]
            print("  present to panel: median of the windows' p50 %.1f ms, of their p95 %.1f ms; worst %.1f ms"
                  % (p50, p95, e["max"]))
        if e["s50"]:
            print("  the title's swap to panel: median of the windows' p50 %.1f ms, of their p95 %.1f ms; worst %.1f ms"
                  % (sorted(e["s50"])[len(e["s50"]) // 2], sorted(e["s95"])[len(e["s95"]) // 2], e["smax"]))
        if e["a50"]:
            print("  the presenter's wait for an image: median of the windows' p50 %.1f ms, of their p95 %.1f ms; worst %.1f ms"
                  % (sorted(e["a50"])[len(e["a50"]) // 2], sorted(e["a95"])[len(e["a95"]) // 2], e["amax"]))
        ready = e["ready"]
        print("  finished 0/1/2/3+ refreshes before the compositor took them: %s; never shown: %d" % (
            "/".join(str(x) for x in ready), e["lost"]))
        if mode.startswith("paced") or mode.startswith("phase"):
            # "paced": the desired time half a refresh before the slot to run 297 - every
            # frame shown a refresh before it - and a quarter of a refresh after it from
            # run 299. Run 298 compared 4, 3, 2 and 1 eighths after it as "paced",
            # "paced3", "paced2", "paced1"; the "phases" setting names them "phase4" to
            # "phase1" from run 299.
            print("  late for their slot: %d (of them finished in time, held by the desired time: %d); "
                  "early: %d" % (e["late"], e["held"], e["early"]))
            print("  the timeline moved earlier %d (whole refreshes, runs 280-283), "
                  "later %d (pushes from run 284), started %d times (%d as it ran ahead, from run 297)" % (
                      e["earlier"], e["later"], e["new"], e["ahead"]))
            if sum(e["slots"]):
                print("  the slots themselves: 3 or fewer refreshes apart %d, 4 %d, 5 or more %d" % tuple(e["slots"]))
        if mode in per_mode:
            print("  the frames line over the %d windows wholly %s:" % (mode_windows[mode], mode))
            for name, t in per_mode[mode].items():
                report("  " + name, t, mode_windows[mode])

power = ROOT / "build" / ("run%s-power.txt" % run)
if not power.exists():
    sys.exit()
plines = power.read_text().splitlines()
header = plines[0].split()
full_mah = int(header[header.index("full_uah") + 1]) / 1000.0
print("power: %s" % plines[0].lstrip("# "))
rows = []
for line in plines[2:]:
    f = line.split()
    if len(f) != 13:
        continue
    h, m, s = (int(x) for x in f[0].split(":"))
    v = [int(x) for x in f[1:]]
    rows.append(dict(sec=h * 3600 + m * 60 + s, busy=v[0], mhz=v[1], gpu_c=v[2], cpu_c=v[3],
                     board_c=v[4], batt_c=v[5], batt_ma=v[6], batt_mv=v[7], batt_mah=v[8],
                     level=v[9], usb_ma=v[10], usb_mv=v[11]))
game = [r for r in rows if r["busy"] >= 90]
print("  %d samples, %d in game (the GPU at 90%% busy or more)" % (len(rows), len(game)))
if not game:
    sys.exit()
n = float(len(game))
volts = sum(r["batt_mv"] for r in game) / n / 1000.0
discharge_ma = sum(-r["batt_ma"] for r in game) / n
usb_w = sum(r["usb_ma"] * r["usb_mv"] for r in game) / n / 1e6
span = game[-1]["sec"] - game[0]["sec"]
counter_ma = (game[0]["batt_mah"] - game[-1]["batt_mah"]) * 3600.0 / span if span > 0 else 0.0
battery_wh = full_mah * volts / 1000.0
for what, ma in (("the battery's current", discharge_ma), ("the charge counter's slope", counter_ma)):
    draw = ma * volts / 1000.0 + usb_w * 0.9  # the charger taken as 90% efficient
    print("  by %s: %.0f mA out of the battery + %.2f W in -> %.1f W, %.1f h from a full battery (%.1f Wh)" % (
        what, ma, usb_w, draw, battery_wh / draw, battery_wh))
print("  level %d%% -> %d%% over %d s" % (game[0]["level"], game[-1]["level"], span))
print("  GPU clock %s MHz, busy %d-%d%%; temperatures: GPU %d-%d C, CPU %d-%d C, board %d-%d C, battery %d-%d C" % (
    "/".join(str(x) for x in sorted({r["mhz"] for r in game})),
    min(r["busy"] for r in game), max(r["busy"] for r in game),
    min(r["gpu_c"] for r in game), max(r["gpu_c"] for r in game),
    min(r["cpu_c"] for r in game), max(r["cpu_c"] for r in game),
    min(r["board_c"] for r in game), max(r["board_c"] for r in game),
    min(r["batt_c"] for r in game), max(r["batt_c"] for r in game)))
