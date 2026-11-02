"""Emit private pinned 6608 control graph with identical retained data boundaries."""
import hashlib
import importlib.util
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
import sys;sys.path.insert(0,str(ROOT/'tools/codegen'));from geometry_fixture import optional_fixture
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
ENTRY_PIN='9cf49a9e5643cb359b721a5d7b84d3f0214df3a2f98660107790d2b2fad3ecce'
PREFIX='buzz_job_6eba_spu_func_'


def main():
    raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit('Unreviewed transform-control lift')
    optional_fixture(ROOT/'analysis/native-geometry/phase3/transform-audit-v1/pc_6608.bin',ENTRY_PIN,'Unreviewed real transform-control entry')
    spec=importlib.util.spec_from_file_location('transform_audit',ROOT/'tools/inspect/geometry-transform-audit.py')
    audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit);audit.source_graph(raw)
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    selected=audit.NORMAL-{0x6e08,0x6ee0,0x6ee8,0x69e8,0x6b10,0x6b18}
    assert len(selected)==12
    all_pcs=selected|{0x2738,0xcc70,0x6188,0x6e08,0x69e8}
    def emit(prefix,qualifier='',test=False):
        output='\n'.join(f'static void {prefix}{pc:08X}(spu_context*);' for pc in sorted(all_pcs))+'\n'
        if test:
            output+='static const buzz_geometry_transform_functions* children=nullptr;\n'
            output+=f'static void {prefix}00006188(spu_context* ctx){{children->vertex(ctx);}}\n'
        else:output+=f'static void {prefix}00006188(spu_context* ctx){{{PREFIX}00006188(ctx);}}\n'
        bridge='::transform_control_test_bridge' if test else 'buzz_geometry_job_transform_reference_cull'
        for pc,child in ((0x6e08,'cull'),(0x69e8,'alternate')):
            root=f'children->{child}' if test else f'{PREFIX}{pc:08X}'
            boundary='children->finish' if test else f'{PREFIX}00006958'
            output+=f'static void {prefix}{pc:08X}(spu_context* ctx){{{bridge}(ctx,{root},{boundary},{qualifier}{prefix}00006958);}}\n'
        output+=f'static void {prefix}0000CC70(spu_context*){{fprintf(stderr,"transform reporting frontier entered\\n");abort();}}\n'
        for pc in sorted(selected|{0x2738}):
            body=audit.reachable(functions[pc])
            targets={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',body)}
            if targets-all_pcs or re.search(r'spu_(?:wrch|rdch|rchcnt|stop|sync|dsync)\s*\(',body):raise SystemExit('Unreviewed transform effects/continuation')
            body=re.sub(PREFIX+r'([0-9A-F]+)',lambda m:f'{qualifier}{prefix}{int(m[1],16):08X}',body)
            output+=f'static void {prefix}{pc:08X}(spu_context* ctx) {{{body}}}\n'
        return output
    out=ROOT/'analysis/native-geometry/phase3';out.mkdir(parents=True,exist_ok=True)
    (out/'test-transform-control-reference.inc').write_text('namespace transform_control_oracle {\n'+emit(PREFIX,'transform_control_oracle::',True)+'}\n',encoding='utf-8')
    (out/'transform-control-original.inc').write_text(emit('buzz_transform_control_original_'),encoding='utf-8')
    print('Pinned transform control:12 original enclosing blocks,pure2738,report trap;identical vertex/normal/alternate data callbacks')


if __name__=='__main__':main()
