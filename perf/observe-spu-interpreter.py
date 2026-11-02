"""Read existing interpreter statistics from an exact EXE/map, without writes.

Use in separate diagnostic runs, not FPS or CPU-cost benchmarks. Enable
SPU_INTERP_STATS=3600 from process start; every requested snapshot must precede
that first reset interval. A zero total proves no interpreted instructions,
including counts below the logger's 65,536-instruction reporting threshold.
Positive totals are diagnostic observations, not exact cross-thread counts:
the game's existing statistics use non-atomic increments.
"""
import argparse
import ctypes as c
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import time

spec=importlib.util.spec_from_file_location('process_work',Path(__file__).with_name('measure-process-work.py'))
work=importlib.util.module_from_spec(spec);spec.loader.exec_module(work)
LAYOUT={'s_is':4,'s_tot':8,'s_img':256,'s_page':256,'s_cnt':512,'s_t0':8}


def bind(executable,link_map):
    result=work.make_binding(executable,link_map)
    text=Path(link_map).read_text(errors='replace');preferred=re.search(r'Preferred load address is\s+([0-9a-fA-F]+)',text)
    base=int(preferred[1],16);symbols={}
    for name,size in LAYOUT.items():
        hits=re.findall(r'^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]+\s+spu_interp_run_until\.'+re.escape(name)+r'\s+([0-9a-fA-F]{16})\s+.*$',text,re.M)
        if len(hits)!=1:raise ValueError('Need one exact interpreter symbol: '+name)
        rva=int(hits[0],16)-base
        if rva<0 or rva+size>result['image_size'] or rva%min(size,8):raise ValueError('Invalid interpreter symbol: '+name)
        symbols[name]={'rva':rva,'size':size}
    spans=sorted((s['rva'],s['rva']+s['size']) for s in symbols.values())
    if any(a[1]>b[0] for a,b in zip(spans,spans[1:])):raise ValueError('Interpreter symbol spans overlap')
    result.update(interpreter_symbols=symbols,statistics_interval_seconds=3600)
    return result


def interpret(values,elapsed_seconds):
    if set(values)!=set(LAYOUT) or any(len(values[n])!=size for n,size in LAYOUT.items()):raise ValueError('Incomplete interpreter snapshot')
    interval=struct.unpack('<i',values['s_is'])[0]
    total,epoch=(struct.unpack('<Q',values[n])[0] for n in ('s_tot','s_t0'))
    counts=struct.unpack('<64q',values['s_cnt']);images=struct.unpack('<64i',values['s_img']);pages=struct.unpack('<64I',values['s_page'])
    if not 0<=elapsed_seconds<3600:raise ValueError('Snapshot could include a statistics reset')
    if interval not in (-1,3600):raise ValueError('Need SPU_INTERP_STATS=3600 from process start')
    if total>=2**63 or any(n<0 for n in counts):raise ValueError('Counter overflow/reset')
    if not total and any(counts):raise ValueError('Inconsistent zero interpreter total')
    if interval==-1 and (total or epoch or any(counts)):raise ValueError('Uninitialized statistics contain counts')
    if total and interval!=3600:raise ValueError('Counters are disabled')
    # No reset can have occurred before one hour. Lost non-atomic increments
    # cannot turn positive activity back to zero within that interval.
    return dict(enabled_interval=interval,total_observed=total,statistics_epoch_ms=epoch,
        buckets=[dict(image=image,page=page,observed=count) for image,page,count in zip(images,pages,counts) if count],
        all_images_zero_proven=not total,
        limitation='Positive counts may undercount concurrent increments or overflow the64 bucket inventory;zero total before first reset proves zero interpreted instructions')


def snapshot(target,binding,epoch):
    values={}
    for name,item in binding['interpreter_symbols'].items():
        if name not in LAYOUT or item['size']!=LAYOUT[name] or item['rva']<0 or item['rva']+item['size']>binding['image_size']:
            raise ValueError('Unreviewed interpreter memory span')
        data=c.create_string_buffer(item['size']);got=c.c_size_t()
        target.check(target.k.ReadProcessMemory(target.process,target.base+item['rva'],data,item['size'],c.byref(got)))
        if got.value!=item['size']:raise ValueError('Partial interpreter read')
        values[name]=data.raw
    elapsed=(target.now()-epoch)/1e9
    return dict(elapsed_seconds=elapsed,raw_hex={n:b.hex() for n,b in values.items()},**interpret(values,elapsed))


def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='command',required=True)
    b=commands.add_parser('bind');b.add_argument('--exe',type=Path,required=True);b.add_argument('--map',type=Path,required=True);b.add_argument('--output',type=Path,required=True)
    m=commands.add_parser('measure');m.add_argument('--pid',type=int,required=True);m.add_argument('--exe',type=Path,required=True);m.add_argument('--binding',type=Path,required=True);m.add_argument('--output',type=Path,required=True);m.add_argument('--at',nargs='+',type=float,required=True)
    args=parser.parse_args()
    if args.output.exists():raise ValueError('Output already exists')
    if args.command=='bind':work.write_new(args.output,bind(args.exe,args.map));return
    if args.at!=sorted(set(args.at)) or not 0<args.at[0]<=args.at[-1]<900:raise ValueError('Need ascending distinct snapshots below900 seconds')
    binding=json.loads(args.binding.read_text());expected=binding['interpreter_symbols']
    if set(expected)!=set(LAYOUT) or binding['statistics_interval_seconds']!=3600:raise ValueError('Wrong statistics binding')
    if work.sha256(args.exe)!=binding['exe_sha256']:raise ValueError('Executable changed')
    with_target=work.NativeProcess(args.pid,args.exe)
    try:
        deadline=time.monotonic()+30
        while not (epoch:=with_target.read_counter(binding,'g_render_perf_epoch')):
            if time.monotonic()>deadline or not with_target.alive():raise ValueError('Renderer epoch unavailable')
            time.sleep(.05)
        if with_target.now()>epoch+args.at[0]*1e9:raise ValueError('Attached after first diagnostic boundary')
        samples=[]
        for seconds in args.at:
            with_target.wait_until(epoch+int(seconds*1e9));item=snapshot(with_target,binding,epoch)
            if abs(item['elapsed_seconds']-seconds)>.1:raise ValueError('Missed interpreter diagnostic boundary')
            samples.append(item)
        work.write_new(args.output,dict(status='complete',pid=args.pid,exe_sha256=binding['exe_sha256'],binding_sha256=work.sha256(args.binding),
            method='ReadProcessMemory of six fixed map-bound existing statistics;no writes/suspension/CPU sampling',requested_seconds=args.at,snapshots=samples,
            all_images_zero_proven=all(s['all_images_zero_proven'] for s in samples)))
    finally:with_target.close()


if __name__=='__main__':main()
