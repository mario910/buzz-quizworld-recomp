"""Resolve experiment-only RVAs captured by the bounded native unwind sampler."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import bisect
import collections
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
mapping = (root / "build-windows/BuzzQuizWorldRecomp.map").read_text(errors="replace")
preferred = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", mapping)[1], 16)
names = {}
for match in re.finditer(r"^\s+0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(.+)$", mapping, re.M):
    names.setdefault(int(match[2], 16) - preferred, match[1])
addresses = sorted(names)


def symbol(value):
    if value == "-":
        return "<external>"
    at = bisect.bisect_right(addresses, int(value, 16)) - 1
    return names[addresses[at]] if at >= 0 else value


cycles = collections.Counter()
stacks = collections.defaultdict(collections.Counter)
for line in Path(sys.argv[1]).read_text().splitlines():
    if line.startswith("#"):
        print(line)
    elif line.startswith("S "):
        _, tid, delta, *rvas = line.split()
        cycles[int(tid)] += int(delta)
        frames = [symbol(rva) for rva in rvas]
        stacks[int(tid)][" -> ".join(frames)] += 1
print("Thread cycle deltas (NOT CPU percentages), unwind snapshots (NOT weighted CPU attribution):")
for tid, total in cycles.most_common(12):
    print(f"thread {tid}: {total} cycles")
    for stack, count in stacks[tid].most_common(5):
        print(f"  {count}: {stack}")
