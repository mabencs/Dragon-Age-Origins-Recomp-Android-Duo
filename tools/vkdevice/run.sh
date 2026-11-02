#!/usr/bin/env bash
# Build, deploy and run the Phase 3.2 Vulkan device bring-up probe.
#
# This runs Xenia's *own* initialisation path - VulkanProvider, VulkanInstance,
# VulkanDevice - on the handheld, with with_gpu_emulation = true. That is the call
# that demands the features Xenos emulation needs and refuses the device without
# them, so a success here means Xenia accepts Adreno 740 for the job.
#
# Presentation is deliberately not requested: there is no surface until the Android
# app shell exists (Phase 4), and device creation does not need one.
#
# Prerequisites:
#   python tools/assemble_runtime.py
#   python tools/build_xenia_shaders.py
#   python tools/prepare_deps.py
#   python tools/compile_runtime.py --target android --scope device
#
# Usage:  ./tools/vkdevice/run.sh [output-file]

set -uo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$ROOT/device/vulkan-device-bringup.txt}"

CANARY="$ROOT/third_party/xenia-canary"
TREE="$ROOT/build/runtime-tree/src"
OBJ="$ROOT/build/runtime-obj/android"
BUILD="$ROOT/build/vkdevice"
REMOTE=/data/local/tmp/vkdev
API=33

: "${ANDROID_NDK_HOME:=$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358}"
for host in windows-x86_64 linux-x86_64 darwin-x86_64; do
    CAND="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$host/bin"
    if [ -d "$CAND" ]; then TOOLBIN="$CAND"; break; fi
done
[ -n "${TOOLBIN:-}" ] || { echo "error: no NDK toolchain under $ANDROID_NDK_HOME" >&2; exit 1; }

CXX="$TOOLBIN/aarch64-linux-android${API}-clang++"
[ -e "$CXX" ] || CXX="$CXX.cmd"
[ -e "$CXX" ] || { echo "error: NDK C++ compiler not found" >&2; exit 1; }

# The NDK toolchain is a native Windows binary here and cannot read MSYS paths.
winp() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

if [ ! -d "$OBJ" ] || [ -z "$(ls -A "$OBJ" 2>/dev/null)" ]; then
    echo "error: no runtime objects in build/runtime-obj/android" >&2
    echo "       run: python tools/compile_runtime.py --target android --scope device" >&2
    exit 1
fi

mkdir -p "$BUILD"
INCS=(-I "$(winp "$TREE")" -I "$(winp "$CANARY")"
      -I "$(winp "$CANARY/third_party/Vulkan-Headers/include")"
      -I "$(winp "$CANARY/third_party/glslang")"
      -I "$(winp "$CANARY/third_party/fmt/include")")

for unit in vkdevice_probe host_stubs; do
    echo "compiling $unit.cc"
    "$CXX" -c -std=c++20 -O1 -fPIC -fexceptions -frtti -w "${INCS[@]}" \
        -o "$(winp "$BUILD/$unit.o")" "$(winp "$HERE/$unit.cc")" \
        || { echo "compile failed: $unit" >&2; exit 1; }
done

# A response file: the object list is far past any command-line limit.
RSP="$BUILD/link.rsp"
{
    printf -- '-static-libstdc++\n-o\n%s\n' "$(winp "$BUILD/vkdevice_probe")"
    printf -- '%s\n' "$(winp "$BUILD/vkdevice_probe.o")"
    printf -- '%s\n' "$(winp "$BUILD/host_stubs.o")"
    for f in "$OBJ"/*.o; do winp "$f"; done
    # libvulkan is shared-only on Android, so -static cannot be used; the C++
    # runtime is linked statically instead so nothing has to be pushed alongside.
    printf -- '-lvulkan\n-llog\n-landroid\n-ldl\n'
} > "$RSP"

echo "linking"
if ! "$CXX" "@$(winp "$RSP")" 2>"$BUILD/link.err"; then
    echo "link failed:" >&2
    grep -E "undefined symbol|unable to find|duplicate symbol" "$BUILD/link.err" \
        | sed 's/.*symbol: //' | sort -u | head -20 >&2
    exit 1
fi
echo "  -> $BUILD/vkdevice_probe ($(stat -c %s "$BUILD/vkdevice_probe" 2>/dev/null) bytes)"

if [ -z "$(adb devices | sed -n '2,$p' | grep -w device)" ]; then
    echo "error: no device in 'device' state" >&2
    adb devices -l >&2
    exit 1
fi

echo "running on device"
adb push "$(winp "$BUILD/vkdevice_probe")" "$REMOTE" >/dev/null || exit 1
adb shell "chmod 755 $REMOTE"
mkdir -p "$(dirname "$OUT")"
{
    echo "Xenia Vulkan device bring-up on the AYN Thor"
    echo "captured $(date -u '+%Y-%m-%d %H:%M:%SZ')"
    echo "NDK $(basename "$ANDROID_NDK_HOME"), API $API, arm64-v8a"
    echo "============================================================"
    echo
    adb shell "cd /data/local/tmp && ./$(basename "$REMOTE")"
} > "$OUT" 2>&1
RC=$?
adb shell "rm -f $REMOTE" >/dev/null 2>&1

echo
sed -n '5,$p' "$OUT"
echo
echo "report -> $OUT"
exit $RC
