"""Symbolic trace of the stage-F vertex loop: which operations produce each stored word.

Registers are 16 byte cells; a cell is an int (concrete) or (node, k) = byte k
of a symbolic 32-bit word node. Loads from the vertex input region produce
symbols; everything else (constant pools, matrices) stays concrete, so the
printed trees show the exact op order with constants as hex.
"""
import re, struct, sys, itertools
sys.path.insert(0, __import__("os").path.dirname(__file__))
import stagef_emu as E
from pathlib import Path

class N:
    ids = itertools.count()
    def __init__(s, op, args): s.op, s.args, s.id = op, args, next(N.ids)
    def __repr__(s): return show(s)

def show(x, depth=0):
    if isinstance(x, int): return "0x%08X" % x
    if depth > 14: return "..."
    if x.op == "in": return "in[%05X]" % x.args[0]
    return "%s(%s)" % (x.op, ", ".join(show(a, depth + 1) for a in x.args))

LANE_OPS = {"fm", "fma", "fnms", "fms", "fs", "fa", "frest", "fi", "fcgt", "cflts", "cuflt",
            "and", "or", "xor", "selb", "a", "ai", "andc", "orc", "sf", "sfi", "ori", "andi",
            "shli", "rotmi", "rotmai", "ceqi", "cgt", "clgt", "ceq"}

def is_concrete(reg): return all(isinstance(c, int) for c in reg)
def to_bytes(reg): return bytes(reg)
def word(reg, k):
    cells = reg[4 * k:4 * k + 4]
    if all(isinstance(c, int) for c in cells): return struct.unpack(">I", bytes(cells))[0]
    if all(isinstance(c, tuple) for c in cells) and len({c[0] for c in cells}) == 1 and \
            [c[1] for c in cells] == [0, 1, 2, 3]:
        return cells[0][0]
    return N("bytes", [c if isinstance(c, int) else N("byte%d" % c[1], [c[0]]) for c in cells])
def from_words(ws):
    out = []
    for w in ws:
        if isinstance(w, int): out += list(struct.pack(">I", w & 0xFFFFFFFF))
        else: out += [(w, k) for k in range(4)]
    return out

