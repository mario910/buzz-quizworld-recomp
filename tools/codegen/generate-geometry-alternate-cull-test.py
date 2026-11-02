"""Emit private original 69E8/6B18/6B10 oracle; this region has no real entry fixture."""
import hashlib
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
PREFIX='buzz_job_6eba_spu_func_'


def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed alternate-cull lift')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x69e8,0x6b18,0x6b10};boundary=0x6958
    text='namespace alternate_cull_oracle {\n'+'\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|{boundary}))+'\n'
    text+=f'static void {PREFIX}{boundary:08X}(spu_context*){{abort();}}\n'
    for pc in sorted(selected):
        body=functions[pc];targets={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',body)}
        if targets-(selected|{boundary}) or re.search(r'spu_(?:wrch|rdch|rchcnt|stop|sync|dsync)\s*\(',body):raise SystemExit('Unreviewed alternate cull effects/continuation')
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:'alternate_cull_oracle::'+m[0],body)
        text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    template=(ROOT/'analysis/native-geometry/phase3/transform-audit-v1/pc_6608.bin').read_bytes()
    if hashlib.sha256(template).hexdigest()!='9cf49a9e5643cb359b721a5d7b84d3f0214df3a2f98660107790d2b2fad3ecce':raise SystemExit('Unreviewed ordinary-controller table template')
    text+='static void seed_tables(spu_context& c){\n'
    addresses=(*range(0xdaf0,0xddf0,16),0xcfe0,0xcff0,*range(0xd000,0xd080,16),0xd930,0xd980,0xdaa0)
    for address in addresses:
        for i,word in enumerate(struct.unpack_from('>4I',template,2048+address)):
            text+=f'{{uint32_t value={word:#x}u;c.ls[{address+i*4}]=uint8_t(value>>24);c.ls[{address+i*4+1}]=uint8_t(value>>16);c.ls[{address+i*4+2}]=uint8_t(value>>8);c.ls[{address+i*4+3}]=uint8_t(value);}}\n'
    text+='}\n}\n';out=ROOT/'analysis/native-geometry/phase3';out.mkdir(parents=True,exist_ok=True)
    (out/'test-alternate-cull-reference.inc').write_text(text,encoding='utf-8')
    print('Pinned alternate cull oracle:3 complete original blocks;6958 trapped;no real-entry coverage claim')


if __name__=='__main__':main()
