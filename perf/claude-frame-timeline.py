"""Dump a condensed timeline of one present-to-present frame from a PS3_FTRACE text trace.
usage: frame_timeline.py trace frame_index [frame_count]
Read-only; prints wall-clock spans (us, relative to frame start). Not CPU time."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import sys, collections

path, fi = sys.argv[1], int(sys.argv[2])
fc = int(sys.argv[3]) if len(sys.argv) > 3 else 1
spans = []
with open(path) as f:
    for line in f:
        if line[0] in "#T":
            continue
        k, who, a, b, t0, t1 = line.split()
        spans.append((int(k), int(who), int(a, 16), int(b, 16), float(t0) / 1e3, float(t1) / 1e3))
spans.sort(key=lambda s: s[4])
pres = [s[4] for s in spans if s[0] == 10]
w0, w1 = pres[fi], pres[fi + fc]
print(f"frame {fi}..{fi+fc} window {w0/1000:.3f}..{w1/1000:.3f} ms len {(w1-w0):.2f} ms")

NAMES = {1: "SYSCALL", 2: "USLEEP", 7: "DRAIN", 9: "FLIPREQ", 10: "PRESENT", 15: "HLE",
         18: "JTS", 19: "GUARD", 20: "POOL", 21: "LIST", 25: "GATE"}
jobs = collections.defaultdict(list)   # native tid -> [(t0,t1,image)]
rows = []
for k, who, a, b, t0, t1 in spans:
    if t1 < w0 or t0 > w1:
        continue
    if k == 16:
        jobs[who].append((t0, t1, a))
        continue
    if k in (22, 23, 24, 3, 4):
        continue
    if k in NAMES:
        rows.append((t0, t1, NAMES[k], who, a, b))

# merge job spans per native thread into busy runs (gap < 15 us)
for tid, lst in jobs.items():
    lst.sort()
    cur = None
    for t0, t1, img in lst:
        if cur and t0 - cur[1] < 0.015:
            cur[1] = max(cur[1], t1); cur[2][img] += 1
        else:
            if cur: rows.append((cur[0], cur[1], "JOBS", tid, 0, dict(cur[2])))
            cur = [t0, t1, collections.Counter({img: 1})]
    if cur: rows.append((cur[0], cur[1], "JOBS", tid, 0, dict(cur[2])))

rows.sort(key=lambda r: r[0])
for t0, t1, n, who, a, b in rows:
    d = t1 - t0
    if n in ("SYSCALL", "USLEEP", "HLE") and d < 0.05:
        continue
    if n == "JOBS" and d < 0.03:
        continue
    extra = f"a={a:x} b={b:x}" if not isinstance(b, dict) else f"imgs={b}"
    print(f"{t0-w0:8.3f} {t1-w0:8.3f} {d:7.3f}  {n:7} who={who:<6} {extra}")