class Sym(E.Emu):
    def __init__(s, gpr_le, ls, sym_lo, sym_hi):
        super().__init__(gpr_le, ls)
        s.r = [list(x) for x in s.r]
        s.lo, s.hi = sym_lo, sym_hi
        s.stores = []
    def val(s, t):
        t = t.strip(); m = re.fullmatch(r"r\[(\d+)\]", t)
        return s.r[int(m[1])] if m else int(t, 0)
    def addr(s, t):
        tot = 0
        for part in re.split(r"\s*\+\s*", t.replace("ctx->gpr", "r").strip()):
            m = re.fullmatch(r"r\[(\d+)\]\._u32\[0\]", part)
            if m:
                w = word(s.r[int(m[1])], 0)
                if not isinstance(w, int): raise ValueError("symbolic address")
                tot += w
            else: tot += int(part, 0)
        return tot & 0x3FFF0
    def load(s, a):
        if s.lo <= a < s.hi: return from_words([N("in", [a + 4 * k]) for k in range(4)])
        return list(s.ls[a:a + 16])
    def apply(s, name, args):
        if all(not isinstance(a, list) or is_concrete(a) for a in args):
            res = E.OPS[name](*[to_bytes(a) if isinstance(a, list) else a for a in args])
            return list(res)
        if name in LANE_OPS:
            ws = []
            for k in range(4):
                lane = [word(a, k) if isinstance(a, list) else a for a in args]
                if all(isinstance(x, int) for x in lane):
                    regs = [x for x in args]
                    res = E.OPS[name](*[to_bytes(a) if isinstance(a, list) and is_concrete(a) else
                                        (bytes(struct.pack(">I", word(a, k)) * 4) if isinstance(a, list) else a)
                                        for a in args])
                    ws.append(struct.unpack_from(">I", res, 4 * k)[0])
                else: ws.append(N(name, lane))
            return from_words(ws)
        if name == "shufb" and is_concrete(args[2]):
            src = args[0] + args[1]; out = []
            for x in args[2]:
                if x & 0x80: out.append(0 if (x & 0xE0) == 0x80 else (0xFF if (x & 0xE0) == 0xC0 else 0x80))
                else: out.append(src[x & 0x1F])
            return out
        if name in ("rotqbyi", "shlqbyi", "rotqmbyi"):
            a, i = args
            if name == "rotqbyi": n = i & 15; return a[n:] + a[:n]
            if name == "shlqbyi": n = i & 31; return (a[n:] + [0] * n) if n < 16 else [0] * 16
            n = -i & 31; return ([0] * n + a[:16 - n]) if n < 16 else [0] * 16
        node = N(name, [x if isinstance(x, int) else N("reg", [word(x, k) for k in range(4)]) for x in args])
        return [(node, k % 4) for k in range(16)]  # opaque: register-level node
    def run(s, pc, max_steps=4000):
        steps = 0
        while pc:
            lines = [l.replace("ctx->gpr", "r") for l in E.func(pc)]
            labels = {l[:-1]: i for i, l in enumerate(lines) if re.fullmatch(r"loc_\w+:", l)}
            i = 0; nxt = None
            while i < len(lines):
                l = lines[i]; i += 1; steps += 1
                if steps > max_steps: return steps
                if l.startswith("/*") or l.startswith("loc_"): continue
                m = re.fullmatch(r"r\[(\d+)\] = spu_ls_read128\(ctx, (.*)\);", l)
                if m: s.r[int(m[1])] = s.load(s.addr(m[2])); continue
                m = re.fullmatch(r"spu_ls_write128\(ctx, (.*), r\[(\d+)\]\);", l)
                if m:
                    a = s.addr(m[1]); v = s.r[int(m[2])]
                    s.stores.append((a, [word(v, k) for k in range(4)])); continue
                m = re.fullmatch(r"r\[(\d+)\] = spu_(\w+)\((.*)\);", l)
                if m: s.r[int(m[1])] = s.apply(m[2], [s.val(x) for x in m[3].split(",")]); continue
                m = re.fullmatch(r"if \(r\[(\d+)\]\._u(32|16)\[0\] (==|!=) 0\) (.*)", l)
                if m:
                    v = word(s.r[int(m[1])], 0)
                    if not isinstance(v, int): raise ValueError("symbolic branch: " + l)
                    v = v & 0xFFFF if m[2] == "16" else v
                    if (v == 0) == (m[3] == "=="): l = m[4]
                    else: continue
                m = re.fullmatch(r"goto (loc_\w+);", l)
                if m: i = labels[m[1]]; continue
                m = re.search(r"g_spu_trampoline_fn = " + E.PREFIX_RE + r"(\w+);", l)
                if m: nxt = m[1]; break
                if l.startswith("SPU_RET"): return steps
                raise ValueError("unhandled: " + l)
            pc = nxt
        return steps

if __name__ == "__main__":
    b = Path(sys.argv[1]).read_bytes(); G = 2048; LS = 0x40000
    lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
    s = Sym(b[:G], b[G:G + LS], lo, hi)
    s.run("60E8")
    for a, ws in s.stores[int(sys.argv[4]) if len(sys.argv) > 4 else 0:][:8]:
        print("store %05X" % a)
        for k, w in enumerate(ws): print("   w%d = %s" % (k, show(w)))

def cse_print(roots):
    """Print expression DAGs with shared subtrees named (structural hashing)."""
    names, lines, memo = {}, [], {}
    def key(x):
        if isinstance(x, int): return ("c", x)
        if id(x) in memo: return memo[id(x)]
        k = (x.op,) + tuple(key(a) for a in x.args); memo[id(x)] = k; return k
    counts = {}
    def count(x):
        if isinstance(x, int): return
        k = key(x); counts[k] = counts.get(k, 0) + 1
        if counts[k] == 1:
            for a in x.args: count(a)
    for r in roots: count(r)
    def emit(x):
        if isinstance(x, int): return "0x%08X" % x
        k = key(x)
        if k in names: return names[k]
        if x.op == "in": return "in[%05X]" % x.args[0]
        text = "%s(%s)" % (x.op, ", ".join(emit(a) for a in x.args))
        if counts[k] > 1 or len(text) > 120:
            n = "t%d" % len(names); names[k] = n; lines.append("%s = %s" % (n, text)); return n
        return text
    outs = [emit(r) for r in roots]
    return lines, outs


