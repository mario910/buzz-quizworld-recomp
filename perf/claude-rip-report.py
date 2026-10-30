"""Aggregate claude-rip-sampler output by symbol (from the build's .map). Diagnostic only."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import bisect, collections, re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
import os
mapping = (root / os.environ.get("BUZZ_BUILD_DIR", "build-windows") / "BuzzQuizWorldRecomp.map").read_text(errors="replace")
preferred = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", mapping)[1], 16)
names = {}
for m in re.finditer(r"^\s+0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(.+)$", mapping, re.M):
    names.setdefault(int(m[2], 16) - preferred, m[1])
addresses = sorted(names)

def symbol(rva):
    if rva.startswith("-"):
        module, _, caller = rva[1:].partition("@")
        return "<" + (module or "external") + ">" + (" from " + symbol(caller) if caller else "")
    at = bisect.bisect_right(addresses, int(rva, 16)) - 1
    return names[addresses[at]] if at >= 0 else rva

per_thread = collections.defaultdict(collections.Counter)
for line in Path(sys.argv[1]).read_text().splitlines():
    if line.startswith("S "):
        _, tid, rva = line.split()
        per_thread[tid][symbol(rva)] += 1
top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
total = collections.Counter()
for tid, c in sorted(per_thread.items(), key=lambda kv: -sum(kv[1].values())):
    n = sum(c.values())
    print(f"thread {tid}: {n} samples")
    for name, k in c.most_common(8):
        print(f"  {100*k/n:5.1f}% {name}")
    total.update(c)
n = sum(total.values())
print(f"ALL threads: {n} samples")
for name, k in total.most_common(top):
    print(f"  {100*k/n:5.1f}% {name}")
