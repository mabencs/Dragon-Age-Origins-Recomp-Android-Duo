#!/usr/bin/env bash
# PLAN 10.11, session 40: the second batch of menu reads, after menus_session40.sh's
# sheets were read (the sub-lists those showed).
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
# Haven: "Jump me between locations" (item 2), its pages.
read_menu zz_mtn_debug menu-mtn-2 "D A" 3 3
# The Deep Roads' "Where in?" of the Dead Trenches and the Anvil, with pauses (".", 2 s)
# where a sub-list opens: the first batch's presses were lost there.
read_menu zz_orz_debug menu-orz-trenches2 "D D D A . D D D D A . D D A ." 3 1
read_menu zz_orz_debug menu-orz-anvil2 "D D D A . D D D D A . D D D A ." 3 1
echo "menus done at $(date +%T)"
