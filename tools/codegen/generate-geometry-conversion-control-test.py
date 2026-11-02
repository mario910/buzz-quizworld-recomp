"""Emit the private pinned conversion oracle, including retained data callees."""
import importlib.util
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import optional_fixture
PREFIX='buzz_job_6eba_spu_func_'
ENTRY_PIN='40386ad034b6fc29264cc5c985d75a415dd2e480c36a7f99b9a474f94a390962'

def main():
    spec=importlib.util.spec_from_file_location('conversion_audit',ROOT/'tools/inspect/geometry-conversion-audit.py')
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
    optional_fixture(ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin',ENTRY_PIN,'Unreviewed real output-conversion entry')
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    pcs={int(pc,16) for pc in graph['edges']};assert len(pcs)==73
    prefix='conversion_control_oracle::'+PREFIX
    lines=['namespace conversion_control_oracle {']
    lines += [f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x6158})]
    lines += [f'static void {PREFIX}00006158(spu_context*){{fprintf(stderr,"conversion reporting frontier\\n");abort();}}']
    lines += ['static void (*formatter)(spu_context*)=nullptr;','static void (*stride)(spu_context*)=nullptr;']
    for pc in sorted(pcs):
        body=audit.ring.reachable_text(functions[pc])
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:prefix+f'{int(m[1],16):08X}',body)
        if pc in (0x4178,0x40c8):
            selected='formatter' if pc==0x4178 else 'stride'
            body=f'\n if({selected}){{{selected}(ctx);return;}}\n'+body
        lines += [f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}']
    lines += ['static void dispatch(spu_context* c){switch(c->pc){']
    lines += [f'case 0x{pc:X}:{PREFIX}{pc:08X}(c);return;' for pc in sorted(pcs|{0x6158})]
    lines += ['default:fprintf(stderr,"conversion unknown dispatch %X\\n",c->pc);abort();}}','}']
    output=ROOT/'analysis/native-geometry/phase3/test-conversion-control-reference.inc';output.write_text('\n'.join(lines)+'\n')
    private='buzz_conversion_original_'
    production='static void buzz_conversion_original_dispatch(spu_context*);\nstatic void buzz_conversion_original_drain(spu_context*,uint32_t);\n'
    production+='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x6158}))+'\n'
    production+=f'static void {private}00006158(spu_context* ctx){{buzz_geometry_job_native_fail(ctx,"conversion reference reporting frontier");}}\n'
    for pc in sorted(pcs):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:private+f'{int(m[1],16):08X}',audit.ring.reachable_text(functions[pc]))
        body=body.replace('spu_drain_call(', 'buzz_conversion_original_drain(')
        # Unaudited callbacks must never be invoked by the cloned reference.
        body=body.replace('spu_indirect_branch(ctx);', 'buzz_geometry_job_native_fail(ctx,"conversion reference custom callback");')
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    production+='static void buzz_conversion_original_dispatch(spu_context* ctx){switch(ctx->pc){\n'
    production+='\n'.join(f'case 0x{pc:X}:{private}{pc:08X}(ctx);return;' for pc in sorted(pcs|{0x6158}))
    production+='\ndefault:buzz_geometry_job_native_fail(ctx,"conversion reference escaped closed graph");}}\n'
    production+=r'''static void buzz_conversion_original_drain(spu_context* ctx,uint32_t link){
    for(unsigned steps=0;!(ctx->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++steps){
        const buzz_geometry_job_fn next=g_spu_trampoline_fn;
        if(!next || steps>=1048576)buzz_geometry_job_native_fail(ctx,"conversion reference synchronous frontier");
        g_spu_trampoline_fn=NULL;if(next==spu_indirect_branch)buzz_conversion_original_dispatch(ctx);else next(ctx);
    }g_spu_trampoline_fn=NULL;
}
static void buzz_conversion_original_run(spu_context* ctx){
    const unsigned stack=ctx->gpr[1]._u32[0];buzz_conversion_original_000054E8(ctx);
    for(unsigned steps=0;!(ctx->gpr[1]._u32[0]==stack && ctx->pc==ctx->gpr[0]._u32[0] &&
        g_spu_trampoline_fn==(ctx->host_depth?NULL:spu_indirect_branch));++steps){
        const buzz_geometry_job_fn next=g_spu_trampoline_fn;
        if(!next || steps>=1048576)buzz_geometry_job_native_fail(ctx,"conversion reference root frontier");
        g_spu_trampoline_fn=NULL;if(next==spu_indirect_branch)buzz_conversion_original_dispatch(ctx);else next(ctx);
    }
}
'''
    (ROOT/'analysis/native-geometry/phase3/conversion-control-original.inc').write_text(production)
    print('Pinned conversion control oracle:73 original standard blocks;report trap;optional identical formatter/stride boundaries')

if __name__=='__main__':main()
