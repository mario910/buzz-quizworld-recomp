"""Inventory physical image-41 stream descriptors and verify observed DMA bounds.

Field selectors are from the audited EU100 lift. Numeric attribute IDs are
retained until their use is proven; no semantic names are guessed from sizes.
"""
import argparse
import collections
import csv
import importlib.util
import json
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location("corpus", Path(__file__).with_name("corpus-inspect.py"))
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


def stream_descriptor(data):
    if len(data) < 8:
        raise ValueError("Stream descriptor shorter than header")
    count, stride = data[:2]
    if not count or not stride or 8 + count * 8 > len(data):
        raise ValueError(f"Invalid stream header: attributes={count} stride={stride} bytes={len(data)}")
    attrs = []
    for i in range(count):
        offset, storage, components, routing, *parameters = data[8 + 8*i:16 + 8*i]
        if not parameters[0] or offset + parameters[0] > stride:
            raise ValueError("Attribute extends beyond its vertex record")
        attrs.append(dict(offset=offset, storage=storage, components=components,
            routing=routing, byte_size=parameters[0], shader_slot=parameters[1], parameters=parameters))
    return dict(stride=stride, control=data[2:8].hex(), attributes=attrs)


def skin_pairs(record):
    if len(record) != 8:
        raise ValueError("Skin record must contain four weight/index pairs")
    return tuple(zip(record[0::2], record[1::2]))


