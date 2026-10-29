"""Resolve claude-rip-sampler output against the linker map (diagnostics).

  rip_resolve.py <map> <samples.txt> [--tids 1,2,3] [--top N] [--prefix P]

Counts in-image samples per symbol (nearest preceding map symbol), optionally
only for the given host thread ids, and prints the top N.
"""
import argparse
import bisect
import collections
import re

parser = argparse.ArgumentParser()
parser.add_argument("map")
parser.add_argument("samples")
parser.add_argument("--tids", default="")
parser.add_argument("--top", type=int, default=40)
parser.add_argument("--prefix", default="")
args = parser.parse_args()

base = 0x140000000
symbols = []
pattern = re.compile(r"^\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})\s")
for line in open(args.map, encoding="latin-1"):
    m = pattern.match(line)
    if m:
        symbols.append((int(m.group(2), 16) - base, m.group(1)))
symbols.sort()
addresses = [a for a, _ in symbols]

wanted = set(args.tids.split(",")) if args.tids else None
counts = collections.Counter()
total = 0
for line in open(args.samples):
    parts = line.split()
    if len(parts) < 3 or parts[0] != "S" or parts[2].startswith("-"):
        continue
    if wanted and parts[1] not in wanted:
        continue
    rva = int(parts[2], 16)
    i = bisect.bisect_right(addresses, rva) - 1
    name = symbols[i][1] if i >= 0 else "?"
    counts[name] += 1
    total += 1

print(f"{total} in-image samples")
shown = 0
for name, n in counts.most_common():
    if args.prefix and not name.startswith(args.prefix):
        continue
    print(f"{n:7d} {100.0 * n / max(total, 1):5.1f}%  {name}")
    shown += 1
    if shown >= args.top:
        break
