"""Extract an inspectable image-41 block/call/LS map; no game code is emitted to git.

This distinguishes lifted blocks from SPU subroutines: trampoline edges are
control flow, brsl/direct calls are call edges. Indirect edges stay unresolved.
Register sets are lexical references, not liveness or a proven function ABI.
"""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path

PREFIX = "buzz_job_6eba_spu_func_"


def extract(path):
    source = path.read_text()
    blocks = {}
    for m in re.finditer(r"^void " + PREFIX + r"([0-9A-F]{8})\(spu_context\* ctx\) \{\n(.*?)^\}", source, re.M | re.S):
        pc, body = m[1], m[2]
        assigned = re.findall(r"ctx->gpr\[(\d+)\](?:\._\w+\[\d+\])?\s*=", body)
        used = re.findall(r"ctx->gpr\[(\d+)\]", re.sub(r"ctx->gpr\[\d+\](?:\._\w+\[\d+\])?\s*=", "", body))
        calls = re.findall(PREFIX + r"([0-9A-F]{8})\(ctx\)", body)
        branches = re.findall(r"g_spu_trampoline_fn = " + PREFIX + r"([0-9A-F]{8})", body)
        blocks[pc] = dict(line=source[:m.start()].count("\n") + 1,
            calls=sorted(set(calls)), branches=sorted(set(branches)),
            indirect=bool(re.search(r"spu_indirect_branch|spu_bisl|spu_bi\(", body)),
            local_labels=re.findall(r"^loc_([0-9A-F]{8}):", body, re.M),
            local_gotos=sorted(set(re.findall(r"goto loc_([0-9A-F]{8})", body))),
            returns="SPU_RET(ctx)" in body,
            registers_read=sorted(set(map(int, used))), registers_written=sorted(set(map(int, assigned))),
            ls_reads=re.findall(r"spu_ls_read\w*\(ctx, ([^;]+)", body),
            ls_writes=re.findall(r"spu_ls_write\w*\(ctx, ([^;]+)", body),
            channels=re.findall(r"spu_(?:wrch|rdch|rchcnt)\(ctx, ([^)]+)\)", body))
    if not blocks:
        raise ValueError("No image-41 blocks found")
    roots = {target for b in blocks.values() for target in b["calls"]}
    functions = {}
    for root in sorted(roots):
        pending, visited = [root], set()
        while pending:
            pc = pending.pop()
            if pc in visited or pc not in blocks:
                continue
            visited.add(pc)
            pending.extend(blocks[pc]["branches"])
        functions[root] = dict(blocks=sorted(visited),
            calls=sorted({c for pc in visited for c in blocks[pc]["calls"]}),
            unresolved_indirect_blocks=sorted(pc for pc in visited if blocks[pc]["indirect"]),
            channel_blocks=sorted(pc for pc in visited if blocks[pc]["channels"]))
    return dict(source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        caveats=["Blocks can belong to multiple roots through tail calls.",
                 "Register references are not live-in/live-out contracts.",
                 "Indirect targets require dynamic evidence."], blocks=blocks, functions=functions)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, default=Path("port/spu/buzz_job_6eba/spu_recomp.c"))
    p.add_argument("--output", type=Path, default=Path("analysis/native-geometry/image41-map.json"))
    p.add_argument("--observed-edges", type=Path, help="Optional replay block-transition CSV")
    a = p.parse_args()
    result = extract(a.source)
    if a.observed_edges:
        edges = []
        with a.observed_edges.open() as f:
            for row in csv.DictReader(f):
                source, target = f"{int(row['source'], 16):08X}", f"{int(row['target'], 16):08X}"
                block = result["blocks"][source]
                kind = ("direct-call" if target in block["calls"] else
                        "direct-control" if target in block["branches"] else
                        "possible-return" if block["returns"] else
                        "indirect" if block["indirect"] else "after-nested-call-or-macro")
                edges.append(dict(source=source, target=target, count=int(row["count"]), kind=kind))
        result["observed_transitions"] = edges
        result["caveats"].append("Observed transitions include returns and nested-call continuations; they are not all call edges.")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(result['blocks'])} blocks, {len(result['functions'])} direct-call roots -> {a.output}")


if __name__ == "__main__":
    main()
