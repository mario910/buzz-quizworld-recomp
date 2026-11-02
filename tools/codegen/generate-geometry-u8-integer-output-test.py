"""Emit private complete storage-7 output oracle; derived code stays private."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import optional_fixture
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='cc422028e1cc75bb049cac44b1d38f760e85b17574a53ad7214bacd5c401d8f6'
PREFIX='buzz_job_6eba_spu_func_'
def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed U8 integer output lift')
    entry=optional_fixture(ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_4B9C.bin',ENTRY_PIN,'Unreviewed U8 integer output entry')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x52b4,0x52e0,0x5368,0x5430,0x5448};endpoints={0x4610,0x4618}
    targets={int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])}
    if targets-selected!=endpoints:raise SystemExit('Unreviewed U8 integer output frontier')
    if entry is not None:
        text='namespace u8_integer_output_oracle {\n'
        text+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|endpoints))+'\n'
        for pc in sorted(endpoints):text+=f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"U8 integer output endpoint entered\\n");abort();}}\n'
        for pc in sorted(selected):text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n'
        text+='static void seed_constants(spu_context& c){\n'
        for reg in (43,49,63,71):
            for lane,word in enumerate(struct.unpack_from('<4I',entry,reg*16)):text+=f'c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n'
        text+='}\n}\n';(ROOT/'analysis/native-geometry/phase3/test-u8-integer-output-reference.inc').write_text(text)
    private='buzz_u8_integer_output_original_'
    production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected))+'\n'
    production+='\n'.join(f'void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(endpoints))+'\n'
    for pc in sorted(selected):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc])
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    (ROOT/'analysis/native-geometry/phase3/u8-integer-output-original.inc').write_text(production)
    print('Pinned complete U8 integer output oracle:5 original blocks;2 distinct frontier traps;real-calibrated masks; synthetic cold datatype,not captured root')
if __name__=='__main__':main()
