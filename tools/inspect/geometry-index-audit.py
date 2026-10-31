"""Audit the pinned image-41 index graph, real headers and delta snapshots.

All game-derived inputs and reports remain private. This is a format/source
audit, not a native-kernel acceptance or performance measurement.
"""
import argparse
import collections
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"
ERRORS = {0x7d54, 0x7d7c, 0x7da4, 0x7dcc}
NORMAL = {0xaea0, 0xaee8, 0xaef8, 0xaf0c, 0x2738, 0x7390, 0x73a0,
          0x7428, 0x74f0, 0x7640, 0x7730, 0x7770, 0x77d8, 0x77e8,
          0x77f8, 0x7838, 0x78a8, 0x79a8, 0x79c8, 0x7a90, 0x7c20, 0x7cd0}
LS = 0x40000
GPR = 2048


def reachable_text(body):
    """Discard lexical tails after pinned whole-line unconditional exits.

    Conditional exits and ordinary drained calls stay in the graph. This is
    deliberately a narrow reader for this pinned lift, not a general C parser.
    """
    lines = []
    for line in body.splitlines():
        lines.append(line)
        if (line.strip() == "SPU_RET(ctx);" or
                re.fullmatch(r"\s*\{ ctx->pc = 0x[0-9A-F]+; g_spu_trampoline_fn = "
                             + PREFIX + r"[0-9A-F]+; return; \}\s*", line)):
            break
    return "\n".join(lines)


