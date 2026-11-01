"""Emit private complete storage-6 output oracle; derived code stays private."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='2f6ae0527eca7c0cc84422263c938bc819845502d7b5dee457291c4b44ed649b'
PREFIX='buzz_job_6eba_spu_func_'
def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed CMP output lift')
    entry=(ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_4FDC.bin').read_bytes()
    if hashlib.sha256(entry).hexdigest()!=ENTRY_PIN:raise SystemExit('Unreviewed CMP output entry')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x4fdc,0x5008,0x50c8,0x51c8,0x51e0};endpoints={0x4610,0x4618}
    targets={int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])}
    if targets-selected!=endpoints:raise SystemExit('Unreviewed CMP output frontier')
    text='namespace cmp_output_oracle {\n'
    text+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|endpoints))+'\n'
    for pc in sorted(endpoints):text+=f'static void {PREFIX}{pc:08X}(spu_context*){{fprintf(stderr,"CMP output endpoint entered\\n");abort();}}\n'
    for pc in sorted(selected):text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n'
    text+='static void seed_constants(spu_context& c){\n'
    for reg in (34,35,36,46,49,50,51,63):
        for lane,word in enumerate(struct.unpack_from('<4I',entry,reg*16)):text+=f'c.gpr[{reg}]._u32[{lane}]={word:#x}u;\n'
    text+='}\n}\n';(ROOT/'analysis/native-geometry/phase3/test-cmp-output-reference.inc').write_text(text)
    private='buzz_cmp_output_original_'
    production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected))+'\n'
    production+='\n'.join(f'void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(endpoints))+'\n'
    for pc in sorted(selected):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc])
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    (ROOT/'analysis/native-geometry/phase3/cmp-output-original.inc').write_text(production)
    print('Pinned complete CMP output oracle:5 original blocks;2 distinct frontier traps;real packing constants')
if __name__=='__main__':main()