class Concolic(Sym):
    """Concrete execution drives addresses and branches; symbolic trees ride along.
    Loads from [lo, hi) are symbolic; symbolic stores are remembered per byte."""
    def __init__(s, gpr_le, ls, lo, hi):
        super().__init__(gpr_le, ls, lo, hi)
        s.c = [bytes(E.mk(struct.unpack_from("<4I", gpr_le, n * 16))) for n in range(128)]
        s.mem = {}          # LS byte address -> symbolic cell
        s.watch = None      # callback(pc_line, reg, cells, concrete)
    def addr(s, t):
        tot = 0
        for part in re.split(r"\s*\+\s*", t.replace("ctx->gpr", "r").strip()):
            m = re.fullmatch(r"r\[(\d+)\]\._u32\[0\]", part)
            tot += E.W(s.c[int(m[1])], 0) if m else int(part, 0)
        return tot & 0x3FFF0
    def load_sym(s, a):
        if s.lo <= a < s.hi:
            cells = from_words([N("in", [a + 4 * k]) for k in range(4)])
        else:
            cells = list(s.ls[a:a + 16])
        return [s.mem.get(a + i, cells[i]) for i in range(16)]
    def run(s, pc, max_steps=10**7, stop=None):
        steps = 0
        while pc and pc != stop:
            lines = [l.replace("ctx->gpr", "r") for l in E.func(pc)]
            labels = {l[:-1]: i for i, l in enumerate(lines) if re.fullmatch(r"loc_\w+:", l)}
            i = 0; nxt = None
            while i < len(lines):
                l = lines[i]; i += 1; steps += 1
                if l.startswith("/*") or l.startswith("loc_"): continue
                m = re.fullmatch(r"r\[(\d+)\] = spu_ls_read128\(ctx, (.*)\);", l)
                if m:
                    a = s.addr(m[2]); d = int(m[1])
                    s.c[d] = bytes(s.ls[a:a + 16]); s.r[d] = s.load_sym(a); continue
                m = re.fullmatch(r"spu_ls_write128\(ctx, (.*), r\[(\d+)\]\);", l)
                if m:
                    a = s.addr(m[1]); v = int(m[2])
                    s.ls[a:a + 16] = s.c[v]
                    for k in range(16):
                        cell = s.r[v][k]
                        if isinstance(cell, int): s.mem.pop(a + k, None)
                        else: s.mem[a + k] = cell
                    s.stores.append((a, [word(s.r[v], k) for k in range(4)])); continue
                m = re.fullmatch(r"r\[(\d+)\] = spu_(\w+)\((.*)\);", l)
                if m:
                    d = int(m[1]); args = [x.strip() for x in m[3].split(",")]
                    cv = [s.c[int(re.fullmatch(r"r\[(\d+)\]", x)[1])] if x.startswith("r[") else int(x, 0) for x in args]
                    sv = [s.r[int(re.fullmatch(r"r\[(\d+)\]", x)[1])] if x.startswith("r[") else int(x, 0) for x in args]
                    s.c[d] = bytes(E.OPS[m[2]](*cv))
                    try:
                        if m[2] == "shufb" and not is_concrete(sv[2]):
                            sv[2] = list(cv[2])     # concrete control: data-dependent shuffle
                        res = s.apply(m[2], sv)
                    except Exception:
                        res = list(s.c[d])
                    s.r[d] = res
                    if s.watch: s.watch(pc, l, d, res, s.c[d])
                    continue
                m = re.fullmatch(r"if \(r\[(\d+)\]\._u(32|16)\[0\] (==|!=) 0\) (.*)", l)
                if m:
                    v = E.W(s.c[int(m[1])], 0); v = v & 0xFFFF if m[2] == "16" else v
                    if (v == 0) == (m[3] == "=="): l = m[4]
                    else: continue
                m = re.fullmatch(r"goto (loc_\w+);", l)
                if m: i = labels[m[1]]; continue
                m = re.search(r"g_spu_trampoline_fn = " + E.PREFIX_RE + r"(\w+);", l)
                if m: nxt = m[1]; break
                if l.startswith("SPU_RET"): return steps
                raise ValueError("unhandled: " + l)
            pc = nxt
        return steps
