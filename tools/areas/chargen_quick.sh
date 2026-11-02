#!/usr/bin/env bash
# PLAN 10.11: a campaign that opens on character creation (The Golems of Amgarrak, Witch
# Hunt): the defaults accepted (the bottom button, 50 times), Quick Play (the top button,
# Xbox Y), the difficulty's OK, then the opening conversation ended - until exploring.
# files/keyboard-default answers the name prompt. bash tools/areas/chargen_quick.sh
S="$(dirname "${BASH_SOURCE[0]}")"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
export MSYS2_ARG_CONV_EXCL='*'
mode() { adb logcat -d -s warden:V | grep -a "game: mode is now" | tail -1 | grep -o "mode is now [0-9]*" | awk '{print $4}'; }
adb shell "run-as dev.daorecompandroid.duo touch files/keyboard-default"
for _ in $(seq 1 90); do [ "$(mode)" = "11" ] && break; sleep 2; done
for _ in $(seq 1 50); do bash "$S/live.sh" press 1000 >/dev/null; sleep 2.5; done
bash "$S/live.sh" press 8000 >/dev/null; sleep 4      # Quick Play
bash "$S/live.sh" press 1000 >/dev/null               # the difficulty's OK
for i in $(seq 1 120); do
  m=$(mode); [ "$m" = "0" ] && { echo "exploring after $i"; exit 0; }
  case "$m" in 4|5|10) bash "$S/live.sh" press 2000 >/dev/null;; esac
  sleep 2.5
done
echo "still mode $(mode)"
