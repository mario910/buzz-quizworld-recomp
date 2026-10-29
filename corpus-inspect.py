"""Inspect SPURS job corpus files written by port/job_corpus.cpp.

    python corpus-inspect.py <dir-or-file> [--job N] [--summary]
    python corpus-inspect.py --mutate <file> <out> --input-byte OFFSET

Layout of a job's local store at entry (see spurs_job.c): the binary at 0,
then ioBuffer, oBuffer, scratch+stack and the cache buffers, each 1 KiB
aligned; CellSpursJobContext2 at r3 and the descriptor copy at r4.
"""
from pathlib import Path
import argparse
import collections
import gzip
import struct

LS = 0x40000
GPR = 128 * 16
KINDS = {1: "GET", 2: "PUT", 3: "GETLLAR", 4: "PUTLLC", 5: "PUTLLC-fail", 6: "PUTLLUC"}


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def load(path):
    raw = gzip.decompress(Path(path).read_bytes())
    magic, version, image, job_ea, seq, ls_size = struct.unpack_from("<IIiIII", raw, 0)
    assert magic == 0x434A5A42 and version == 1 and ls_size == LS
    o = 24
    entry_gpr, entry_ls = raw[o:o + GPR], raw[o + GPR:o + GPR + LS]
    o += GPR + LS
    (count,) = struct.unpack_from("<I", raw, o)
    o += 4
    transfers = []
    for _ in range(count):
        kind, lsa, ea, size = struct.unpack_from("<IIII", raw, o)
        o += 16
        transfers.append((kind, lsa, ea, size, raw[o:o + size]))
        o += size
    exit_gpr, exit_ls = raw[o:o + GPR], raw[o + GPR:o + GPR + LS]
    o += GPR + LS
    exit_state = struct.unpack_from("<7I", raw, o)
    return dict(raw=raw, image=image, job_ea=job_ea, seq=seq, entry_gpr=entry_gpr,
                entry_ls=entry_ls, transfers=transfers, exit_gpr=exit_gpr,
                exit_ls=exit_ls, exit=exit_state, ls_offset=24 + GPR)


def reg_word(gpr, r, w=0):
    # u128 in host order: _u32[0] is the SPU preferred slot.
    return struct.unpack_from("<I", gpr, r * 16 + w * 4)[0]


def layout(job):
    ls = job["entry_ls"]
    ctx = reg_word(job["entry_gpr"], 3)
    desc = reg_word(job["entry_gpr"], 4)
    io = be32(ls, ctx + 0x00)
    cache = [be32(ls, ctx + 0x04 + 4 * i) for i in range(4)]
    size_desc = be32(ls, ctx + 0x14)
    n_io, n_cache = be16(ls, ctx + 0x18), be16(ls, ctx + 0x1A)
    out = be32(ls, ctx + 0x1C)
    scratch = be32(ls, ctx + 0x20)
    # CellSpursJob256 header
    d = ls[desc:desc + 0x400]
    dma = []
    n_dma = be16(d, 10) // 8
    for i in range(n_dma):
        word = struct.unpack_from(">Q", d, 0x30 + 8 * i)[0]
        dma.append(((word >> 32) & 0x7FFF, word & 0xFFFFFFFF))
    return dict(ctx=ctx, desc=desc, io=io, cache=cache, size_desc=size_desc, n_io=n_io,
                n_cache=n_cache, out=out, scratch=scratch, dma=dma,
                user=d[0x30 + 8 * n_dma:0x80])


def summary(paths):
    shapes = collections.Counter()
    sizes = collections.Counter()
    for p in paths:
        job = load(p)
        lay = layout(job)
        shape = tuple((KINDS[k], s) for k, _, _, s, _ in job["transfers"])
        shapes[shape] += 1
        sizes[tuple(s for s, _ in lay["dma"])] += 1
    print(f"{len(paths)} jobs")
    print("transfer shapes:")
    for shape, n in shapes.most_common(10):
        print(f"  {n:4d}x {shape}")
    print("input DMA list sizes:")
    for shape, n in sizes.most_common(10):
        print(f"  {n:4d}x {shape}")


def show(path):
    job = load(path)
    lay = layout(job)
    print(f"{path}: image {job['image']} job {job['job_ea']:08X} seq {job['seq']}")
    print(f"  ctx {lay['ctx']:05X} desc {lay['desc']:05X} io {lay['io']:05X} (n={lay['n_io']}) "
          f"out {lay['out']:05X} scratch {lay['scratch']:05X} cache {[hex(c) for c in lay['cache']]}"
          f" (n={lay['n_cache']})")
    for i, (size, ea) in enumerate(lay["dma"]):
        print(f"  in[{i}] {size:5d} bytes from {ea:08X}")
    print("  user data:", lay["user"].hex(" ", 4))
    for kind, lsa, ea, size, data in job["transfers"]:
        print(f"  {KINDS.get(kind, kind):11s} lsa={lsa:05X} ea={ea:08X} size={size:5d} "
              f"{data[:32].hex(' ', 4)}")
    print("  exit pc/status/stop/mbox:", [hex(v) for v in job["exit"]])
    changed = sum(1 for a, b in zip(job["entry_ls"], job["exit_ls"]) if a != b)
    print(f"  LS bytes changed by the job: {changed}")


def mutate(src, dst, offset):
    job = load(src)
    raw = bytearray(job["raw"])
    raw[job["ls_offset"] + offset] ^= 0x01
    Path(dst).write_bytes(gzip.compress(bytes(raw), 1))
    print(f"flipped entry LS byte {offset:05X} -> {dst}")


def main():
    a = argparse.ArgumentParser()
    a.add_argument("where", nargs="?")
    a.add_argument("--job", type=int)
    a.add_argument("--summary", action="store_true")
    a.add_argument("--mutate", nargs=2)
    a.add_argument("--input-byte", type=lambda v: int(v, 0))
    args = a.parse_args()
    if args.mutate:
        mutate(args.mutate[0], args.mutate[1], args.input_byte)
        return
    where = Path(args.where)
    paths = sorted(where.glob("*.bjc.gz")) if where.is_dir() else [where]
    if args.summary:
        summary(paths)
    for p in paths if args.job is None else [paths[args.job]]:
        if not args.summary:
            show(p)


if __name__ == "__main__":
    main()
