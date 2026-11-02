#!/usr/bin/env bash
# sample_power.sh - the GPU's load, the temperatures and the power draw during a long run.
#
#   bash tools/sample_power.sh <run> [minutes]     # alongside: bash tools/boot_capture.sh <run> 660
#
# Waits 40 s (the boot), then every 10 s for <minutes> (default 10) appends one line to
# build/run<run>-power.txt: the Adreno's busy percentage and clock, the hottest GPU
# sensor, a big-core CPU sensor (cpu-1-0), the board (xo-therm) and the battery in C,
# the battery's current (positive into the battery on this device), voltage, charge
# counter and level, and the USB input's current and voltage. The first line holds the
# panel brightness and the battery's full charge. On a 900 mA USB port the game draws
# more than the port gives, so the battery discharges while plugged in and its discharge
# plus the input is the draw. `python tools/frame_report.py <run>` reads the file.
set -u
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1
ADB="${ADB:-$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe}"
RUN="${1:?run number}"
MINUTES="${2:-10}"
OUT="build/run$RUN-power.txt"
sleep 40
"$ADB" shell 'echo "# brightness $(settings get system screen_brightness) mode $(settings get system screen_brightness_mode) full_uah $(cat /sys/class/power_supply/battery/charge_full)"' > "$OUT" 2>/dev/null
echo "time busy% gpu_mhz gpu_c cpu_c board_c battery_c batt_ma batt_mv batt_mah level usb_ma usb_mv" >> "$OUT"
end=$(( $(date +%s) + MINUTES * 60 ))
while [ "$(date +%s)" -lt "$end" ]; do
  "$ADB" shell 'g=0; for z in /sys/class/thermal/thermal_zone*; do t=$(cat $z/type); case $t in gpuss-*) v=$(cat $z/temp); [ $v -gt $g ] && g=$v;; cpu-1-0) c=$(cat $z/temp);; xo-therm) x=$(cat $z/temp);; battery) b=$(cat $z/temp);; esac; done; B=/sys/class/power_supply/battery; U=/sys/class/power_supply/usb; echo "$(date +%T) $(cut -d" " -f1 /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage) $(( $(cat /sys/class/kgsl/kgsl-3d0/gpuclk) / 1000000 )) $((g / 1000)) $((c / 1000)) $((x / 1000)) $((b / 1000)) $(( $(cat $B/current_now) / 1000 )) $(( $(cat $B/voltage_now) / 1000 )) $(( $(cat $B/charge_counter) / 1000 )) $(cat $B/capacity) $(( $(cat $U/current_now) / 1000 )) $(( $(cat $U/voltage_now) / 1000 ))"' >> "$OUT" 2>/dev/null
  sleep 10
done
echo "done: $(wc -l < "$OUT") lines in $OUT"
