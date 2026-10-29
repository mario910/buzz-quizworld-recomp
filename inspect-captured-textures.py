"""Render bounded texture-upload diagnostics; never modifies game data."""
import io
import re
import struct
import sys
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(sys.argv[1])
pattern = re.compile(r'texture\[(\d+)\] key=(\w+) source=(\w+) (\d+)x(\d+) fmt=(\w+) dxgi=(\d+) pitch=(\d+) rows=(\d+) rowbytes=(\d+) cube=(\d+)')
tiles = []
for match in pattern.finditer((root / 'manifest.txt').read_text()):
    slot, key, source, w, h, fmt, dxgi, pitch, rows, rowbytes, cube = match.groups()
    slot, w, h, dxgi, pitch, rows, rowbytes = map(int, (slot,w,h,dxgi,pitch,rows,rowbytes))
    filename = root / f'texture_{slot:03}.bin'
    if not filename.exists(): continue
    data = filename.read_bytes()
    raw = b''.join(data[y*pitch:y*pitch+rowbytes] for y in range(rows))
    if dxgi in (71,74,77):
        fourcc = {71:b'DXT1',74:b'DXT3',77:b'DXT5'}[dxgi]
        header = struct.pack('<7I',124,0x81007,h,w,len(raw),0,1)+bytes(44)
        header += struct.pack('<II4s5I',32,4,fourcc,0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0)
        im = Image.open(io.BytesIO(b'DDS '+header+raw)).convert('RGBA')
    elif dxgi == 28: im = Image.frombytes('RGBA',(w,h),raw)
    elif dxgi == 61: im = Image.frombytes('L',(w,h),raw).convert('RGBA')
    elif dxgi == 49:
        im = Image.frombytes('LA',(w,h),raw).convert('RGBA')
    else:
        print('Skipped format',dxgi,filename.name); continue
    guest_file=root/f'guest_{slot:03}.bin'
    if guest_file.exists():
        guest=guest_file.read_bytes()
        if dxgi in (71,74,77) and guest!=raw:
            print(f'CHANGED compressed source slot={slot} key={key} uploaded={len(raw)} guest={len(guest)}')
        if im.convert('RGB').getbbox() is None:
            print(f'BLACK slot={slot} key={key} source_nonzero={sum(v!=0 for v in guest)}/{len(guest)} uploaded_nonzero={sum(v!=0 for v in raw)}/{len(raw)}')
    im.save(root / f'texture_{slot:03}.png')
    tile = Image.new('RGB',(160,190),(65,65,65))
    rgb=im.convert('RGB');rgb.thumbnail((156,140));tile.paste(rgb,(2,2))
    draw=ImageDraw.Draw(tile);draw.text((2,144),f'{slot} {key}\n{w}x{h} fmt={fmt}\nalpha={im.getchannel("A").getextrema()}',fill='white')
    tiles.append(tile)
for start in range(0,len(tiles),48):
    page=tiles[start:start+48]
    sheet=Image.new('RGB',(160*6,190*((len(page)+5)//6)),(25,25,25))
    for index,tile in enumerate(page):sheet.paste(tile,((index%6)*160,(index//6)*190))
    path=root/f'textures_{start//48:02}.png';sheet.save(path);print(path)
