"""Emit the pinned full command relocation oracle; derived data stays private."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
ENTRY_PIN="45a4825168d86c503e82480d23680eadfe4aff646109b5c32941f6d98d1563bb"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed index relocation source")
    entry=(ROOT/"analysis/native-geometry/phase3/index-audit-v1/pc_79A8.bin").read_bytes()
    if hashlib.sha256(entry).hexdigest()!=ENTRY_PIN:raise SystemExit("Unreviewed real relocation entry")
    functions={int(pc,16):b for pc,b in re.findall(r"(?ms)^void "+PREFIX+r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0x79a8,0x79c8}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if references-selected!={0x7a90}:raise SystemExit("Unreviewed relocation endpoint")
    text="namespace index_relocate_oracle {\n"
    text+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected|references))
    text+=f'\nstatic void {PREFIX}00007A90(spu_context*){{fprintf(stderr,"relocation triangle endpoint entered\\n");abort();}}\n'
    text+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    text+="\nstatic void seed_tables(spu_context& c){\n"
    for address in (0xd160,0xd170,0xd180,0xd980):
        for i,word in enumerate(struct.unpack_from(">4I",entry,2048+address)):
            text+=f"{{uint32_t value={word:#x}u;c.ls[{address+i*4}]=uint8_t(value>>24);c.ls[{address+i*4+1}]=uint8_t(value>>16);c.ls[{address+i*4+2}]=uint8_t(value>>8);c.ls[{address+i*4+3}]=uint8_t(value);}}\n"
    text+="}\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-index-relocate-reference.inc").write_text(text,encoding="utf-8")
    print("Pinned complete command relocation oracle:79A8/79C8;7A90 trap;4 LS tables")


if __name__=="__main__":main()
