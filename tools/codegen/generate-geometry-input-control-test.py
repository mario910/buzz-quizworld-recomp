"""Pin all23 standard input blocks; private independent whole93F0 oracle."""
from pathlib import Path
import hashlib,importlib.util,re
ROOT=Path(__file__).resolve().parents[2];PREFIX='buzz_job_6eba_spu_func_'
ENTRY_PIN='40386ad034b6fc29264cc5c985d75a415dd2e480c36a7f99b9a474f94a390962'
def main():
 spec=importlib.util.spec_from_file_location('input_control_audit',ROOT/'tools/inspect/geometry-input-audit.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
 raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
 if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin').read_bytes()).hexdigest()!=ENTRY_PIN:raise SystemExit('Unreviewed input calibration anchor')
 fs={int(pc,16):audit.ring.reachable_text(body) for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
 pcs={int(pc,16) for pc in graph['edges']};assert len(pcs)==23;out=ROOT/'analysis/native-geometry/phase3'
 ns='input_control_oracle';private=ns+'::'+PREFIX;lines=[f'namespace {ns} {{']+[f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x95dc})]
 lines+=[f'static void {PREFIX}000095DC(spu_context*){{fprintf(stderr,"input control custom frontier\\n");abort();}}']
 for pc in sorted(pcs):
  body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:private+f'{int(m[1],16):08X}',fs[pc]);lines+=[f'static void {PREFIX}{pc:08X}(spu_context* ctx){{{body}}}']
 lines+=['static void dispatch(spu_context* c){switch(c->pc){']+[f'case 0x{pc:X}:{PREFIX}{pc:08X}(c);return;' for pc in sorted(pcs|{0x95dc})]+['default:fprintf(stderr,"input control escaped %X\\n",c->pc);abort();}}','}']
 (out/'test-input-control-reference.inc').write_text('\n'.join(lines)+'\n')
 private='buzz_input_original_';lines=['static void buzz_input_original_dispatch(spu_context*);','static void buzz_input_original_drain(spu_context*,uint32_t);']+[f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x95dc})]
 lines+=[f'static void {private}000095DC(spu_context* c){{buzz_geometry_job_native_fail(c,"input reference custom frontier");}}']
 for pc in sorted(pcs):
  body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:private+f'{int(m[1],16):08X}',fs[pc]);body=body.replace('spu_drain_call(', 'buzz_input_original_drain(');lines+=[f'static void {private}{pc:08X}(spu_context* ctx){{{body}}}']
 lines+=['static void buzz_input_original_dispatch(spu_context* c){switch(c->pc){']+[f'case 0x{pc:X}:{private}{pc:08X}(c);return;' for pc in sorted(pcs|{0x95dc})]+['default:buzz_geometry_job_native_fail(c,"input reference escaped closed graph");}}']
 lines+=[r'''static void buzz_input_original_drain(spu_context* c,uint32_t link){
 for(unsigned steps=0;!(c->pc==link && (!g_spu_trampoline_fn || g_spu_trampoline_fn==spu_indirect_branch));++steps){
  const buzz_geometry_job_fn next=g_spu_trampoline_fn;if(!next || steps>=1048576)buzz_geometry_job_native_fail(c,"input reference synchronous frontier");
  g_spu_trampoline_fn=NULL;if(next==spu_indirect_branch)buzz_input_original_dispatch(c);else next(c);
 }g_spu_trampoline_fn=NULL;
}
static void buzz_input_original_run(spu_context* c){
 const unsigned stack=c->gpr[1]._u32[0];buzz_input_original_000093F0(c);
 for(unsigned steps=0;!(c->gpr[1]._u32[0]==stack && c->pc==c->gpr[0]._u32[0] && g_spu_trampoline_fn==(c->host_depth?NULL:spu_indirect_branch));++steps){
  const buzz_geometry_job_fn next=g_spu_trampoline_fn;if(!next || steps>=1048576)buzz_geometry_job_native_fail(c,"input reference root frontier");
  g_spu_trampoline_fn=NULL;if(next==spu_indirect_branch)buzz_input_original_dispatch(c);else next(c);
 }
}''']
 (out/'input-control-original.inc').write_text('\n'.join(lines)+'\n');print('Pinned whole input control:23 original blocks/all6 formats/95DC trap; independent closed original production reference')
if __name__=='__main__':main()
