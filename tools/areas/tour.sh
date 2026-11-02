#!/usr/bin/env bash
# PLAN 10.11: the areas' tour - each line of the list "label|script|presses" is a
# debug-menu jump (jump.sh) and a probe (area_probe.sh); stops at the first failed jump,
# leaving the game as it is for a look. Progress in build/areas/tour.log.
#   bash tour.sh <list file>
S="$(dirname "${BASH_SOURCE[0]}")"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
mkdir -p build/areas
# The route a reboot puts on: probe, or TOUR_ROUTE (probe-dlc for the downloadable
# content's areas, the campaigns' probes for theirs).
reboot_probe() {
  export MSYS_NO_PATHCONV=1
  bash tools/route.sh "${TOUR_ROUTE:-probe}" >/dev/null 2>&1
  adb shell "run-as dev.daorecompandroid.duo rm -f files/route-call"
  adb logcat -c; adb shell am force-stop dev.daorecompandroid.duo
  adb shell "am start -n dev.daorecompandroid.duo/.GameActivity --ez boot true" >/dev/null
  for _ in $(seq 1 150); do
    n=$(adb logcat -d -s warden:V | grep -c "game: mode is now 0"); [ "$n" -ge 1 ] && break; sleep 2
  done
  sleep 5
}
while IFS='|' read -r -u 3 label script presses; do
  case "$label" in ""|\#*) continue ;; esac
  echo "== $label ($script: $presses) at $(date +%T)" | tee -a build/areas/tour.log
  out=$(bash "$S/jump.sh" "$script" "$presses" 2>&1)
  echo "$out" | tee -a build/areas/tour.log
  case "$out" in
    jumped*) ;;
    *)
      # The player dead, a menu, a movie - or a menu left open: boot the probe route
      # again (about 90 s) and try the area once more; skipped if it fails again.
      echo "rebooting for $label" | tee -a build/areas/tour.log
      reboot_probe
      out=$(bash "$S/jump.sh" "$script" "$presses" 2>&1)
      echo "$out" | tee -a build/areas/tour.log
      case "$out" in jumped*) ;; *) echo "skipped $label" | tee -a build/areas/tour.log; echo "$label | SKIPPED: $out" >> build/areas/results.txt; continue ;; esac
      ;;
  esac
  bash "$S/area_probe.sh" "$label" 2>&1 | tail -1 | tee -a build/areas/tour.log
done 3< "$1"
echo "tour done at $(date +%T)" | tee -a build/areas/tour.log
