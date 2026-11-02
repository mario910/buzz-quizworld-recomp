"""Emit the pinned full flag expansion oracle; all derived data stays private."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed index expansion source")
    entry=(ROOT/"analysis/native-geometry/phase3/index-audit-v1/pc_7838.bin").read_bytes()
    if hashlib.sha256(entry).hexdigest()!="97ad132caddc054a07692bc1a13a4da1784e0924ce09a0c97e03de957f108e11":raise SystemExit("Unreviewed real expansion entry")
    functions={int(pc,16):b for pc,b in re.findall(r"(?ms)^void "+PREFIX+r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0x7838,0x78a8}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if references-selected!={0x79a8,0x7dcc}:raise SystemExit("Unreviewed expansion endpoints")
    fixture=(ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if hashlib.sha256(fixture).hexdigest()!="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":raise SystemExit("Unreviewed expansion tables")
    text="namespace index_expand_oracle {\n"
    text+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected|references))
    text+="\n".join(f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"expansion endpoint {pc:X} entered\\n");abort();}}' for pc in sorted(references-selected))
    text+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    text+="\nstatic void seed_tables(spu_context& c){\n"
    for address in (0xd950,0xd9d0,0xd8a0,0xd130,0xd150,0xd140,0xd980):
        for i,word in enumerate(struct.unpack_from(">4I",fixture,2048+address)):
            text+=f"{{uint32_t value={word:#x}u;c.ls[{address+i*4}]=uint8_t(value>>24);c.ls[{address+i*4+1}]=uint8_t(value>>16);c.ls[{address+i*4+2}]=uint8_t(value>>8);c.ls[{address+i*4+3}]=uint8_t(value);}}\n"
    text+="}\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-index-expand-reference.inc").write_text(text,encoding="utf-8")
    print("Pinned complete flag expansion oracle:7838/78A8;distinct79A8/7DCC traps;7 LS tables")


if __name__=="__main__":main()
