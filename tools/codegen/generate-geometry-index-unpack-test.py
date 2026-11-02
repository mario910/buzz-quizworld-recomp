"""Pinned complete low/high index-unpack oracle and private shuffle tables."""
import hashlib
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed index-unpack source")
    for name,digest in (
        ("index-audit-v1/pc_74F0.bin","27a6a0830abc711308f4df147e0cb71ce8212792c55e0fbe7396cc3fae2200e2"),
        ("index-unpack-v1-high-entry/pc_74F0.bin","9d726ed96a22ebc9af502ca0e53a34b2748f709c41bc3e906cffe18beb94069f")):
        if hashlib.sha256((ROOT/"analysis/native-geometry/phase3"/name).read_bytes()).hexdigest()!=digest:
            raise SystemExit("Unreviewed original unpack entry: "+name)
    functions={int(pc,16):body for pc,body in re.findall(
        r"(?ms)^void "+PREFIX+r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0x74f0,0x7640,0x7c20,0x7cd0,0x77e8}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if references-selected!={0x7730,0x77f8}:raise SystemExit("Unreviewed index-unpack boundaries")
    fixture=(ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if hashlib.sha256(fixture).hexdigest()!="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":raise SystemExit("Unreviewed index shuffle fixture")
    text="namespace index_unpack_oracle {\n"
    text+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected|references))
    # Distinct diagnostics keep the two boundary identities distinct under
    # MSVC's identical-COMDAT folding; an identical abort-only pair can fold.
    text+="\n".join(f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"unpack boundary {pc:X} entered\\n");abort();}}' for pc in sorted(references-selected))
    text+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    text+="\nstatic void seed_tables(spu_context& c){\n"
    for address in (0xd110,0xd120,0xd8e0,0xd8f0,0xd980,0xd9e0,0xda30):
        for i,word in enumerate(struct.unpack_from(">4I",fixture,2048+address)):
            text+=f"{{uint32_t value={word:#x}u;c.ls[{address+i*4}]=uint8_t(value>>24);c.ls[{address+i*4+1}]=uint8_t(value>>16);c.ls[{address+i*4+2}]=uint8_t(value>>8);c.ls[{address+i*4+3}]=uint8_t(value);}}\n"
    text+="}\n}\n"
    dest=ROOT/"analysis/native-geometry/phase3";dest.mkdir(parents=True,exist_ok=True)
    (dest/"test-index-unpack-reference.inc").write_text(text,encoding="utf-8")
    print("Pinned complete index-unpack oracle:5 original blocks,7730/77F8 trapped;7 LS tables pinned")


if __name__=="__main__":main()
