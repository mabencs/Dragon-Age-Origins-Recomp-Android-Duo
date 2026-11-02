#!/usr/bin/env bash
# PLAN 10.11, session 40: the debug menus the first pass could not read - each
# photographed with menu_read.sh (p0 the script's main menu, p1 after the presses to the
# list, then pages). The game must be on the probe route, exploring.
#   bash tools/areas/menus_session40.sh
S="$(dirname "${BASH_SOURCE[0]}")"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
mode() { adb logcat -d -s warden:V | grep -a "game: mode is now" | tail -1 | grep -o "mode is now [0-9]*" | awk '{print $4}'; }
# A menu left open is closed by its last item (Back, then Exit): up from the first item
# wraps to the last, so U then A, up to four times - not B and A, which would pick
# the first item of whatever page is open.
close_menu() {
  for _ in 1 2 3 4; do
    [ "$(mode)" = "0" ] && return 0
    bash "$S/live.sh" press 0001 >/dev/null; sleep 1.2
    bash "$S/live.sh" press 1000 >/dev/null; sleep 2
  done
  echo "(the menu did not close: mode $(mode))"
}
read_menu() { echo "== $2 ($1: $3) at $(date +%T)"; bash "$S/menu_read.sh" "$1" "$2" "$3" "${4:-3}" "${5:-1}" 2>&1 | tail -2; close_menu; }
# The prelude's "Jump to an area" list: its pages (not three an item - 5 downs a page).
read_menu zz_pre_debug menu-pre "A" 5 2
# Haven, the origins: the main menu, then the first item's sub-list.
read_menu zz_mtn_debug menu-mtn "" 3 1
read_menu zz_mtn_debug menu-mtn-1 "A" 3 1
read_menu zz_bdc_debug menu-bdc "" 3 1
read_menu zz_bhn_debug menu-bhn "" 3 1
read_menu zz_bhn_debug menu-bhn-1 "A" 3 1
read_menu zz_bec_debug menu-bec "" 3 1
read_menu zz_bec_debug menu-bec-1 "A" 3 1
read_menu zz_bed_debug menu-bed "" 3 1
read_menu zz_bed_debug menu-bed-1 "A" 3 1
# The Deep Roads' "Where in?" of Ortan Thaig, the Dead Trenches, the Anvil.
read_menu zz_orz_debug menu-orz-ortan "D D D A D D D D A D A" 3 1
read_menu zz_orz_debug menu-orz-trenches "D D D A D D D D A D D A" 3 1
read_menu zz_orz_debug menu-orz-anvil "D D D A D D D D A D D D A" 3 1
# Redcliffe: the plot jump's third item, then the climax and the epilogue scripts.
read_menu zz_arl_debug menu-arl "A" 3 1
read_menu zz_cli_debug menu-cli "" 3 1
read_menu zz_epi_debug menu-epi "" 3 1
echo "menus done at $(date +%T)"
