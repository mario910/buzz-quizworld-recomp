"""Emit the pinned private allocator oracle and stop at reporting frontiers."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ring_audit', ROOT / 'tools/inspect/geometry-ring-audit.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def main():
    raw = (ROOT / 'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
    audit.source_graph(raw)
    functions = {int(pc, 16): body for pc, body in re.findall(
        r'(?ms)^void ' + audit.PREFIX + r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}', raw.decode())}
    text = 'namespace ring_oracle {\n'
    for pc in sorted(audit.BLOCKS):
        text += f'static void {audit.PREFIX}{pc:08X}(spu_context*);\n'
    for pc in sorted(audit.BLOCKS):
        body = audit.reachable_text(functions[pc])
        if pc in (0x3dd0, 0x4078):
            body = 'abort();'  # Stop before invoking reporting effects.
        else:
            body = re.sub(audit.PREFIX + r'([0-9A-F]+)', lambda m: 'ring_oracle::' + m[0], body)
            body = body.replace('spu_wrch(', 'buzz_geometry_job_wrch(').replace('spu_rdch(', 'buzz_geometry_job_rdch(')
        text += f'static void {audit.PREFIX}{pc:08X}(spu_context* ctx) {{{body}\n}}\n'
    fixture = ROOT / 'analysis/native-geometry/phase3/ring-kernel-v1-fixture'
    pins = {
        'pc_37C0.bin': '9026b4ff07a20bac23577cb1d8a7d3147423aafb46cf70e73a05c21a95e837d9',
        'pc_38D0_exit.bin': 'd72889206e21703fe5f09d5b39fb3f9b5fdef0f856d3e53caba833b9ea03289f',
        'pc_3948_exit.bin': '6f7c5faebff57be2d407a0835833aca1eab0ecbcd5b6393f26cf61325a1337aa',
    }
    jobs = set()
    for name, pin in pins.items():
        if hashlib.sha256((fixture / name).read_bytes()).hexdigest() != pin:
            raise ValueError(f'Unreviewed real allocator fixture {name}')
        meta = json.loads((fixture / (name + '.json')).read_text())
        if meta['status'] or meta['stop']:
            raise ValueError('Real allocator fixture stopped')
        jobs.add(meta['job'])
    if jobs != {'menu-both_job_41_0000.bjc.gz'} or '[replay] 1 exact, 0 with concurrent input changes, 0 FAILED (6 transfers)' not in (fixture / 'replay.log').read_text():
        raise ValueError('Real allocator fixture replay/job binding failed')
    text += '''static void load_real(spu_context& c,const char* name){
        char path[512];snprintf(path,sizeof path,"analysis/native-geometry/phase3/ring-kernel-v1-fixture/%s",name);
        FILE* f=fopen(path,"rb");assert(f);
        assert(fread(c.gpr,sizeof c.gpr,1,f)==1 && fread(c.ls,SPU_LS_SIZE,1,f)==1 && fgetc(f)==EOF);fclose(f);
    }
'''
    text += '}\n'
    out = ROOT / 'analysis/native-geometry/phase3'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'test-ring-reference.inc').write_text(text)
    print(f'Pinned ring oracle {hashlib.sha256(raw).hexdigest()}:39 blocks;reporting frontiers trapped;original channel order retained')


if __name__ == '__main__':
    main()
