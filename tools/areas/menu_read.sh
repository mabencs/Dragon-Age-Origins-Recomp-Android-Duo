#!/usr/bin/env bash
# PLAN 10.11: a module's "Jump to area" list photographed page by page, each state checked.
#   bash menu_read.sh <script> <label> "<presses to the list>" <downs per page> <pages>
# Waits for the menu (mode 4) after the call, presses the way to the list, screenshots
# page 1, then turns pages with <downs> downs and A (the "more" item), stopping when the
# menu is gone (a jump or an exit). build/<label>-pN.png and build/<label>-sheet.png.
export MSYS_NO_PATHCONV=1
S="$(dirname "${BASH_SOURCE[0]}")"
SCRIPT="$1"; LABEL="$2"; TOLIST="$3"; DOWNS="${4:-3}"; PAGES="${5:-5}"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
mode() { adb logcat -d -s warden:V | grep -a "game: mode is now" | tail -1 | grep -o "mode is now [0-9]*" | awk '{print $4}'; }
wait_mode() { for _ in $(seq 1 "$2"); do [ "$(mode)" = "$1" ] && return 0; sleep 1; done; return 1; }
press() { local m; case "$1" in .) sleep 2; return;; A) m=1000;; D) m=0002;; U) m=0001;; B) m=2000;; *) m=$1;; esac; bash "$S/live.sh" press "$m" >/dev/null; sleep "${2:-1.2}"; }
for i in $(seq 1 120); do
  [ "$(mode)" = "0" ] && break
  if [ $((i % 2)) = 1 ]; then press B 1.2; else press A 1.2; fi
done
wait_mode 0 20 || { echo "not exploring (mode $(mode))"; exit 1; }
adb logcat -c
bash "$S/live.sh" call "$SCRIPT" >/dev/null
wait_mode 4 15 || { echo "no menu for $SCRIPT"; exit 1; }
sleep 2
bash "$S/live.sh" shot "$LABEL-p0" >/dev/null
for p in $TOLIST; do press "$p" 1.3; done
sleep 1.5
bash "$S/live.sh" shot "$LABEL-p1" >/dev/null
for page in $(seq 2 "$PAGES"); do
  for _ in $(seq 1 "$DOWNS"); do press D 0.8; done
  press A 2.5
  [ "$(mode)" = "4" ] || { echo "menu gone before page $page (mode $(mode))"; break; }
  bash "$S/live.sh" shot "$LABEL-p$page" >/dev/null
done
python - "$LABEL" "$PAGES" <<'EOF' 2>/dev/null
import os, sys
from PIL import Image, ImageDraw
label, pages = sys.argv[1], int(sys.argv[2])
crops = []
for p in range(0, pages + 1):
    f = f"build/{label}-p{p}.png"
    if not os.path.exists(f): continue
    c = Image.open(f).convert("RGB").crop((140, 740, 1100, 1000)); d = ImageDraw.Draw(c); d.rectangle((0, 0, 110, 18), fill="black"); d.text((3, 3), f"{label} p{p}", fill="white"); crops.append(c)
sheet = Image.new("RGB", (960, 260 * len(crops)), "black")
for i, c in enumerate(crops): sheet.paste(c, (0, 260 * i))
sheet.save(f"build/{label}-sheet.png"); print("sheet", len(crops), "pages")
EOF
