"""Check the real game's RGB readback against its packed GPU sample-grid.

BMP capture omits alpha; exact alpha is covered by zeta-msaa-gpu-test.c.
Only valid when the manifest says the packed copy is clean (not stale).
"""
from pathlib import Path
import re
import struct
import sys

def bmp(path):
    data = path.read_bytes()
    assert data[:2] == b"BM"
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    planes, bpp = struct.unpack_from("<HH", data, 26)
    compression = struct.unpack_from("<I", data, 30)[0]
    assert width > 0 and height > 0 and planes == 1 and bpp == 24 and compression == 0
    pitch = (width * 3 + 3) & ~3
    assert len(data) >= offset + pitch * height
    return width, height, [data[offset + y*pitch:offset + y*pitch + width*3] for y in range(height)]

folder = Path(sys.argv[1])
manifest = (folder / "manifest.txt").read_text()
count = 0
for m in re.finditer(r"offrt\[\d+\] off=0x([0-9A-F]+) (\d+)x(\d+) dxgi=28 sample_grid=(\d+)x(\d+) packed=1 dirty=0", manifest):
    address, w, h, sx, sy = m.groups()
    w, h, sx, sy = map(int, (w, h, sx, sy))
    sw, sh, source = bmp(folder / f"rt_{address}.bmp")
    pw, ph, packed = bmp(folder / f"color_grid_{address}.bmp")
    assert (sw, sh) == (w, h) and (pw, ph) == (w*sx, h*sy)
    for y in range(h):
        expected = b"".join(source[y][x:x+3]*sx for x in range(0, w*3, 3))
        for n in range(sy):
            assert packed[y*sy+n] == expected, (address, y, n)
    count += 1
    print(f"PASS: actual game color {address}, {w}x{h} -> {pw}x{ph}, every RGB texel exact")
assert count, "No clean packed RGBA8 readback in capture"
assert "color_grid_units=" in manifest, "No actual consumer bound the sample-grid image"
