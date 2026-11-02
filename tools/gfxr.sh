#!/usr/bin/env bash
# gfxr.sh - capture the game's Vulkan frames on the Thor with GFXReconstruct, and replay
# them there (PLAN 3.10).
#
#   bash tools/gfxr.sh setup                 fetch GFXReconstruct (the Vulkan SDK 1.4.363.0
#                                            Android release, SHA-256 checked) into
#                                            build/gfxr, strip its layer, copy the layer
#                                            into the app's data directory, install the
#                                            replay app (com.lunarg.gfxreconstruct.replay)
#   bash tools/gfxr.sh arm <name> [<frames>] enable the layer for the app: capture the
#                                            frames given ("1800-1801"), or with no frames
#                                            wait for `trigger`; then boot as usual
#                                            (boot_capture.sh)
#   bash tools/gfxr.sh trigger               capture the next frame now (armed without frames)
#   bash tools/gfxr.sh pull <name>           move the capture to /sdcard/Download/<name>.gfxr
#   bash tools/gfxr.sh replay <name> [args]  replay it on the Thor, a screenshot of every
#                                            frame into build/gfxr-<name>-frame_N.png; extra
#                                            args go to gfxrecon-replay (--loop-frame 1
#                                            --loop-count 50, --pbi-all, --validate, ...)
#   bash tools/gfxr.sh off                   disable the layer (the settings, the properties)
#
# How it fits this app (docs/TESTING.md, "GFXReconstruct"): the layer is loaded from the
# app's own data directory - a debuggable app's - because Android 11's package
# visibility hides the replay app's copy from ours; memory is tracked with the default
# page guard, which coexists with the runtime's write-watch handler (it passes on the
# faults it does not own); the capture is written to the app's files directory and
# copied out by run-as. A trimmed King's Camp frame is about 160 MB and takes a second
# to write (run 330). Replays are for looking, not for timing: a looped frame costs
# three times the game's own GPU time (run 330) - per-pass timing is files/gpu-timing.txt.
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
PKG=dev.daorecompandroid.duo
REPLAY=com.lunarg.gfxreconstruct.replay
VERSION=vulkan-sdk-1.4.363.0
URL="https://github.com/LunarG/gfxreconstruct/releases/download/$VERSION/gfxreconstruct-$VERSION-android.zip"
SHA256=30c4c1bade07c646516326c7fe30e4fc4f8fbb2b040b28ba795c0f50c645e9c6
DIR=build/gfxr
DATA=/data/user/0/$PKG
CMD="${1:?setup, arm, trigger, pull, replay or off}"

start_replay() {  # <args...> - force-stop first: a start sent to an exiting replay is lost
  "$ADB" shell "am force-stop $REPLAY"
  "$ADB" logcat -c
  "$ADB" shell "am start -n $REPLAY/android.app.NativeActivity -a android.intent.action.MAIN -c android.intent.category.LAUNCHER --es args '$*'" >/dev/null
  for _ in $(seq 1 300); do
    if "$ADB" logcat -d -s gfxrecon:V | grep -q 'Exiting android_main'; then break; fi
    sleep 1
  done
  if "$ADB" logcat -d -s gfxrecon:V | grep -q 'Usage:'; then
    echo "the replay app did not take its arguments: $*"
  fi
  "$ADB" logcat -d -s gfxrecon:V | grep -v 'declares the command-line name' \
    | grep -E 'ERROR|FATAL|Load time|Total time|Measured FPS' | sed -E 's/^.*gfxrecon *: //'
}

case "$CMD" in
  setup)
    mkdir -p "$DIR"
    if [ ! -f "$DIR/gfxr-android.zip" ] || [ "$(sha256sum "$DIR/gfxr-android.zip" | cut -d' ' -f1)" != "$SHA256" ]; then
      echo "fetching $URL"
      curl -sL -o "$DIR/gfxr-android.zip" "$URL"
      [ "$(sha256sum "$DIR/gfxr-android.zip" | cut -d' ' -f1)" = "$SHA256" ] || { echo "not the archive this project used"; exit 1; }
    fi
    python - "$DIR" "$VERSION" <<'PY'
import sys, zipfile
d, v = sys.argv[1], sys.argv[2]
z = zipfile.ZipFile(d + "/gfxr-android.zip")
base = "gfxreconstruct-%s/" % v
for member, out in (("layer/arm64-v8a/libVkLayer_gfxreconstruct.so", "libVkLayer_gfxreconstruct.so"),
                    ("tools/replay-debug.apk", "replay-debug.apk"),
                    ("USAGE_android.md", "USAGE_android.md")):
    open(d + "/" + out, "wb").write(z.read(base + member))
