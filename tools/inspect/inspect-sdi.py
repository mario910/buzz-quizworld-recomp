"""Inspect local SDIVIDEO assets and optionally extract exact ZIP entries.

This is a format diagnostic, not a replacement player or a video transcode.
No game data is changed; extraction refuses to overwrite a different file.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def inspect_sdi(data):
    if len(data) < 80 or data[64:72] != b"SDIVIDEO":
        raise ValueError("Missing SDIVIDEO header")
    words = struct.unpack_from(">20I", data)
    width, height = words[4:6]
    index_offset, stream_offset, stream_copy, declared_size, count = words[9:14]
    if not (0 < width <= 8192 and 0 < height <= 8192):
        raise ValueError("Invalid dimensions")
    if declared_size != len(data):
        raise ValueError("Declared file size mismatch")
    if (stream_offset != stream_copy or stream_offset % 2048 or
            not 80 <= index_offset <= stream_offset < len(data)):
        raise ValueError("Invalid stream/index offsets")
    if not 0 < count <= (stream_offset-index_offset)//4:
        raise ValueError("Index exceeds header region")
    offsets = struct.unpack_from(f">{count}I", data, index_offset)
    if any(not stream_offset <= offset < len(data) for offset in offsets):
        raise ValueError("Index points outside stream")
    if any(a > b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("Non-monotonic packet index")
    if data[stream_offset:stream_offset+4] != b"\0\0\1\xba":
        raise ValueError("No MPEG pack start at stream offset")
    sequence = data.find(b"\0\0\1\xb3", stream_offset,
                         min(len(data), stream_offset+65536))
    sequence_dimensions = None
    if sequence >= 0 and sequence+8 <= len(data):
        dimensions = int.from_bytes(data[sequence+4:sequence+7], "big")
        sequence_dimensions = [dimensions >> 12, dimensions & 4095]
    return {
        "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "width": width, "height": height, "pitch_hint": words[3],
        "luma_bytes_hint": words[6], "rate_milli_hint": words[7],
        "field_0x20": words[8], "index_offset": index_offset,
        "stream_offset": stream_offset, "index_count": count,
        "flags_raw": words[18], "checksum_raw": words[19],
        "first_packet_offsets": list(offsets[:8]),
        "last_packet_offsets": list(offsets[-4:]),
        "repeated_offsets": sum(a == b for a, b in zip(offsets, offsets[1:])),
        "sequence_offset": sequence,
        "mpeg_sequence_dimensions": sequence_dimensions,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pak", type=Path)
    parser.add_argument("entries", nargs="+")
    parser.add_argument("--extract", type=Path)
    args = parser.parse_args()
    reports = []
    with zipfile.ZipFile(args.pak) as archive:
        for name in args.entries:
            entry = archive.getinfo(name)
            if entry.file_size > 128*1024*1024:
                raise ValueError("Diagnostic entry exceeds 128 MiB")
            data = archive.read(entry)  # ZIP size/CRC checked by zipfile.
            report = {"entry": name, **inspect_sdi(data)}
            if args.extract:
                leaf = Path(name.replace("\\", "/")).name
                if not leaf or leaf in (".", ".."):
                    raise ValueError("Invalid entry filename")
                args.extract.mkdir(parents=True, exist_ok=True)
                target = args.extract.resolve()/leaf
                if target.exists():
                    if hashlib.sha256(target.read_bytes()).hexdigest() != report["sha256"]:
                        raise FileExistsError(f"Preserve existing file: {target}")
                else:
                    with target.open("xb") as output:
                        output.write(data)
                report["extracted"] = str(target)
            reports.append(report)
    print(json.dumps(reports, indent=2))


if __name__ == "__main__":
    main()
