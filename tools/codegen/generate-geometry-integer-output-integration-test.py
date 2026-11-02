"""Emit the private pinned conversion oracle, with native cold-data hooks."""
import importlib.util
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
PREFIX='buzz_job_6eba_spu_func_'
ENTRY_PIN='40386ad034b6fc29264cc5c985d75a415dd2e480c36a7f99b9a474f94a390962'

def main():
    spec=importlib.util.spec_from_file_location('conversion_audit',ROOT/'tools/inspect/geometry-conversion-audit.py')
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
    if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin').read_bytes()).hexdigest()!=ENTRY_PIN:
        raise SystemExit('Unreviewed real output-conversion entry')
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    pcs={int(pc,16) for pc in graph['edges']};assert len(pcs)==73
    prefix='integer_output_oracle::'+PREFIX
    lines=['namespace integer_output_oracle {']
    lines += [f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x6158})]
    lines += [f'static void {PREFIX}00006158(spu_context*){{fprintf(stderr,"conversion reporting frontier\\n");abort();}}']
    lines += ['static void (*cold_handler)(spu_context*,unsigned)=nullptr;']
    lines += ['static void (*formatter)(spu_context*)=nullptr;','static void (*stride)(spu_context*)=nullptr;']
    for pc in sorted(pcs):
        body=audit.ring.reachable_text(functions[pc])
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:prefix+f'{int(m[1],16):08X}',body)
        if pc in (0x4178,0x40c8):
            selected='formatter' if pc==0x4178 else 'stride'
            body=f'\n if({selected}){{{selected}(ctx);return;}}\n'+body
        if pc in (0x4dd0,0x52b4):body=f'\n if(cold_handler){{cold_handler(ctx,0x{pc:X});return;}}\n'+body
        lines += [f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}']
    lines += ['static void dispatch(spu_context* c){switch(c->pc){']
    lines += [f'case 0x{pc:X}:{PREFIX}{pc:08X}(c);return;' for pc in sorted(pcs|{0x6158})]
    lines += ['default:fprintf(stderr,"conversion unknown dispatch %X\\n",c->pc);abort();}}','}']
    output=ROOT/'analysis/native-geometry/phase3/test-integer-output-integration-reference.inc';output.write_text('\n'.join(lines)+'\n')
    print('Pinned integer integration oracle:73 original blocks;2 optional native cold-data hooks;synthetic descriptor changes only')

if __name__=='__main__':main()
