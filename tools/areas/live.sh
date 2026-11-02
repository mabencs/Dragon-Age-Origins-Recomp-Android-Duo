#!/usr/bin/env bash
# PLAN 10.11: driving a running game from the PC.
#   bash live.sh call <script>            files/route-call: the script's main, within a second
#   bash live.sh press <mask> [hold] [lx ly rx ry]   one inject-pad line, now (sticks -32768..32767)
#   bash live.sh walk <seconds> [ly] [rx]  the left stick forward (ly, default 20000) for the
#                                          seconds, with the right stick turning (rx, default 0)
#   bash live.sh shot <name>               build/<name>.png
#   bash live.sh mode                       the last "game: mode is now" line and the swaps
#   bash live.sh fps                        the swaps over the last status lines (5 s apart)
export MSYS_NO_PATHCONV=1
PKG=dev.daorecompandroid.duo
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
case "$1" in
  call)
    adb shell "run-as $PKG sh -c 'echo $2 > files/route-call'" ;;
  press)
    adb shell "run-as $PKG sh -c 'echo \"$2 0.2 ${3:-0.2} 0 0 ${4:-0} ${5:-0} ${6:-0} ${7:-0}\" > files/inject-pad'" ;;
  walk)
    adb shell "run-as $PKG sh -c 'echo \"0000 0.2 $2 0 0 0 ${3:-20000} ${4:-0} 0\" > files/inject-pad'" ;;
  shot)
    adb exec-out screencap -p > "build/$2.png"; echo "build/$2.png" ;;
  mode)
    adb logcat -d -s warden:V | grep -a "game: mode is now\|route: route-call\|route: registry slot\|not in the registry" | tail -4 | sed 's/.*warden  : i> [0-9A-F]* //; s/.*warden  : w> [0-9A-F]* //' ;;
  fps)
    adb logcat -d -s warden:V | grep -a "GPU swaps" | tail -3 | grep -o "GPU swaps [0-9]*" | awk '{print $3}' | tr '\n' ' '; echo ;;
  *)
    echo "call|press|walk|shot|mode|fps"; exit 1 ;;
esac
