"""Stream a trace, keeping completed host presents only (no guest-flip FPS).

python tools/present_phase_report.py trace ... --phase 14 20
Phase times are relative to the trace header, not the first frame. Camera
alignment must still be checked when comparing a different menu script.
"""
import argparse
import bisect
import json
import re
import statistics
from pathlib import Path


def read_presents(path):
    start = end = None
    dropped = None
    presents = []
    with open(path, encoding="utf-8") as stream:
        for line in stream:
            if line.startswith("# ps3_ftrace "):
                fields = dict(re.findall(r"(\w+)=([\d.]+)", line))
                start, end = float(fields["start_us"]), float(fields["end_us"])
                dropped = int(fields["dropped"])
            elif line.startswith("10 "):
                presents.append(float(line.split()[5]))  # completed, not begun
    if start is None or end <= start:
        raise ValueError(f"Missing/invalid trace window: {path}")
    presents = sorted(t for t in presents if start <= t < end)
    return start, end, dropped, presents


def interval_stats(presents):
    gaps = sorted((b - a) / 1000 for a, b in zip(presents, presents[1:]))
    if not gaps:
        return None
    def percentile(fraction):
        x = (len(gaps) - 1) * fraction
        lo = int(x)
        hi = min(lo + 1, len(gaps) - 1)
        return gaps[lo] + (gaps[hi] - gaps[lo]) * (x - lo)
    return {"mean_ms": statistics.mean(gaps), "p95_ms": percentile(.95),
            "p99_ms": percentile(.99), "max_ms": gaps[-1]}


def report(path, phase):
    start, end, dropped, presents = read_presents(path)
    low, high = (start + seconds * 1e6 for seconds in phase)
    if not start <= low < high <= end:
        raise ValueError("Phase must lie inside the trace window")
    selected = presents[bisect.bisect_left(presents, low):bisect.bisect_left(presents, high)]
    seconds = (end - start) / 1e6
    counts = [bisect.bisect_left(presents, start + (s + 1) * 1e6)
              - bisect.bisect_left(presents, start + s * 1e6)
              for s in range(int(seconds))]
    return {"trace": Path(path).name, "dropped": dropped,
            "fps": len(presents) / seconds, "per_second": counts,
            "phase_seconds": phase, "phase_fps": len(selected) / (phase[1] - phase[0]),
            "pacing": interval_stats(presents), "phase_pacing": interval_stats(selected)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", nargs="+")
    parser.add_argument("--phase", nargs=2, type=float, default=[14, 20])
    args = parser.parse_args()
    for trace in args.traces:
        print(json.dumps(report(trace, args.phase), ensure_ascii=False))
