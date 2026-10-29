"""Bounded debugger sampling of this experiment only. No guest RAM writes.

Briefly suspends each target thread to read RIP, always resumes in finally.
Cycles describe work between snapshots; sampling changes scheduling, so this
is diagnostic evidence, never a benchmark. Prints no unrelated process data.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[1]))
import argparse
import bisect
import collections
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct
import re
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int)
p.add_argument('--seconds',type=float,default=8)
p.add_argument('--interval',type=float,default=.02)
a=p.parse_args()
assert 0<a.seconds<=30 and .005<=a.interval<=1
k=c.WinDLL('kernel32',use_last_error=True)
for name in ('OpenProcess','OpenThread','CreateToolhelp32Snapshot'):
    getattr(k,name).restype=w.HANDLE
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD]
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD]
k.CloseHandle.argtypes=[w.HANDLE]
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
k.K32EnumProcessModules.argtypes=[w.HANDLE,c.c_void_p,w.DWORD,c.POINTER(w.DWORD)]
k.SuspendThread.argtypes=[w.HANDLE];k.SuspendThread.restype=w.DWORD
k.ResumeThread.argtypes=[w.HANDLE];k.ResumeThread.restype=w.DWORD
k.GetThreadContext.argtypes=[w.HANDLE,c.c_void_p]
k.QueryThreadCycleTime.argtypes=[w.HANDLE,c.POINTER(c.c_ulonglong)]
class Thread(c.Structure):
    _fields_=[('size',w.DWORD),('usage',w.DWORD),('tid',w.DWORD),('pid',w.DWORD),('base',w.LONG),('delta',w.LONG),('flags',w.DWORD)]
k.Thread32First.argtypes=[w.HANDLE,c.POINTER(Thread)]
k.Thread32Next.argtypes=[w.HANDLE,c.POINTER(Thread)]
process=k.OpenProcess(0x410,False,a.pid)
if not process: raise c.WinError(c.get_last_error())
handles={}
try:
    path=c.create_unicode_buffer(32768);length=w.DWORD(len(path))
    if not k.QueryFullProcessImageNameW(process,0,path,c.byref(length)):raise c.WinError(c.get_last_error())
    root=Path(__file__).resolve().parents[1]
    assert Path(path.value).resolve()==(root/'build-windows/BuzzQuizWorldRecomp.exe').resolve(), 'Not the experiment'
    module=(c.c_void_p*1)();needed=w.DWORD()
    assert k.K32EnumProcessModules(process,module,c.sizeof(module),c.byref(needed))
    image_base=module[0]
    pe=Path(path.value).read_bytes();pe_offset=struct.unpack_from('<I',pe,0x3c)[0]
    image_size=struct.unpack_from('<I',pe,pe_offset+24+56)[0]
    link_map=(root/'build-windows/BuzzQuizWorldRecomp.map').read_text(errors='replace')
    preferred=int(re.search(r'Preferred load address is ([0-9a-fA-F]+)',link_map)[1],16)
    symbol_names={}
    for match in re.finditer(r'^\s+0001:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(.+)$',link_map,re.M):
        symbol_names.setdefault(int(match[2],16)-preferred,match[1])
    addresses=sorted(symbol_names)
    snapshot=k.CreateToolhelp32Snapshot(4,0)
    if snapshot in (None,c.c_void_p(-1).value):raise c.WinError(c.get_last_error())
    try:
        entry=Thread();entry.size=c.sizeof(entry)
        ok=k.Thread32First(snapshot,c.byref(entry))
        while ok:
            if entry.pid==a.pid:
                h=k.OpenThread(0x40|0x8|0x2,False,entry.tid)
                if h:handles[entry.tid]=h
            ok=k.Thread32Next(snapshot,c.byref(entry))
    finally:k.CloseHandle(snapshot)
    cycles={};totals=collections.Counter();samples=collections.Counter()
    storage=c.create_string_buffer(1232+16);context=(c.addressof(storage)+15)&~15
    start=time.monotonic();rounds=0
    while time.monotonic()-start<a.seconds:
        for tid,h in handles.items():
            cycle=c.c_ulonglong()
            if not k.QueryThreadCycleTime(h,c.byref(cycle)):continue
            previous=cycles.get(tid,cycle.value);cycles[tid]=cycle.value
            delta=max(0,cycle.value-previous);totals[tid]+=delta
            c.memset(context,0,1232);c.c_uint32.from_address(context+48).value=0x100001
            if k.SuspendThread(h)==0xffffffff:continue
            try:
                if not k.GetThreadContext(h,context):continue
                rip=c.c_uint64.from_address(context+248).value
            finally:
                if k.ResumeThread(h)==0xffffffff:raise c.WinError(c.get_last_error())
            if image_base<=rip<image_base+image_size:
                samples[(tid,rip-image_base)]+=1
        rounds+=1;time.sleep(a.interval)
    print(f'pid={a.pid} rounds={rounds} seconds={time.monotonic()-start:.3f} image={image_size:x}')
    print('Thread cycle deltas (not CPU percentages):')
    for tid,n in totals.most_common(12):print(f'thread {tid}: {n} cycles')
    print('EXE RIP snapshots (not weighted CPU utilization):')
    functions=collections.Counter()
    for (tid,rva),n in samples.items():
        at=bisect.bisect_right(addresses,rva)-1
        functions[(tid,symbol_names[addresses[at]] if at>=0 else hex(rva))]+=n
    for (tid,name),n in functions.most_common(28):print(f'thread {tid} samples {n}: {name}')
finally:
    for h in handles.values():k.CloseHandle(h)
    k.CloseHandle(process)
