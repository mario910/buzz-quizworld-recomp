"""Private pinned built-in format-1 oracle and full original conversion graph."""
from pathlib import Path
import hashlib,importlib.util,re
ROOT=Path(__file__).resolve().parents[2];PREFIX='buzz_job_6eba_spu_func_'
ENTRY_PIN='40386ad034b6fc29264cc5c985d75a415dd2e480c36a7f99b9a474f94a390962'
def main():
 spec=importlib.util.spec_from_file_location('builtin_audit',ROOT/'tools/inspect/geometry-conversion-audit.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
 raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
 if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin').read_bytes()).hexdigest()!=ENTRY_PIN:raise SystemExit('Unreviewed calibration')
 functions={int(pc,16):audit.ring.reachable_text(body) for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
 selected={0x5e28,0x6050,0x6058};endpoints={0x5648};targets={int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])};assert targets-selected==endpoints
 lines=['namespace builtin20_output_oracle {']+[f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|endpoints)]
 lines+=[f'static void {PREFIX}00005648(spu_context*){{fprintf(stderr,"builtin float3 endpoint entered\\n");abort();}}']
 lines+=[f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}' for pc in sorted(selected)]+['}']
 out=ROOT/'analysis/native-geometry/phase3';(out/'test-builtin20-output-reference.inc').write_text('\n'.join(lines)+'\n')
 private='buzz_builtin20_output_original_';production='\n'.join(f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected))+'\n'+f'void {PREFIX}00005648(spu_context*);\n'
 for pc in sorted(selected):
  body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc]);production+=f'static void {private}{pc:08X}(spu_context* ctx) {{{body}}}\n'
 (out/'builtin20-output-original.inc').write_text(production)
 pcs={int(pc,16) for pc in graph['edges']};assert len(pcs)==73;ns='builtin20_conversion_oracle';prefix=ns+'::'+PREFIX
 lines=[f'namespace {ns} {{']+[f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x6158})]
 lines+=[f'static void {PREFIX}00006158(spu_context*){{fprintf(stderr,"builtin conversion report\\n");abort();}}','static void (*native_data)(spu_context*)=nullptr;']
 for pc in sorted(pcs):
  body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:prefix+f'{int(m[1],16):08X}',functions[pc])
  if pc==0x5e28:body='\n if(native_data){native_data(ctx);return;}\n'+body
  lines+=[f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}']
 lines+=['static void dispatch(spu_context* c){switch(c->pc){']+[f'case 0x{pc:X}:{PREFIX}{pc:08X}(c);return;' for pc in sorted(pcs|{0x6158})]+['default:fprintf(stderr,"builtin conversion escaped %X\\n",c->pc);abort();}}','}']
 (out/'test-builtin20-conversion-reference.inc').write_text('\n'.join(lines)+'\n')
 print('Pinned built-in 20-byte oracle:3 original blocks;5648 endpoint trap;73-block independent full conversion graph;synthetic format1 calibration,not captured cold root')
if __name__=='__main__':main()
