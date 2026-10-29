"""Correlate actual jobs/lists with the main PPU wait, not summed CPU spans."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
import bisect
import collections
import statistics
import sys


def union(spans):
    out = []
    for lo, hi in sorted(spans):
        if hi <= lo:
            continue
        if out and lo <= out[-1][1]:
            out[-1] = (out[-1][0], max(hi, out[-1][1]))
        else:
            out.append((lo, hi))
    return out


def overlap(a, b):
    i = j = 0
    total = 0
    while i < len(a) and j < len(b):
        total += max(0, min(a[i][1], b[j][1]) - max(a[i][0], b[j][0]))
        if a[i][1] < b[j][1]:
            i += 1
        else:
            j += 1
    return total


def subtract(a, b):
    """Subtract unions of half-open intervals without counting overlaps twice."""
    a, b = union(a), union(b)
    out = []
    j = 0
    for lo, hi in a:
        while j < len(b) and b[j][1] <= lo:
            j += 1
        cursor, k = lo, j
        while k < len(b) and b[k][0] < hi:
            if b[k][0] > cursor:
                out.append((cursor, min(hi, b[k][0])))
            cursor = max(cursor, b[k][1])
            if cursor >= hi:
                break
            k += 1
        if cursor < hi:
            out.append((cursor, hi))
    return out


def elapsed(spans):
    return sum(hi - lo for lo, hi in union(spans))


def main(path):
    records = []
    with open(path) as stream:
        print(stream.readline().strip())
        for line in stream:
            f = line.split()
            if len(f) == 6 and f[0].isdigit():
                k, tid = map(int, f[:2])
                a, b = (int(x, 16) for x in f[2:4])
                lo, hi = map(float, f[4:])
                records.append((k, tid, a, b, lo, hi))
    flips = sorted(r[4] for r in records if r[0] == 9)
    if len(flips) < 2:
        raise ValueError("No frame intervals")
    window = [(flips[0], flips[-1])]
    frames = len(flips) - 1
    waits = union((r[4], r[5]) for r in records if r[0] == 1 and r[1] == 1 and r[3] == 0x2521F0)
    waits = [(max(lo, window[0][0]), min(hi, window[0][1])) for lo, hi in waits]
    waits = union(waits)
    print(f"{frames} frame intervals; main completion wait={overlap(waits, window)/frames/1000:.2f} ms/frame")
    # A main wait is NOT automatically idle time or time spent on image41.
    # Account for every instrumented image before looking for the missing gap.
    all_jobs = union((r[4], r[5]) for r in records if r[0] == 16)
    uncovered = subtract(waits, all_jobs)
    print(f"main wait covered by ANY instrumented job: {overlap(waits, all_jobs)/frames/1000:.2f} ms/frame; "
          f"not covered: {elapsed(uncovered)/frames/1000:.2f} ms/frame")
    print("Uncovered wait overlaps (NON-additive, not necessarily idle):")
    for kind, label in ((7, 'RSX FIFO drain'), (18, 'JTS publication wait'),
                        (19, 'guard wait'), (20, 'parallel list'), (21, 'serial list')):
        spans = union((r[4], r[5]) for r in records if r[0] == kind)
        print(f"  {label}: {overlap(uncovered, spans)/frames/1000:.2f} ms/frame")
    for chain in sorted({r[2] for r in records if r[0] == 18}):
        spans = union((r[4], r[5]) for r in records if r[0] == 18 and r[2] == chain)
        print(f"  JTS chain {chain:08X}: {overlap(uncovered, spans)/frames/1000:.2f} ms/frame (correlation only)")
    gaps = sorted((hi - lo for lo, hi in uncovered))
    if gaps:
        print(f"Uncovered segments: n={len(gaps)} median={statistics.median(gaps):.1f} us "
              f"p95={gaps[int(.95*len(gaps))]:.1f} us max={gaps[-1]:.1f} us")
    print("All instrumented images overlapping the main wait:")
    for image in sorted({r[2] for r in records if r[0] == 16}):
        spans = union((r[4], r[5]) for r in records if r[0] == 16 and r[2] == image)
        covered = overlap(waits, spans)
        if covered:
            print(f"  image {image}: {covered/frames/1000:.2f} ms/frame")
    for image in (40, 41, 39, 36):
        jobs = [r for r in records if r[0] == 16 and r[2] == image]
        intervals = union((r[4], r[5]) for r in jobs)
        busy = sum(max(0, min(r[5], window[0][1])-max(r[4], window[0][0])) for r in jobs)
        occupied = overlap(intervals, window)
        blocked = overlap(waits, intervals)
        print(f"image {image}: jobs={len(jobs)} elapsed/job={statistics.median(r[5]-r[4] for r in jobs):.1f} us "
              f"sum={busy/frames/1000:.2f} ms/frame union={occupied/frames/1000:.2f} "
              f"effective concurrency={busy/occupied if occupied else 0:.2f}; main wait overlap={blocked/frames/1000:.2f}")
    geometry = sorted((r[4], r[5]) for r in records if r[0] == 16 and r[2] == 41)
    starts = [r[0] for r in geometry]
    samples = collections.defaultdict(list)
    for r in records:
        if r[0] != 20 or r[4] < window[0][0] or r[5] > window[0][1]:
            continue
        begin, end = r[4:]
        lo = bisect.bisect_left(starts, begin)
        hi = bisect.bisect_right(starts, end)
        inside = [(a, b) for a, b in geometry[lo:hi] if b <= end]
        if not inside:
            continue
        samples['count'].append(len(inside))
        samples['head'].append(inside[0][0] - begin)
        samples['tail'].append(end - max(b for a, b in inside))
        samples['holes'].append((end-begin) - sum(b-a for a, b in union(inside)))
        samples['duration'].append(end-begin)
    if samples:
        print(f"Parallel lists matched={len(samples['count'])}; mean jobs/list={statistics.mean(samples['count']):.2f}")
        for label in ('head', 'tail', 'holes', 'duration'):
            values = samples[label]
            print(f"  {label}: median={statistics.median(values):.1f} us p95={sorted(values)[int(.95*len(values))]:.1f} us sum/frame={sum(values)/frames/1000:.2f} ms")
    commands = collections.defaultdict(list)
    for k, tid, chain, cmd, lo, hi in records:
        if k == 22:
            commands[chain].append((lo, cmd if cmd & 7 == 7 else cmd & 7))
    labels = {0:'JOB/NOP', 1:'RESET', 2:'SYNC', 3:'NEXT', 4:'CALL', 5:'FLUSH', 6:'LIST', 15:'GUARD', 119:'RET', 127:'END'}
    for chain, entries in sorted(commands.items()):
        ops = [op for _, op in sorted(entries)]
        print(f"chain {chain:08X} commands={len(ops)} transitions:")
        for pair, n in collections.Counter(zip(ops, ops[1:])).most_common(10):
            print(f"  {labels.get(pair[0],str(pair[0]))} -> {labels.get(pair[1],str(pair[1]))}: {n}")
    for kind, label in ((23, 'pool enqueue -> first claim'), (24, 'pool last done -> owner return')):
        times = [r[5]-r[4] for r in records if r[0] == kind and r[4] >= window[0][0] and r[5] <= window[0][1]]
        if times:
            print(f"{label}: median={statistics.median(times):.1f} us p95={sorted(times)[int(.95*len(times))]:.1f} us sum/frame={sum(times)/frames/1000:.2f} ms")
    print("Overlaps are not additive. Concurrent list attribution is approximate without per-batch IDs.")
    print("Short jobs filtered by PS3_FTRACE_MIN_US (unless SPURS_JOB_TRACE_ALL=1), taskset/video work absent from kind16, and hook overhead remain outside this attribution.")


if __name__ == '__main__':
    main(sys.argv[1])
