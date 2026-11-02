#!/usr/bin/env bash
# Boot guest code several times and report whether it behaves the same each run.
#
# Task 5.1. Two runs appeared to disagree about where the title crashes and which
# kernel imports it reaches, which would mean guest startup depends on state nothing
# initialises. That is a claim worth checking rather than reasoning about: if the crash
# site is identical across runs, startup is deterministic and the two runs differed for
# some other reason.
#
# Reports, per run: the innermost guest frame, the fault address, and every import
# reached. Then says whether they all agree.
#
# Usage:  ./tools/appbringup/boot_repeat.sh [runs]

set -uo pipefail
export MSYS2_ARG_CONV_EXCL='*'

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RUNS="${1:-5}"
PKG=dev.daorecompandroid.duo

winvar() { powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable('$1','User')" 2>/dev/null | tr -d '\r'; }
: "${ANDROID_HOME:=$(winvar ANDROID_HOME)}"
ADB="$ANDROID_HOME/platform-tools/adb.exe"
[ -x "$ADB" ] || ADB="$(command -v adb)"
[ -n "${ADB:-}" ] || { echo "error: adb not found" >&2; exit 1; }

"$ADB" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1

summary=""
for run in $(seq 1 "$RUNS"); do
    "$ADB" logcat -c
    "$ADB" shell am force-stop "$PKG" >/dev/null 2>&1
    "$ADB" shell "am start -n $PKG/.GameActivity --ez boot true" >/dev/null 2>&1

    # Wait for the boot marker rather than sleeping a guessed amount. Startup does a
    # lot before guest code runs - reading a 20 MB executable, applying the title
    # update, building a 31 MB call table - and that currently happens on the main
    # thread, so the 1500 ms boot delay does not even start for ten seconds or more.
    #
    # A fixed sleep here is what made two runs look like two different code paths: the
    # short ones sampled before guest code had run at all. Poll instead.
    for _ in $(seq 1 60); do
        if "$ADB" logcat -d -s warden:V | grep -q 'calling the title entry point'; then
            break
        fi
        sleep 1
    done
    # Then a moment for the crash that follows.
    sleep 3

    # The *guest* link register, not the host backtrace.
    #
    # The host backtrace is not a stable identifier. XenonRecomp emits direct calls,
    # and at -O2 the compiler tail-calls and inlines them, so which guest functions
    # survive as host frames changes between builds for reasons that have nothing to
    # do with the guest: one build showed six frames and the next three, with
    # identical guest state. lr is the guest's own return address and does not move.
    lr="$("$ADB" logcat -d -s Warden:V | grep -oE 'guest lr=[0-9A-F]+' | tail -1 \
              | sed 's/guest lr=/lr=/')"
    # And what it was calling, when the handler could decode it.
    target="$("$ADB" logcat -d -s Warden:V \
              | grep -oE 'INDIRECT CALL to guest [0-9A-F]+' | tail -1 \
              | sed 's/INDIRECT CALL to guest /calling=/')"
    imports="$("$ADB" logcat -d 2>&1 | grep -cE 'unimplemented import reached')"
    allocs="$("$ADB" logcat -d 2>&1 | grep -cE 'NtAllocateVirtualMemory\(')"
    [ -n "$lr" ] || lr="(no guest lr)"
    [ -n "$target" ] || target="(not an indirect call)"

    printf '  run %d: %-14s %-18s imports %-3s allocs %s\n' \
        "$run" "$lr" "$target" "$imports" "$allocs"
    summary="$summary$lr|$target|$imports|$allocs"$'\n'
done

"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1

echo
distinct="$(printf '%s' "$summary" | sort -u | grep -c .)"
if [ "$distinct" = "1" ]; then
    echo "  all $RUNS runs identical: startup is deterministic"
    echo "  (so the earlier disagreement was not nondeterminism)"
else
    echo "  $distinct distinct outcomes across $RUNS runs: startup is NOT deterministic"
    printf '%s' "$summary" | sort | uniq -c | sed 's/^/    /'
fi
