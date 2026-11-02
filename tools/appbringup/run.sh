#!/usr/bin/env bash
# Build, install and run the app on the handheld, and capture what it reports.
#
# Phase 4. This is the APK-level counterpart to tools/vkdevice/run.sh. That script
# proved Xenia's Vulkan code works in a binary pushed to /data/local/tmp; this proves
# it works inside an installed app - which is a different claim, because an app has a
# different SELinux domain, loads its library through the ART loader, and gets its
# window from a real SurfaceView rather than from nothing at all.
#
# What it checks, in order:
#   the APK builds           (Gradle -> CMake -> NDK)
#   it installs and launches
#   JNI registration succeeds
#   a Vulkan device is created in-process
#   a VkSurfaceKHR is created from the SurfaceView's ANativeWindow
#   what that swapchain can actually do (present modes, formats, colour spaces)
#
# Prerequisites: python tools/setup_gradle.py, and the runtime tree assembled
# (see app/src/main/cpp/CMakeLists.txt for the list).
#
# Usage:
#   ./tools/appbringup/run.sh [output-file]
#   ./tools/appbringup/run.sh --wait-extract [output-file]
#
# --wait-extract blocks until the game-data extraction reaches a terminal state. It is
# off by default because a full extraction is 6.4 GiB and takes minutes, which is not
# what you want from a check you run after every code change.

set -uo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WAIT_EXTRACT=0
if [ "${1:-}" = "--wait-extract" ]; then
    WAIT_EXTRACT=1
    shift
fi
OUT="${1:-$ROOT/device/android-app-bringup.txt}"

PKG=dev.daorecompandroid.duo
ACTIVITY="$PKG/.GameActivity"
APK="$ROOT/app/build/outputs/apk/debug/app-debug.apk"

# A shell started before the user-scope variables were set does not inherit them,
# which looks exactly like a missing JDK. Read them from the registry in that case.
winvar() { powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('$1','User')" 2>/dev/null | tr -d '\r'; }
: "${JAVA_HOME:=$(winvar JAVA_HOME)}"
: "${ANDROID_HOME:=$(winvar ANDROID_HOME)}"
export JAVA_HOME ANDROID_HOME

ADB="$ANDROID_HOME/platform-tools/adb.exe"
[ -x "$ADB" ] || ADB="$(command -v adb)"
[ -n "${ADB:-}" ] || { echo "error: adb not found" >&2; exit 1; }

winp() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi; }

cd "$ROOT" || exit 1

echo "building"
if ! ./gradlew.bat :app:assembleDebug --console=plain -q > "$ROOT/build/appbringup.log" 2>&1; then
    echo "build failed:" >&2
    grep -E "error:|FAILURE|What went wrong|^> " "$ROOT/build/appbringup.log" | head -25 >&2
    exit 1
fi
echo "  -> $APK ($(stat -c %s "$APK" 2>/dev/null) bytes)"

if [ -z "$("$ADB" devices | sed -n '2,$p' | grep -w device)" ]; then
    echo "error: no device in 'device' state" >&2
    "$ADB" devices -l >&2
    exit 1
fi

# The surface is only created for a window on an awake display. With the screen off
# the app launches, reports a Vulkan device, and never gets a window - which looks
# like a surface bug and is not one.
"$ADB" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
wakefulness="$("$ADB" shell dumpsys power | grep -o 'mWakefulness=[A-Za-z]*' | head -1)"
echo "  device $wakefulness"

echo "installing"
"$ADB" install -r -d "$(winp "$APK")" 2>&1 | grep -v "^Performing" | head -3

"$ADB" logcat -c
"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1
echo "launching"
# The report alone: a launch starts the game unless it says otherwise.
"$ADB" shell am start -n "$ACTIVITY" --ez boot false >/dev/null 2>&1 || { echo "error: am start failed" >&2; exit 1; }

# Wait for the report rather than sleeping a guessed amount: it is emitted when the
# surface is measured, which is after the first layout pass.
for _ in $(seq 1 30); do
    if "$ADB" logcat -d -s warden:V | grep -q 'HDR10_ST2084'; then break; fi
    sleep 0.5
done

if [ "$WAIT_EXTRACT" = "1" ]; then
    echo "waiting for extraction (6.4 GiB; ^C to stop waiting, it keeps going)"
    # Up to 40 minutes. Reports progress as it goes so a stall is visible rather than
    # looking like a hang.
    for _ in $(seq 1 480); do
        line="$("$ADB" logcat -d -s warden:V | sed -n 's/.*\[report\] //p' \
                | grep -E '^  (state|bytes) ' | tail -2 | tr -s ' ' | tr '\n' ' ')"
        [ -n "$line" ] && printf '\r  %-76s' "$line"
        if "$ADB" logcat -d -s warden:V | grep -qE 'extraction complete|extraction failed|extraction cancelled'; then
            break
        fi
        sleep 5
    done
    echo
fi

mkdir -p "$(dirname "$OUT")"
{
    echo "Dragon Age Origins Recomp Android Duo app bring-up on the AYN Thor"
    echo "captured $(date -u '+%Y-%m-%d %H:%M:%SZ')"
    echo "apk $(stat -c %s "$APK" 2>/dev/null) bytes, $("$ADB" shell getprop ro.product.model | tr -d '\r'), Android $("$ADB" shell getprop ro.build.version.release | tr -d '\r') (API $("$ADB" shell getprop ro.build.version.sdk | tr -d '\r'))"
    echo "============================================================"
    echo
    echo "LIFECYCLE"
    "$ADB" logcat -d -s Warden:V | grep -v '\[report\]' \
        | sed -n 's/^.*Warden  *: /  /p'
    echo
    # Both sides of the JNI boundary mark their report lines with "[report]", so a
    # single timestamp-ordered pass reassembles one report from Java and native
    # output - game data included, in the order it was produced. The sed also strips
    # Xenia's own log decoration ("i> 00005EEE ").
    "$ADB" logcat -d -s Warden:V warden:V | sed -n 's/.*\[report\] //p'
    echo
    # What is actually on the panel. "The presenter painted a frame" and "something is
    # visible" are different claims, and they came apart once already - see BUG-026.
    echo "SCREEN"
    mkdir -p "$ROOT/device/shots"
    if "$ADB" exec-out screencap -p > "$ROOT/device/shots/appbringup.png" 2>/dev/null; then
        python "$(winp "$HERE/sample_screen.py")" "$(winp "$ROOT/device/shots/appbringup.png")" 2>&1
    else
        echo "  screencap failed"
    fi
    echo
    crash="$("$ADB" logcat -d -s AndroidRuntime:E DEBUG:F libc:F | grep -c .)"
    if [ "$crash" -gt 0 ]; then
        echo "CRASHES / FATALS  ($crash lines)"
        "$ADB" logcat -d -s AndroidRuntime:E DEBUG:F libc:F | head -40
    else
        echo "no crashes, no native fatals"
    fi
} > "$OUT" 2>&1

sed -n '5,$p' "$OUT"
echo
echo "report -> $OUT"
