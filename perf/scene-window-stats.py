"""Frame-time summary of BUZZ_RENDER_PERF CSVs over one window (seconds from
the CSV epoch): presented FPS, median/p90/p99 interval, mean FIFO drain.
  scene-window-stats.py 150:250 a.csv b.csv ...
  scene-window-stats.py --per 10 140:250 a.csv   (also FPS/drain per slice)"""

import csv
import statistics
import sys

args = sys.argv[1:]
per = 0
if args[:1] == ["--per"]:
    per, args = float(args[1]), args[2:]
lo, hi = map(float, args[0].split(":"))
for path in args[1:]:
    rows = [r for r in csv.DictReader(open(path, newline=""))
            if lo * 1000 <= float(r["elapsed_ms"]) < hi * 1000]
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    if len(rows) < 2:
        print(f"{name}: no frames in {lo}..{hi} s")
        continue
    iv = sorted(float(r["interval_ms"]) for r in rows)
    pick = lambda q: iv[min(len(iv) - 1, int(q * (len(iv) - 1)))]
    span = (float(rows[-1]["elapsed_ms"]) - float(rows[0]["elapsed_ms"])) / 1000
    drain = statistics.mean(float(r["drain_ms"]) for r in rows)
    print(f"{name:32} n={len(iv):5} fps={(len(iv) - 1) / span:6.2f} median={statistics.median(iv):6.2f} "
          f"p90={pick(.9):6.2f} p99={pick(.99):6.2f} drain={drain:6.2f} >40ms={100 * sum(t > 40 for t in iv) / len(iv):5.1f}%")
    if per:
        cells, t = [], lo
        while t < hi:
            s = [r for r in rows if t * 1000 <= float(r["elapsed_ms"]) < (t + per) * 1000]
            cells.append(f"{len(s) / per:4.1f}/{statistics.mean(float(r['drain_ms']) for r in s):4.1f}" if s else "   -")
            t += per
        print("    fps/drain per slice:", " ".join(cells))
