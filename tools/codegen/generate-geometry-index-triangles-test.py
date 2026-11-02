"""Emit pinned complete 7A90 oracle and private LS tables."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
ENTRY_PIN="1610be268893b8664d92d89dc1f2d36f76edfaf2770d5db20967cced651c90f7"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed triangle source")
    entry=(ROOT/"analysis/native-geometry/phase3/index-audit-v1/pc_7A90.bin").read_bytes()
    if hashlib.sha256(entry).hexdigest()!=ENTRY_PIN:raise SystemExit("Unreviewed real triangle entry")
    functions={int(pc,16):b for pc,b in re.findall(r"(?ms)^void "+PREFIX+r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    references={int(pc,16) for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[0x7a90])}
    if references!={0x2738}:raise SystemExit("Unreviewed triangle endpoint")
    text="namespace index_triangles_oracle {\n"
    text+=f'static void {PREFIX}00002738(spu_context*){{fprintf(stderr,"triangle publication endpoint entered\\n");abort();}}\n'
    text+=f"static void {PREFIX}00007A90(spu_context* ctx) {{{functions[0x7a90]}}}\n"
    text+="static void seed_tables(spu_context& c){\n"
    for address in (0xd160,0xd170,0xd180,0xd980,*range(0xd690,0xd890,16)):
        for i,word in enumerate(struct.unpack_from(">4I",entry,2048+address)):
            text+=f"{{uint32_t value={word:#x}u;c.ls[{address+i*4}]=uint8_t(value>>24);c.ls[{address+i*4+1}]=uint8_t(value>>16);c.ls[{address+i*4+2}]=uint8_t(value>>8);c.ls[{address+i*4+3}]=uint8_t(value);}}\n"
    text+="}\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-index-triangles-reference.inc").write_text(text,encoding="utf-8")
    print("Pinned complete triangles oracle:7A90;2738 trap;36 LS table quads")


if __name__=="__main__":main()
