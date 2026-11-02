"""Audit original 6608 transform/cull flow and independent integer boundaries.

Private snapshots contain GPR/LS only; these proofs are not native acceptance.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
NORMAL={0x6608,0x6618,0x66b8,0x66c8,0x6744,0x6790,0x6958,0x69b0,0x69e8,0x6b10,0x6b18,0x6dfc,0x6e00,0x6e08,0x6ee0,0x6ee8,0x710c,0x7110}
LIBRARY={0x6188,0x2738};VERTEX={0x6188,0x63e0,0x6490,0x65ec,0x65fc};PREFIX='buzz_job_6eba_spu_func_'
MASK=0xffffffff;GPR=2048;LS=0x40000


def reachable(body):
    lines=[]
    for line in body.splitlines():
        lines.append(line)
        if line.strip()=='SPU_RET(ctx);' or re.fullmatch(r'\s*\{ ctx->pc = 0x[0-9A-F]+; g_spu_trampoline_fn = '+PREFIX+r'[0-9A-F]+; return; \}\s*',line):break
    return '\n'.join(lines)


def source_graph(raw):
    if hashlib.sha256(raw).hexdigest()!=PIN:raise ValueError('Unreviewed transform lift')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    seen=set();todo=[0x6608];edges={};dead={}
    while todo:
        pc=todo.pop()
        if pc in seen or pc in LIBRARY or pc==0xcc70:continue
        body=functions[pc];live=reachable(body);targets={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',live)}
        if re.search(r'\bspu_(?:wrch|rdch|rchcnt|stop|sync|dsync)\s*\(',live):raise ValueError('Unexpected transform control I/O')
        seen.add(pc);todo.extend(targets-seen);edges[f'{pc:X}']=[f'{x:X}' for x in sorted(targets)]
        removed={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',body)}-targets
        if removed:dead[f'{pc:X}']=[f'{x:X}' for x in sorted(removed)]
    if seen!=NORMAL:raise ValueError('Transform graph changed')
    vertex_seen=set();todo=[0x6188]
    while todo:
        pc=todo.pop()
        if pc in vertex_seen:continue
        vertex_seen.add(pc);todo.extend({int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',reachable(functions[pc]))}-vertex_seen)
    if vertex_seen!=VERTEX:raise ValueError('Vertex-preparation graph changed')
    return dict(source_sha256=PIN,control_blocks=len(seen),edges=edges,dead_lexical_targets=dead,
                retained_vertex_blocks=[f'{x:X}' for x in sorted(VERTEX)],reporting_frontier='6744 -> CC70',
                existing_stagef_cull=['6E08','6EE0','6EE8'],alternate_cull=['69E8','6B10','6B18'])


def splat(x):return (x&MASK,)*4
def add(a,b):return tuple((x+y)&MASK for x,y in zip(a,b))
def data(v):return struct.pack('>4I',*v)
def words(b):return struct.unpack('>4I',b)
def rotate(v,n):b=data(v);n&=15;return words(b[n:]+b[:n])
def selector(position,size):
    b=bytearray(range(16,32));start=position&~(size-1)&15;b[start:start+size]=bytes(range(4-size,4));return words(b)
def shuffle(a,b,c):
    source=data(a)+data(b)
    return words(bytes(source[x&31] if x<128 else 0 if x<192 else 255 if x<224 else 128 for x in data(c)))
def div3(value):
    """Source's split 16-bit products/carry form the high word of n*AAAAAAAB."""
    value&=65535;low0=value*0xaaaa;low1=value*0xaaab;shifted=(low0<<16)&MASK
    high=(low0>>16)+int(low1+shifted>MASK)
    return high>>1


def unpack(raw):
    if len(raw)!=GPR+LS:raise ValueError('Invalid snapshot length')
    return [struct.unpack_from('<4I',raw,i*16) for i in range(128)],bytearray(raw[GPR:])
def packed(r,ls):return b''.join(struct.pack('<4I',*v) for v in r)+ls
def frame_snapshot(raw,through_setup=True):
    r,ls=unpack(raw);sp=r[1][0]
    store=lambda at,v:struct.pack_into('>4I',ls,at&0x3fff0,*v)
    store(sp-0x190,r[126]);r[2]=splat(0x6618);r[126]=(0x6618,0,0,0)
    if through_setup:
        store(sp-16,r[80]);r[80]=r[4];store(sp-32,r[81]);r[81]=r[3];store(sp+16,r[0])
        r[126]=tuple((x-y)&MASK for x,y in zip(r[126],r[2]))
        for reg in range(82,104):store(sp-(reg-79)*16,r[reg])
        store(sp-480,r[1]);r[1]=add(r[1],splat(-480))
        if not r[4][0]:raise ValueError('Captured frame model requires the active 6744 route')
    return packed(r,ls)
