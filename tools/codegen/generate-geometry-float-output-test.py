"""Emit private complete storage-2 output oracle; no lifted tail is native."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import optional_fixture
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='3f592a6baa0a5b4f03cd50d0b260a7faf229ca8c8cc31ef471087497689f3bb9'
PREFIX='buzz_job_6eba_spu_func_'
def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed float output lift')
    entry=optional_fixture(ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_46D0.bin',ENTRY_PIN,'Unreviewed float output entry')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x46d0,0x46fc,0x4750,0x47f0,0x4808};endpoints={0x4610,0x4618}
    targets={int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])}
    if targets-selected!=endpoints:raise SystemExit('Unreviewed float output frontier')
    if entry is not None:
        text='namespace float_output_oracle {\n'
        text+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|endpoints))+'\n'
        for pc in sorted(endpoints):text+=f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"float output endpoint entered\\n");abort();}}\n'
        for pc in sorted(selected):text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n'
        text+='static void seed_shuffle(spu_context& c){\n'
        for i,word in enumerate(struct.unpack_from('>4I',entry,2048+0xd980)):
            text+=f'buzz::geometry_job_put32(c.ls+{0xd980+i*4},{word:#x}u);\n'
        text+='}\n}\n';(ROOT/'analysis/native-geometry/phase3/test-float-output-reference.inc').write_text(text)
    private='buzz_float_output_original_'
    production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected))+'\n'
    production+='\n'.join(f'void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(endpoints))+'\n'
    for pc in sorted(selected):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc])
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    (ROOT/'analysis/native-geometry/phase3/float-output-original.inc').write_text(production)
    print('Pinned complete float output oracle:5 original blocks;2 distinct frontier traps;real shuffle bytes')
if __name__=='__main__':main()
