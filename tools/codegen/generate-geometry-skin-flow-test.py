"""Pinned AF18 control/palette oracle with explicit retained data boundaries."""
import hashlib
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
PREFIX='buzz_job_6eba_spu_func_'
def main():
 raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
 if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed skin flow source')
 functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
 selected={0xaf18,0xaf40,0xaff8,0xb078,0xb100,0xb24c,0xb250,0xb318,0xb348,0xb4c0,0xb868,0xc488,0xc500}
 ordinary={0x2750,0x2778,0x2780,0x2790,0x2738}
 roots={0xb880,0xbd4c,0xbe20,0xbf7c,0xc14c,0xc280,0xb5a0}
 output='namespace skin_flow_oracle {\n'
 output+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|ordinary|roots|{0xcc70,0xc518,0xbb58}))+'\n'
 output+='static void rigid_suffix(spu_context*);\n'
 for pc in sorted(ordinary):
  body=functions[pc].split('SPU_RET(ctx);')[0]+'SPU_RET(ctx);'
  output+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{skin_flow_trace(ctx,0x{pc:x});{body}}}\n'
 for pc in sorted(roots|{0xbb58}):
  tail=0xb868 if pc not in {0xb880,0xbb58} else 0xb4c0
  output+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{skin_flow_model(ctx,0x{pc:x});ctx->pc=0x{tail:x};g_spu_trampoline_fn={PREFIX}{tail:08X};}}\n'
 for pc in (0xcc70,0xc518):output+=f'static void {PREFIX}{pc:08X}(spu_context*) {{abort();}}\n'
 output+=f'static void rigid_suffix(spu_context* ctx) {{skin_flow_model(ctx,0xb348);ctx->pc=0xb4c0;g_spu_trampoline_fn={PREFIX}0000B4C0;}}\n'
 for pc in sorted(selected):
  body=functions[pc]
  if pc==0xb348:
   marker='        ctx->gpr[27] = spu_ilhu(0x8000);'
   assert body.count(marker)==1
   body=body[:body.index(marker)]+'        rigid_suffix(ctx);\n'
  if pc==0xb4c0:body=body.split('        ctx->stop_code =')[0]
  if pc==0xc500:body=body[:body.index('        /* nop */;',body.index('0xB24C'))]
  output+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}\n'
 output+='static const buzz_geometry_skin_flow_functions library={'+','.join(f'{PREFIX}{pc:08X}' for pc in (0x2750,0x2778,0x2780,0x2790,0x2738,0xb880))+',rigid_suffix,{'+','.join(f'{PREFIX}{pc:08X}' for pc in (0xbd4c,0xbe20,0xbf7c,0xc14c,0xc280,0xb5a0))+'},'+f'{PREFIX}0000B4C0,{PREFIX}0000B868'+'};\n'
 output+='static void dispatch(spu_context* c){switch(c->pc){'+''.join(f'case 0x{pc:x}:return {PREFIX}{pc:08X}(c);' for pc in sorted(roots|{0xb868}))+'default:abort();}}\n}\n'
 out=ROOT/'analysis/native-geometry/phase3';out.mkdir(parents=True,exist_ok=True)
 output=re.sub(r'(?<!::)buzz_job_6eba_spu_func_([0-9A-F]+)\(ctx\)',r'skin_flow_oracle::buzz_job_6eba_spu_func_\1(ctx)',output)
 (out/'test-skin-flow-reference.inc').write_text(output,encoding='utf-8')
 print('Pinned skin flow oracle:13 complete control/palette blocks;5 original pure LS callees; explicit retained data models')
if __name__=='__main__':main()
