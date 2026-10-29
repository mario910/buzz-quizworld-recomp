"""Do JTS publication waits end on the host 1 ms timer tick (timeout) or on a wake?
Tick grid = ends of main-thread event_flag_wait spans (syscall 0x55) that lasted ~1/2 ms,
which can only be timeouts. Read-only; trace times are us."""
import sys, bisect, collections

path = sys.argv[1]
ticks, jts, frames = [], [], []
with open(path) as f:
    for line in f:
        if line[0] in "#T":
            continue
        k, who, a, b, t0, t1 = line.split()
        k = int(k); t0 = float(t0); t1 = float(t1)
        if k == 1 and who == "1" and int(a, 16) == 0x55:
            d = t1 - t0
            if 950 <= d <= 1050 or 1950 <= d <= 2050:
                ticks.append(t1)
        elif k == 18:
            jts.append((t0, t1, int(a, 16)))
        elif k == 10:
            frames.append(t0)
ticks.sort()
nframes = len(frames) - 1
print(f"main timeout ticks={len(ticks)} jts spans={len(jts)} frames={nframes}")

def near_tick(t):
    i = bisect.bisect_left(ticks, t)
    best = min((abs(ticks[j] - t) for j in (i - 1, i) if 0 <= j < len(ticks)), default=1e9)
    return best

buckets = collections.Counter()
tot = collections.Counter()
for t0, t1, chain in jts:
    d = t1 - t0
    dist = near_tick(t1)
    on = dist <= 25
    key = ("tick" if on else "off") + ("<100us" if d < 100 else ">=100us")
    buckets[key] += 1
    tot[key] += d
    tot[("chain", chain, on)] += d
for k in sorted(buckets):
    print(f"{k:12} n={buckets[k]:7} total={tot[k]/1000/nframes:7.2f} ms/frame")
for k, v in sorted((k, v) for k, v in tot.items() if isinstance(k, tuple)):
    print(f"chain {k[1]:08X} {'tick-aligned' if k[2] else 'woken/other'}: {v/1000/nframes:6.2f} ms/frame")
# duration histogram of off-tick long waits
hist = collections.Counter(min(int((t1 - t0) // 100), 20) for t0, t1, c in jts
                           if t1 - t0 >= 100 and near_tick(t1) > 25)
print("off-tick >=100us duration histogram (100us bins):", dict(sorted(hist.items())))
