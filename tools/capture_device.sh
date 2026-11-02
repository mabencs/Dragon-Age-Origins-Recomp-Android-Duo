#!/usr/bin/env bash
# capture_device.sh - re-capture the AYN Thor's hardware profile over adb.
#
# Regenerates the raw data behind device/thor-profile.md. Run it after an OS
# update, or on a different device, then update that document by hand.
#
# Usage:  ./tools/capture_device.sh [output-file]
#
# Note for Git Bash on Windows: MSYS rewrites anything resembling a Unix path,
# which mangles `adb shell` arguments. Hence MSYS2_ARG_CONV_EXCL below.

set -uo pipefail
export MSYS2_ARG_CONV_EXCL='*'

OUT="${1:-device/capture-$(date +%Y%m%d-%H%M%S).txt}"
mkdir -p "$(dirname "$OUT")"

if ! command -v adb >/dev/null 2>&1; then
    echo "error: adb not found on PATH" >&2
    exit 1
fi

if [ -z "$(adb devices | sed -n '2p' | grep -w device)" ]; then
    echo "error: no device in 'device' state. Check the USB cable and that USB" >&2
    echo "       debugging is authorised on the handheld." >&2
    adb devices -l >&2
    exit 1
fi

section() {
    printf '\n========================================================\n%s\n========================================================\n' "$1"
}

{
    echo "Device capture - $(date -u '+%Y-%m-%d %H:%M:%SZ')"
    adb devices -l

    section "IDENTITY / BUILD"
    adb shell getprop | grep -Ei 'ro\.product\.(model|device|name|manufacturer)|ro\.build\.version\.(release|sdk)|ro\.soc|ro\.board|ro\.product\.cpu\.abi' | sort

    section "CPU"
    echo "core count: $(adb shell 'cat /proc/cpuinfo | grep -c processor')"
    adb shell "cat /proc/cpuinfo | grep -E 'processor|CPU part'"
    echo
    echo "# CPU part IDs: d4e=Cortex-X3  d47=A710  d4d=A715  d46=A510"

    section "MEMORY"
    adb shell 'head -3 /proc/meminfo'

    section "STORAGE"
    adb shell 'df -h /data'

    section "GPU / GRAPHICS"
    adb shell getprop | grep -Ei 'vulkan|gles|gpu|adreno|gfx\.driver' | sort
    echo
    adb shell 'cat /sys/class/kgsl/kgsl-3d0/gpu_model' 2>/dev/null || echo "(gpu_model unavailable)"

    section "DISPLAYS - summary"
    adb shell 'dumpsys display' | grep -E 'DisplayDeviceInfo|mDisplayId=|FLAG_PRESENTATION'

    section "DISPLAYS - viewports"
    adb shell 'dumpsys display' | grep -E 'mViewports'

    section "DISPLAYS - colour and HDR"
    adb shell 'dumpsys SurfaceFlinger' | grep -iE 'wide color|color mode|ColorMode::' | head -20

    section "THERMAL"
    adb shell 'dumpsys thermalservice' | head -30

    section "RAW: dumpsys display"
    adb shell 'dumpsys display'
} > "$OUT" 2>&1

echo "captured -> $OUT"
echo
echo "Key values to check against device/thor-profile.md:"
grep -E 'DisplayDeviceInfo\{"' "$OUT" | sed 's/^\s*/  /' | cut -c1-140
