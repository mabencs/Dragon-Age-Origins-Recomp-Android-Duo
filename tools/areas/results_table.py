# PLAN 10.11: docs/AREAS.md's results table from build/areas/results.txt (the last line
# per label wins).
import re
rows = {}
for line in open("C:/Repos/DragonAgeOriginsAndroid/build/areas/results.txt", encoding="utf-8"):
    parts = [p.strip() for p in line.strip().split("|")]
    if len(parts) < 5:
        continue
    label, rim = parts[0], parts[1]
    rates = {m.group(1): m.group(2) for m in re.finditer(r"(standing|walk2|walk) ([\d.?]+) fps", line)}
    busy = re.search(r"busy ([\d,]+)", line); talk = re.search(r"talk (\d+)", line); errors = re.search(r"errors (\d+)", line)
    rows[label] = (rim.removeprefix("al_").removesuffix(".rim"), rates.get("standing", "?"), rates.get("walk", "?"), rates.get("walk2", "?"),
                   busy.group(1) if busy else "?", talk.group(1) if talk else "0", errors.group(1) if errors else "?")
for label, (rim, s, w, w2, busy, talk, errors) in rows.items():
    note = []
    if talk != "0": note.append(f"arrival talk ({talk} presses)")
    if errors not in ("0", "?"): note.append(f"{errors} errors logged")
    print(f"| {label} | `{rim}` | {s} | {w} | {w2} | {busy} | {'; '.join(note)} |")
