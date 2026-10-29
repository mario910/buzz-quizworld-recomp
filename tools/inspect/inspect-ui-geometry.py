"""Evaluate position for verified Buzz VS hashes 4BDD73EC/94D9FAED.

Both write position from MVP rows c64..67. The latter also computes lighting;
that computation is intentionally not emulated here.

Read-only analysis of RSX_CAP_VERTICES + RSX_CAP_UI_CONSTANTS captures.
This is not a generic RSX vertex-program interpreter.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
import argparse
import math
import pathlib
import re
import struct

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('manifest',type=pathlib.Path)
p.add_argument('--draw',type=int,nargs='+',required=True)
args=p.parse_args()
blocks={int(m[1]):m[0] for m in re.finditer(
    r'^op(\d+) DRAW[^\n]*\n(?:(?!^op|^---).*(?:\n|$))*',args.manifest.read_text(),re.M)}
def values(text):return tuple(map(float,text.split(',')))
for number in args.draw:
    b=blocks[number]
    actual=re.search(r'vs_hash=([0-9A-F]+)',b)
    if not actual or int(actual[1],16) not in (0x4BDD73EC,0x94D9FAED):
        raise ValueError(f'draw {number}: vertex program is not the verified VS')
    constants={int(m[1]):values(m[2]) for m in re.finditer(r'\bc(\d+)=\(([^)]+)\)',b)}
    scale=values(re.search(r'vp_scale=\(([^)]+)\)',b)[1])
    offset=values(re.search(r'vp_offset=\(([^)]+)\)',b)[1])
    viewport=tuple(map(int,re.search(r'vp=(\d+),(\d+),(\d+)x(\d+)',b).groups()))
    raw=(args.manifest.parent/f'op_{number:04d}_vertices.bin').read_bytes()
    screen=[];outside=0
    for at in range(0,len(raw),256):
        position=struct.unpack_from('<4f',raw,at)
        clip=[sum(x*y for x,y in zip(position,constants[row])) for row in range(64,68)]
        if clip[3]<=0:outside+=1
        ndc=[(clip[k]/clip[3])*scale[k]+offset[k] if clip[3] else math.nan for k in range(3)]
        screen.append((viewport[0]+(.5+.5*ndc[0])*viewport[2],
                       viewport[1]+(.5-.5*ndc[1])*viewport[3],ndc[2]))
    cw=ccw=zero=0
    for at in range(0,len(screen)-2,3):
        a,b,c=screen[at:at+3]
        area=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        if abs(area)<1e-6:zero+=1
        elif area>0:cw+=1
        else:ccw+=1
    bounds=[(min(v[k] for v in screen),max(v[k] for v in screen)) for k in range(3)]
    print(f'draw {number}: vertices={len(screen)} screen bbox/depth={bounds}')
    print(f'  triangles screen-CW={cw}, CCW={ccw}, degenerate={zero}; w<=0 vertices={outside}')
    print(' ',re.search(r'cb_slot=.*',blocks[number])[0])
