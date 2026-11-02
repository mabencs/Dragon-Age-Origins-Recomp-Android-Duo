#!/usr/bin/env bash
# route.sh - put a scripted route on the Thor, or take it off.
#
#   bash tools/route.sh <name>   copy tools/routes/<name>/* into the app's files directory
#   bash tools/route.sh off      remove inject-pad and route there (hands back on the pad)
#   bash tools/route.sh show     print what is there now
#
# A route is plain text, no game material: `inject-pad` (timed controller presses,
# docs/TESTING.md) and optionally `route` (what warden_game.cc does on its own: skip the
# module's start script, call a debug script by name). Routes so far:
#   ostagar  New Game -> the default start area with a default player -> the prelude
#            debug menu -> "Jump to an area" -> the King's Camp -> "I'm all done":
#            explore mode at Ostagar about two minutes after the first input poll.
#   wilds    the same, but "Start in the Korcari Wilds": explore mode there about 108 s
#            after the first poll (run 218). The menu does not come back after this jump.
#            Then the left stick walks up the path into the first fight, wolves, and A
#            attacks: combat from about 120 s to about 172 s (runs 219-220).
#   morrigan the same menu's (MORE) -> "Start at the very end of the Prelude (with
#            Morrigan)": the Wilds' start with Morrigan in the party, explore about 119 s
#            after the first poll; then the same walk into the wolves, from about 130 s,
#            with her spells (run 223).
#   chargen  New Game -> the intro movie skipped with X -> character creation.
#   probe    New Game with the module's start script skipped, nothing else: the default
#            start area with a default player about 45 s after the first poll, for calls
#            through files/route-call (PLAN 10.11: the areas' tour, tools/areas/).
#   probe-dlc the same with the downloadable content kept (.keep-dlc: no hide-dlc), so
#            New Game shows the campaign chooser and the 72 s A picks Dragon Age:
#            Origins with its add-ins (Warden's Keep, The Stone Prisoner, Return to
#            Ostagar) - for their areas through `!zz_gwb_debug` and the like.
#   load     START, then A on the main menu's first item: with a save on the device that
#            is Resume, which loads the newest save (explore mode about 60 s after the
#            first poll); with none, the A answers the storage prompt instead.
#            Refused unless the newest save is the test character's (.resumes).
# The presses are timed from the title's first input poll, so they assume this device's
# load times; if a step lands early or late, the guest-output captures show where.
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
PKG=dev.daorecompandroid.duo
NAME="${1:?route name, off or show}"

files() { "$ADB" shell "run-as $PKG sh -c '$1'"; }

case "$NAME" in
  off)
    files 'rm -f files/inject-pad files/route files/hide-dlc; ls files'
    ;;
  show)
    files 'for f in inject-pad route capture-every trace-mode; do [ -f files/$f ] && { echo "== $f"; cat files/$f; }; done; true'
    ;;
  *)
    dir="tools/routes/$NAME"
    [ -d "$dir" ] || { echo "no route $dir"; exit 1; }
    # A route that resumes (a .resumes file) loads the newest save, whoever's it is, and
    # a run that walks on from it may autosave over it (session 35: entering an area
    # autosaves). So the newest save must be the test character's - Jaden, the routes'
    # own new games, which the wilds and morrigan routes autosave as - never the owner's
    # own game (the owner's character's, 2026-10-27). OWNER_SAVE_OK=1 overrides, with the owner's word.
    if [ -f "$dir/.resumes" ] && [ -z "${OWNER_SAVE_OK:-}" ]; then
      newest=$(files 'ls -t files/content/00000001/*.name 2>/dev/null | head -1' | tr -d '\r')
      label=$(files "cat \"$newest\"" | tr -d '\000' | head -c 80 |
              LC_ALL=C sed 's/[^A-Za-z0-9_ -].*//')
      case "$label" in
        Jaden_*) echo "(the newest save is the test character's: $label)" ;;
        *) echo "refused: the newest save is not the test character's ($label);" \
                "resuming it would play on in the owner's own game"; exit 1 ;;
      esac
    fi
    files 'rm -f files/inject-pad files/route files/hide-dlc'
    for f in "$dir"/*; do
      base=$(basename "$f")
      # adb push needs a Windows path from Git Bash; copy through /sdcard into run-as.
      "$ADB" push "$(cygpath -w "$f")" "/sdcard/warden-$base" >/dev/null 2>&1
      files "cp /sdcard/warden-$base files/$base"
      "$ADB" shell rm -f "/sdcard/warden-$base"
    done
    # A route that starts a new game presses A at 56 s for the title's storage prompt,
    # which appears only while the device holds no save. With a save there, START goes
    # straight to the main menu, whose first item is then Resume - and that A would load
    # the newest save instead. Such a route carries a .new-game file (not copied: the
    # glob above skips dotfiles); with saves on the device its 56 s A becomes a d-pad
    # down, onto New Game, and the rest of the route is unchanged (runs 205-206).
    if [ -f "$dir/.new-game" ] && files 'ls files/content/00000001 2>/dev/null' | grep -q '^0x'; then
      files 'sed -i "s/^1000 56 /0002 56 /" files/inject-pad'
      echo "(saves on the device: the 56 s A is a d-pad down, onto New Game)"
    fi
    # With downloadable content installed (Awakening, PLAN 10.10), New Game first asks for
    # the campaign, and every press after it lands a step early (run 494). A route's new
    # game is played without it: files/hide-dlc makes the game see none (warden_xam.cc);
    # "off" and the next route take it away.
    # A route made for the downloadable content (a .keep-dlc file: the probe-dlc route
    # and the campaigns' probes, PLAN 10.11) keeps it, and its presses allow for the
    # chooser.
    if [ -f "$dir/.new-game" ] && [ ! -f "$dir/.keep-dlc" ]; then
      files 'echo route > files/hide-dlc'
    fi
    files 'for f in inject-pad route; do [ -f files/$f ] && { echo "== $f"; cat files/$f; }; done; true'
    ;;
esac
