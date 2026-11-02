#!/usr/bin/env bash
# boot_capture.sh - boot the title once on the Thor and keep its log.
#
#   bash tools/boot_capture.sh <run-number> [settle-seconds] [--install]
#
# Clears logcat, starts the title, waits for the guest entry point (up to 90 s), lets
# it run for `settle` seconds (default 30), saves the whole logcat to build/runN.log,
# stops the app, and prints the parts that matter: swaps and input, spin chains, the
# stubs reached, the last guest-thread report. --install installs the debug APK first.
# Marker files in the app's files directory (docs/TESTING.md: inject-pad, probe.txt,
# sample-threads, trace-swap, skip_intro_logos, hack-*) shape the run.
#
# Git Bash on Windows: adb is a Windows binary, so MSYS must not rewrite its arguments.
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
RUN="${1:?run number}"
SETTLE="${2:-30}"
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
PKG=dev.daorecompandroid.duo
LOG="build/run$RUN.log"

if [ "${3:-}" = "--install" ]; then
    "$ADB" install -r -d "$(cygpath -w app/build/outputs/apk/debug/app-debug.apk)" 2>&1 | grep -v Performing
fi
"$ADB" shell input keyevent KEYCODE_WAKEUP
# A 16 MB buffer: the default one overflows in a two-minute run and the start of the
# log - the markers read, the entry point, the first imports - is lost.
"$ADB" logcat -G 16M
"$ADB" logcat -c
"$ADB" shell am force-stop "$PKG"
"$ADB" shell "am start -n $PKG/.GameActivity --ez boot true" >/dev/null
for _ in $(seq 1 90); do
    if "$ADB" logcat -d -s warden:V | grep -q 'calling the title entry point'; then break; fi
    sleep 1
done
sleep "$SETTLE"
"$ADB" logcat -d > "$LOG"
"$ADB" shell am force-stop "$PKG"

strip() { sed -E 's/^.{6}([0-9:.]+) .*warden *: [a-z]> [0-9A-F]+ /\1 /'; }
# Faults: a guest fault's report, or a native crash. Not every fatal-level line - guest
# call chains are logged at that level by diagnostics too (the mode-request probe's
# twelve in every boot, warden_game.cc).
echo "lines $(wc -l < "$LOG"), erf opens $(grep -c 'NtCreateFile(.*\.erf' "$LOG"), faults $(grep -cE 'fault on guest thread|Fatal signal' "$LOG")"
echo "--- gpu / input / net:"
grep -E 'gpu-interrupt|VdSwap\(|XamInput|inject-pad|WSAWaitForMultipleEvents\(' "$LOG" | strip | head -14
echo "--- spin chains:"
grep 'KeDelayExecutionThread(.*) from' "$LOG" | strip
echo "--- stubs:"
grep -o 'unimplemented import reached: [A-Za-z0-9_]*' "$LOG" | sed 's/.*: //' | sort -u | tr '\n' ' '; echo
echo "--- last report:"
n=$(grep -n 'guest threads, the last' "$LOG" | tail -1 | cut -d: -f1)
[ -n "$n" ] && sed -n "$n,$((n+24))p" "$LOG" | grep 'warden' | sed -E 's/^.*warden *: [a-z]> [0-9A-F]+ //' | cut -c1-200