def source_graph(raw):
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise ValueError("Unreviewed image-41 index source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void " + PREFIX + r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    todo, seen, edges, dead = [0xaea0], set(), {}, {}
    while todo:
        pc = todo.pop()
        if pc in seen or pc in ERRORS:
            continue
        body = functions[pc]
        live = reachable_text(body)
        refs = lambda text: {int(x, 16) for x in re.findall(PREFIX + r"([0-9A-F]+)", text)}
        targets = refs(live)
        if re.search(r"\bspu_(?:wrch|rdch|rchcnt|stop|sync|dsync)\s*\(", live):
            raise ValueError(f"Unreviewed I/O in normal index block {pc:X}")
        seen.add(pc)
        todo.extend(targets - seen)
        edges[f"{pc:X}"] = [f"{x:X}" for x in sorted(targets)]
        if refs(body) - targets:
            dead[f"{pc:X}"] = [f"{x:X}" for x in sorted(refs(body) - targets)]
    if seen != NORMAL:
        raise ValueError(f"Unreviewed normal index graph: {seen ^ NORMAL}")
    error_edges = {}
    for pc in sorted(ERRORS):
        body, live = functions[pc], reachable_text(functions[pc])
        references = lambda text: {int(x, 16) for x in re.findall(PREFIX + r"([0-9A-F]+)", text)}
        error_edges[f"{pc:X}"] = [f"{x:X}" for x in sorted(references(live))]
        if references(body) - references(live):
            dead[f"{pc:X}"] = [f"{x:X}" for x in sorted(references(body) - references(live))]
    return dict(source_sha256=PIN, normal_blocks=len(seen), edges=edges,
                error_frontier=[f"{x:X}" for x in sorted(ERRORS)], error_edges=error_edges,
                dead_lexical_targets=dead)


def add(a, b):
    return tuple((x + y) & 0xffffffff for x, y in zip(a, b))


def halfwords(a, b, subtract=False):
    # Physical host halfword order is irrelevant to lane-wise AH/SFH. Retain
    # each half's position within its word and explicitly prohibit word carry.
    return tuple(sum((((y >> shift) - (x >> shift) if subtract else
                       (x >> shift) + (y >> shift)) & 0xffff) << shift
                     for shift in (0, 16)) for x, y in zip(a, b))


def delta_snapshot(raw):
    """Independent scalar model of a complete 7770 entry through 77F8.

    Keep four incoming cached deltas and eight separate halfword chains;
    cache all four next input quads before the ordered output stores. The
    first zero-step warmup overwrites its address on the next iteration.
    """
    if len(raw) != GPR + LS:
        raise ValueError("Truncated/trailing register/LS snapshot")
    regs = [struct.unpack_from("<4I", raw, i * 16) for i in range(128)]
    local = bytearray(raw[GPR:])
    count = regs[12][0]
    if not 1 <= count <= 2049:
        raise ValueError("Unreviewed delta iteration count")

    def load(address):
        return struct.unpack_from(">4I", local, address & 0x3fff0)

    def store(address, value):
        struct.pack_into(">4I", local, address & 0x3fff0, *value)

    for _ in range(count):
        offsets, output, step, carry = regs[14], regs[13], regs[9], regs[11]
        raw_next = [load(regs[base][0] + offsets[0]) for base in (31, 17, 16, 15)]
        values = []
        for cached in (19, 18, 20, 21):
            carry = halfwords(regs[cached], carry)
            values.append(carry)
        for base, value in zip((31, 17, 16, 15), values):
            store(regs[base][0] + output[0], value)
        regs[96], regs[99] = output, add(output, step)
        regs[75], regs[28], regs[47], regs[11] = values
        regs[49], regs[48], regs[22], regs[25] = raw_next
        regs[74] = add(offsets, (64,) * 4)
        regs[14], regs[13] = regs[74], regs[99]
        regs[12] = add(regs[12], (0xffffffff,) * 4)
        for destination, value in zip((19, 18, 20, 21), raw_next):
            regs[destination] = halfwords(regs[10], value, subtract=True)
        regs[9] = (64,) * 4
    return b"".join(struct.pack("<4I", *r) for r in regs) + local


def audit_snapshot(directory):
    first, last = directory / "pc_7770.bin", directory / "pc_7770_exit.bin"
    before, after = first.read_bytes(), last.read_bytes()
    first_meta = json.loads(first.with_suffix(".bin.json").read_text())
    last_meta = json.loads(last.with_suffix(".bin.json").read_text())
    if (first_meta["job"] != last_meta["job"] or first_meta["block"] != 0x7770 or
            last_meta["block"] != 0x7770 or first_meta["exit"] or not last_meta["exit"] or
            last_meta["pc"] != 0x77f8 or any(first_meta[x] != last_meta[x] for x in ("status", "stop"))):
        raise ValueError("Delta snapshot boundary differs")
    model = delta_snapshot(before)
    if model != after:
        at = next(i for i, (a, b) in enumerate(zip(model, after)) if a != b)
        raise ValueError(f"Delta scalar reconstruction differs at snapshot byte {at:#x}")
    return dict(job=first_meta["job"], all_gpr_all_ls_exact=True, bytes_compared=len(model),
                entry_sha256=hashlib.sha256(before).hexdigest(),
                exit_sha256=hashlib.sha256(after).hexdigest(),
                limitation="Snapshots omit full architectural/channel state; not a native acceptance gate")


def inventory(directory):
    spec = importlib.util.spec_from_file_location("index_formats", Path(__file__).with_name("geometry-format-inventory.py"))
    formats = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(formats)
    corpus = formats.corpus
    widths, zero_paths, deltas, biases, counts = (collections.Counter() for _ in range(5))
    digest, jobs, transfers = hashlib.sha256(), 0, 0
    for path in sorted(directory.glob("*.bjc.gz")):
        packed = path.read_bytes()
        digest.update(path.name.encode() + b"\0" + hashlib.sha256(packed).digest())
        job = corpus.load(path)
        if job["image"] != 41:
            raise ValueError("Non-image-41 input")
        slots = formats.staged_slots(job, corpus.layout(job))
        config = slots[16]
        flavor, count = config[6] >> 4, corpus.be16(config, 10)
        if flavor not in (2, 3):
            raise ValueError("Unreviewed index flavor")
        section = formats.index_sections(slots[2], count)
        widths[section["width"]] += 1
        zero_paths[(section["width"] == 0, section["deltas"] == 0)] += 1
        deltas[section["deltas"]] += 1
        biases[section["bias"]] += 1
        counts[count] += 1
        jobs += 1
        transfers += len(job["transfers"])
    if not jobs:
        raise ValueError("No corpus jobs")
    return dict(jobs=jobs, transfers=transfers, compressed_files_sha256=digest.hexdigest(),
                width_jobs=dict(sorted(widths.items())), bias_jobs=dict(sorted(biases.items())),
                zero_paths=[dict(width_zero=w, deltas_zero=d, jobs=n) for (w, d), n in sorted(zero_paths.items())],
                delta_count_range=[min(deltas), max(deltas)], index_count_range=[min(counts), max(counts)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("snapshots", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.corpus, args.snapshots, args.output = args.corpus.resolve(), args.snapshots.resolve(), args.output.resolve()
    if (ROOT / "analysis/native-geometry/trace-lift/source.sha256").read_text().strip() != PIN:
        raise ValueError("Tracer source pin differs")
    result = source_graph((ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes())
    result["corpus"] = inventory(args.corpus)
    result["delta_snapshot"] = audit_snapshot(args.snapshots)
    with (args.snapshots / "all.entries.csv").open() as f:
        entries = {int(r["pc"], 16): int(r["entries"]) for r in csv.DictReader(f)}
    result["trace_entries"] = {f"{pc:X}": entries.get(pc, 0) for pc in sorted(NORMAL | ERRORS)}
    widths = result["corpus"]["width_jobs"]
    expected_entries = {0x7390: result["corpus"]["jobs"], 0x77f8: result["corpus"]["jobs"],
                        0x77d8: widths.get(0, 0),
                        0x7cd0: sum(n for w, n in widths.items() if 1 <= w <= 8),
                        0x7640: sum(n for w, n in widths.items() if w > 8),
                        0x7730: sum(p["jobs"] for p in result["corpus"]["zero_paths"] if not p["deltas_zero"])}
    if any(entries.get(pc, 0) != n for pc, n in expected_entries.items()) or any(entries.get(pc, 0) for pc in ERRORS):
        raise ValueError("Real index headers/trace entries disagree or an error path was observed")
    report = args.snapshots / "replay.log"
    expected = (f'[replay] {result["corpus"]["jobs"]} exact, 0 with concurrent input changes, '
                f'0 FAILED ({result["corpus"]["transfers"]} transfers)')
    if expected not in report.read_text():
        raise ValueError("Full-corpus original replay did not pass")
    result["original_replay_exact"] = True
    result["native_acceptance"] = False
    result["bindings"] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in sorted(args.snapshots.glob("*")) if path.is_file() and path != args.output}
    result["tracer_exe_sha256"] = hashlib.sha256((ROOT / "analysis/native-geometry/trace-game.exe").read_bytes()).hexdigest()
    result["audit_sources"] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in (
        "tools/inspect/geometry-index-audit.py", "tools/inspect/geometry-format-inventory.py",
        "tools/inspect/corpus-inspect.py", "port/geometry_trace.h", "tools/codegen/generate-geometry-trace.py",
        "analysis/native-geometry/trace-lift/spu_recomp.c", "analysis/native-geometry/trace-lift/source.sha256")}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f'PASS index audit: {result["normal_blocks"]} normal blocks, '
          f'{result["corpus"]["jobs"]} real headers, all GPR/LS delta snapshot exact; native gates OPEN')


if __name__ == "__main__":
    main()
