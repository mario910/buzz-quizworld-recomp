"""Developer-only format conversion of private RSX input dumps; requires Pillow.

Never a launcher/runtime dependency. No remap, gamma or artistic correction is
applied: PNGs show the decoded resource channels, not necessarily shader output.
"""
import argparse
import io
import json
from pathlib import Path
import struct

from PIL import Image


def convert(meta_path: Path, output: Path) -> None:
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    w, h, fmt = meta["width"], meta["height"], meta["host_format"]
    data = meta_path.with_suffix(".bin").read_bytes()
    pitch, rows = meta["row_bytes"], meta["rows"]
    if not (0 < w <= 4096 and 0 < h <= 4096 and len(data) == pitch * rows):
        raise ValueError(f"Invalid dimensions/payload: {meta_path}")
    name = output / meta_path.stem
    if fmt in (3, 4, 5):
        tight = ((w + 3) // 4) * (8 if fmt == 3 else 16)
        if rows != (h + 3) // 4 or pitch < tight:
            raise ValueError(f"Invalid BC rows: {meta_path}")
        payload = b"".join(data[y * pitch:y * pitch + tight] for y in range(rows))
        fourcc = struct.unpack("<I", {3: b"DXT1", 4: b"DXT3", 5: b"DXT5"}[fmt])[0]
        header = [124, 0x81007, h, w, len(payload), 0, 1] + [0] * 11
        header += [32, 4, fourcc, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0]
        image = Image.open(io.BytesIO(b"DDS " + struct.pack("<31I", *header) + payload)).convert("RGBA")
    elif fmt in (0, 1, 2):
        mode, channels = {0: ("L", 1), 1: ("LA", 2), 2: ("RGBA", 4)}[fmt]
        if rows != h or pitch < w * channels:
            raise ValueError(f"Invalid color rows: {meta_path}")
        payload = b"".join(data[y * pitch:y * pitch + w * channels] for y in range(h))
        image = Image.frombytes(mode, (w, h), payload)
        if fmt == 1:
            for n, channel in enumerate(image.split()):
                channel.save(str(name) + f"-channel{n}.png")
            return
    else:
        print(f"Skipped non-byte resource format {fmt}: {meta_path.name}")
        return
    image.save(str(name) + ".png")
    if image.mode == "RGBA":
        image.getchannel("A").save(str(name) + "-alpha.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or args.directory / "png"
    output.mkdir(parents=True, exist_ok=True)
    for path in sorted(args.directory.glob("input*.json")):
        convert(path, output)


if __name__ == "__main__":
    main()
