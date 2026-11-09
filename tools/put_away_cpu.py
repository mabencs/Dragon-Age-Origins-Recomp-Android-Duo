"""What the app costs while put away: per-thread CPU, from the device's /proc (BUG-058, BUG-119).

    python tools/put_away_cpu.py threads <package> <seconds> [label]
        Each thread's share of one core over <seconds>, busiest first, and the process's
        total. Take it playing, then again with the device locked
        (adb shell input keyevent KEYCODE_SLEEP; give it 15 s to settle).

    python tools/put_away_cpu.py cycles <package> <count>
        <count> times: wake and unlock, 15 s in front, lock, 12 s to settle, then the
        process's and its render thread's CPU over 20 s. Start it in play - the render
        thread's spin (BUG-119) only shows there, and only in some lock-ups.

Over USB the device never suspends (the USB controller holds a wakeup source), so these
are the costs while it is awake: the moments it wakes by itself with the lid shut, and
every moment with the app behind another. Uses the first USB device (adb -d).
"""
import collections
import subprocess
import sys
import time


def sh(cmd):
    return subprocess.run(['adb', '-d', 'shell', cmd], capture_output=True, text=True,
                          timeout=60).stdout


def snapshot(pid):
    """{tid: (name, utime + stime in ticks)} and /proc/stat's cpu line."""
    out = sh(f'for t in /proc/{pid}/task/*; do echo "$(basename $t) $(cat $t/stat)"; done; '
             'echo SYS $(head -1 /proc/stat)')
    threads, cpu = {}, None
    for line in out.splitlines():
        if line.startswith('SYS '):
            cpu = [int(x) for x in line.split()[2:]]
            continue
        tid, rest = line.split(' ', 1)
        name = rest[rest.index('(') + 1:rest.rindex(')')]
        fields = rest[rest.rindex(')') + 2:].split()
        threads[tid] = (name, int(fields[11]) + int(fields[12]))
    return time.time(), threads, cpu


def threads(pkg, seconds, label):
    pid = sh(f'pidof {pkg}').strip()
    if not pid:
        sys.exit(f'{pkg} is not running')
    t0, a, s0 = snapshot(pid)
    time.sleep(seconds)
    t1, b, s1 = snapshot(pid)
    wall = t1 - t0
    used = collections.Counter()
    for tid, (name, ticks) in b.items():
        delta = ticks - a.get(tid, (name, 0))[1]
        if delta:
            used[tid] = delta
    busy = (sum(s1) - sum(s0)) - ((s1[3] + s1[4]) - (s0[3] + s0[4]))  # less idle, iowait
    print(f'{label} {pkg} pid {pid}: {wall:.0f} s, {len(b)} threads; process '
          f'{sum(used.values()) / wall:.1f}% of one core; the device {busy / wall:.1f}%')
    for tid, delta in used.most_common(15):
        print(f'  {b[tid][0]:<18} {tid:>6} {delta / wall:6.1f}%')


def cycles(pkg, count):
    pid = sh(f'pidof {pkg}').strip()
    if not pid:
        sys.exit(f'{pkg} is not running')

    def ticks():
        _, threads_now, _ = snapshot(pid)
        total = sum(t for _, t in threads_now.values())
        render = sum(t for n, t in threads_now.values() if n.startswith('DragonAge::Rend'))
        return time.time(), total, render

    for i in range(count):
        sh('input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard')
        time.sleep(15)
        sh('input keyevent KEYCODE_SLEEP')
        time.sleep(12)
        t0, a0, r0 = ticks()
        time.sleep(20)
        t1, a1, r1 = ticks()
        wall = t1 - t0
        print(f'cycle {i + 1}: locked, process {(a1 - a0) / wall:.1f}%, render thread '
              f'{(r1 - r0) / wall:.1f}% of one core', flush=True)


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'threads':
        threads(sys.argv[2], float(sys.argv[3]), sys.argv[4] if len(sys.argv) > 4 else '')
    elif len(sys.argv) == 4 and sys.argv[1] == 'cycles':
        cycles(sys.argv[2], int(sys.argv[3]))
    else:
        sys.exit(__doc__)
