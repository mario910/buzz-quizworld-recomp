"""Frame-time spread of BUZZ_RENDER_PERF CSVs over one window (seconds from
the CSV epoch), for capped runs: FPS, interval SD, share of intervals within
+-2 ms of 33.3 ms, and how many presents would span other than two 60 Hz
refreshes (interval nearer 16.7 or 50 ms than 33.3 ms).
  frame-pacing-stats.py 160:240 a.csv b.csv"""

import csv
import statistics
import sys

lo, hi = map(float, sys.argv[1].split(":"))
for path in sys.argv[2:]:
    rows = [r for r in csv.DictReader(open(path, newline=""))
            if lo * 1000 <= float(r["elapsed_ms"]) < hi * 1000]
    iv = [float(r["interval_ms"]) for r in rows]
    if len(iv) < 2:
        print(f"{path}: no frames")
        continue
    span = (float(rows[-1]["elapsed_ms"]) - float(rows[0]["elapsed_ms"])) / 1000
    near = sum(abs(v - 1000 / 30) <= 2 for v in iv)
    off = sum(v < 25 or v > 41.7 for v in iv)
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    print(f"{name:32} fps={(len(iv) - 1) / span:6.2f} mean={statistics.mean(iv):6.2f} sd={statistics.pstdev(iv):5.2f} "
          f"within2ms={100 * near / len(iv):5.1f}% off-cadence={100 * off / len(iv):4.1f}%")
