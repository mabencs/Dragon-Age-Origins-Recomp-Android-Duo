#!/usr/bin/env bash
# scene_capture.sh <run> <presses file> <last tick> [first tick] - a scene on the Thor, captured
# every 5 s, for comparing with the reference renderer (PLAN 3.3b, tools/compare_frames.py).
#
#   bash tools/scene_capture.sh 354 presses.txt 28 6
#
# Wakes the Thor (with the screen off the app is paused and the game held, presses and
# all), makes <presses file> the game's files/inject-pad (docs/TESTING.md: one press per
# line, `<buttons hex> <delay s> [<hold s>]`), launches the game as the app icon does,
# and once the capture of <last tick> exists (5 s ticks) keeps logcat in
# build/run<run>.log and the guest output captures from <first tick> (default 1) in
# build/run<run>/g<tick>.png, then stops the app. GLYPHS=xbox, nintendo or playstation
# sets the button prompts for the run; the owner's own choice (files/glyphs) is put back
# afterwards, and without GLYPHS it is left as it is.
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
RUN=${1:?run number}; PRESSES=${2:?presses file}; LAST=${3:?last tick}; FIRST=${4:-1}
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
P=dev.daorecompandroid.duo
mkdir -p "build/run$RUN"
"$ADB" shell am force-stop $P
"$ADB" shell input keyevent KEYCODE_WAKEUP
"$ADB" shell wm dismiss-keyguard >/dev/null 2>&1
"$ADB" push "$(cygpath -w "$PRESSES")" /data/local/tmp/inject-pad >/dev/null
"$ADB" shell "chmod 644 /data/local/tmp/inject-pad"
"$ADB" shell "run-as $P sh -c 'rm -f files/guest-output-*.png; echo 1 > files/capture-every; cp /data/local/tmp/inject-pad files/inject-pad'"
if [ -n "${GLYPHS:-}" ]; then
    "$ADB" shell "run-as $P sh -c 'cp files/glyphs files/glyphs.scene-bak 2>/dev/null; echo $GLYPHS > files/glyphs'"
fi
"$ADB" logcat -c
"$ADB" shell "monkey -p $P -c android.intent.category.LAUNCHER 1" >/dev/null 2>&1
echo "$(date +%T) launched"
until "$ADB" shell "run-as $P ls files/guest-output-$LAST.png" >/dev/null 2>&1 \
        || ! "$ADB" shell pidof $P >/dev/null; do
    sleep 3
done
"$ADB" logcat -d > "build/run$RUN.log"
for t in $(seq "$FIRST" "$LAST"); do
    "$ADB" exec-out run-as $P cat files/guest-output-$t.png > "build/run$RUN/g$t.png" 2>/dev/null
    [ "$(stat -c %s "build/run$RUN/g$t.png" 2>/dev/null || echo 0)" -gt 1000 ] || rm -f "build/run$RUN/g$t.png"
done
echo "$(date +%T) pid '$("$ADB" shell pidof $P)'; faults $(grep -c 'Fatal signal' "build/run$RUN.log"); captures: $(ls "build/run$RUN" | tr '\n' ' ')"
"$ADB" shell am force-stop $P
"$ADB" shell "run-as $P sh -c 'rm -f files/inject-pad files/capture-every files/guest-output-*.png'"
if [ -n "${GLYPHS:-}" ]; then
    "$ADB" shell "run-as $P sh -c 'if [ -f files/glyphs.scene-bak ]; then mv files/glyphs.scene-bak files/glyphs; else rm -f files/glyphs; fi'"
fi
"$ADB" shell rm -f /data/local/tmp/inject-pad
