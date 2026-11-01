"""Emit pinned full transform-finalization oracle; derived bytes stay private."""
import hashlib
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='f31aedf47937442351f865f0fdceb98438fefa1196804d018fcb798b70136985'
PREFIX='buzz_job_6eba_spu_func_'


def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed transform-finish source')
    if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/transform-audit-v1/pc_6958.bin').read_bytes()).hexdigest()!=ENTRY_PIN:
        raise SystemExit('Unreviewed real transform-finish entry')
    functions={int(pc,16):b for pc,b in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected=(0x2738,0x6958,0x66c8)
    text='namespace transform_finish_oracle {\n'+'\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in selected)+'\n'
    for pc in selected:
        body=functions[pc]
        if pc==0x2738:body=body.split('SPU_RET(ctx);')[0]+'SPU_RET(ctx);'
        targets={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',body)}
        if targets-set(selected):raise SystemExit('Unreviewed finalization callee')
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:'transform_finish_oracle::'+m[0],body)
        text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    text+='}\n';out=ROOT/'analysis/native-geometry/phase3';out.mkdir(parents=True,exist_ok=True)
    (out/'test-transform-finish-reference.inc').write_text(text,encoding='utf-8')
    print('Pinned transform-finish oracle:6958/66C8 and pure2738 publication;dead2750 tail removed')


if __name__=='__main__':main()
