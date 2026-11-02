#!/usr/bin/env bash
# suspend_check.sh - put the game away and bring it back, the ways a handheld does it,
# and say whether every return worked (BUG-056, BUG-057).
#
#   bash tools/suspend_check.sh <run-number>
#
# Run it alongside a capture that is already booting, as a short command of its own:
#   bash tools/boot_capture.sh <run> 150 --install      (in the background)
#   bash tools/suspend_check.sh <run>
# At the title screen it goes home and back, presses START (the screen must change),
# turns the screen off with the power button and wakes it, then goes home and back three
# times in quick succession. While away it samples the process's threads' CPU (top). It
# keeps a screenshot after each return (build/run<run>-*.png) and the log
# (build/run<run>-suspend.log), and prints what decides it: the suspensions and
# resumptions, the paint requests (all posted, some through the pipe from another
# thread), the window's input focus after each return, and any ANR.
#
# A return works when the window takes input focus again (the events buffer's
# input_focus lines), no ANR is logged, and every paint request is posted. Run 319 was
# the first to pass: five returns, focus within 0.5 s each, nine threads held each time,
# the process at about 10% of one core while away (13% at play, run 320; less since the
# reports and the ring poller rest while suspended).
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
RUN="${1:?run number}"
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
PKG=dev.daorecompandroid.duo

wait_for() {  # <pattern> <seconds>
  for _ in $(seq 1 "$2"); do
    if "$ADB" logcat -d -s warden:V | grep -q "$1"; then return 0; fi
    sleep 1
  done
  return 1
}
shot() { "$ADB" exec-out screencap -p > "build/run$RUN-$1.png"; echo "$(date +%T) screenshot build/run$RUN-$1.png"; }
press() { "$ADB" shell "run-as $PKG sh -c 'echo $1 1 0.15 > files/inject-pad'"; }
away_cpu() {  # the process's threads over 5 s while away: the total and the busiest three
  local pid
  pid=$("$ADB" shell pidof $PKG)
  "$ADB" shell "top -H -b -n 2 -d 5 -p $pid -o TID,%CPU,S,CMD" > "build/run$RUN-top-$1.txt"
  awk '/Threads:/ {n++} n==2 && $1 ~ /^[0-9]+$/ {t += $2; print} END {print "total " t "% of one core"}' \
    "build/run$RUN-top-$1.txt" | sort -k2 -nr | head -4 | sed "s/^/  away ($1): /"
}
back() { "$ADB" shell am start -n $PKG/.GameActivity >/dev/null 2>&1; }

wait_for 'game: mode is now 8' 170 || { echo "no title screen within 170 s"; exit 1; }
sleep 6
shot title

echo "== home and back"
since=$(date +%T)  # the focus counted from here: the five returns, not the launch
"$ADB" shell input keyevent KEYCODE_HOME
sleep 4
away_cpu home
back
sleep 4
shot home-back
press 0010
sleep 4
shot home-start

echo "== the power button"
"$ADB" shell input keyevent KEYCODE_POWER
sleep 4
away_cpu power
"$ADB" shell input keyevent KEYCODE_WAKEUP
sleep 4
shot power-back

echo "== three quick returns"
for _ in 1 2 3; do
  "$ADB" shell input keyevent KEYCODE_HOME
  sleep 0.5
  back
  sleep 1.5
done
sleep 3
shot quick

echo "== verdict"
"$ADB" logcat -d > "build/run$RUN-suspend.log"
suspended=$(grep -c 'guest suspended (#' "build/run$RUN-suspend.log")
resumed=$(grep -c 'guest resumed (#' "build/run$RUN-suspend.log")
grep -E 'guest (suspended|resumed) \(#' "build/run$RUN-suspend.log" | sed -E 's/^.* [a-z]> [0-9A-F]+ /  /' | cut -c1-150
paints=$(grep -o 'paints: [0-9]* requested ([0-9]* from another thread, [0-9]* dropped), [0-9]* posted, [0-9]* painted' "build/run$RUN-suspend.log" | tail -1)
echo "  $paints"
# The events buffer survives logcat -c, so only this check's own returns are counted: five,
# each within about half a second of the request (runs 319-322).
focus=$("$ADB" logcat -d -b events -v time | grep 'Focus entering' | grep "dev.daorecompandroid.duo" \
  | awk -v since="$since" '$2 >= since' | wc -l)
anr=$("$ADB" logcat -d -b all | grep -c "ANR in $PKG")
echo "  the window took input focus $focus times since $since (5 returns); ANR lines $anr"
requested=$(echo "$paints" | sed -E 's/paints: ([0-9]+) requested.*/\1/')
posted=$(echo "$paints" | sed -E 's/.*, ([0-9]+) posted.*/\1/')
if [ "$suspended" -ge 5 ] && [ "$resumed" -ge 5 ] && [ "$focus" -ge 5 ] && [ "$anr" -eq 0 ] && [ -n "$requested" ] && [ "$requested" = "$posted" ] && [ -n "$("$ADB" shell pidof $PKG)" ]; then
  echo "PASS: $suspended suspensions, $resumed resumptions, focus taken back $focus times, every paint posted, no ANR, the app still running"
else
  echo "FAIL: $suspended suspensions, $resumed resumptions, focus taken back $focus times, paints '$paints', ANR lines $anr, pid '$("$ADB" shell pidof $PKG)'"
fi
