#!/usr/bin/env bash
# PLAN 10.11, session 40: the add-in DLC's debug menus (their .ncs scripts dispatched with
# files/route-call's !name), photographed with menu_read.sh. The game must be on the
# probe-dlc route, exploring.
#   bash tools/areas/menus_dlc.sh [<script> <label> "<presses>" [downs pages]]...
S="$(dirname "${BASH_SOURCE[0]}")"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
mode() { adb logcat -d -s warden:V | grep -a "game: mode is now" | tail -1 | grep -o "mode is now [0-9]*" | awk '{print $4}'; }
close_menu() {
  for _ in 1 2 3 4; do
    [ "$(mode)" = "0" ] && return 0
    bash "$S/live.sh" press 0001 >/dev/null; sleep 1.2
    bash "$S/live.sh" press 1000 >/dev/null; sleep 2
  done
  echo "(the menu did not close: mode $(mode))"
}
read_menu() { echo "== $2 ($1: $3) at $(date +%T)"; bash "$S/menu_read.sh" "$1" "$2" "$3" "${4:-3}" "${5:-1}" 2>&1 | tail -2; close_menu; }
if [ $# -ge 3 ]; then
  while [ $# -ge 3 ]; do read_menu "$1" "$2" "$3" "${4:-3}" "${5:-1}"; shift 5 2>/dev/null || shift $#; done
else
  read_menu '!zz_gwb_debug' menu-gwb "" 3 1
  read_menu '!zz_shl_debug' menu-shl "" 3 1
  read_menu '!zz_kcc_debug' menu-kcc "" 3 1
fi
echo "menus done at $(date +%T)"
