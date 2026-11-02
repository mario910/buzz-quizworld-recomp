"""Rank exclusive block samples for one native-work target on SPURS lanes.

Shares are sampled estimates, never exact function times or an FPS benchmark.
Choose lanes from the first sample and keep that explicit set for all later
samples of the same process. Independent processes need their own lane IDs.
"""
import argparse
import bisect
import collections
import json
import re
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("samples", nargs="+", type=Path)
    p.add_argument("--map", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--image", type=int, choices=(40, 41, 43, 45, 47), default=41,
                   help="Reviewed SPU image role; defaults to the original image-41 report")
    p.add_argument("--lanes", help="Comma-separated thread IDs; auto-select is diagnostic only")
    a = p.parse_args()
    mapping = a.map.read_text(errors="replace")
    preferred = int(re.search(r"Preferred load address is ([0-9A-Fa-f]+)", mapping)[1], 16)
    symbols = {}
    for m in re.finditer(r"^\s+0001:[\da-fA-F]+\s+(\S+)\s+([\da-fA-F]{16})\s+(?:f\s+)?(.+)$", mapping, re.M):
        symbols.setdefault(int(m[2], 16) - preferred, m[1])
    addresses = sorted(symbols)
    def symbol(rva):
        if rva.startswith("-"):
            return "<external>"
        pos = bisect.bisect_right(addresses, int(rva, 16)) - 1
        return symbols[addresses[pos]] if pos >= 0 else "<unknown>"
    by_thread = collections.defaultdict(collections.Counter)
    for path in a.samples:
        for line in path.read_text().splitlines():
            if line.startswith("S "):
                _, tid, rva = line.split()
                by_thread[tid][symbol(rva)] += 1
    prefixes = {40: "buzz_job_abe1", 41: "buzz_job_6eba", 43: "buzz_job_19d0",
                45: "disc_job_007d0980", 47: "disc_job_007f4b80"}
    geometry = lambda name: name.startswith(prefixes[a.image] + "_spu_func_")
    count_key = f"image{a.image}_samples"
    share_key = f"image{a.image}_share_percent"
    candidates = sorted(((sum(n for name, n in counts.items() if geometry(name)), tid)
        for tid, counts in by_thread.items()), reverse=True)
    lanes = a.lanes.split(",") if a.lanes else [tid for hits, tid in candidates[:4] if hits >= 20]
    if not lanes:
        raise ValueError("No geometry lanes; specify --lanes from a verified sample")
    total = collections.Counter()
    for tid in lanes:
        if tid not in by_thread:
            raise ValueError(f"Missing lane {tid}")
        total.update(by_thread[tid])
    denominator = sum(total.values())
    image_samples = sum(n for name, n in total.items() if geometry(name))
    ranked = [dict(symbol=name, samples=n, lane_share_percent=100*n/denominator,
                   **{share_key: 100*n/image_samples})
              for name, n in total.most_common() if geometry(name)]
    result = dict(samples=[str(p) for p in a.samples], map_sha256=__import__("hashlib").sha256(a.map.read_bytes()).hexdigest(),
        lanes=lanes, automatic_lanes=not bool(a.lanes), lane_samples=denominator,
        **{count_key: image_samples}, ranking=ranked,
        candidates=[dict(thread=tid, **{count_key: hits}) for hits, tid in candidates[:10]],
        caveats=[f"Exclusive leaf RIP samples; helper time is outside image-{a.image} symbols.",
                 "Sampler skips inactive intervals; lane denominator is active sampled intervals.",
                 "Nearest map symbol may cover alignment padding; external caller guesses are excluded."])
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"lanes={','.join(lanes)} samples={denominator} image{a.image}={image_samples}")
    for r in ranked[:15]:
        print(f"{r['lane_share_percent']:6.2f}% lanes {r[share_key]:6.2f}% image{a.image} {r['samples']:5} {r['symbol']}")


if __name__ == "__main__":
    main()
