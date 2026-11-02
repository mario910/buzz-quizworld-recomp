"""Emit private complete storage-5 output oracle; derived code stays private."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import optional_fixture
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='b919cf806d1b65fe51a32cc0cb25bbdea4fe897b148d84e3d9f571a8bac66958'
PREFIX='buzz_job_6eba_spu_func_'
def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed I16 output lift')
    entry=optional_fixture(ROOT/'analysis/native-geometry/phase3/snorm-output-v1-fixture/pc_4478.bin',ENTRY_PIN,'Unreviewed I16 output entry')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x4dd0,0x4e00,0x4e80,0x4f3c,0x4f50};endpoints={0x4610,0x4618}
    targets={int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])}
    if targets-selected!=endpoints:raise SystemExit('Unreviewed I16 output frontier')
    if entry is not None:
        text='namespace i16_output_oracle {\n'
        text+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|endpoints))+'\n'
        for pc in sorted(endpoints):text+=f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"I16 output endpoint entered\\n");abort();}}\n'
        for pc in sorted(selected):text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n'
        text+='static void seed_constants(spu_context& c){\n'
        for reg in (49,63,72):
            for lane,word in enumerate(struct.unpack_from('<4I',entry,reg*16)):text+=f'c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n'
        text+='}\n}\n';(ROOT/'analysis/native-geometry/phase3/test-i16-output-reference.inc').write_text(text)
    private='buzz_i16_output_original_'
    production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected))+'\n'
    production+='\n'.join(f'void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(endpoints))+'\n'
    for pc in sorted(selected):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc])
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    (ROOT/'analysis/native-geometry/phase3/i16-output-original.inc').write_text(production)
    print('Pinned complete I16 output oracle:5 original blocks;2 distinct frontier traps;real-calibrated masks; synthetic cold datatype,not captured root')
if __name__=='__main__':main()
