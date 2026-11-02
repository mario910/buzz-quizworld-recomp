"""Emit private complete storage-3 output oracle; derived code stays private."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import optional_fixture
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='1ac8eebe1106324ff2fe380e05e76d6af88ea3ede3033f7ffaf267c2aeff0ba0'
PREFIX='buzz_job_6eba_spu_func_'
def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed half output lift')
    entry=optional_fixture(ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_4880.bin',ENTRY_PIN,'Unreviewed half output entry')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x4880,0x48b0,0x492c,0x497c,0x4988,0x4a9c,0x4ab0};endpoints={0x4610,0x4618}
    targets={int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])}
    if targets-selected!=endpoints:raise SystemExit('Unreviewed half output frontier')
    if entry is not None:
        text='namespace half_output_oracle {\n'
        text+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|endpoints))+'\n'
        for pc in sorted(endpoints):text+=f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"half output endpoint entered\\n");abort();}}\n'
        for pc in sorted(selected):text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n'
        text+='static void seed_constants(spu_context& c){\n'
        for reg in (33,37,38,56,57,58,63):
            for lane,word in enumerate(struct.unpack_from('<4I',entry,reg*16)):text+=f'c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n'
        text+='}\n}\n';(ROOT/'analysis/native-geometry/phase3/test-half-output-reference.inc').write_text(text)
    private='buzz_half_output_original_'
    production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected))+'\n'
    production+='\n'.join(f'void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(endpoints))+'\n'
    for pc in sorted(selected):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc])
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    (ROOT/'analysis/native-geometry/phase3/half-output-original.inc').write_text(production)
    print('Pinned complete half output oracle:7 original blocks;2 distinct frontier traps;real packing constants')
if __name__=='__main__':main()
