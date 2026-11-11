"""Per-thread CPU use from perf/android-supervisor.sh task snapshots.

  android-threads.py <run dir> [--from S] [--to S]

Uses the first and last snapshot inside the window (seconds after the first
snapshot) and prints each thread's CPU% of one core, its last CPU, and the
process total. Clock ticks are 100 Hz on Android.
"""
import argparse, collections, glob, os, re

p = argparse.ArgumentParser()
p.add_argument("run")
p.add_argument("--from", dest="start", type=float, default=0)
p.add_argument("--to", dest="end", type=float, default=1e9)
a = p.parse_args()
snaps = sorted((float(os.path.basename(f)[6:-4]), f)
               for f in glob.glob(os.path.join(a.run, "**", "tasks-*.txt"), recursive=True))
t0 = snaps[0][0]
snaps = [s for s in snaps if a.start <= s[0] - t0 <= a.end and os.path.getsize(s[1])]
if len(snaps) < 2: raise SystemExit('no task snapshots in that window (run stopped early?)')
(ta, fa), (tb, fb) = snaps[0], snaps[-1]

def parse(f):
    out = {}
    for line in open(f, errors="replace"):
        m = re.match(r"(\d+) \((.*)\) (.*)", line.strip())
        if not m: continue
        rest = m[3].split()
        out[m[1]] = (m[2], int(rest[11]) + int(rest[12]), rest[36], rest[0])
    return out

A, B = parse(fa), parse(fb)
dt = tb - ta
rows = []
for tid, (name, ticks, cpu, state) in B.items():
    before = A.get(tid, (name, 0))[1]
    rows.append(((ticks - before) / dt, tid, name, cpu, state))
rows.sort(reverse=True)
print(f"window {ta - t0:.1f}-{tb - t0:.1f} s ({dt:.1f} s), {len(B)} threads")
total = 0
for pct, tid, name, cpu, state in rows:
    total += pct
    if pct >= 1: print(f"{pct:6.1f}%  cpu{cpu} {state} {tid:>6} {name}")
print(f"total {total:.1f}% of one core")
