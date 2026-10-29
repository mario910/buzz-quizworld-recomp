"""Read-only PPC BL -> firmware-NID cross references for a local ELF/import map."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct


def callers(elf, imports):
    by_target = {int(item["stub"], 16): item for item in imports}
    result = {}
    for offset, addr, size, flags in elf.segments:
        if not flags & 1:
            continue
        for delta in range(0, size - 3, 4):
            word = struct.unpack_from(">I", elf.data, offset + delta)[0]
            if word >> 26 != 18 or not word & 1:
                continue
            displacement = word & 0x03FFFFFC
            if displacement & 0x02000000:
                displacement -= 0x04000000
            target = displacement if word & 2 else addr + delta + displacement
            if target in by_target:
                item = by_target[target]
                key = item["library"] + ":" + item["nid"]
                result.setdefault(key, []).append(addr + delta + 4)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("imports", type=Path)
    parser.add_argument("--nid", action="append", default=[])
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("compare_versions", Path(__file__).with_name("compare-game-versions.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = callers(module.Elf(args.elf), json.loads(args.imports.read_text()))
    print(json.dumps({key: [f"0x{pc:08x}" for pc in values] for key, values in result.items()
                      if not args.nid or any(key.endswith(nid.lower()) for nid in args.nid)}, indent=2))
