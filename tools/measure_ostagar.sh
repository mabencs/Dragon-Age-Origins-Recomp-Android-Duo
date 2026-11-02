#!/usr/bin/env bash
# measure_ostagar.sh - the frame-rate benchmark at Ostagar's King's Camp.
#
#   bash tools/measure_ostagar.sh <run-number> [--simpleperf]
#
# Needs the ostagar route on the device (bash tools/route.sh ostagar) and the APK
# installed. Boots the title, waits for explore mode at Ostagar (the second time the
# game enters mode 0 - the first is the test area the route passes through), lets it
# settle 15 s, then samples the Adreno's busy percentage and clock, two CPU cores'
# frequencies and the GPU temperature once a second for 20 s. --simpleperf instead
# records 15 s with simpleperf (call graphs and off-CPU time, the NDK's arm64 binary)
# and writes per-thread reports to build/perf<run>/. The log goes to build/run<run>.log,
# and the swaps per second over the last five `GPU:` status lines are printed.
#
# Reference numbers (2026-10-25): the debug APK with the runtime at -O0, 8 fps; with
# the runtime and the app code at -O2, 13.5 fps with the console's two 2x MSAA tiles;
# one tile without MSAA (the default now), 20 fps. The GPU is at 99% at 680 MHz in
# all three.
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
PKG=dev.daorecompandroid.duo
RUN="${1:?run number}"
MODE="${2:-}"
SP="$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358/simpleperf/bin/android/arm64/simpleperf"

if [ "$MODE" = "--simpleperf" ]; then
  "$ADB" push "$(cygpath -w "$SP")" /data/local/tmp/simpleperf >/dev/null 2>&1
  "$ADB" shell chmod 755 /data/local/tmp/simpleperf
fi
"$ADB" shell input keyevent KEYCODE_WAKEUP
"$ADB" logcat -G 16M
"$ADB" logcat -c
"$ADB" shell am force-stop "$PKG"
"$ADB" shell "am start -n $PKG/.GameActivity --ez boot true" >/dev/null
for _ in $(seq 1 400); do
  n=$("$ADB" logcat -d -s warden:V | grep -c 'game: mode is now 0')
  [ "$n" -ge 2 ] && break
  sleep 1
done
echo "explore at Ostagar: $(date +%T)"
sleep 15
if [ "$MODE" = "--simpleperf" ]; then
  OUT="build/perf$RUN"
  mkdir -p "$OUT"
  "$ADB" shell "/data/local/tmp/simpleperf record --app $PKG -g --duration 15 -e cpu-clock -f 1000 --trace-offcpu -o /data/local/tmp/perf.data" > "$OUT/record.txt" 2>&1
  for sort in comm comm,symbol; do
    "$ADB" shell "/data/local/tmp/simpleperf report -i /data/local/tmp/perf.data --sort $sort --percent-limit 0.3" > "$OUT/by-${sort/,/-}.txt" 2>&1
  done
  "$ADB" shell "/data/local/tmp/simpleperf report -i /data/local/tmp/perf.data --children --sort comm,tid,symbol --percent-limit 1" > "$OUT/children.txt" 2>&1
  echo "simpleperf reports in $OUT"
else
  for _ in $(seq 1 20); do
    "$ADB" shell 'printf "%s gpu busy %s clock %s cpu7 %s cpu4 %s temp %s\n" "$(date +%T)" "$(cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage)" "$(cat /sys/class/kgsl/kgsl-3d0/gpuclk)" "$(cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq)" "$(cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq)" "$(cat /sys/class/kgsl/kgsl-3d0/temp 2>/dev/null)"'
    sleep 1
  done
fi
"$ADB" logcat -d > "build/run$RUN.log"
"$ADB" shell am force-stop "$PKG"
grep 'GPU: write pointer' "build/run$RUN.log" | python -c "
import sys, re
points = []
for line in sys.stdin:
    swaps = re.search(r'GPU swaps (\d+)', line)
    draws = re.search(r'\[(\d+) submitted', line)
    if not swaps:
        continue
    h, m, s = line[6:18].split(':')
    points.append((int(h) * 3600 + int(m) * 60 + float(s), int(swaps.group(1)),
                   draws.group(1) if draws else '?'))
last = points[-5:]
if len(last) >= 2:
    fps = (last[-1][1] - last[0][1]) / (last[-1][0] - last[0][0])
    print('Ostagar: %.1f swaps/s over the last %d status lines, %s draws a frame'
          % (fps, len(last), last[-1][2]))
"
