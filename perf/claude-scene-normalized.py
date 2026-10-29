"""Frame interval vs image41 jobs in that frame, so variants are compared on equal
scene load (the menu camera tour changes geometry over wall time). Diagnostic only.
usage: perf/claude-scene-normalized.py bin_jobs trace...  (times in trace are us)"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import bisect, collections, statistics, sys

width = int(sys.argv[1])
for path in sys.argv[2:]:
    presents, jobs = [], []
    with open(path) as f:
        for line in f:
            if line[0] in "#T":
                continue
            k, who, a, b, t0, t1 = line.split()
            if k == "10":
                presents.append(float(t0))
            elif k == "16" and a == "29":          # image 41 (hex 0x29)
                jobs.append(float(t0))
    presents.sort(); jobs.sort()
    bins = collections.defaultdict(list)
    for p0, p1 in zip(presents, presents[1:]):
        n = bisect.bisect_left(jobs, p1) - bisect.bisect_left(jobs, p0)
        bins[n // width * width].append((p1 - p0) / 1000)
    print(path.split("\\")[-1].split("/")[-1])
    for b in sorted(bins):
        v = bins[b]
        if len(v) >= 10:
            print(f"  jobs {b:5}-{b+width-1:<5} frames={len(v):4} median={statistics.median(v):6.2f} ms")
