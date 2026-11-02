#!/usr/bin/env bash
# deploy.sh - the device loop for the AYN Thor.
#
# Phase 0.6. Until there is an APK (Phase 4) the build/install/run path is a
# no-op that says so; everything else here is useful immediately.
#
#   ./tools/deploy.sh check          device state, ABI, API, displays
#   ./tools/deploy.sh info           full hardware profile -> device/
#   ./tools/deploy.sh vulkan         Vulkan capability probe -> device/
#   ./tools/deploy.sh shot [name]    screenshot BOTH displays -> device/shots/
#   ./tools/deploy.sh logs [tag...]  follow logcat, filtered to this project
#   ./tools/deploy.sh crash          dump the native crash buffer
#   ./tools/deploy.sh perf           framerate / thermal / memory snapshot
#   ./tools/deploy.sh run [--release] [--no-build]
#                                    build, install, launch, follow logs
#
# Notes for Git Bash on Windows: adb and the NDK toolchain are native Windows
# binaries, so paths handed to them must be Windows-style (winpath below), and
# MSYS must be told to stop rewriting adb's remote arguments.

set -uo pipefail
export MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG="${DAO_PACKAGE:-dev.daorecompandroid.duo}"
ACTIVITY="${DAO_ACTIVITY:-.GameActivity}"
MAIN_DISPLAY=0
DECK_DISPLAY="${DAO_DECK_DISPLAY:-4}"

winpath() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi
}

die() { echo "error: $*" >&2; exit 1; }

need_device() {
    command -v adb >/dev/null 2>&1 || die "adb not on PATH"
    if [ -z "$(adb devices | sed -n '2,$p' | grep -w device)" ]; then
        echo "error: no device in 'device' state." >&2
        echo "       check the USB cable and that USB debugging is authorised." >&2
        adb devices -l >&2
        exit 1
    fi
}

cmd_check() {
    need_device
    echo "device:"
    adb devices -l | sed -n '2,$p' | sed 's/^/  /'
    echo "identity:"
    for p in ro.product.model ro.product.device ro.soc.model \
             ro.build.version.release ro.build.version.sdk ro.product.cpu.abi; do
        printf '  %-28s %s\n' "$p" "$(adb shell getprop $p | tr -d '\r')"
    done
    echo "displays:"
    adb shell dumpsys display 2>/dev/null \
        | grep -oE 'DisplayDeviceInfo\{"[^"]+": uniqueId="[^"]+", [0-9]+ x [0-9]+' \
        | sed 's/DisplayDeviceInfo{//; s/uniqueId=//; s/^/  /'
    echo "app:"
    if adb shell pm list packages 2>/dev/null | grep -q "package:$PKG"; then
        echo "  $PKG installed"
    else
        echo "  $PKG not installed (expected until Phase 4)"
    fi
}

cmd_info() {
    need_device
    exec "$ROOT/tools/capture_device.sh" "$ROOT/device/capture-$(date +%Y%m%d-%H%M%S).txt"
}

cmd_vulkan() {
    need_device
    exec "$ROOT/tools/vkprobe/run.sh" "$ROOT/device/vulkan-capabilities.txt"
}

# screencap -d wants the PHYSICAL display id (a long number), not the logical
# display id that dumpsys display and am start use. Resolve it by hardware port:
# port 131 is the main panel, port 132 is the lower screen.
physical_display_id() {
    local port="$1"
    adb shell 'dumpsys SurfaceFlinger --display-id' 2>/dev/null \
        | tr -d '\r' \
        | awk -v p="port=$port" '$0 ~ p { print $2 }' \
        | head -1
}

wake_device() {
    # A screenshot of a sleeping panel comes back black or empty.
    local awake
    awake="$(adb shell dumpsys power 2>/dev/null | tr -d '\r' | grep -m1 'mWakefulness=' | cut -d= -f2)"
    if [ "$awake" != "Awake" ]; then
        echo "  (device is ${awake:-asleep}; waking it)"
        adb shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
        adb shell wm dismiss-keyguard >/dev/null 2>&1
    fi
}