PY
    STRIP="$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"
    "$STRIP" --strip-unneeded -o "$DIR/libVkLayer_gfxreconstruct.stripped.so" "$DIR/libVkLayer_gfxreconstruct.so"
    "$ADB" push "$(cygpath -w "$DIR/libVkLayer_gfxreconstruct.stripped.so")" /data/local/tmp/libVkLayer_gfxreconstruct.so >/dev/null
    "$ADB" shell "run-as $PKG cp /data/local/tmp/libVkLayer_gfxreconstruct.so $DATA/; rm /data/local/tmp/libVkLayer_gfxreconstruct.so"
    "$ADB" install -g -t -r -d --force-queryable "$(cygpath -w "$DIR/replay-debug.apk")" | tail -1
    echo "the layer in $DATA, the replay app installed"
    ;;
  arm)
    NAME="${2:?capture name}"
    FRAMES="${3:-}"
    "$ADB" shell "settings put global enable_gpu_debug_layers 1; settings put global gpu_debug_app $PKG; settings put global gpu_debug_layers VK_LAYER_LUNARG_gfxreconstruct; setprop debug.gfxrecon.capture_file $DATA/files/$NAME.gfxr; setprop debug.gfxrecon.capture_file_timestamp false"
    if [ -n "$FRAMES" ]; then
      "$ADB" shell "setprop debug.gfxrecon.capture_frames $FRAMES; setprop debug.gfxrecon.capture_android_trigger ''; setprop debug.gfxrecon.capture_trigger_frames ''"
      echo "armed: frames $FRAMES into $NAME"
    else
      "$ADB" shell "setprop debug.gfxrecon.capture_frames ''; setprop debug.gfxrecon.capture_android_trigger false; setprop debug.gfxrecon.capture_trigger_frames 1"
      echo "armed: one frame into $NAME at 'bash tools/gfxr.sh trigger'"
    fi
    ;;
  trigger)
    "$ADB" shell setprop debug.gfxrecon.capture_android_trigger true
    for _ in $(seq 1 120); do
      if "$ADB" logcat -d -s gfxrecon:V | grep -q 'Finished recording'; then break; fi
      sleep 1
    done
    "$ADB" shell setprop debug.gfxrecon.capture_android_trigger false
    "$ADB" logcat -d -s gfxrecon:V | grep -E 'Recording|Finished' | tail -2 | sed -E 's/^.*INFO - //'
    ;;
  pull)
    NAME="${2:?capture name}"
    file=$("$ADB" shell "run-as $PKG ls files" | tr -d '\r' | grep -E "^$NAME.*\.gfxr$" | head -1)
    [ -n "$file" ] || { echo "no capture $NAME*.gfxr in the app's files"; exit 1; }
    "$ADB" shell "run-as $PKG cat files/$file > /sdcard/Download/$NAME.gfxr && run-as $PKG rm files/$file; ls -la /sdcard/Download/$NAME.gfxr"
    ;;
  replay)
    NAME="${2:?capture name}"
    shift 2
    # One space between arguments: the replay app takes the empty word between two
    # spaces for an argument, and answers with its usage.
    ARGS="--screenshot-all --screenshot-format png --screenshot-dir /sdcard/Download"
    [ $# -gt 0 ] && ARGS="$ARGS $*"
    "$ADB" shell "rm -f /sdcard/Download/screenshot_frame_*.png"
    start_replay "$ARGS /sdcard/Download/$NAME.gfxr"
    for shot in $("$ADB" shell "ls /sdcard/Download" | tr -d '\r' | grep -E '^screenshot_frame_[0-9]+\.png$'); do
      "$ADB" pull "/sdcard/Download/$shot" "build/gfxr-$NAME-${shot#screenshot_}" >/dev/null && echo "build/gfxr-$NAME-${shot#screenshot_}"
    done
    "$ADB" shell "rm -f /sdcard/Download/screenshot_frame_*.png; am force-stop $REPLAY"
    ;;
  off)
    "$ADB" shell "settings delete global enable_gpu_debug_layers; settings delete global gpu_debug_app; settings delete global gpu_debug_layers; setprop debug.gfxrecon.capture_android_trigger ''; setprop debug.gfxrecon.capture_frames ''; setprop debug.gfxrecon.capture_trigger_frames ''; setprop debug.gfxrecon.capture_file ''" >/dev/null
    echo "the layer is off (the settings deleted)"
    ;;
  *)
    echo "setup, arm, trigger, pull, replay or off"
    exit 1
    ;;
esac
