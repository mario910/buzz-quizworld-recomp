"""Post-optimization SPURS dependency diagnostics, NOT summed CPU usage.

Uses completed host presents. Overlap/exposure and nearest-job tails are
correlations, not proofs that an arbitrary job caused a particular wake.
SPURS_JOB_TRACE_ALL=1 is required for meaningful short-job coverage.
"""
import argparse
import bisect
import collections
import importlib.util
from pathlib import Path
import statistics
import re

spec = importlib.util.spec_from_file_location("intervals", Path(__file__).resolve().parents[1] / "inspect-geometry-dependencies.py")
intervals = importlib.util.module_from_spec(spec)
spec.loader.exec_module(intervals)
union, subtract = intervals.union, intervals.subtract


class Coverage:
    """Integral of a disjoint union, so nested spans are never counted twice."""
    def __init__(self, spans):
        self.spans = union(spans)
        self.starts = [a for a, _ in self.spans]
        self.ends = [b for _, b in self.spans]
        self.prefix = [0]
        for a, b in self.spans:
            self.prefix.append(self.prefix[-1] + b - a)

    def until(self, time):
        index = bisect.bisect_right(self.ends, time)
        return self.prefix[index] + (max(0, time - self.starts[index]) if index < len(self.spans) else 0)

    def measure(self, lo, hi):
        return max(0, self.until(hi) - self.until(lo))


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return 0
    at = (len(values) - 1) * fraction
    low = int(at)
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (at - low)


def read(path):
    records, presents, header = [], [], ""
    keep = {1, 5, 7, 16, 18, 19, 20, 21, 23, 24, 27}
    with open(path, encoding="utf-8") as stream:
        for line in stream:
            if line.startswith("#"):
                header = line.strip()
            elif line.startswith("10 "):
                presents.append(float(line.split()[5]))
            elif line and line[0].isdigit():
                fields = line.split()
                kind = int(fields[0])
                if kind in keep:
                    records.append((kind, int(fields[1]), int(fields[2], 16), int(fields[3], 16), float(fields[4]), float(fields[5])))
    window = re.search(r"start_us=([\d.]+) end_us=([\d.]+)", header)
    if window:
        lo, hi = map(float, window.groups())
        presents = [p for p in presents if lo <= p < hi]
    return header, records, sorted(presents)


def report(path, worst=3):
    header, rows, presents = read(path)
    if len(presents) < 2:
        raise ValueError("Need at least two completed presents")
    lo, hi = presents[0], presents[-1]
    frames = list(zip(presents, presents[1:]))
    count = len(frames)
    selected = lambda kind: [(r[4], r[5]) for r in rows if r[0] == kind]
    jobs = [r for r in rows if r[0] == 16]
    by_image = collections.defaultdict(list)
    for r in jobs:
        by_image[r[2]].append((r[4], r[5]))
    all_jobs = Coverage(selected(16))
    geometry = Coverage(by_image[41])
    main = Coverage((r[4], r[5]) for r in rows if r[0] == 1 and r[1] == 1 and r[2] == 0x55)
    uncovered = Coverage(subtract(main.spans, all_jobs.spans))
    coverages = {"any job": all_jobs, "image41 union": geometry,
                 "main flag wait": main, "main wait without job": uncovered,
                 "RSX drain": Coverage(selected(7)), "RSX acquire": Coverage(selected(5))}
    for chain in sorted({r[2] for r in rows if r[0] in (18, 19, 20, 21)}):
        for kind, label in ((18, "JTS"), (19, "guard"), (20, "pool"), (21, "serial")):
            spans = [(r[4], r[5]) for r in rows if r[0] == kind and r[2] == chain]
            if spans:
                coverages[f"{chain:08X} {label}"] = Coverage(spans)
    print(header)
    print(f"{count} completed-present intervals; mean={(hi-lo)/count/1000:.3f} ms; "
          f"p95={percentile([b-a for a,b in frames], .95)/1000:.3f} ms")
    print("Union coverage (overlapping rows are NON-additive), ms/frame:")
    for name, coverage in coverages.items():
        print(f"  {name:27} {coverage.measure(lo,hi)/count/1000:8.3f}")
    print("Jobs: mean elapsed; union/frame; exposure while image41 is idle (not automatically critical):")
    for image, spans in sorted(by_image.items()):
        coverage = Coverage(spans)
        exposed = Coverage(subtract(coverage.spans, geometry.spans))
        durations = [b-a for a,b in spans if lo <= a and b <= hi]
        if durations:
            print(f"  image {image:2}: n={len(durations):7} mean={statistics.mean(durations):8.2f} us "
                  f"union={coverage.measure(lo,hi)/count/1000:7.3f} ms "
                  f"exposed={exposed.measure(lo,hi)/count/1000:7.3f} ms")
    print("Pool transition latencies (not additive CPU time):")
    for kind, name in ((23, "enqueue -> first claim"), (24, "last done -> owner return")):
        durations = [r[5]-r[4] for r in rows if r[0] == kind and lo <= r[4] and r[5] <= hi]
        if durations:
            print(f"  {name}: n={len(durations)} mean={statistics.mean(durations):.2f} us "
                  f"p95={percentile(durations,.95):.2f} us max={max(durations):.2f} us "
                  f"sum/frame={sum(durations)/count/1000:.3f} ms")
    endings = sorted((r[5], r[2]) for r in jobs)
    handoffs = [r for r in rows if r[0] == 27 and lo <= r[4] and r[5] <= hi]
    if handoffs:
        observed = sum(r[3] != 0 for r in handoffs)
        duration = sum(r[5]-r[4] for r in handoffs)
        print(f"Last-worker handoff: {len(handoffs)} waits, {observed} observed publications "
              f"({100*observed/len(handoffs):.1f}%), elapsed sum/frame={duration/count/1000:.3f} ms "
              "(elapsed, not measured CPU)")
    end_times = [r[0] for r in endings]
    tails = collections.defaultdict(list)
    for begin, end in main.spans:
        if begin < lo or end > hi:
            continue
        index = bisect.bisect_right(end_times, end) - 1
        if index >= 0 and endings[index][0] >= begin:
            tails[endings[index][1]].append(end - endings[index][0])
    print("Nearest job before main wait ends (correlation only):")
    for image, values in sorted(tails.items(), key=lambda kv: -len(kv[1])):
        print(f"  image {image}: n={len(values)} gap median={statistics.median(values):.2f} us "
              f"p95={percentile(values,.95):.2f} us")
    print("Worst completed-present intervals:")
    for begin, end in sorted(frames, key=lambda pair: pair[1]-pair[0], reverse=True)[:worst]:
        print(f"  {begin/1e6:.6f}..{end/1e6:.6f}s {(end-begin)/1000:.3f} ms")
        for name, coverage in coverages.items():
            value = coverage.measure(begin, end) / 1000
            if value > .2:
                print(f"    {name:27} {value:8.3f}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace")
    parser.add_argument("--worst", type=int, default=3)
    args = parser.parse_args()
    report(args.trace, args.worst)
