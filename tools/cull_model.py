"""De-pipelined model of the image-41 cull loop (SPU 0x6D28 -> 0x6888).

Checked against probe pairs (entry/exit state) from BUZZ_STAGEF_PROBE; the
cull bits come from the exact SPU emulator ops, the compaction mirrors the
SPU's three table-driven quadword stores per group of four triangles.
    python tools/cull_model.py <probe dir> [...]
"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import stagef_emu as E

G = 2048
OP = E.OPS

def reg(gpr, n): return E.mk(struct.unpack_from("<4I", gpr, n * 16))

def model(gpr, ls_in):
    ls = bytearray(ls_in)
    r = lambda n: reg(gpr, n)
    W = lambda v, k=0: E.W(v, k)
    q = lambda a: bytes(ls[a & 0x3FFF0:(a & 0x3FFF0) + 16])
    groups = W(r(15)); base = W(r(13)); off = W(r(50)); verts = W(r(10))
    cursor = W(r(39)); lim0, lim1 = W(r(66), 0), W(r(66), 1); scratch = W(r(59))
    t_mask1, t_mask2, t_align = W(r(8)), W(r(7)), W(r(6))
    # mode registers, exactly as 0x6D28 derives r48
    cfg = q(W(r(81)))
    mode = OP["rothmi"](OP["andi"](OP["rotqbyi"](cfg, 3), 255), -4)
    pick = OP["or"](OP["ceqi"](mode, 2), OP["ceqi"](mode, 0))
    r48 = OP["xor"](r(11), OP["and"](OP["shufb"](pick, pick, r(12)), E.mk([0x80000000] * 4)))
    r72, r82, r83 = r(72), r(82), r(83)
    pad = W(r(84))
    sel0 = bytes(ls[0xD800:0xD810]); toggle = bytes(ls[0xCEB0:0xCEC0])
    snapshot = bytes(ls)               # indices as they were before any store
    kept_total = 0
    for g in range(groups):
        o = base + off + 24 * g
        # the group's 24 bytes as the SPU assembles them (alternating selector)
        sel = sel0 if g % 2 == 0 else OP["xor"](sel0, toggle)
        qa = snapshot[o & 0x3FFF0:(o & 0x3FFF0) + 16]
        qb = snapshot[(o + 16) & 0x3FFF0:((o + 16) & 0x3FFF0) + 16]
        d0 = OP["shufb"](qa, qb, sel); d1 = OP["shufb"](qb, qb, sel)
        idx = [[struct.unpack_from(">H", d0 + d1, 6 * l + 2 * v)[0] for l in range(4)] for v in range(3)]
        def gather(v, w): return E.mk([struct.unpack_from(">I", snapshot, ((verts + 16 * idx[v][l]) & 0x3FFF0) + 4 * w)[0] for l in range(4)])
        X0, Y0, F0, P0 = (gather(0, w) for w in range(4))
        X1, Y1, F1, P1 = (gather(1, w) for w in range(4))
        X2, Y2, F2, P2 = (gather(2, w) for w in range(4))
        c = OP["fs"](OP["fm"](OP["fs"](X1, X0), OP["fs"](Y2, Y0)), OP["fm"](OP["fs"](Y1, Y0), OP["fs"](X2, X0)))
        same = OP["and"](OP["ceqh"](P0, P1), OP["ceqh"](P0, P2))
        zero = OP["or"](OP["fcmeq"](c, r72), OP["clgti"](same, 0))
        back = OP["and"](OP["and"](OP["fcgt"](OP["xor"](c, r48), r72), OP["clgt"](r82, OP["or"](OP["or"](F0, F1), F2))),
                         OP["rotmai"](OP["and"](P2, OP["and"](P1, P0)), -31))
        clip = OP["clgti"](OP["and"](OP["and"](F0, F1), F2), 0)
        lane = OP["or"](zero, OP["fnms"](back, r83, clip))
        mask = sum(((W(lane, l) & 1) << (3 - l)) for l in range(4))
        if g == groups - 1: mask |= pad
        kept = 4 - bin(mask).count("1")
        # compaction: three quadword stores at the aligned cursor
        t1 = q(t_mask1 + 16 * mask); t2 = q(t_mask2 + 16 * mask); t3 = q(t_align + 16 * (cursor & 15))
        part0 = OP["shufb"](d0, d1, t1)
        part1 = OP["shufb"](d1, d1, t2)
        keep = bytes([0] * (cursor & 15) + [0xFF] * (16 - (cursor & 15)))
        ctl0 = OP["selb"](sel0, t3, keep)
        q0 = OP["shufb"](q(cursor), part0, ctl0)
        q1 = OP["shufb"](part0, part1, t3)
        q2 = OP["shufb"](part1, part1, t3)
        a0 = cursor & 0x3FFF0
        a1 = ((scratch if cursor > lim0 else cursor) + 0x10) & 0x3FFF0
        a2 = ((scratch if cursor > lim1 else cursor) + 0x20) & 0x3FFF0
        ls[a0:a0 + 16] = q0; ls[a1:a1 + 16] = q1; ls[a2:a2 + 16] = q2
        cursor += 6 * kept; kept_total += kept
    return kept_total, ls

if __name__ == "__main__":
    for d in sys.argv[1:]:
        for entry in sorted(Path(d).glob("probe_*_6D28.bin")):
            a = entry.read_bytes(); b = Path(str(entry).replace("_6D28", "_6888")).read_bytes()
            kept, ls = model(a[:G], a[G:])
            want = b[G:]; base = E.W(reg(a, 39), 0)
            want_kept = struct.unpack_from("<I", b, 51 * 16)[0]
            end = (base + 6 * want_kept + 15) & ~15
            bad = [i for i in range(base, end) if ls[i] != want[i]]
            print(entry.name, "kept %d/%d" % (kept, want_kept), "contract bytes differ: %d" % len(bad),
                  ("first %05X" % bad[0]) if bad else "")
