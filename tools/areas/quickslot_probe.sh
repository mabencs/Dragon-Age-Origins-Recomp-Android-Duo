#!/usr/bin/env bash
# PLAN 8.8: which button presses which quick slot - one injected press, then the
# mirror's quick slots dumped (files/deck-dump) twice: right after the press and a
# second later. The game must be running, exploring or fighting.
#   bash tools/areas/quickslot_probe.sh <label> <mask> [hold] [rt]
# e.g. "x 4000", "rt-x 4000 0.4 255", "dright 0008"
S="$(dirname "${BASH_SOURCE[0]}")"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
export MSYS2_ARG_CONV_EXCL='*'
LABEL="$1"; MASK="$2"; HOLD="${3:-0.2}"; RT="${4:-0}"
dump() {
  adb shell "run-as dev.daorecompandroid.duo sh -c 'rm -f files/deck-snapshot.txt; touch files/deck-dump'"
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 0.3
    adb shell "run-as dev.daorecompandroid.duo cat files/deck-snapshot.txt 2>/dev/null" | tr -d '\r' \
      | grep -E "SelectedChar.Name|QuickSlots\.[0-5]	|QuickSlots\.[0-5]\.(ItemIcon|PendingActivation|CooldownPercentage|Status)|CurrentGameMode" \
      | grep -v "	$" | sort > "build/qs-$LABEL-$1.txt"
    [ -s "build/qs-$LABEL-$1.txt" ] && return
  done
}
adb shell "run-as dev.daorecompandroid.duo sh -c 'echo \"$MASK 0.2 $HOLD 0 $RT\" > files/inject-pad'"
sleep 0.5
dump now
sleep 1
dump later
adb exec-out screencap -d 4630946441858561667 -p > "build/qs-$LABEL.png"
echo "== $LABEL ($MASK, rt $RT)"; paste "build/qs-$LABEL-now.txt" "build/qs-$LABEL-later.txt" | head -40
