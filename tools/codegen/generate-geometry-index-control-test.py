"""Emit pinned complete original index graph for strict control comparisons."""
import hashlib
import importlib.util
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='259f19482817dbeb3d2640a21fc56f82e8436733d629d616d679df09537aefe1'
PREFIX='buzz_job_6eba_spu_func_'


def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed index control source')
    entry=(ROOT/'analysis/native-geometry/phase3/index-audit-v1/pc_AEA0.bin').read_bytes()
    if hashlib.sha256(entry).hexdigest()!=ENTRY_PIN:raise SystemExit('Unreviewed real index control entry')
    spec=importlib.util.spec_from_file_location('index_audit',ROOT/'tools/inspect/geometry-index-audit.py')
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit);audit.source_graph(raw)
    functions={int(pc,16):b for pc,b in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected=audit.NORMAL;errors=audit.ERRORS;all_pcs=selected|errors
    def emit(prefix,qualifier=''):
        output='\n'.join(f'static void {prefix}{pc:08X}(spu_context*);' for pc in sorted(all_pcs))+'\n'
        for pc in sorted(errors):output+=f'static void {prefix}{pc:08X}(spu_context*){{fprintf(stderr,"index control error {pc:X} entered\\n");abort();}}\n'
        for pc in sorted(selected):
            body=audit.reachable_text(functions[pc])
            body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:f'{qualifier}{prefix}{int(m[1],16):08X}',body)
            output+=f'static void {prefix}{pc:08X}(spu_context* ctx) {{{body}}}\n'
        return output
    out=ROOT/'analysis/native-geometry/phase3';out.mkdir(parents=True,exist_ok=True)
    test='namespace index_control_oracle {\n'+emit(PREFIX,'index_control_oracle::')
    test+='static const buzz_geometry_index_functions library={'+','.join(f'{PREFIX}{pc:08X}' for pc in (0x74f0,0x7730,0x77f8,0x7838,0x79a8,0x7a90,0x2738))+'};\n'
    test+='static void seed_tables(spu_context& c){\n'
    addresses=(*range(0xd110,0xd190,16),*range(0xd690,0xd890,16),0xd8a0,0xd8e0,0xd8f0,0xd950,0xd980,0xd9d0,0xd9e0,0xda30)
    for address in addresses:
        for i,word in enumerate(struct.unpack_from('>4I',entry,2048+address)):
            test+=f'{{uint32_t value={word:#x}u;c.ls[{address+i*4}]=uint8_t(value>>24);c.ls[{address+i*4+1}]=uint8_t(value>>16);c.ls[{address+i*4+2}]=uint8_t(value>>8);c.ls[{address+i*4+3}]=uint8_t(value);}}\n'
    test+='}\n}\n'
    (out/'test-index-control-reference.inc').write_text(test,encoding='utf-8')
    (out/'index-control-original.inc').write_text(emit('buzz_index_control_original_'),encoding='utf-8')
    print('Pinned index control oracle:22 complete original normal blocks;4 distinct error traps;dead tails removed')


if __name__=='__main__':main()
