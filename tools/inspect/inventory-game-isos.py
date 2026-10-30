"""Inspect local Quiz World ISOs without extracting large game assets.

Only PARAM.SFO and EBOOT are copied into the ignored analysis directory.
The built-in decryptor prepares a private ELF; no game is booted.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from launcher.disc_image import DiscImage
from launcher import model
from launcher.setup_pipeline import fingerprint, iso_plan
from launcher.ps3_crypto import KeyStore, decrypt_elf


def fields(data):
    model.title_id(data)  # Validate bounds before reading the selected fields.
    keys, values, count = struct.unpack_from("<III", data, 8)
    result = {}
    for index in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from("<HHIII", data, 20 + index * 16)
        end = data.index(b"\0", keys + key)
        name = data[keys + key:end].decode("ascii")
        if name in ("TITLE_ID", "VERSION", "APP_VER", "TITLE") and fmt == 0x204:
            result[name] = data[values + offset:values + offset + length].rstrip(b"\0").decode("utf-8")
    return result


def inspect(path, key_path, output):
    image = DiscImage()
    image.open(str(path))
    try:
        mode, plan, total = iso_plan(image)
        def read(relative, limit):
            item = next(item for item in plan if item[0].as_posix() == relative)
            if item[2] > limit:
                raise ValueError("Oversized metadata/executable")
            buffer = io.BytesIO()
            image.get_file_from_iso_fp(buffer, **{mode: item[1]})
            return buffer.getvalue()
        sfo = read("PS3_GAME/PARAM.SFO", 1024**2)
        metadata = fields(sfo)
        binary = read("PS3_GAME/USRDIR/EBOOT.BIN", 256 * 1024**2)
    finally:
        image.close()
    sample = output / (metadata["TITLE_ID"] + "-" + hashlib.sha256(str(path.resolve()).encode()).hexdigest()[:10])
    sample.mkdir(parents=True, exist_ok=True)
    (sample / "PARAM.SFO").write_bytes(sfo)
    executable = sample / "EBOOT.BIN"
    executable.write_bytes(binary)
    elf = sample / "EBOOT.elf"
    if binary[:4] == b"SCE\0":
        elf.write_bytes(decrypt_elf(executable, KeyStore(key_path)))
    elif binary[:6] == b"\x7fELF\x02\x02":
        elf.write_bytes(binary)
    else:
        raise ValueError("Unreadable/encrypted disc content; no external disc keys are fetched")
    return dict(iso=str(path.resolve()), iso_size=path.stat().st_size, metadata=metadata,
                namespace=mode, game_files=len(plan), game_bytes=total,
                eboot_sha256=hashlib.sha256(binary).hexdigest(), elf=fingerprint(elf))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", type=Path, required=True)
    parser.add_argument("--keys", type=Path)
    args = parser.parse_args()
    output = ROOT / "analysis/iso-variant-inventory"
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for path in sorted(args.folder.glob("*Quiz World*.iso")):
        print("Inspecting " + path.name, file=sys.stderr, flush=True)
        try:
            result = inspect(path, args.keys, output)
        except Exception as exc:
            result = dict(iso=str(path), error=str(exc))
        results.append(result)
    report = output / "inventory.json"
    report.write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(results, indent=2, ensure_ascii=False))
