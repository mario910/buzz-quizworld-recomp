"""Read-only ELF string/BE-pointer cross references for title Lua bindings."""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
import argparse
import struct
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('elf',type=Path)
p.add_argument('names',nargs='+')
a=p.parse_args()
data=a.elf.read_bytes()
if data[:6]!=b'\x7fELF\x02\x02':
    raise SystemExit('Expected a big-endian ELF64')
phoff=struct.unpack_from('>Q',data,32)[0]
phsize,phcount=struct.unpack_from('>HH',data,54)
segments=[]
for i in range(phcount):
    header=phoff+i*phsize
    if struct.unpack_from('>I',data,header)[0]==1:
        off,addr=struct.unpack_from('>QQ',data,header+8)
        size=struct.unpack_from('>Q',data,header+32)[0]
        segments.append((off,addr,size))
def ea(offset):
    for off,addr,size in segments:
        if off<=offset<off+size:return addr+offset-off
    return 0
def offset(addr):
    for off,base,size in segments:
        if base<=addr<base+size:return off+addr-base
    return None
for name in a.names:
    pos=data.find(name.encode()+b'\0')
    if pos<0:
        print(name,'NOT FOUND');continue
    address=ea(pos)
    print(name,f'string={address:08X}')
    needle=struct.pack('>I',address)
    at=0
    while True:
        at=data.find(needle,at)
        if at<0:break
        if at%4==0:
            words=struct.unpack('>4I',data[at:at+16])
            print(f'  ref={ea(at):08X} words=',*[f'{w:08X}' for w in words])
            for w in words[1:3]:
                o=offset(w)
                if o is not None and o+8<=len(data):
                    print(f'    ptr={w:08X} first2=',*[f'{v:08X}' for v in struct.unpack('>2I',data[o:o+8])])
        at+=1