def finish_snapshot(raw,restore=False):
    r,ls=unpack(raw);load=lambda at:struct.unpack_from('>4I',ls,at&0x3fff0)
    store=lambda at,v:struct.pack_into('>4I',ls,at&0x3fff0,*v)
    if not restore:
        r[64]=add(r[50],r[50]);r[83]=load(r[81][0]);r[80]=add(r[64],r[50]);r[85]=load(r[81][0]+0x180)
        r[84]=selector(r[1][0],2);r[97]=rotate(r[83],13);r[52]=shuffle(r[80],r[85],r[84]);r[51]=tuple(x&16 for x in r[97])
        store(r[81][0]+0x180,r[52])
        if not r[51][0]:
            r[48]=add(r[80],r[80]);r[50]=load(r[81][0]+0x50);r[3]=r[81];r[36]=add(r[48],r[50]);r[81]=add(r[36],splat(15))
            r[4]=tuple(x&0xfffffff0 for x in r[81]);r[0]=(0x69a8,0,0,0)
            r[6]=load(r[3][0]+0x50);r[5]=selector(r[1][0]+8,4);r[2]=shuffle(r[4],r[6],r[5]);store(r[3][0]+0x50,r[2])
    else:
        r[1]=add(r[1],splat(480));r[3]=r[80];r[0]=load(r[1][0]+16)
        for reg in range(80,104):r[reg]=load(r[1][0]-(reg-79)*16)
        r[126]=load(r[1][0]-0x190)
    return packed(r,ls)


def snapshots(directory):
    def get(name):return (directory/name).read_bytes()
    pairs=(('pc_6608.bin','pc_6608_exit.bin',lambda b:frame_snapshot(b,False)),
           ('pc_6608.bin','pc_6618_exit.bin',frame_snapshot),
           ('pc_6958.bin','pc_6958_exit.bin',finish_snapshot),
           ('pc_66C8.bin','pc_66C8_exit.bin',lambda b:finish_snapshot(b,True)))
    results=[]
    for first,last,model in pairs:
        a,b=get(first),get(last);ma=json.loads(get(first+'.json'));mb=json.loads(get(last+'.json'))
        if ma['job']!=mb['job'] or any(ma[k]!=mb[k] for k in ('status','stop')):raise ValueError('Snapshot jobs/status disagree')
        actual=model(a)
        if actual!=b:
            at=next(i for i,(x,y) in enumerate(zip(actual,b)) if x!=y);raise ValueError(f'Scalar boundary {first}/{last} differs at {at:X}')
        results.append(dict(entry=first,exit=last,all_gpr_all_ls_exact=True,bytes_compared=len(actual),entry_sha256=hashlib.sha256(a).hexdigest(),exit_sha256=hashlib.sha256(b).hexdigest()))
    if get('pc_6958_exit.bin')!=get('pc_66C8.bin'):raise ValueError('Publication/restore snapshot handoff changed')
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    a.directory=a.directory.resolve();a.output=a.output.resolve()
    if a.output.exists():raise ValueError('Choose a fresh verdict filename')
    result=source_graph((ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes());result['scalar_snapshots']=snapshots(a.directory)
    if any(div3(n)!=n//3 for n in range(65536)):raise ValueError('Halfword division identity differs')
    entries={int(r['pc'],16):int(r['entries']) for r in csv.DictReader((a.directory/'all.entries.csv').open())}
    edges={(int(r['source'],16),int(r['target'],16)):int(r['count']) for r in csv.DictReader((a.directory/'all.edges.csv').open())}
    n=entries.get(0x6608,0)
    if n!=19508 or any(entries.get(pc,0)!=n for pc in (0x6618,0x6744,0x6790,0x6188,0x63e0,0x6e08,0x6958,0x66c8)):raise ValueError('Real active-stage coverage differs')
    if any(entries.get(pc,0) for pc in (0x66b8,0x69b0,0x69e8,0x6b18,0x710c,0x7110)) or edges.get((0x6744,0xcc70),0):raise ValueError('Previously cold/reporting path observed')
    if edges.get((0x6958,0x2738),0)!=n:raise ValueError('Actual end-publication coverage differs')
    if '[replay] 19508 exact, 0 with concurrent input changes, 0 FAILED (58195 transfers)' not in (a.directory/'replay.log').read_text():raise ValueError('Original replay did not pass')
    result.update(original_replay_exact=True,jobs=n,transfers=58195,halfword_division_cases=65536,native_acceptance=False,
        trace_entries={f'{pc:X}':entries.get(pc,0) for pc in sorted(NORMAL|VERTEX)},
        observed_modes={'1':n-entries[0x6e00],'2':entries[0x6dfc],'3':entries[0x6e00]-entries[0x6dfc]},
        limitation='GPR/LS snapshots omit architectural/channel state; no performance or full-stage native acceptance claim')
    result['bindings']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(a.directory.iterdir()) if p.is_file() and p!=a.output}
    result['sources']={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ('tools/inspect/geometry-transform-audit.py','port/geometry_trace.h','tools/codegen/generate-geometry-trace.py','analysis/native-geometry/trace-game.exe')}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS transform audit:18 control blocks;19508 original exact jobs;4 scalar GPR/LS boundaries;65536 division cases;full stage OPEN')


if __name__=='__main__':main()
