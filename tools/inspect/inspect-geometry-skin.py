"""Reproduce the pinned image-41 skin controller/data dependency inventory."""
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[2]
SOURCE_PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
FIXTURE_PIN="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    source=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    fixture=(ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if hashlib.sha256(source).hexdigest()!=SOURCE_PIN:raise SystemExit("Unreviewed skin lift")
    if len(fixture)!=264192 or hashlib.sha256(fixture).hexdigest()!=FIXTURE_PIN:raise SystemExit("Unreviewed skin table fixture")
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",source.decode())}
    selected={pc:body for pc,body in functions.items() if 0xaf18<=pc<0xc518}
    # C500 always queues B250 or B24C. Its following C518 tail is lexical
    # dead code. B4C0's STOPs after its indirect return are jump-table bytes.
    dead="        /* nop */;\n        { ctx->pc = 0xC518;"
    if dead not in selected[0xc500]:raise SystemExit("Unreviewed C500 suffix")
    selected[0xc500]=selected[0xc500].split(dead)[0]
    if "g_spu_trampoline_fn = spu_indirect_branch; return;" not in selected[0xb4c0]:raise SystemExit("Unreviewed skin table dispatch")
    selected[0xb4c0]=selected[0xb4c0].split("g_spu_trampoline_fn = spu_indirect_branch; return;")[0]
    ls=fixture[2048:]
    table=[(0xb554+struct.unpack_from(">i",ls,0xb554+i*4)[0])&0x3ffff for i in range(19)]
    expected=[0xbd4c,0xbe20,0xbf7c]+[0xb868]*13+[0xc14c,0xc280,0xb5a0]
    if table!=expected:raise SystemExit("Unreviewed directional dispatch table")
    edges={pc:{int(target,16) for target in re.findall(PREFIX+r"([0-9A-F]+)",body)} for pc,body in selected.items()}
    edges[0xb4c0].update(table)
    reached=set();pending=[0xaf18]
    while pending:
        pc=pending.pop()
        if pc in reached or pc not in selected:continue
        reached.add(pc);pending.extend(edges[pc])
    external={target for pc in reached for target in edges[pc] if target not in selected}
    if external!={0x2738,0x2750,0x2778,0x2780,0x2790,0xcc70}:raise SystemExit("Unreviewed skin library")
    channel=re.compile(r"\bspu_(?:wrch|rdch|rchcnt)\(")
    if any(channel.search(selected[pc]) for pc in reached):raise SystemExit("New skin channel operation")
    getters=external-{0xcc70}
    library={pc:functions[pc].split("SPU_RET(ctx);")[0] for pc in getters}
    if any(channel.search(body) or re.search(PREFIX+r"[0-9A-F]+",body) for body in library.values()):raise SystemExit("New skin library side effect")
    report={"source_sha256":SOURCE_PIN,"fixture_sha256":FIXTURE_PIN,"entry":"AF18",
            "region_blocks":len(selected),"reachable_region_blocks":len(reached),
            "channel_sites_on_valid_paths":0,"ordinary_library":[f"{pc:X}" for pc in sorted(getters)],
            "error_library":"CC70","directional_table":[f"{pc:X}" for pc in table],
            "edges":{f"{pc:X}":[f"{target:X}" for target in sorted(edges[pc])] for pc in sorted(reached)},
            "required_regions":{"controller":"AF18/AFF8/B250/B318/B4C0/B868/C488/C500",
                                "palette_transpose":"B078/B100","rigid_position":"B348/B3C8",
                                "weighted_position":"B880/B934/B940","cofactor_normal":"BB58/BB68",
                                "directional_roots":["BD4C","BE20","BF7C","C14C","C280","B5A0"]}}
    destination=ROOT/"analysis/native-geometry/phase3/skin-audit.json"
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(report,indent=2)+"\n")
    print(f"Pinned skin inventory: {len(reached)}/{len(selected)} region blocks; 5 pure LS library calls; 6 directional roots; zero valid-path channel sites -> {destination}")


if __name__=="__main__":main()
