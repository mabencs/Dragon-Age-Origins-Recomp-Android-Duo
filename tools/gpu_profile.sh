#!/usr/bin/env bash
# gpu_profile.sh - PLAN 10.9's profile: a frame at the King's Camp traced (files/trace-swap,
# about half a minute after arrival on the ostagar route) with its GPU time per render pass
# (files/gpu-timing.txt, fresh) -> build/run<N>.log and build/run<N>-gpu-timing.txt.
#
#   bash tools/gpu_profile.sh <run> [swap]     # swap 4000 by default
#
# Switches to set first (each removed again at the end), docs/TESTING.md section 4:
#   files/hack-split-pass "2:850:2100"   the traced frame's draws 850-2100 two to a pass
#   files/hack-pipeline-stats            the driver's statistics per pipeline, per stage
#   files/hack-no-mul-zero ieee          IEEE multiplication (inexact; for measuring)
#   files/dump-shaders                   every translated shader in files/shaders
export MSYS2_ARG_CONV_EXCL='*'
RUN="$1"
SWAP="${2:-4000}"
PKG=dev.daorecompandroid.duo
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
bash tools/route.sh ostagar >/dev/null 2>&1
adb shell "run-as $PKG sh -c 'echo $SWAP > files/trace-swap; rm -f files/gpu-timing.txt'"
adb shell input keyevent KEYCODE_WAKEUP
adb logcat -G 16M
adb logcat -c
adb shell am force-stop $PKG
adb shell "am start -n $PKG/.GameActivity --ez boot true" >/dev/null
for _ in $(seq 1 400); do
  n=$(adb logcat -d -s warden:V | grep -c 'game: mode is now 0')
  [ "$n" -ge 2 ] && break
  sleep 1
done
echo "at the camp: $(date +%T)"
for _ in $(seq 1 120); do
  adb shell "run-as $PKG test -s files/gpu-timing.txt" && break
  sleep 1
done
sleep 20
adb logcat -d > "build/run$RUN.log"
adb exec-out "run-as $PKG cat files/gpu-timing.txt" > "build/run$RUN-gpu-timing.txt"
adb shell am force-stop $PKG
adb shell "run-as $PKG rm -f files/trace-swap files/hack-split-pass files/hack-pipeline-stats files/hack-no-mul-zero files/dump-shaders"
bash tools/route.sh off >/dev/null 2>&1
grep -c "FT gpu frame" "build/run$RUN-gpu-timing.txt"
grep "FT gpu frame" "build/run$RUN-gpu-timing.txt"
grep -a "GPU swaps" "build/run$RUN.log" | tail -3 | grep -o "GPU swaps [0-9]*"
