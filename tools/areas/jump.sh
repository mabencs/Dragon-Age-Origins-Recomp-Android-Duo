#!/usr/bin/env bash
# PLAN 10.11: a debug-menu jump, each step checked: waits until no conversation is open,
# calls the module's debug script, waits for its menu (mode 4), presses the given
# sequence (A, D for down, U for up; 1.2 s apart), then waits for the loading mode (9)
# and for explore (0). Prints "jumped <rim>" or "failed <where>" (with a screenshot).
#   bash jump.sh <script> <presses>      e.g. bash jump.sh zz_cir_debug "A D D A"
export MSYS_NO_PATHCONV=1
S="$(dirname "${BASH_SOURCE[0]}")"
SCRIPT="$1"; PRESSES="$2"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
mode() { adb logcat -d -s warden:V | grep -a "game: mode is now" | tail -1 | grep -o "mode is now [0-9]*" | awk '{print $4}'; }
wait_mode() { for _ in $(seq 1 "$2"); do [ "$(mode)" = "$1" ] && return 0; sleep 1; done; return 1; }
# The player dead (mode 2, the death screen): never press on - its A is "Load Last Save",
# which could load the owner's own game; the tour reboots instead.
[ "$(mode)" = "2" ] && { echo "failed dead (the death screen)"; exit 1; }
# A conversation left open (an arrival talk): B ends its lines, A picks a reply.
for i in $(seq 1 120); do
  [ "$(mode)" = "0" ] && break
  [ "$(mode)" = "2" ] && { echo "failed dead (the death screen)"; exit 1; }
  if [ $((i % 2)) = 1 ]; then bash "$S/live.sh" press 2000 >/dev/null; else bash "$S/live.sh" press 1000 >/dev/null; fi
  sleep 1.2
done
wait_mode 0 20 || { echo "failed not-exploring (mode $(mode))"; adb exec-out screencap -p > "build/areas/fail-${SCRIPT%% *}.png"; exit 1; }
adb logcat -c
bash "$S/live.sh" call "$SCRIPT" >/dev/null
# A "-" for the presses: the script jumps by itself (a console script given its value,
# "!zz_drk market"), no menu to wait for.
if [ "$PRESSES" = "-" ]; then
  PRESSES=""
else
  wait_mode 4 12 || { echo "failed no-menu for $SCRIPT (mode $(mode))"; adb exec-out screencap -p > "build/areas/fail-${SCRIPT%% *}.png"; exit 1; }
fi
sleep 1.5
for p in $PRESSES; do
  # A "." is a 2 s pause (a sub-list that opens slowly: the Deep Roads' "Which?").
  case "$p" in .) sleep 2; continue;; esac
  case "$p" in A) m=1000;; D) m=0002;; U) m=0001;; B) m=2000;; *) m=$p;; esac
  bash "$S/live.sh" press "$m" >/dev/null
  sleep 1.2
done
# A loading mode seen at any time since the call counts (the log was cleared before it):
# a load that finishes between two presses leaves the last mode line at 0 or 4 already
# (Eamon's estate in session 38's night).
seen=0
for _ in $(seq 1 20); do
  n=$(adb logcat -d -s warden:V | grep -a "game: mode is now" | grep -ac "mode is now 9")
  [ "$n" -ge 1 ] && { seen=1; break; }
  sleep 1
done
[ "$seen" = 1 ] || { echo "failed no-loading after $PRESSES (mode $(mode))"; adb exec-out screencap -p > "build/areas/fail-${SCRIPT%% *}.png"; exit 1; }
loaded=0
for _ in $(seq 1 120); do
  # Explore, a conversation - or combat (1): some DLC arrivals are fights the test
  # character loses, and the probe measures the fight rather than nothing.
  m=$(mode); if [ "$m" = "0" ] || [ "$m" = "4" ] || [ "$m" = "1" ]; then loaded=1; break; fi; sleep 1
done
[ "$loaded" = "1" ] || { echo "failed still-loading (mode $(mode))"; exit 1; }
rim=$(adb logcat -d -s warden:V | grep -a "NtReadFile" | grep -ao "al_[a-z0-9_]*\.rim" | awk '!seen[$0]++' | tail -1)
echo "jumped $rim"
