#!/usr/bin/env bash
# PLAN 10.11: one area's profile once it is loaded (jump.sh): an arrival screenshot, 10 s
# standing, a 6 s walk forward with screenshots, a 4 s camera turn with screenshots, a
# 6 s walk; the frame rate of each part from the status lines' own timestamps (one every
# 5 s), the GPU's busy percentage sampled, the game's error count; one line in
# build/areas/results.txt.
#   bash area_probe.sh <label>
export MSYS_NO_PATHCONV=1
S="$(dirname "${BASH_SOURCE[0]}")"
LABEL="$1"
cd /c/Repos/DragonAgeOriginsAndroid || exit 1
mkdir -p build/areas
shot() { adb exec-out screencap -p > "build/areas/$LABEL-$1.png"; }
busy() { adb shell 'cut -d" " -f1 /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage' | tr -d '\r'; }
now() { adb shell date +%s; }
# An arrival conversation or cutscene (mode 4): B ends its lines and A picks the first reply, alternating, up to 60 s.
mode() { adb logcat -d -s warden:V | grep -a "game: mode is now" | tail -1 | grep -o "mode is now [0-9]*" | awk '{print $4}'; }
talk=0
for i in $(seq 1 120); do
  # Exploring, a fight on arrival - or dead (2): never press on at the death screen (its
  # A is "Load Last Save"); the probe then measures the death screen over the scene.
  m=$(mode); { [ "$m" = "0" ] || [ "$m" = "1" ] || [ "$m" = "2" ]; } && break
  talk=$((talk + 1))
  if [ $((i % 2)) = 1 ]; then bash "$S/live.sh" press 2000 >/dev/null; else bash "$S/live.sh" press 1000 >/dev/null; fi
  sleep 1.2
done
sleep 5
rim=$(adb logcat -d -s warden:V | grep -a "NtReadFile" | grep -ao "al_[a-z0-9_]*\.rim" | awk '!seen[$0]++' | tail -1)
shot arrival
t_stand0=$(now); b0=$(busy); sleep 5; b1=$(busy); sleep 5; t_stand1=$(now)
bash "$S/live.sh" walk 6 20000 0 >/dev/null
t_walk0=$(now); sleep 2; shot walk1; sleep 2; shot walk2; sleep 2.5; t_walk1=$(now); b2=$(busy)
bash "$S/live.sh" walk 4 0 18000 >/dev/null
sleep 2; shot turn1; sleep 2.5; shot turn2
bash "$S/live.sh" walk 6 20000 0 >/dev/null
t_walk2=$(now); sleep 3; shot walk3; sleep 3.5; t_walk3=$(now); b3=$(busy)
sleep 1
errors=$(adb logcat -d -s warden:V | grep -ac "warden  : e>")
adb logcat -d -v epoch -s warden:V | grep -a "GPU swaps" | sed 's/^ *\([0-9]*\)\.[0-9]* .*GPU swaps \([0-9]*\).*/\1 \2/' > "build/areas/$LABEL-swaps.txt"
rates=$(python - "$t_stand0" "$t_stand1" "$t_walk0" "$t_walk1" "$t_walk2" "$t_walk3" "build/areas/$LABEL-swaps.txt" <<'EOF'
import sys
t = [float(x) for x in sys.argv[1:7]]
rows = [tuple(map(float, l.split())) for l in open(sys.argv[7]) if len(l.split()) == 2]
def rate(a, b):
    r = [(rows[i][1] - rows[i-1][1]) / (rows[i][0] - rows[i-1][0]) for i in range(1, len(rows))
         if rows[i][0] > a and rows[i-1][0] < b and rows[i][0] > rows[i-1][0]]
    return f"{sum(r)/len(r):.1f}" if r else "?"
print(f"standing {rate(t[0], t[1])} fps | walk {rate(t[2], t[3])} fps | walk2 {rate(t[4], t[5])} fps")
EOF
)
echo "$LABEL | $rim | $rates | busy $b0,$b1,$b2,$b3 | talk $talk | errors $errors" | tee -a build/areas/results.txt