cmd_shot() {
    need_device
    local name="${1:-shot-$(date +%Y%m%d-%H%M%S)}"
    local dir="$ROOT/device/shots"
    mkdir -p "$dir"
    wake_device

    local main_id deck_id
    main_id="$(physical_display_id 131)"
    deck_id="$(physical_display_id 132)"

    capture() {
        local label="$1"
        local pid="${2:-}"
        local out="$dir/${name}-${label}.png"
        if [ -n "$pid" ]; then
            adb exec-out screencap -d "$pid" -p > "$out" 2>/dev/null
        else
            adb exec-out screencap -p > "$out" 2>/dev/null
        fi
        if [ -s "$out" ]; then
            echo "  ${name}-${label}.png  $(stat -c %s "$out") bytes  (display $pid)"
        else
            echo "  ${name}-${label}.png  EMPTY - panel likely powered off"
            rm -f "$out"
        fi
    }

    capture main "$main_id"
    capture deck "$deck_id"
}

cmd_logs() {
    need_device
    local tags=("$@")
    if [ ${#tags[@]} -eq 0 ]; then
        tags=(DAORecomp:V DAO:V Vulkan:V AAudio:V Swappy:V ActivityManager:W)
    fi
    echo "following logcat (ctrl-c to stop): ${tags[*]}"
    adb logcat -c 2>/dev/null
    adb logcat "${tags[@]}" '*:S'
}

cmd_crash() {
    need_device
    echo "=== native crash buffer ==="
    adb logcat -b crash -d | tail -80
    local ndk="${ANDROID_NDK_HOME:-}"
    local syms="$ROOT/build/android/obj/local/arm64-v8a"
    if [ -n "$ndk" ] && [ -d "$syms" ]; then
        echo
        echo "to symbolicate:"
        echo "  \"$ndk/ndk-stack\" -sym \"$syms\" -dump <logfile>"
    fi
}

cmd_perf() {
    need_device
    echo "=== thermal ==="
    adb shell dumpsys thermalservice 2>/dev/null | grep -iE "Temperature|status|HAL " | head -15
    echo "=== gpu ==="
    adb shell "cat /sys/class/kgsl/kgsl-3d0/gpuclk /sys/class/kgsl/kgsl-3d0/gpubusy" 2>/dev/null | sed 's/^/  /'
    echo "=== battery ==="
    adb shell dumpsys battery 2>/dev/null | grep -iE "level|temperature|current now" | sed 's/^/  /'
    if adb shell pm list packages 2>/dev/null | grep -q "package:$PKG"; then
        echo "=== framestats ==="
        adb shell dumpsys gfxinfo "$PKG" framestats 2>/dev/null | head -20
        echo "=== meminfo ==="
        adb shell dumpsys meminfo "$PKG" 2>/dev/null | grep -iE "TOTAL|Native|Graphics" | sed 's/^/  /'
    fi
}

cmd_run() {
    local variant=debug build=1
    for a in "$@"; do
        case "$a" in
            --release)  variant=release ;;
            --debug)    variant=debug ;;
            --no-build) build=0 ;;
            *) die "unknown option: $a" ;;
        esac
    done

    if [ ! -f "$ROOT/gradlew" ] && [ ! -f "$ROOT/settings.gradle" ] && [ ! -f "$ROOT/settings.gradle.kts" ]; then
        echo "No Android app module yet - that arrives in Phase 4."
        echo "Until then use: check | info | vulkan | shot | logs | perf"
        return 0
    fi

    need_device
    if [ "$build" = 1 ]; then
        echo "building ($variant)"
        local task="assembleDebug"
        [ "$variant" = release ] && task="assembleRelease"
        ( cd "$ROOT" && ./gradlew "$task" ) || die "build failed"
    fi

    local apk
    apk="$(find "$ROOT/app/build/outputs/apk/$variant" -name '*.apk' -print -quit 2>/dev/null)"
    [ -n "$apk" ] || die "no APK found for $variant"
    echo "installing $(basename "$apk")"
    adb install -r "$(winpath "$apk")" || die "install failed"

    echo "launching $PKG/$ACTIVITY on display $MAIN_DISPLAY"
    adb shell am start --display "$MAIN_DISPLAY" -n "$PKG/$ACTIVITY" >/dev/null
    cmd_logs
}

case "${1:-check}" in
    check)  shift || true; cmd_check "$@" ;;
    info)   shift || true; cmd_info "$@" ;;
    vulkan) shift || true; cmd_vulkan "$@" ;;
    shot)   shift || true; cmd_shot "$@" ;;
    logs)   shift || true; cmd_logs "$@" ;;
    crash)  shift || true; cmd_crash "$@" ;;
    perf)   shift || true; cmd_perf "$@" ;;
    run)    shift || true; cmd_run "$@" ;;
    -h|--help|help)
        sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' ;;
    *) die "unknown command: $1  (try --help)" ;;
esac
