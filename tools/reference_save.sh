#!/usr/bin/env bash
# reference_save.sh <save folder> - a save from the Thor into DAORecomp's Windows build,
# so both renderers load the same scene (PLAN 3.3b).
#
#   bash tools/reference_save.sh '0xd5332a7_1_a!0'
#
# The saves are the game's own files in both: the port keeps them in its files
# directory under content/00000001/<folder>/ with <folder>.name beside it, and DAORecomp
# under userdata/content/454108C0/00000001/. The save on the Thor is only read; one of
# that name in DAORecomp's is replaced. `adb shell run-as dev.daorecompandroid.duo ls -t
# files/content/00000001` lists the Thor's, newest first (Resume loads the newest).
# The game's profile comes along (files/profile/*.bin to userdata/profile/454108C0/):
# its settings - the tutorials off, the difficulty - so both show the same scene.
set -eu
export MSYS2_ARG_CONV_EXCL='*'
SAVE=${1:?the save folder, e.g. 0xd5332a7_1_a!0}
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
DEST="${DAO_REFERENCE:-/c/Games/DAORecomp}/userdata/content/454108C0/00000001"
mkdir -p "$DEST"
rm -rf "$DEST/$SAVE" "$DEST/$SAVE.name"
"$ADB" exec-out "run-as dev.daorecompandroid.duo tar cf - -C files/content/00000001 '$SAVE' '$SAVE.name'" \
    | tar xf - -C "$DEST"
PROFILE="${DAO_REFERENCE:-/c/Games/DAORecomp}/userdata/profile/454108C0"
mkdir -p "$PROFILE"
"$ADB" exec-out "run-as dev.daorecompandroid.duo tar cf - -C files/profile ." | tar xf - -C "$PROFILE"
find "$DEST/$SAVE" "$DEST/$SAVE.name" "$PROFILE" -type f -printf '%s %p\n'
