"""Summarize native chain waits/lists; elapsed spans are not CPU usage."""
import collections
import re
import statistics
import sys


def main(path):
    groups = collections.defaultdict(list)
    flips = []
    with open(path) as stream:
        header = stream.readline().strip()
        lo, hi = map(float, re.search(r'start_us=([\d.]+) end_us=([\d.]+)', header).groups())
        for line in stream:
            fields = line.split()
            if len(fields) != 6 or not fields[0].isdigit():
                continue
            kind, who = map(int, fields[:2])
            a, b = (int(value, 16) for value in fields[2:4])
            start, end = max(lo, float(fields[4])), min(hi, float(fields[5]))
            if kind == 9 and lo <= start < hi:
                flips.append(start)
            if kind in (18, 19, 20, 21) and end > start:
                groups[kind, a].append((end - start, b))
    frames = max(1, len(flips) - 1)
    print(header)
    print(f'{frames} frame intervals; window={(hi-lo)/1e6:.3f}s')
    for (kind, chain), records in sorted(groups.items()):
        label = {18:'JTS wait', 19:'guard wait', 20:'parallel list', 21:'serial list'}[kind]
        durations = [r[0] for r in records]
        extra = f' jobs/list={statistics.mean(r[1] for r in records):.1f}' if kind >= 20 else ''
        print(f'{chain:08X} {label:14s} calls={len(records):6d} '
              f'sum={sum(durations)/1e6:7.3f}s '
              f'mean/frame={sum(durations)/frames/1000:7.2f}ms '
              f'median={statistics.median(durations)/1000:7.3f}ms{extra}')
    print('Overlapping chain/PPU/job spans must not be added as CPU time or avoidable delay.')


if __name__ == '__main__':
    main(sys.argv[1])
