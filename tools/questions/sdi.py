"""Buzz! Quiz World SDI containers (CRI Sofdec MPEG-1 system stream + SDI header).

Picture questions show one frame of a "picture sheet" (QUESTIONASSETS/VIDEO/
picsNNN.sdi). build_picture_sheet() makes a new sheet from images: FFmpeg
encodes one MPEG-1 intra picture per image (640x368, 25 fps), and the muxing,
Sofdec header packs and SDI header/checksum are written here to match the
shipped sheets.

  python -I sdi.py check <file.sdi>
  python -I sdi.py build <template picsNNN.sdi> <out.sdi> <image> [<image> ...]

The template supplies the three Sofdec header packs (system header, SFD
header, CRITAGS); only counts, sizes and the name are patched.
"""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

PACK = 2048
PES_PAYLOAD = PACK - 12 - 6        # pack header 12, PES header 6 -> 2030-byte PES
DATA_START = 0x800
WIDTH, HEIGHT = 640, 368
DISPLAY_WIDTH, DISPLAY_HEIGHT = 640, 360   # 16:9 scene; stored video rows are 368
TICKS_PER_FRAME = 3600             # 90 kHz / 25 fps
MUX_RATE = 0x040F39                # units of 50 B/s, from the shipped sheets


def header_checksum(head):
    """Loader check (func_0024BDB8 over 0x4C bytes, header words 14/15 swapped)."""
    h = bytearray(head[:0x4C])
    h[0x38:0x40] = h[0x3C:0x40] + h[0x38:0x3C]
    return zlib.crc32(bytes(h)) ^ 0xFFFFFFFF ^ 0xFABCDEF7


def check(data):
    w = struct.unpack_from(">20I", data)
    if data[64:72] != b"SDIVIDEO":
        raise ValueError("missing SDIVIDEO")
    if struct.unpack_from(">I", data, 0x4C)[0] != header_checksum(data):
        raise ValueError("header checksum mismatch")
    if w[12] != len(data):
        raise ValueError("size field mismatch")
    offs = struct.unpack_from(f">{w[13]}I", data, w[9])
    for o in offs:
        if data[o:o + 4] != b"\0\0\1\xba":
            raise ValueError(f"index entry {o:#x} is not a pack")
    return dict(width=w[4], height=w[5], frames=w[13], picture_sheet=w[0] == 1, fps_x1000=w[7])


def _scr_bytes(scr):
    # MPEG-1 pack header: '0010' + SCR[32..30] + 1, SCR[29..15] + 1, SCR[14..0] + 1, mux rate
    return bytes([0x21 | ((scr >> 29) & 0x0E), (scr >> 22) & 0xFF, ((scr >> 14) & 0xFE) | 1,
                  (scr >> 7) & 0xFF, ((scr << 1) & 0xFE) | 1])


def _mux_bytes(rate):
    v = (rate << 1) | 0x800001
    return v.to_bytes(3, "big")


def _ts(prefix, t):
    return bytes([prefix | ((t >> 29) & 0x0E) | 1, (t >> 22) & 0xFF, ((t >> 14) & 0xFE) | 1,
                  (t >> 7) & 0xFF, ((t << 1) & 0xFE) | 1])


def _video_packs(frames, first_pack_no):
    """frames: list of MPEG-1 picture byte strings (each from a sequence header).
    Returns (packs bytes, [pack index where each frame starts])."""
    out, starts = bytearray(), []
    n = first_pack_no
    for i, f in enumerate(frames):
        starts.append(n)
        pos = 0
        first = True
        while pos < len(f):
            if first:
                pts = i * TICKS_PER_FRAME
                hdr = b"\x60\x2e" + _ts(0x30, pts) + _ts(0x10, pts)     # STD + PTS/DTS (12 bytes)
            else:
                hdr = b"\xff" * 9 + b"\x60\x2e\x0f"                     # stuffing + STD (12 bytes)
            room = PES_PAYLOAD - len(hdr)
            chunk = f[pos:pos + room]
            pos += len(chunk)
            pes = hdr + chunk
            body = b"\0\0\1\xe0" + struct.pack(">H", len(pes)) + pes
            if len(chunk) < room:   # last pack of a frame: pad with a padding stream
                pad = PACK - 12 - len(body)
                if pad >= 6:
                    body += b"\0\0\1\xbe" + struct.pack(">H", pad - 6) + b"\xff" * (pad - 6)
                else:   # too little room for a padding packet: grow stuffing instead
                    body = body[:6] + b"\xff" * pad + body[6:]
                    body = body[:4] + struct.pack(">H", len(body) - 6) + body[6:]
            scr = round(n * PACK * 90000 / (MUX_RATE * 50))
            out += b"\0\0\1\xba" + _scr_bytes(scr) + _mux_bytes(MUX_RATE) + body
            assert len(out) % PACK == 0
            first = False
            n += 1
    return bytes(out), starts


