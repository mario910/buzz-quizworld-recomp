"""Private closed formatter oracle: nine control plus 37 original data blocks."""
import hashlib,importlib.util,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PREFIX='buzz_job_6eba_spu_func_'
ENTRY_PIN='f78601404ee5a584a69a5ea1a74317b387e630789ed3b6f72d03352335d14a8b'
def main():
    spec=importlib.util.spec_from_file_location('formatter_audit',ROOT/'tools/inspect/geometry-conversion-audit.py')
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
    if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin').read_bytes()).hexdigest()!=ENTRY_PIN:
        raise SystemExit('Unreviewed formatter calibration')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    pcs={int(pc,16) for pc in graph['edges'] if 0x4178<=int(pc,16)<0x54e8};assert len(pcs)==46
    roots=set(audit.FIELDS.values())
    lines=['namespace formatter_oracle {']
    lines += [f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(pcs)]
    lines += ['static void (*observer)(spu_context*,unsigned)=nullptr;','static void (*mock_data)(spu_context*,unsigned)=nullptr;']
    for pc in sorted(pcs):
        body=audit.ring.reachable_text(functions[pc])
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:'formatter_oracle::'+PREFIX+f'{int(m[1],16):08X}',body)
        if pc in roots:body=f'\n if(observer)observer(ctx,0x{pc:X});if(mock_data){{mock_data(ctx,0x{pc:X});return;}}\n'+body
        lines += [f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}']
    lines += ['static void dispatch(spu_context* c){switch(c->pc){']
    lines += [f'case 0x{pc:X}:{PREFIX}{pc:08X}(c);return;' for pc in sorted(pcs)]
    lines += ['default:fprintf(stderr,"formatter unknown dispatch %X\\n",c->pc);abort();}}','}']
    out=ROOT/'analysis/native-geometry/phase3';(out/'test-formatter-reference.inc').write_text('\n'.join(lines)+'\n')
    private='buzz_formatter_original_'
    production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(pcs))+'\n'
    for pc in sorted(pcs):
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:private+f'{int(m[1],16):08X}',audit.ring.reachable_text(functions[pc]))
        production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    production+='static void buzz_formatter_original_dispatch(spu_context* ctx){switch(ctx->pc){\n'
    production+='\n'.join(f'case 0x{pc:X}:{private}{pc:08X}(ctx);return;' for pc in sorted(pcs))
    production+='\ndefault:buzz_geometry_job_native_fail(ctx,"formatter reference escaped closed graph");}}\n'
    production+='''static void buzz_formatter_original_run(spu_context* ctx){
    const unsigned stack=ctx->gpr[1]._u32[0];buzz_formatter_original_00004178(ctx);
    for(unsigned steps=0;!(ctx->gpr[1]._u32[0]==stack && ctx->pc==ctx->gpr[0]._u32[0] &&
        g_spu_trampoline_fn==(ctx->host_depth?nullptr:spu_indirect_branch));++steps){
        const auto next=g_spu_trampoline_fn;
        if(!next || steps>=1048576)buzz_geometry_job_native_fail(ctx,"formatter reference frontier");
        g_spu_trampoline_fn=nullptr;if(next==spu_indirect_branch)buzz_formatter_original_dispatch(ctx);else next(ctx);
    }
}
'''
    (out/'formatter-original.inc').write_text(production)
    print('Pinned formatter oracle:46 original blocks;all7 data families;exact restored-frame return frontier;no native reference data')
if __name__=='__main__':main()
