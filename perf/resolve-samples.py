"""Resolve Windows sampler RVAs using the matching MSVC/LLD link map.

Counts are wall-clock thread snapshots, NOT CPU utilization. Symbols describe
native code; identical-code folding may give an address more than one name.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import argparse
import bisect
import collections
import pathlib
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=pathlib.Path)
    parser.add_argument("log", type=pathlib.Path)
    parser.add_argument("--reports", type=int, default=3)
    args = parser.parse_args()
    source = args.map.read_text(errors="replace")
    base = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", source)[1], 16)
    symbols = collections.defaultdict(list)
    for match in re.finditer(r"^\s+0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(.+)$", source, re.M):
        symbols[int(match[2], 16) - base].append((match[1], match[3].strip()))
    addresses = sorted(symbols)
    reports = args.log.read_text(errors="replace").split("[samp] --- report")
    totals = collections.Counter()
    for report in reports[-args.reports:]:
        for rva, count in re.findall(r"\[samp\] rva 0x([0-9A-Fa-f]+)\s+[\d.]+% \((\d+)\)", report):
            rva = int(rva, 16)
            index = bisect.bisect_right(addresses, rva) - 1
            if index >= 0:
                totals[addresses[index]] += int(count)
    for address, count in totals.most_common(25):
        names = symbols[address]
        print(f"{count:6d}  RVA {address:08X}  {names[0][0]}  [{names[0][1]}]" +
              (f" (+{len(names)-1} folded aliases)" if len(names) > 1 else ""))


if __name__ == "__main__":
    main()