def index_sections(data, count):
    """Physical compressed-index sections selected by index flavors 2/3."""
    if len(data) < 8 or count % 3:
        raise ValueError("Invalid compressed-index header/triangle count")
    deltas, bias, mask_bytes = struct.unpack_from(">HHH", data)
    width = data[6]
    if width > 16:
        raise ValueError("Index delta width exceeds halfword")
    triangle_bytes = (count // 3 * 2 + 7) // 8
    packed_offset = 8 + mask_bytes + triangle_bytes
    packed_end = packed_offset + (deltas * width + 7) // 8
    if packed_end > len(data):
        raise ValueError("Compressed-index sections exceed staged payload")
    return dict(deltas=deltas, bias=bias, width=width, mask_bytes=mask_bytes,
                triangle_bytes=triangle_bytes, packed_offset=packed_offset, packed_end=packed_end)


def rsx_methods(data):
    if len(data) % 4:
        raise ValueError("RSX command block is not word aligned")
    words = struct.unpack(">" + "I" * (len(data) // 4), data)
    cursor = 0
    while cursor < len(words):
        header = words[cursor]
        count, method = (header >> 18) & 0x7ff, header & 0x1fff
        if header & 0xa0000003:
            raise ValueError("Unreviewed RSX control command")
        if cursor + 1 + count > len(words):
            raise ValueError("RSX method parameters exceed command block")
        for i in range(count):
            yield method + (0 if header & 0x40000000 else i * 4), words[cursor + 1 + i]
        cursor += count + 1


def verify_rsx(job, rows, descriptor, position_only, index_count):
    expected = {a["shader_slot"]: a for a in descriptor["attributes"][:1 if position_only else None]}
    stride = 12 if position_only else descriptor["stride"]
    index_gpu = vertex_gpu = None
    base = gpu_base = None
    for row, transfer in zip(rows, job["transfers"]):
        if row["pc"] == "3948": base, gpu_base = struct.unpack_from(">2I", transfer[-1])
        if row["pc"] == "2EC8" and index_gpu is None:
            if base is None: raise ValueError("Index PUT precedes reservation")
            index_gpu = gpu_base + transfer[2] - base
        if row["pc"] == "36D8" and vertex_gpu is None:
            if base is None: raise ValueError("Vertex PUT precedes reservation")
            vertex_gpu = gpu_base + transfer[2] - base
    blocks = formats = 0
    for row, transfer in zip(rows, job["transfers"]):
        if row["pc"] != "2968": continue
        blocks += 1
        drawn = 0
        for method, value in rsx_methods(transfer[-1]):
            if 0x1740 <= method < 0x1780 and value & 0xf0:
                attr = expected.get((method - 0x1740) // 4)
                if attr is None or value & 0xffff != (stride << 8 | attr["components"] << 4 | attr["storage"]):
                    raise ValueError("RSX vertex format differs from effective descriptor")
                formats += 1
            if 0x1680 <= method < 0x16c0 and vertex_gpu is not None:
                attr = expected.get((method - 0x1680) // 4)
                if attr is not None and value != vertex_gpu + attr["offset"]:
                    raise ValueError("RSX vertex address differs from ring allocation")
            if method == 0x181c and index_gpu is not None and value != index_gpu:
                raise ValueError("RSX index address differs from ring allocation")
            if method == 0x1824: drawn += (value >> 24) + 1
            if method == 0x1808:
                if drawn > index_count or drawn % 3:
                    raise ValueError("RSX draw count exceeds indices or is not a triangle list")
                drawn = 0  # One allocation can be drawn by several render passes.
        if drawn:
            raise ValueError("Unterminated RSX indexed draw")
    return blocks, formats


def ring_allocation(job, rows, ring):
    """Check ordinary per-lane reservations, including wrap, against output PUTs."""
    if len(rows) != len(job["transfers"]):
        raise ValueError("DMA trace/corpus transfer count differs")
    reads, writes, payloads = [], [], []
    for row, transfer in zip(rows, job["transfers"]):
        kind, lsa, ea, size, data = transfer
        if (int(row["lsa"], 16), int(row["ea"], 16), int(row["size"])) != (lsa, ea, size):
            raise ValueError("DMA trace/corpus transfer shape differs")
        if row["pc"] == "3948": reads.append((ea, data))
        if row["pc"] == "3AFC": writes.append((ea, data))
        if row["pc"] in ("2EC8", "36D8"): payloads.append((ea, size))
    if not reads and not writes and not payloads:
        return "no-output"
    if len(reads) != 1 or len(writes) != 1:
        raise ValueError("Unreviewed alternate allocator path")
    ea, before = reads[0]
    lane = corpus.be32(job["entry_ls"], 0x1C8)  # getter 0x2718
    if writes[0][0] != ea or ea != ring + 128 + lane * 128:
        raise ValueError("Allocation line is not a per-lane ring line")
    begin, gpu, end, old = struct.unpack_from(">4I", before)
    after = writes[0][1]
    if before[:12] != after[:12]:
        raise ValueError("Allocation changed ring bounds/GPU base")
    new = corpus.be32(after, 12)
    start = min((ea for ea, _ in payloads), default=new)
    length = sum(size for _, size in payloads)
    if start not in (old, begin) or not (begin <= start <= new <= end):
        raise ValueError("Output reservation is outside ring or has unknown start")
    if new - start != ((length + 127) & ~127):
        raise ValueError("Reservation size differs from 128-byte-rounded output")
    cursor = start
    for ea, size in payloads:
        if ea != cursor:
            raise ValueError("Output payloads are not contiguous in reservation")
        cursor += size
    return "wrap" if start != old else "linear"


def staged_slots(job, layout):
    cursor = layout["io"]
    slots = []
    for size, ea in layout["dma"]:
        slots.append(job["entry_ls"][cursor:cursor + size] if size and ea else b"")
        if size and ea:
            cursor += (size + 15) & ~15
    return slots


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("corpus", type=Path)
    p.add_argument("--dma", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    paths = sorted(a.corpus.glob("*.bjc.gz"))
    if not paths:
        raise SystemExit("No corpus jobs found")
    vertex_dma, index_dma = collections.Counter(), collections.Counter()
    dma_rows = collections.defaultdict(list)
    if a.dma:
        with a.dma.open() as f:
            for row in csv.DictReader(f):
                dma_rows[row["job"]].append(row)
                if row["pc"] == "36D8": vertex_dma[row["job"]] += int(row["size"])
                if row["pc"] == "2EC8": index_dma[row["job"]] += int(row["size"])
    configs, descriptors, headers = collections.Counter(), collections.Counter(), collections.Counter()
    failures, bounds, jobs = [], collections.Counter(), 0
    unit_selectors, skin_weight_sums = collections.Counter(), collections.Counter()
    skin_indices_outside_palette = 0
    for path in paths:
        job = corpus.load(path)
        if job["image"] != 41:
            continue
        lay = corpus.layout(job)
        slots = staged_slots(job, lay)
        if len(slots) != 21 or len(slots[16]) < 16:
            failures.append(dict(job=path.name, error="Unreviewed slot/config layout"))
            continue
        cfg = slots[16]
        flags = corpus.be32(lay["user"], 20)
        position_only = bool(flags & 0x80)
        secondary_enabled = bool(flags & 0x100)
        nverts, nindices = corpus.be16(cfg, 8), corpus.be16(cfg, 10)
        configs[tuple(cfg[:8])] += 1
        headers[(nverts, nindices)] += 1
        decoded = {}
        try:
            if cfg[6] >> 4 in (2, 3):
                index_sections(slots[2], nindices)
                bounds["compressed-index-sections-pass"] += 1
            for role, selector, slot in (("inputA", cfg[2], 19), ("inputB", cfg[3], 20), ("output", cfg[4], 1)):
                if selector == 255:
                    desc = stream_descriptor(slots[slot])
                    decoded[role] = desc
                    descriptors[(role, json.dumps(desc, sort_keys=True))] += 1
            # Built-in input A 0 is verified raw BE float3. Other IDs remain numeric.
            stride_a = decoded.get("inputA", {}).get("stride", 12 if cfg[2] == 0 else None)
            stride_b = decoded.get("inputB", {}).get("stride", 12 if cfg[3] == 0 else None)
            for role, stride, group in (("inputA", stride_a, slots[8:11]), ("inputB", stride_b, slots[11:14])):
                if role == "inputB" and not secondary_enabled:
                    bounds["inputB-disabled-by-job-flags"] += 1
                    continue
                if role == "inputB" and not sum(map(len, group)):
                    bounds["inputB-absent"] += 1
                    continue
                if stride is None:
                    bounds[role + "-unresolved-builtin"] += 1
                elif sum(map(len, group)) < nverts * stride:
                    failures.append(dict(job=path.name, error=role + " payload shorter than count*stride"))
                else:
                    bounds[role + "-size-pass"] += 1
            if secondary_enabled and "inputB" in decoded:
                payload = b"".join(slots[11:14])
                stride = decoded["inputB"]["stride"]
                if len(payload) >= nverts * stride:
                    for attr in decoded["inputB"]["attributes"]:
                        if attr["storage"] != 9: continue
                        for vertex in range(nverts):
                            offset = vertex * stride + attr["offset"]
                            packed = int.from_bytes(payload[offset:offset + 3], "big")
                            selector = packed & 15
                            unit_selectors[selector] += 1
                            if selector >> 2 >= 3:
                                failures.append(dict(job=path.name, error="unit-vector selector uses reserved axis"))
                                break
            if cfg[6] & 15 == 1:
                skin = slots[6]
                if len(skin) < nverts * 8:
                    failures.append(dict(job=path.name, error="skinning payload shorter than 8 bytes/vertex"))
                else:
                    bounds["skinning-payload-size-pass"] += 1
                    palette = sum(map(len, slots[3:6])) // 48
                    for vertex in range(nverts):
                        record = skin[8*vertex:8*vertex+8]
                        pairs = skin_pairs(record)
                        weight_sum = sum(w for w, i in pairs)
                        skin_weight_sums[weight_sum] += 1
                        if weight_sum != 255:
                            raise ValueError("Skin weights do not sum to 255")
                        if palette:
                            outside = sum(1 for w, i in pairs if w and i >= palette)
                            skin_indices_outside_palette += outside
                            if outside:
                                raise ValueError("Active skin index exceeds palette")
            if a.dma:
                allocation = ring_allocation(job, dma_rows[path.name], corpus.be32(lay["user"], 0))
                bounds["ring-" + allocation + "-pass"] += 1
                if "output" in decoded:
                    blocks, formats = verify_rsx(job, dma_rows[path.name], decoded["output"], position_only, nindices)
                    bounds["rsx-command-blocks-pass"] += blocks
                    bounds["rsx-active-formats-pass"] += formats
                stride_out = decoded.get("output", {}).get("stride", {0:12, 1:20, 2:24, 3:24}.get(cfg[4]))
                # Job driver 0x1F30 rewrites custom output header to {1,12,1}
                # when descriptor user flags bit 7 selects position-only output.
                if position_only:
                    stride_out = 12
                if vertex_dma[path.name] and stride_out is not None:
                    expected = (nverts * stride_out + 15) & ~15
                    if vertex_dma[path.name] != expected:
                        failures.append(dict(job=path.name, error="vertex DMA size", actual=vertex_dma[path.name], expected=expected))
                    else: bounds["vertex-dma-size-pass"] += 1
                if index_dma[path.name] > ((nindices * 2 + 15) & ~15):
                    failures.append(dict(job=path.name, error="index DMA exceeds original index count"))
                else: bounds["index-dma-bound-pass"] += 1
            jobs += 1
        except ValueError as error:
            failures.append(dict(job=path.name, error=str(error)))
    result = dict(jobs=jobs, bounds=dict(bounds), failures=failures,
        unit_vector_selectors=dict(sorted(unit_selectors.items())),
        skin_weight_sums=dict(sorted(skin_weight_sums.items())),
        skin_indices_outside_palette=skin_indices_outside_palette,
        configs=[dict(bytes=list(cfg), jobs=n) for cfg, n in configs.most_common()],
        descriptors=[dict(role=role, descriptor=json.loads(desc), jobs=n) for (role, desc), n in descriptors.most_common()],
        distinct_vertex_index_counts=len(headers))
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{jobs} jobs, {len(configs)} configs, {len(descriptors)} custom descriptors, {len(failures)} unresolved/failed bounds")
    print(dict(bounds))
    for failure in failures[:5]: print(failure)
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
