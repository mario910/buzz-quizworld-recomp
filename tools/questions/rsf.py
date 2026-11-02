"""Buzz! Quiz World .rsf audio: 32-byte big-endian header + ATRAC3 frames.

  python -I rsf.py info <file.rsf>
  python -I rsf.py towav <file.rsf> <out.wav>      # decode via FFmpeg (ATRAC3)
  python -I rsf.py silence <out.rsf> <seconds>

Header (BE): u8 load type (4/5 preloaded/on-demand, 6 streamed), u8 0,
u8 channels, u8 ?, u16 rate, u16 ?, u32 data size, u32 data size, u32 frame
size, u32 ?, ... ; frames start at 0x20.
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HDR = 0x20
SAMPLES_PER_FRAME = 1024


def info(d):
    kind, _, ch, b3, rate, b6 = struct.unpack_from(">BBBBHH", d, 0)
    size, size2, frame, f14 = struct.unpack_from(">IIII", d, 8)
    return dict(kind=kind, channels=ch, rate=rate, data_size=size, frame_size=frame,
                frames=size // frame if frame else 0, seconds=(size // frame) * SAMPLES_PER_FRAME / rate)


def to_riff(d):
    """Wrap as WAVE_FORMAT_ATRAC3 (0x270) with the 14-byte extradata FFmpeg expects."""
    i = info(d)
    ch, rate, align = i["channels"], i["rate"], i["frame_size"]
    extra = struct.pack("<HIHHHH", 1, SAMPLES_PER_FRAME * ch, 0, 0, 1, 0)
    fmt = struct.pack("<HHIIHHH", 0x270, ch, rate, align * rate // SAMPLES_PER_FRAME, align, 0, len(extra)) + extra
    data = d[HDR:HDR + i["data_size"]]
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body


def to_wav(d, out):
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "in.wav"
        src.write_bytes(to_riff(d))
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(src), str(out)], check=True)


def frame_levels(d):
    """Peak sample per ATRAC3 frame (decoded with FFmpeg)."""
    i = info(d)
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "in.wav"
        src.write_bytes(to_riff(d))
        pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", str(src), "-f", "s16le", "-ac", "1", "-"],
                             check=True, capture_output=True).stdout
    n = len(pcm) // 2
    s = struct.unpack(f"<{n}h", pcm[:2 * n])
    return [max(map(abs, s[k:k + SAMPLES_PER_FRAME]), default=0) for k in range(0, n, SAMPLES_PER_FRAME)]


def speech_header(data_size, frame_size=0x98, channels=1, rate=48000):
    """Header as used by on-demand question speech (type 5, mono, 0x98 frames)."""
    return struct.pack(">BBBBHHIIIIQ", 5, 0, channels, 1, rate, 1, data_size, data_size,
                       frame_size, data_size, 0)


def silence(seconds, frame_size=0x98, rate=48000):
    """Digitally silent ATRAC3 clip: sound unit id 0x28, 1 band, no gain points,
    no tonal components, zero coded subbands (0xA0 then zero bits)."""
    n = max(1, round(seconds * rate / SAMPLES_PER_FRAME))
    data = (b"\xA0" + bytes(frame_size - 1)) * n
    return speech_header(len(data), frame_size, 1, rate) + data


def main(argv):
    if argv[:1] == ["info"]:
        print(info(Path(argv[1]).read_bytes()))
    elif argv[:1] == ["towav"]:
        to_wav(Path(argv[1]).read_bytes(), argv[2])
    elif argv[:1] == ["silence"]:
        Path(argv[1]).write_bytes(silence(float(argv[2])))
        print(info(Path(argv[1]).read_bytes()))
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
