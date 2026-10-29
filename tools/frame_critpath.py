"""Per-frame critical-path statistics from a PS3_FTRACE trace (times in ms).
    python tools/frame_critpath.py trace [pool_walker_tid] [serial_walker_tid]
Workers are the threads with the most SPU_JOB spans (the image41 pool)."""
import collections, sys
f = sys.argv[1]
spans = []
for line in open(f):
    if line[0] in "#T": continue
    k, who, a, b, t0, t1 = line.split()
    spans.append((int(k), who, int(a, 16), int(b, 16), float(t0) / 1e3, float(t1) / 1e3))
cnt = collections.Counter(s[1] for s in spans if s[0] == 16)
workers = {t for t, _ in cnt.most_common(4)}
pres = sorted(s[4] for s in spans if s[0] == 10)
def union(iv):
    iv = sorted(i for i in iv if i[1] > i[0]); tot = 0; cur = None
    for a, b in iv:
        if cur and a <= cur[1]: cur[1] = max(cur[1], b)
        else:
            if cur: tot += cur[1] - cur[0]
            cur = [a, b]
    if cur: tot += cur[1] - cur[0]
    return tot
rows = []
for i in range(10, len(pres) - 2):
    w0, w1 = pres[i], pres[i + 1]
    clip = lambda a, b: (max(a, w0), min(b, w1))
    win = [s for s in spans if s[5] > w0 and s[4] < w1]
    r = {"len": w1 - w0}
    r["pool41"] = union([clip(s[4], s[5]) for s in win if s[0] == 16 and s[1] in workers])
    r["anyjob"] = union([clip(s[4], s[5]) for s in win if s[0] == 16])
    r["mainsys"] = sum(clip(s[4], s[5])[1] - clip(s[4], s[5])[0] for s in win if s[1] == "1" and s[0] == 1)
    r["main55"] = sum(clip(s[4], s[5])[1] - clip(s[4], s[5])[0] for s in win if s[1] == "1" and s[0] == 1 and s[2] == 0x55)
    r["jts"] = union([clip(s[4], s[5]) for s in win if s[0] == 18])
    r["drain"] = union([clip(s[4], s[5]) for s in win if s[0] == 7])
    r["jobs_or_drain"] = union([clip(s[4], s[5]) for s in win if s[0] in (16, 7)])
    rows.append(r)
n = len(rows)
for key in rows[0]:
    print("%-14s %7.2f ms" % (key, sum(r[key] for r in rows) / n))