def encode_frames(images, quality=2):
    """MPEG-1 pictures fitted to 16:9, stored as the game's 640x368 raster."""
    # Fit in display coordinates before stretching to the storage raster.
    # The scene maps all 368 rows onto a 16:9 screen (media aspect enum 4).
    vf = (f"scale={DISPLAY_WIDTH}:{DISPLAY_HEIGHT}:force_original_aspect_ratio=decrease,"
          f"pad={DISPLAY_WIDTH}:{DISPLAY_HEIGHT}:(ow-iw)/2:(oh-ih)/2,"
          f"scale={WIDTH}:{HEIGHT},setsar=1")
    with tempfile.TemporaryDirectory() as td:
        # Normalise each image (any format FFmpeg reads) to a numbered PNG.
        for k, p in enumerate(images):
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(p), "-vf", vf, "-frames:v", "1",
                            str(Path(td) / f"f{k:05d}.png")], check=True)
        out = Path(td) / "out.m1v"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", "25", "-i", str(Path(td) / "f%05d.png"),
                        "-vf", "format=yuv420p", "-c:v", "mpeg1video", "-g", "1", "-q:v", str(quality),
                        "-f", "mpeg1video", str(out)], check=True)
        es = out.read_bytes()
    starts = []
    i = es.find(b"\0\0\1\xb3")
    while i != -1:
        starts.append(i)
        i = es.find(b"\0\0\1\xb3", i + 4)
    if len(starts) != len(images):
        raise RuntimeError(f"expected {len(images)} sequence headers, got {len(starts)}")
    es = bytearray(es)
    for s in starts:
        # Match the shipped sheets: pel aspect code 0xC, VBV buffer size 608;
        # keep FFmpeg's frame-rate code and quantiser-matrix flags.
        es[s + 7] = 0xC0 | (es[s + 7] & 0x0F)
        v = struct.unpack_from(">I", es, s + 8)[0]
        v = (v & ~(0x3FF << 3)) | (608 << 3)
        struct.pack_into(">I", es, s + 8, v)
    es = bytes(es)
    return [es[a:b] for a, b in zip(starts, starts[1:] + [len(es)])]


def _put_count(buf, off, value, width=4):
    s = str(value).encode() + b"\0"
    buf[off:off + width] = s.ljust(width, b" ")[:width]


def build_picture_sheet(template, images, name="pics004", quality=2):
    t = Path(template).read_bytes()
    check(t)
    head_packs = bytearray(t[DATA_START:DATA_START + 3 * PACK])
    frames = encode_frames(images, quality)
    n = len(frames)
    video, starts = _video_packs(frames, 3)

    # Sofdec header pack (+0x800): name, duration (ms), frame count, max/avg frame size
    sfd = PACK
    for off in (0x40, 0x1C0):
        head_packs[sfd + off:sfd + off + 8] = name.encode()[:7].ljust(7) + b" "
    struct.pack_into("<IIII", head_packs, sfd + 0xBC, n * 40, n, max(map(len, frames)),
                     sum(map(len, frames)) // n)
    # CRITAGS pack (+0x1000): frame counts / last frame as text
    tags = 2 * PACK
    for off in (0x68, 0x92, 0x1D5):
        _put_count(head_packs, tags + off, n)
    _put_count(head_packs, tags + 0xE6, n - 1)
    nm = name.encode()
    i = head_packs.find(t[DATA_START + tags + 0x1B0:DATA_START + tags + 0x1B8][:4], tags)
    if b"pics" in head_packs[tags + 0x1B0:tags + 0x1C0]:
        j = head_packs.find(b"pics", tags + 0x1B0)
        head_packs[j:j + 7] = nm[:7]

    body = bytes(head_packs) + video
    size = DATA_START + len(body)
    head = bytearray(t[:DATA_START])
    struct.pack_into(">I", head, 12 * 4, size)
    struct.pack_into(">I", head, 13 * 4, n)
    index_off = struct.unpack_from(">I", head, 9 * 4)[0]
    if index_off + 4 * n > DATA_START:
        raise ValueError(f"too many pictures for one sheet (max {(DATA_START - index_off) // 4})")
    head[index_off:DATA_START] = bytes(DATA_START - index_off)
    struct.pack_into(f">{n}I", head, index_off, *(DATA_START + s * PACK for s in starts))
    struct.pack_into(">I", head, 0x4C, header_checksum(head))
    out = bytes(head) + body
    check(out)
    return out


def main(argv):
    if argv and argv[0] == "check":
        print(check(Path(argv[1]).read_bytes()))
    elif argv and argv[0] == "build" and len(argv) >= 4:
        data = build_picture_sheet(argv[1], argv[3:], name=Path(argv[2]).stem)
        Path(argv[2]).write_bytes(data)
        print(argv[2], len(data), check(data))
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
