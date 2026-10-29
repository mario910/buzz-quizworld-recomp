"""Read-only inspection of a captured draw and its translated shaders."""
import argparse
import pathlib
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("manifest", type=pathlib.Path)
p.add_argument("--draw", nargs="+", type=int, required=True)
p.add_argument("--vs", type=pathlib.Path)
p.add_argument("--fp", type=pathlib.Path)
p.add_argument("--shaders", action="store_true")
args = p.parse_args()
draws = dict((int(m[1]), m[0]) for m in re.finditer(
    r"^op(\d+) DRAW[^\n]*\n(?:(?!^op|^---).*(?:\n|$))*", args.manifest.read_text(), re.M))
vss = args.vs.read_text() if args.vs else ""
fps = args.fp.read_text() if args.fp else ""
for number in args.draw:
    block = draws[number]
    print(block.splitlines()[0])
    vsid = re.search(r"vs=(-?\d+)", block)[1]
    fpaddr = re.search(r"fp=0x([\dA-Fa-f]+)", block)[1]
    vs = re.search(r"/\* VS slot=" + vsid + r"\b.*?(?=/\* VS slot=|\Z)", vss, re.S)
    fp = re.search(r"/\* fp_addr=0x" + fpaddr + r"\b.*?(?=/\* fp_addr=|\Z)", fps, re.S | re.I)
    used = {int(c) for c in re.findall(r"vp_c\[(\d+)\]", vs[0])} if vs else set()
    for line in block.splitlines()[1:]:
        const = re.match(r"\s+c(\d+)=", line)
        if (const and int(const[1]) in used) or re.match(r"\s+(t\d |cb_slot|fp_|v0\.a[038]=)", line):
            print(line)
    if args.shaders:
        print("--- VERTEX SHADER ---\n" + (vs[0] if vs else "not captured"))
        print("--- FRAGMENT SHADER ---\n" + (fp[0] if fp else "not captured"))
