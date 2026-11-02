#!/usr/bin/env bash
# Build vkprobe for arm64-v8a, push it to the device, run it, and keep the output.
#
# Doubles as the Phase 0 smoke test for the whole toolchain: if this produces a
# capability report from the handheld, then the NDK, the cross-compile, adb and
# native execution on the device are all working.
#
# Usage:  ./tools/vkprobe/run.sh [output-file]

set -uo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$ROOT/device/vulkan-capabilities.txt}"

API=33
ABI=aarch64-linux-android
REMOTE=/data/local/tmp/vkprobe

: "${ANDROID_NDK_HOME:=$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358}"
NDK="$ANDROID_NDK_HOME"

# The NDK ships host toolchains per platform; pick whichever is present.
for host in windows-x86_64 linux-x86_64 darwin-x86_64; do
    CANDIDATE="$NDK/toolchains/llvm/prebuilt/$host/bin"
    if [ -d "$CANDIDATE" ]; then TOOLBIN="$CANDIDATE"; break; fi
done
if [ -z "${TOOLBIN:-}" ]; then
    echo "error: no NDK host toolchain under $NDK" >&2
    exit 1
fi

CC="$TOOLBIN/${ABI}${API}-clang"
[ -x "$CC" ] || CC="$TOOLBIN/${ABI}${API}-clang.cmd"
if [ ! -e "$CC" ]; then
    echo "error: cross compiler not found: $CC" >&2
    exit 1
fi

BUILD="$ROOT/build/vkprobe"
mkdir -p "$BUILD"
BIN="$BUILD/vkprobe"

# The NDK toolchain on Windows is a native Windows binary and does not
# understand MSYS-style /c/... paths, so hand it Windows paths.
winpath() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi
}

echo "compiling for arm64-v8a (API $API)"
"$CC" -O2 -Wall -Wextra -Wno-unused-parameter \
      -o "$(winpath "$BIN")" "$(winpath "$HERE/vkprobe.c")" -ldl \
      || { echo "compile failed" >&2; exit 1; }

echo "  -> $BIN ($(stat -c %s "$BIN" 2>/dev/null || echo '?') bytes)"
file "$BIN" 2>/dev/null | sed 's/^/  /'

if [ -z "$(adb devices | sed -n '2p' | grep -w device)" ]; then
    echo "error: no device in 'device' state" >&2
    adb devices -l >&2
    exit 1
fi

echo "pushing and running on device"
# adb is likewise a native Windows binary here.
adb push "$(winpath "$BIN")" "$REMOTE" >/dev/null || { echo "push failed" >&2; exit 1; }
adb shell "chmod 755 $REMOTE"

mkdir -p "$(dirname "$OUT")"
{
    echo "AYN Thor Vulkan capability probe"
    echo "captured $(date -u '+%Y-%m-%d %H:%M:%SZ')"
    echo "NDK $(basename "$NDK"), API $API, arm64-v8a"
    echo "================================================================"
    echo
    adb shell "$REMOTE"
} > "$OUT" 2>&1

adb shell "rm -f $REMOTE" >/dev/null 2>&1

echo "report -> $OUT"
echo
grep -E "DEVICE 0|api version|MISSING|geometryShader|tessellationShader|sparseBinding|fragment_shader_interlock|hdr_metadata|swapchain_colorspace" "$OUT" | sed 's/^/  /'
