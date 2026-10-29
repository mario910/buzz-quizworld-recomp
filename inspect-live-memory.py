"""Read-only guest-memory inspection of this experiment's Windows process.

Usage: python inspect-live-memory.py PID EA[:SIZE] ... (hex guest addresses).
Locates the arena by matching the verified ELF entry bytes, never writes RAM.
"""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct
import sys

class MBI(c.Structure):
    _fields_=[('BaseAddress',c.c_void_p),('AllocationBase',c.c_void_p),
              ('AllocationProtect',w.DWORD),('PartitionId',w.WORD),
              ('RegionSize',c.c_size_t),('State',w.DWORD),
              ('Protect',w.DWORD),('Type',w.DWORD)]
k=c.WinDLL('kernel32',use_last_error=True)
k.OpenProcess.restype=w.HANDLE
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD]
k.VirtualQueryEx.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(MBI),c.c_size_t]
k.VirtualQueryEx.restype=c.c_size_t
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
k.CloseHandle.argtypes=[w.HANDLE]
h=k.OpenProcess(0x410,False,int(sys.argv[1]))
if not h:raise c.WinError(c.get_last_error())
try:
    path=c.create_unicode_buffer(32768); n=w.DWORD(len(path))
    if not k.QueryFullProcessImageNameW(h,0,path,c.byref(n)):raise c.WinError(c.get_last_error())
    root=Path(__file__).resolve().parent
    assert Path(path.value).resolve()==(root/'build-windows/BuzzQuizWorldRecomp.exe').resolve(),path.value
    def read(addr,size):
        b=c.create_string_buffer(size); n=c.c_size_t()
        return b.raw if k.ReadProcessMemory(h,addr,b,size,c.byref(n)) and n.value==size else None
    sys.path.insert(0,str(root)); import buzz_paths
    elf=buzz_paths.eboot_elf().read_bytes()
    phoff=struct.unpack_from('>Q',elf,32)[0]; phsize,phnum=struct.unpack_from('>HH',elf,54)
    for i in range(phnum):
        typ,flags,off,va,_,sz,_,_=struct.unpack_from('>IIQQQQQQ',elf,phoff+i*phsize)
        if typ==1 and va<=0x10230<va+sz: expected=elf[off+0x10230-va:off+0x10230-va+32];break
    at=0;seen=set();arena=None
    while at<0x7FFFFFFFFFFF:
        m=MBI()
        if not k.VirtualQueryEx(h,at,c.byref(m),c.sizeof(m)):break
        base=m.AllocationBase
        if base and base not in seen:
            seen.add(base)
            if read(base+0x10230,32)==expected:arena=base;break
        at=(m.BaseAddress or 0)+m.RegionSize
    if arena is None:raise RuntimeError('Verified guest arena not found')
    print(f'guest arena: {arena:#x}')
    arguments=sys.argv[2:]
    dump_dir=None
    if '--dump-dir' in arguments:
        option=arguments.index('--dump-dir')
        dump_dir=Path(arguments[option+1]).resolve()
        dump_dir.mkdir(parents=True,exist_ok=True)
        del arguments[option:option+2]
    for arg in arguments:
        native=arg.startswith('native@')
        if native:
            modules=(c.c_void_p*1)();needed=w.DWORD()
            k.K32EnumProcessModules.argtypes=[w.HANDLE,c.c_void_p,w.DWORD,c.POINTER(w.DWORD)]
            if not k.K32EnumProcessModules(h,modules,c.sizeof(modules),c.byref(needed)):raise c.WinError(c.get_last_error())
            origin=modules[0];arg=arg[len('native@'):]
        else:origin=arena
        parts=arg.split(':'); ea=int(parts[0],16); size=int(parts[1],16) if len(parts)>1 else 0x80
        b=read(origin+ea,size)
        if b is None:print(f'{ea:08X}: unreadable');continue
        if dump_dir:
            destination=dump_dir/f'{ea:08X}.bin'
            destination.write_bytes(b)
            print(f'{ea:08X}: dumped {len(b)} bytes to {destination}')
            continue
        for off in range(0,len(b),16):
            line=b[off:off+16]
            print(f'{ea+off:08X}: '+ ' '.join(line[i:i+4].hex() for i in range(0,len(line),4)))
finally:k.CloseHandle(h)
