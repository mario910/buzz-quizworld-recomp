"""Pinned builtin input format1 and shared93F0 control, private originals only."""
from pathlib import Path
import hashlib,importlib.util,re
ROOT=Path(__file__).resolve().parents[2];PREFIX='buzz_job_6eba_spu_func_'
ENTRY_PIN='40386ad034b6fc29264cc5c985d75a415dd2e480c36a7f99b9a474f94a390962'
def main():
 spec=importlib.util.spec_from_file_location('input_ring_audit',ROOT/'tools/inspect/geometry-ring-audit.py');ring=importlib.util.module_from_spec(spec);spec.loader.exec_module(ring)
 raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
 if hashlib.sha256(raw).hexdigest()!='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812':raise SystemExit('Unreviewed input lift')
 if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/conversion-audit-v1/pc_54E8.bin').read_bytes()).hexdigest()!=ENTRY_PIN:raise SystemExit('Unreviewed input calibration')
 functions={int(pc,16):ring.reachable_text(body) for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
 selected={0xa8f8,0xaaf8,0xab00};ends={0x9498}
 assert {int(pc,16) for start in selected for pc in re.findall(PREFIX+r'([0-9A-F]+)',functions[start])}-selected==ends
 out=ROOT/'analysis/native-geometry/phase3';lines=['namespace builtin20_input_oracle {']+[f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|ends)]
 lines+=[f'static void {PREFIX}00009498(spu_context*){{fprintf(stderr,"builtin input data frontier executed\\n");abort();}}']
 lines+=[f'static void {PREFIX}{pc:08X}(spu_context* ctx){{{functions[pc]}}}' for pc in sorted(selected)]+['}']
 (out/'test-builtin20-input-reference.inc').write_text('\n'.join(lines)+'\n')
 pcs=selected|{0x93f0,0x9408,0x9498,0x9518,0x9580};ns='builtin20_input_control_oracle';prefix=ns+'::'+PREFIX
 lines=[f'namespace {ns} {{']+[f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(pcs|{0x95dc})]
 lines+=[f'static void {PREFIX}000095DC(spu_context*){{fprintf(stderr,"builtin input custom dispatch escaped\\n");abort();}}','static void (*native_data)(spu_context*)=nullptr;']
 for pc in sorted(pcs):
  body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:prefix+f'{int(m[1],16):08X}',functions[pc])
  if pc==0xa8f8:body='\nif(native_data){native_data(ctx);return;}\n'+body
  lines+=[f'static void {PREFIX}{pc:08X}(spu_context* ctx){{{body}}}']
 lines+=['static void dispatch(spu_context* c){switch(c->pc){']+[f'case 0x{pc:X}:{PREFIX}{pc:08X}(c);return;' for pc in sorted(pcs|{0x95dc})]+['default:fprintf(stderr,"builtin input control escaped %X\\n",c->pc);abort();}}','}']
 (out/'test-builtin20-input-control-reference.inc').write_text('\n'.join(lines)+'\n')
 private='buzz_builtin20_input_original_';lines=[f'static void {private}{pc:08X}(spu_context*);' for pc in sorted(selected)]+[f'void {PREFIX}00009498(spu_context*);']
 for pc in sorted(selected):
  body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:(private if int(m[1],16) in selected else PREFIX)+m[1],functions[pc]);lines+=[f'static void {private}{pc:08X}(spu_context* ctx){{{body}}}']
 (out/'builtin20-input-original.inc').write_text('\n'.join(lines)+'\n')
 print('Pinned builtin input1:3 original data blocks/9498 trap;separate8-block original93F0 graph/95DC trap;synthetic input1,not captured cold root')
if __name__=='__main__':main()
