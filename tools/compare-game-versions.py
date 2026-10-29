"""Read-only compatibility evidence for two locally supplied PS3 ELF64 files.

No instructions or game data are printed; reports contain offsets/hashes only.
Candidates are evidence for review, NOT permission to apply another version's hooks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


class Elf:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if self.data[:6] != b"\x7fELF\x02\x02":
            raise ValueError("Expected big-endian ELF64")
        offset = struct.unpack_from(">Q", self.data, 32)[0]
        stride, count = struct.unpack_from(">HH", self.data, 54)
        self.segments = []
        for index in range(count):
            kind, flags, start, addr, _, size, _, _ = struct.unpack_from(">IIQQQQQQ", self.data, offset + index * stride)
            if kind == 1:
                self.segments.append((start, addr, size, flags))

    def address(self, offset):
        for start, addr, size, _ in self.segments:
            if start <= offset < start + size:
                return addr + offset - start
        return None

    def read(self, addr, size):
        for start, base, length, _ in self.segments:
            if base <= addr and addr + size <= base + length:
                return self.data[start + addr - base:start + addr - base + size]
        raise ValueError(f"Unmapped address {addr:x}")


def matches(haystack, needle):
    found, start = [], 0
    while True:
        at = haystack.find(needle, start)
        if at < 0:
            return found
        if at % 4 == 0:
            found.append(at)
        start = at + 1


def ppc_without_branch_displacements(data):
    output = bytearray(data)
    for offset in range(0, len(data) - 3, 4):
        word = struct.unpack_from(">I", data, offset)[0]
        op = word >> 26
        if op == 18:
            struct.pack_into(">I", output, offset, word & 0xFC000003)
        elif op == 16:
            struct.pack_into(">I", output, offset, word & 0xFFFF0003)
    return output


def compare(old, new):
    pcs = (0x46B8F8, 0x46B388, 0x47CD10, 0x46C3F0, 0x46B3A0,
           0x333340, 0x490D84, 0x4906D4, 0x490A30, 0x28AA8C)
    functions = []
    normalized = ppc_without_branch_displacements(new.data)
    for pc in pcs:
        entry = {"reference_pc": f"0x{pc:08x}", "exact_candidates": []}
        for size in (256, 128, 64, 32, 16):
            found = matches(new.data, old.read(pc, size))
            if len(found) == 1:
                entry.update(exact_bytes=size, exact_candidates=[f"0x{new.address(found[0]):08x}"])
                break
        functions.append(entry)
        # Match a 256-byte neighborhood around call-return sites. Branch
        # displacement differences are ignored, but BO/BI/AA/LK and all data
        # offsets/register operands remain exact. Still only candidate evidence.
        anchor = pc - 64
        pattern = ppc_without_branch_displacements(old.read(anchor, 256))
        candidates = matches(normalized, pattern)
        entry["branch_normalized_neighborhoods"] = [f"0x{new.address(at) + 64:08x}" for at in candidates if new.address(at) is not None]
    # Full raw job spans used by our current generator. Never infer equality
    # from just a GUID/prefix: the ENTIRE original span must occur unchanged.
    import ast
    source = (Path(__file__).resolve().parents[1] / "generate-raw-jobs.py").read_text()
    tree = ast.parse(source)
    constants = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id in ("ELF_OFFSET", "captured"):
            constants[node.targets[0].id] = ast.literal_eval(node.value)
    images = []
    for name, fingerprint, size, end in constants["captured"]:
        offset = constants["ELF_OFFSET"][fingerprint]
        chunk = old.data[offset:offset + size]
        found = matches(new.data, chunk)
        prefixes = matches(new.data, chunk[16:80])
        code_matches = [offset - 16 for offset in prefixes if offset >= 16 and new.data[offset:offset + end - 16] == chunk[16:end]]
        images.append(dict(name=name, size=size, sha256=hashlib.sha256(chunk).hexdigest(),
                           reference_offset=f"0x{offset:x}", candidate_offsets=[f"0x{x:x}" for x in found],
                           instruction_region_matches=[f"0x{x:x}" for x in code_matches], prefix_count=len(prefixes)))
    kernels = []
    for role, image_offset, pc in (("geometry-vertex", 0x76EC00, 0x6338),
                                   ("geometry-matrix", 0x76EC00, 0xC3A0),
                                   ("geometry-normalize", 0x76EC00, 0x2C10),
                                   ("image36-leaf", 0x77D100, 0x06D8)):
        candidates = []
        for size in (256, 128, 64, 32, 16):
            found = matches(new.data, old.data[image_offset + pc:image_offset + pc + size])
            if found:
                candidates = [f"0x{position:x}" for position in found]
                break
        kernels.append(dict(role=role, exact_prefix_bytes=size if candidates else 0, candidate_file_offsets=candidates))
    return dict(reference_sha256=hashlib.sha256(old.data).hexdigest(),
                candidate_sha256=hashlib.sha256(new.data).hexdigest(), functions=functions, raw_images=images,
                kernel_prefix_candidates=kernels)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(Elf(args.reference), Elf(args.candidate)), indent=2))
