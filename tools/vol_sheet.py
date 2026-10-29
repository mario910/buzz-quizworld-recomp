"""Contact sheet of a volume texture dumped by RSX_VOL_DUMP (diagnostics).

  vol_sheet.py <vol_*.bin> <out.png> [--stride BYTES] [--scale N]

Decodes DXT1 (f86) or linear ARGB (fA5/fA1-as-L8) slices assuming they are
consecutive 2D images (--stride overrides the slice size) and tiles them.
"""
import re, struct, sys, zlib


def png(path, w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def c565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31)


def dxt1(data, w, h):
    out = bytearray(w * h * 3)
    bw = w // 4
    for by in range(h // 4):
        for bx in range(bw):
            o = (by * bw + bx) * 8
            # RSX stores DXT blocks little-endian like PC DXT
            c0, c1, bits = struct.unpack_from("<HHI", data, o)
            a, b = c565(c0), c565(c1)
            if c0 > c1:
                pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
            else:
                pal = [a, b, tuple((x + y) // 2 for x, y in zip(a, b)), (0, 0, 0)]
            for py in range(4):
                for px in range(4):
                    c = pal[bits >> (2 * (py * 4 + px)) & 3]
                    i = ((by * 4 + py) * w + bx * 4 + px) * 3
                    out[i:i + 3] = bytes(c)
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    m = re.search(r"_(\d+)x(\d+)x(\d+)_f([0-9A-F]{2})_m\d+_p(\d+)", src)
    w, h, d, fmt, pitch = int(m[1]), int(m[2]), int(m[3]), int(m[4], 16), int(m[5])
    data = open(src, "rb").read()
    if fmt & 0x9F == 0x86:
        stride = w * h // 2
        dec = lambda s: dxt1(data[s:s + stride], w, h)
    else:
        bpp = 1 if fmt & 0x9F == 0x81 else 4
        row = pitch or w * bpp
        stride = row * h
        def dec(s):
            out = bytearray(w * h * 3)
            for y in range(h):
                for x in range(w):
                    if bpp == 1:
                        v = data[s + y * row + x]; out[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes((v, v, v))
                    else:
                        a, r, g, b = data[s + y * row + x * 4: s + y * row + x * 4 + 4]
                        out[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes((r, g, b))
            return out
    if "--stride" in args:
        stride = int(args[args.index("--stride") + 1], 0)
    scale = int(args[args.index("--scale") + 1]) if "--scale" in args else (4 if w >= 256 else 1)
    cols = 8
    rows = (d + cols - 1) // cols
    tw, th = w // scale, h // scale
    sheet = bytearray(cols * tw * rows * th * 3)
    for z in range(d):
        if (z + 1) * stride > len(data):
            break
        img = dec(z * stride)
        ox, oy = (z % cols) * tw, (z // cols) * th
        for y in range(th):
            for x in range(tw):
                si = ((y * scale) * w + x * scale) * 3
                di = ((oy + y) * cols * tw + ox + x) * 3
                sheet[di:di + 3] = img[si:si + 3]
    png(dst, cols * tw, rows * th, sheet)
    print(f"{src}: {w}x{h}x{d} fmt={fmt:02X} stride={stride} -> {dst}")


main()
