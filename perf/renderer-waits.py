#!/usr/bin/env python3
"""Attribute CPU-observed renderer waits in a BUZZ_RENDERER_WAIT_TRACE capture.

Usage: python perf/renderer-waits.py capture.trace [--output summary.json]
Use complete executed FIFO flip intervals. Nested spans are reported separately,
never added to drain/present totals. This does not measure GPU busy time or FPS
without instrumentation. Guest GET != PUT can be a parked FIFO jump.
"""
import argparse
import bisect
import json
from pathlib import Path
import re
import statistics


def analyze(path):
    events = []
    header = ""
    keep = {5, 6, 7, 8, 9, 100, 101, 102, 103, 104, 105, 106, 107, 108}
    with path.open(encoding="utf-8") as source:
        for line in source:
            if line.startswith("#"):
                header += line.strip() + " "
                continue
            if not line.strip() or line.startswith("T"):
                continue
            kind, who, a, b, start, end = line.split()
            if int(kind) in keep:
                events.append((int(kind), int(who), int(a, 16), int(b, 16),
                               float(start) / 1000, float(end) / 1000))
    events.sort(key=lambda event: event[4])
    flips = [e[4] for e in events if e[0] == 8]
    if len(flips) < 2:
        raise ValueError("Need at least two executed FIFO flips (event 8)")
    dropped = re.search(r"dropped=(\d+)", header)
    if dropped and int(dropped[1]):
        raise ValueError("Capture dropped spans; increase PS3_FTRACE_CAP")
    lo, hi = flips[0], flips[-1]
    frames = len(flips) - 1

    def clipped(event):
        return max(0, min(hi, event[5]) - max(lo, event[4]))

    names = {5: "semaphore_wait", 6: "semaphore_gave_up", 7: "fifo_drain",
             100: "rsx_idle", 101: "host_present", 102: "vulkan_fence",
             105: "swapchain_acquire", 106: "queue_present"}
    reasons = {100: {0: "sdk", 1: "kick", 2: "timeout_or_zero_poll"},
               102: {0: "other", 1: "synchronous", 2: "draw_context_0",
                     3: "draw_context_1", 4: "present_copy", 5: "window_copy"}}
    groups = {}
    for kind, name in names.items():
        for reason in reasons.get(kind, {None: ""}):
            spans = [e for e in events if e[0] == kind and
                     (reason is None or e[2] == reason) and e[5] >= lo and e[4] < hi]
            durations = [clipped(e) for e in spans]
            key = name + ("/" + reasons[kind][reason] if reason is not None else "")
            groups[key] = {"count": len(spans), "ms_per_frame": sum(durations) / frames,
                           "max_ms": max(durations, default=0),
                           "median_ms": statistics.median(durations) if durations else 0}
    nested = {}
    for parent in (7, 101):
        parents = [e for e in events if e[0] == parent]
        starts = [e[4] for e in parents]
        total = 0
        for event in events:
            if event[0] != 102:
                continue
            index = bisect.bisect_right(starts, event[4]) - 1
            if index >= 0 and event[5] <= parents[index][5]:
                total += clipped(event)
        nested[names[parent]] = total / frames
    before = None
    pairs = []
    requests = {}
    delays = []
    for event in events:
        if event[0] == 103:
            before = event
        elif event[0] == 104 and before and lo <= before[4] < hi:
            pairs.append((before, event))
            before = None
        elif event[0] == 9:
            requests[event[2]] = event[4]
        elif event[0] == 8 and lo <= event[4] < hi and event[2] in requests:
            delays.append(event[4] - requests.pop(event[2]))
    # Use the last observed stop, not GET != PUT, to attribute an idle span.
    # Keep stops before the first flip so the first complete interval is valid.
    stops = [e for e in events if e[0] == 107]
    stop_times = [e[4] for e in stops]
    stop_names = {1: "semaphore", 2: "parked_jump", 3: "flip",
                  4: "caught_up", 5: "budget", 6: "unmapped", 7: "other"}
    idle_by_stop = {}
    for event in events:
        if event[0] != 100 or event[5] < lo or event[4] >= hi:
            continue
        index = bisect.bisect_right(stop_times, event[4]) - 1
        name = stop_names.get(stops[index][1], "unknown") if index >= 0 else "unknown"
        group = idle_by_stop.setdefault(name, {"count": 0, "ms_per_frame": 0,
                                              "kick_wakes": 0, "timeout_wakes": 0})
        group["count"] += 1
        group["ms_per_frame"] += clipped(event) / frames
        group["kick_wakes"] += event[2] == 1
        group["timeout_wakes"] += event[2] == 2
    stop_counts = {name: sum(e[1] == reason and lo <= e[4] < hi for e in stops)
                   for reason, name in stop_names.items()}
    # A guest GET snapshot alone can be stale. Require agreement with the
    # actual consumer head recorded by a normal caught-up/flip stop, and a
    # different PUT already visible BEFORE entering the wait.
    idle_spans = [e for e in events if e[0] == 100]
    idle_starts = [e[4] for e in idle_spans]
    published_before_idle = {"count": 0, "ms_per_frame": 0}
    for before, after in pairs:
        si = bisect.bisect_right(stop_times, before[4]) - 1
        ii = bisect.bisect_right(idle_starts, before[4]) - 1
        if si < 0 or ii < 0:
            continue
        stop, idle = stops[si], idle_spans[ii]
        if (stop[1] in (3, 4) and before[3] == stop[2] and before[2] != stop[2]
                and before[4] <= idle[5] <= after[4]):
            published_before_idle["count"] += 1
            published_before_idle["ms_per_frame"] += clipped(idle) / frames
    return {"trace": str(path), "frames": frames, "window_ms": hi-lo,
            "instrumented_frame_ms": (hi-lo)/frames, "spans": groups,
            "nested_fence_ms_per_frame": nested,
            "fifo_stop_counts": stop_counts,
            "idle_after_fifo_stop": idle_by_stop,
            "producer_kick_count": sum(e[0] == 108 and lo <= e[4] < hi for e in events),
            "published_before_idle": published_before_idle,
            "request_to_fifo_ms": {"samples": len(delays),
                "mean": statistics.mean(delays) if delays else None,
                "max": max(delays, default=None)},
            "fifo_idle_snapshots": {"pairs": len(pairs),
                "empty_before": sum(a[2] == a[3] for a, b in pairs),
                "put_changed_during_wait": sum(a[2] != b[2] for a, b in pairs)},
            "notes": ["Nested fences are included in drain/present; do not add them.",
                      "CPU wait duration is not GPU busy time.",
                      "GET != PUT does not prove runnable work (parked jumps).",
                      "Use a separate trace-disabled run for FPS comparisons."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.trace), indent=2) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    print(result)
