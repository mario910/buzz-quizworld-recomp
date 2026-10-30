"""Audit private geometry corpora and make per-image replay indexes (hard links).

Phase times have a one-second boundary exclusion because process-launch and
capture clocks start slightly apart. Counts alone do not prove scene coverage.
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location("corpus", Path(__file__).with_name("corpus-inspect.py"))
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


def scene_of(path):
    meta = Path(str(path) + ".json")
    timeline = path.parent / "phases.jsonl"
    if timeline.exists() and meta.exists():
        elapsed = json.loads(meta.read_text())["elapsed_ms"]
        phases = [json.loads(line) for line in timeline.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        if any(abs(elapsed - p["elapsed_ms"]) < 1000 for p in phases):
            return "boundary"
        return next((p["phase"] for p in reversed(phases) if p["elapsed_ms"] <= elapsed), "boot")
    if any(part in ("menu", "menu-both") for part in path.parts):
        return "menu"
    return "unlabelled"


def audit(path):
    job = corpus.load(path)
    lay = corpus.layout(job)
    ls, desc = job["entry_ls"], lay["desc"]
    d = ls[desc:desc + lay["size_desc"]]
    assert len(job["entry_gpr"]) == corpus.GPR and len(ls) == corpus.LS
    assert lay["n_io"] == len(lay["dma"])
    staged = sum((size + 15) & ~15 for size, ea in lay["dma"] if size and ea)
    io_size = corpus.be32(d, 0x14)
    if staged > io_size:
        raise ValueError(f"{path}: staged input {staged} exceeds ioBuffer {io_size}")
    for kind, lsa, ea, size, data in job["transfers"]:
        if kind not in corpus.KINDS or lsa + size > corpus.LS or len(data) != size or ea + size > 2**32:
            raise ValueError(f"{path}: invalid transfer")
    return job, lay, dict(binary=corpus.be16(d, 8) << 4, io=io_size,
        output=corpus.be32(d, 0x18), stack=(corpus.be16(d, 0x1c) << 4) or 8192,
        scratch=corpus.be16(d, 0x1e) << 4)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("root", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--index", type=Path)
    a = p.parse_args()
    if a.index and a.index.resolve().is_relative_to(a.root.resolve()):
        raise SystemExit("Replay index must be outside the source corpus tree")
    counts, shapes, sizes, records, errors = collections.Counter(), collections.Counter(), collections.Counter(), [], []
    files = sorted(a.root.rglob("*.bjc.gz"))
    if not files:
        raise SystemExit("No corpus jobs found")
    for path in files:
        try:
            job, lay, region = audit(path)
            scene = scene_of(path)
            image = job["image"]
            counts[(scene, image)] += 1
            shapes[(image, tuple((k, n) for k, _, _, n, _ in job["transfers"]))] += 1
            sizes[(image, tuple(region.values()))] += 1
            relative = path.relative_to(a.root)
            records.append(dict(path=relative.as_posix(), image=image, scene=scene,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                transfers=len(job["transfers"]), user_offset=lay["user_offset"], regions=region))
            if a.index:
                name = "_".join(relative.parts)
                for cohort in ("all", scene):
                    target = a.index / cohort / f"image{image}" / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if not target.exists():
                        os.link(path, target)
        except (AssertionError, ValueError, OSError, IndexError, struct.error, EOFError) as error:
            errors.append(dict(path=str(path), error=str(error)))
    result = dict(counts=[dict(scene=s, image=i, jobs=n) for (s, i), n in sorted(counts.items())],
        transfer_shapes=[dict(image=i, shape=shape, jobs=n) for (i, shape), n in shapes.most_common()],
        region_shapes=[dict(image=i, sizes=shape, jobs=n) for (i, shape), n in sizes.most_common()],
        files=records, errors=errors,
        timing_caveat="Phase boundaries exclude +/- 1s; scene labels follow scripted milestones and need frame verification.")
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2) + "\n")
    for row in result["counts"]:
        print(f"{row['scene']:16} image {row['image']}: {row['jobs']}")
    print(f"{len(records)} valid, {len(errors)} invalid -> {a.output}")
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
