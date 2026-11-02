"""Audit pinned 54E8/4178 output conversion, tables and original snapshots.

This is a bounded standard-library graph. User callbacks and reporting are
explicit frontiers, not evidence of pure/native execution or acceptance.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[2]
PREFIX='buzz_job_6eba_spu_func_'
PIN='5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812'
TABLES={0x5604:(0xc4,0x824,0x15c,0x42c),0x445c:(0x1c,0x274,0x424,0x740,0x974,0xb80,0xe58),0x4104:(0x68,0x60,0x58,0x58)}
COMPUTED={0x5558:0x5604,0x43d0:0x445c,0x40d0:0x4104}
FIELDS={1:0x4478,2:0x46d0,3:0x4880,4:0x4b9c,5:0x4dd0,6:0x4fdc,7:0x52b4}
CONTROL={0x54e8,0x5500,0x5538,0x5558,0x5614,0x5648,0x56ac}
BUILTINS={0x56c8,0x5718,0x5760,0x5970,0x5978,0x5a30,0x5d18,0x5d20,0x5e28,0x6050,0x6058}
spec=importlib.util.spec_from_file_location('ring_audit',ROOT/'tools/inspect/geometry-ring-audit.py')
ring=importlib.util.module_from_spec(spec);spec.loader.exec_module(ring)
spec=importlib.util.spec_from_file_location('transform_audit',ROOT/'tools/inspect/geometry-transform-audit.py')
scalar=importlib.util.module_from_spec(spec);spec.loader.exec_module(scalar)


def source_graph(raw):
    if hashlib.sha256(raw).hexdigest()!=PIN:raise ValueError('Unreviewed conversion lift')
    functions={int(pc,16):body for pc,body in re.findall(r'(?ms)^void '+PREFIX+r'([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}',raw.decode())}
    seen=set();todo=[0x54e8];edges={};dead={};indirect=[]
    while todo:
        pc=todo.pop()
        if pc in seen or pc==0x6158:continue
        body=functions[pc];live=ring.reachable_text(body)
        if re.search(r'\bspu_(wrch|rdch|rchcnt|stop|sync|dsync)\(',live):raise ValueError('New conversion I/O frontier')
        targets={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',live)}
        if pc in COMPUTED:
            base=COMPUTED[pc];targets|={base+offset for offset in TABLES[base]}
        if 'spu_indirect_branch' in live:indirect.append(f'{pc:X}')
        removed={int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',body)}-targets
        if removed:dead[f'{pc:X}']=[f'{x:X}' for x in sorted(removed)]
        seen.add(pc);todo.extend(targets-seen);edges[f'{pc:X}']=[f'{x:X}' for x in sorted(targets)]
    if len(seen)!=73 or not (CONTROL|BUILTINS)<=seen:raise ValueError('Conversion inventory changed')
    fields={}
    for storage,entry in FIELDS.items():
        region=set();todo=[entry]
        while todo:
            pc=todo.pop()
            if pc in region or pc in (0x4610,0x4618):continue
            region.add(pc);todo.extend({int(x,16) for x in re.findall(PREFIX+r'([0-9A-F]+)',ring.reachable_text(functions[pc]))}-region)
        fields[str(storage)]=[f'{pc:X}' for pc in sorted(region)]
    if sum(map(len,fields.values()))!=37:raise ValueError('Field data inventory changed')
    return dict(source_sha256=PIN,standard_nonreport_blocks=73,control_blocks=7,builtin_blocks=11,
        field_controller_blocks=9,field_data_blocks=37,size_blocks=7,getter_publication_blocks=2,
        edges=edges,dead_lexical_targets=dead,indirect_sites=sorted(indirect),field_regions=fields,
        reporting_frontier='6158 -> CC70/0298; then 5648',
        callback_frontiers={'4114':'custom stride at manager+88, queued','5614':'custom conversion at manager+8C, synchronous'},
        limitation='Bounded standard graph only; callbacks/report bodies are not native acceptance')


def validate_tables(raw):
    _,ls=scalar.unpack(raw)
    result={}
    for base,expected in TABLES.items():
        actual=struct.unpack_from('>'+str(len(expected))+'I',ls,base)
        if actual!=expected:raise ValueError(f'Conversion table changed at {base:X}')
        result[f'{base:X}']=[f'{base+offset:X}' for offset in actual]
    return result


def frame(raw,pc):
    r,ls=scalar.unpack(raw);sp=r[1][0]
    def store(at,v):struct.pack_into('>4I',ls,at&0x3fff0,*v)
    def load(at):return struct.unpack_from('>4I',ls,at&0x3fff0)
    if pc==0x54e8:
        store(sp-32,r[81]);r[81]=r[3];store(sp-64,r[126]);r[6]=scalar.splat(0x5500);r[126]=(0x5500,0,0,0)
    elif pc==0x5500:
        store(sp+16,r[0]);store(sp-16,r[80]);store(sp-48,r[82]);r[126]=tuple((a-b)&0xffffffff for a,b in zip(r[126],r[6]))
        store(sp-96,r[1]);r[1]=scalar.add(r[1],scalar.splat(-96));r[5]=load(r[3][0]);r[4]=scalar.rotate(r[5],13);r[2]=tuple(v&16 for v in r[4])
        if r[2][0]:r[3]=load(r[3][0]+0x50);r[82]=scalar.rotate(r[3],4)
    elif pc==0x5538:
        r[1]=scalar.add(r[1],scalar.splat(96));r[3]=r[82];sp=r[1][0];r[0]=load(sp+16)
        for reg in (80,81,82):r[reg]=load(sp-(reg-79)*16)
        r[126]=load(sp-64)
    else:raise ValueError('Unknown conversion frame boundary')
    return scalar.packed(r,ls)


def original_evidence(directory):
    def get(name):return (directory/name).read_bytes()
    def rows(name):
        with (directory/name).open(newline='') as stream:return list(csv.DictReader(stream))
    entries={int(row['pc'],16):int(row['entries']) for row in rows('all.entries.csv')}
    edges={(int(row['source'],16),int(row['target'],16)):int(row['count']) for row in rows('all.edges.csv')}
    if entries.get(0x10)!=19508 or any(entries.get(pc)!=7201 for pc in CONTROL|{0x4178}):raise ValueError('Original conversion coverage changed')
    for edge in ((0x5500,0x5558),(0x5614,0x56ac),(0x56ac,0x4178)):
        if edges.get(edge)!=7201:raise ValueError('Original custom conversion route changed')
    callback_absent={}
    for pc,fallthrough,count in ((0x4114,0x4128,21603),(0x5614,0x56ac,7201),(0x95dc,0x9668,11414)):
        outgoing={target:n for (source,target),n in edges.items() if source==pc}
        if entries.get(pc)!=count or outgoing!={fallthrough:count}:raise ValueError(f'Unaudited callback route observed at {pc:X}')
        callback_absent[f'{pc:X} -> {fallthrough:X}']=count
    if any(entries.get(pc,0) for pc in BUILTINS|{0x6158}):raise ValueError('Previously cold output path observed')
    counts={str(storage):entries.get(pc,0) for storage,pc in FIELDS.items()}
    if list(counts.values())!=[1104,7201,4513,36,0,4424,0] or entries.get(0x43d0)!=sum(counts.values()):raise ValueError('Output field dispatch coverage changed')
    boundaries=[]
    for pc in (0x54e8,0x5500,0x5538):
        name=f'pc_{pc:04X}';before=get(name+'.bin');after=get(name+'_exit.bin')
        a=json.loads(get(name+'.bin.json'));b=json.loads(get(name+'_exit.bin.json'))
        if a['job']!=b['job'] or a['status'] or b['status'] or a['stop'] or b['stop']:raise ValueError('Snapshot job/status mismatch')
        if frame(before,pc)!=after:raise ValueError(f'Full GPR/LS frame boundary differs {pc:X}')
        boundaries.append(dict(pc=f'{pc:X}',bytes_compared=len(before),entry_sha256=hashlib.sha256(before).hexdigest(),exit_sha256=hashlib.sha256(after).hexdigest()))
    r,ls=scalar.unpack(get('pc_54E8.bin'));out,outls=scalar.unpack(get('pc_5538_exit.bin'));manager=r[3][0]&0x3fff0
    if json.loads(get('pc_54E8.bin.json'))['job']!=json.loads(get('pc_5538_exit.bin.json'))['job']:raise ValueError('Publication snapshots from different jobs')
    old=struct.unpack_from('>4I',ls,manager+0x50);actual=struct.unpack_from('>4I',outls,manager+0x50);descriptor=struct.unpack_from('>I',ls,manager+0x74)[0]
    vertices=struct.unpack_from('>H',ls,manager+8)[0];stride=ls[descriptor+1]
    expected=(old[0],old[2],(old[2]+((vertices*stride+15)&~15))&0xffffffff,old[3])
    if actual!=expected or out[3]!=scalar.rotate(old,8):raise ValueError('Independent conversion publication/return differs')
    if '[replay] 19508 exact, 0 with concurrent input changes, 0 FAILED (58195 transfers)' not in (directory/'replay.log').read_text():raise ValueError('Original conversion replay failed')
    return dict(jobs=19508,transfers=58195,conversion_roots=7201,field_dispatches=17278,
        field_entries=counts,all_builtin_entries=0,custom_conversion_callbacks=0,callback_absent_edges=callback_absent,frame_boundaries=boundaries,
        independent_publication=dict(vertices=vertices,stride=stride,byte_count=vertices*stride,all_metadata_words_exact=True,full_return_vector_exact=True),
        tables=validate_tables(get('pc_54E8.bin')),native_acceptance=False,
        limitation='Original-only trace and GPR/LS snapshots; no captured channel/architectural state, live, native conversion or speed claim')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists() or not args.output.resolve().is_relative_to(ROOT/'analysis'):raise ValueError('Choose a fresh private output')
    result=source_graph((ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes());result['original']=original_evidence(args.directory)
    paths=[p for p in args.directory.iterdir() if p.is_file() and p!=args.output]
    paths += [Path(__file__),ROOT/'tools/inspect/geometry-ring-audit.py',ROOT/'tools/inspect/geometry-transform-audit.py',ROOT/'tools/codegen/generate-geometry-trace.py',ROOT/'port/geometry_trace.h',ROOT/'analysis/native-geometry/trace-game.exe']
    result['bindings']={str(p.resolve().relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    args.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS output-conversion audit:73 standard blocks;19508 original exact jobs;7201 custom roots;17278 field dispatches;3 full GPR/LS frame models;independent publication/return;native gates OPEN')


if __name__=='__main__':main()
