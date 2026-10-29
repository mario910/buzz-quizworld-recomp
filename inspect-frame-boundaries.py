"""Stream a trace and audit whether committed flips became host presents.

This is a count/serial correlation, not proof that every pixel is correct.
Use the renderer capture and UI as separate checks of frame completeness.
"""
import argparse
import collections
import json

GATES = {1: 'strict_fifo_boundary', 2: 'legacy_fifo_boundary', 3: 'catchup_skip',
         4: 'host_decision', 5: 'clear_present', 6: 'small_batch_hold',
         7: 'ticker_suppressed', 8: 'offscreen_execution', 9: 'empty_batch'}


def inspect(stream):
    counts = collections.Counter()
    serial_gaps = collections.Counter()
    last_serial = None
    minimum = None
    maximum = 0
    header = ''
    for line in stream:
        if line.startswith('#'):
            header = line.strip()
        fields = line.split()
        if len(fields) != 6 or not fields[0].isdigit():
            continue
        kind, who = map(int, fields[:2])
        a, b = (int(value, 16) for value in fields[2:4])
        if kind in (8, 9, 10):
            counts[{8: 'fifo_flips', 9: 'flip_requests', 10: 'host_presents'}[kind]] += 1
        if kind != 25:
            continue
        counts[GATES.get(who, 'unknown_gate')] += 1
        if who == 4:
            if last_serial is not None:
                serial_gaps[(a-last_serial) & 0xffffffff] += 1
            last_serial = a
            minimum = b if minimum is None else min(minimum, b)
            maximum = max(maximum, b)
    return {'header': header, 'counts': dict(sorted(counts.items())),
            'host_decision_serial_gaps': dict(sorted(serial_gaps.items())),
            'host_decision_draw_range': [minimum, maximum]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace')
    args = parser.parse_args()
    with open(args.trace) as stream:
        print(json.dumps(inspect(stream), indent=2))
