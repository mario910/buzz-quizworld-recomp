"""Emit pinned private vertex-preparation oracle, stopping before Stage F."""
import hashlib
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='628719d0622b933a4c1871e6636ffdb25277cf6b089f61310c25b83508bcf1cc'
PREFIX='buzz_job_6eba_spu_func_'


def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed vertex-preparation lift')
    if hashlib.sha256((ROOT/'analysis/native-geometry/phase3/transform-audit-v1/pc_6188.bin').read_bytes()).hexdigest()!=ENTRY_PIN:
        raise SystemExit('Unreviewed real vertex-preparation entry')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected={0x6188,0x65ec,0x65fc};boundary=0x63e0
    text='namespace vertex_prepare_oracle {\n'
    text+='\n'.join(f'static void {PREFIX}{pc:08X}(spu_context*);' for pc in sorted(selected|{boundary}))+'\n'
    text+=f'static void {PREFIX}{boundary:08X}(spu_context*) {{ abort(); }}\n'
    for pc in sorted(selected):
        lines=[]
        for line in functions[pc].splitlines():
            lines.append(line)
            if re.fullmatch(r'\s*\{ ctx->pc = 0x[0-9A-F]+; g_spu_trampoline_fn = '+PREFIX+r'[0-9A-F]+; return; \}\s*',line):break
        body='\n'.join(lines)
        targets={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',body)}
        if targets-(selected|{boundary}) or re.search(r'spu_(?:wrch|rdch|ls_write128)',body):raise SystemExit('Unreviewed vertex-preparation effects/continuation')
        body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:'vertex_prepare_oracle::'+m[0],body)
        text+=f'static void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}\n'
    text+='}\n';out=ROOT/'analysis/native-geometry/phase3';out.mkdir(parents=True,exist_ok=True)
    (out/'test-vertex-prepare-reference.inc').write_text(text,encoding='utf-8')
    print('Pinned vertex-preparation oracle:6188/65EC/65FC;63E0 trap;dead6608 tail excluded')


if __name__=='__main__':main()
