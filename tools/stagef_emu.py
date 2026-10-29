"""Exact emulator for lifted SPU functions (C source of port/spu/<image>), used to
derive and check native kernels. Image 41 by default; set_image() for another."""
import re, struct, sys
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LS = 0x40000
src = ""
PREFIX_RE = ""
_funcs = {}

def set_image(name):
    """name: a port/spu directory, e.g. 'buzz_job_6eba' or 'buzz_job_c0bc'."""
    global src, PREFIX_RE
    src = (ROOT / "port/spu" / name / "spu_recomp.c").read_text(encoding="utf-8")
    PREFIX_RE = re.escape(name) + r"_spu_func_0000"
    _funcs.clear()

set_image("buzz_job_6eba")

def func(pc):
    if pc not in _funcs:
        m = re.search(r"(?ms)^void " + PREFIX_RE + r"%s\(spu_context\* ctx\) \{(.*?)^\}" % pc, src)
        _funcs[pc] = [l.strip() for l in m.group(1).splitlines() if l.strip()]
    return _funcs[pc]

# registers: 16-byte big-endian bytearrays
def W(v, i): return struct.unpack_from(">I", v, 4 * i)[0]
def words(v): return struct.unpack(">4I", v)
def mk(ws): return struct.pack(">4I", *[w & 0xFFFFFFFF for w in ws])
def f32(v): return list(struct.unpack(">4f", bytes(v)))
def _c(x):
    if x != x or abs(x) > 3.4028234e38: x = math.copysign(3.4028234e38, x if x == x else 1)
    return x
def fromf(a): return struct.pack(">4f", *[_c(x) for x in a])

def shufb(a, b, c):
    src = a + b; out = bytearray(16)
    for i, x in enumerate(c):
        if x & 0x80: out[i] = 0x00 if (x & 0xE0) == 0x80 else (0xFF if (x & 0xE0) == 0xC0 else 0x80)
        else: out[i] = src[x & 0x1F]
    return bytes(out)

def s32(x): return x - (1 << 32) if x & 0x80000000 else x
def sext(v, bits): return v - (1 << bits) if v & (1 << (bits - 1)) else v

OPS = {}
def op(name):
    def d(f): OPS[name] = f; return f
    return d
ws = lambda f: (lambda *a: mk([f(*[W(x, i) if isinstance(x, (bytes, bytearray)) else x for x in a]) for i in range(4)]))
OPS.update({
 "a": ws(lambda x, y: x + y), "sf": ws(lambda x, y: y - x), "and": ws(lambda x, y: x & y),
 "or": ws(lambda x, y: x | y), "xor": ws(lambda x, y: x ^ y), "orc": ws(lambda x, y: x | (~y)),
 "andc": ws(lambda x, y: x & ~y), "ai": ws(lambda x, i: x + i), "sfi": ws(lambda x, i: i - x),
 "ori": ws(lambda x, i: x | (i & 0xFFFFFFFF)), "andi": ws(lambda x, i: x & (i & 0xFFFFFFFF)),
 "shli": ws(lambda x, i: (x << (i & 63)) if (i & 63) < 32 else 0),
 "rotmi": ws(lambda x, i: (x >> (-i & 63)) if (-i & 63) < 32 else 0),
 "rotmai": ws(lambda x, i: (s32(x) >> min(-i & 63, 31))),
 "ceqi": ws(lambda x, i: 0xFFFFFFFF if x == (i & 0xFFFFFFFF) else 0),
 "cgt": ws(lambda x, y: 0xFFFFFFFF if s32(x) > s32(y) else 0),
 "clgt": ws(lambda x, y: 0xFFFFFFFF if x > y else 0),
 "ceq": ws(lambda x, y: 0xFFFFFFFF if x == y else 0),
})
@op("il")
def _(i): return mk([i] * 4)
@op("ila")
def _(i): return mk([i & 0x3FFFF] * 4)
@op("ilhu")
def _(i): return mk([(i & 0xFFFF) << 16] * 4)
@op("ilh")
def _(i): return mk([((i & 0xFFFF) << 16) | (i & 0xFFFF)] * 4)
@op("iohl")
def _(a, i): return mk([w | (i & 0xFFFF) for w in words(a)])
@op("fsmbi")
def _(i): return bytes(0xFF if (i >> (15 - k)) & 1 else 0 for k in range(16))
@op("fsm")
def _(a):
    m = W(a, 0) & 0xF; return mk([0xFFFFFFFF if (m >> (3 - k)) & 1 else 0 for k in range(4)])
@op("andbi")
def _(a, i): return bytes(x & (i & 0xFF) for x in a)
@op("orbi")
def _(a, i): return bytes(x | (i & 0xFF) for x in a)
@op("xorbi")
def _(a, i): return bytes(x ^ (i & 0xFF) for x in a)
@op("ceqbi")
def _(a, i): return bytes(0xFF if x == (i & 0xFF) else 0 for x in a)
@op("cgtbi")
def _(a, i): return bytes(0xFF if sext(x, 8) > sext(i & 0xFF, 8) else 0 for x in a)
@op("clgtbi")
def _(a, i): return bytes(0xFF if x > (i & 0xFF) else 0 for x in a)
@op("ceqhi")
def _(a, i): return b"".join(struct.pack(">H", 0xFFFF if struct.unpack_from(">H", a, 2*k)[0] == (i & 0xFFFF) else 0) for k in range(8))
@op("xsbh")
def _(a): return b"".join(struct.pack(">h", sext(a[2*k+1], 8)) for k in range(8))
@op("ah")
def _(a, b): return b"".join(struct.pack(">H", (struct.unpack_from(">H", a, 2*k)[0] + struct.unpack_from(">H", b, 2*k)[0]) & 0xFFFF) for k in range(8))
@op("shufb")
def _(a, b, c): return shufb(a, b, c)
@op("selb")
def _(a, b, c): return bytes((x & ~m | y & m) & 0xFF for x, y, m in zip(a, b, c))
@op("rotqbyi")
def _(a, i): n = i & 15; return bytes(a[n:] + a[:n])
@op("rotqby")
def _(a, b): n = W(b, 0) & 15; return bytes(a[n:] + a[:n])
@op("shlqbyi")
def _(a, i): n = i & 31; return bytes(a[n:] + b"\0" * n) if n < 16 else bytes(16)
@op("rotqmbyi")
def _(a, i): n = -i & 31; return bytes(b"\0" * n + a[:16 - n]) if n < 16 else bytes(16)
@op("rotqmby")
def _(a, b): n = -W(b, 0) & 31; return bytes(b"\0" * n + a[:16 - n]) if n < 16 else bytes(16)
@op("gbh")
def _(a):
    m = 0
    for k in range(8): m = (m << 1) | (a[2*k+1] & 1)
    return mk([m, 0, 0, 0])
@op("gb")
def _(a):
    m = 0
    for k in range(4): m = (m << 1) | (W(a, k) & 1)
    return mk([m, 0, 0, 0])
@op("cntb")
def _(a): return bytes(bin(x).count("1") for x in a)
@op("mpyu")
def _(a, b): return mk([(W(a, k) & 0xFFFF) * (W(b, k) & 0xFFFF) for k in range(4)])
@op("mpyhhu")
def _(a, b): return mk([(W(a, k) >> 16) * (W(b, k) >> 16) for k in range(4)])
@op("cg")
def _(a, b): return mk([1 if W(a, k) + W(b, k) > 0xFFFFFFFF else 0 for k in range(4)])
@op("addx")
def _(a, b, c): return mk([W(a, k) + W(b, k) + (W(c, k) & 1) for k in range(4)])
@op("rotma")
def _(a, b): return mk([s32(W(a, k)) >> min(-W(b, k) & 63, 31) for k in range(4)])
@op("fm")
def _(a, b): return fromf([x*y for x,y in zip(f32(a),f32(b))])
@op("fs")
def _(a, b): return fromf([x-y for x,y in zip(f32(a),f32(b))])
@op("fa")
def _(a, b): return fromf([x+y for x,y in zip(f32(a),f32(b))])
@op("fma")
def _(a, b, c): return fromf([x*y+z for x,y,z in zip(f32(a),f32(b),f32(c))])
@op("fnms")
def _(a, b, c): return fromf([z-x*y for x,y,z in zip(f32(a),f32(b),f32(c))])
@op("fcgt")
def _(a, b): return mk([0xFFFFFFFF if x > y else 0 for x, y in zip(f32(a), f32(b))])
@op("frest")
def _(a): return fromf([1/x if x else 3.4e38 for x in f32(a)])
@op("fi")
def _(a, b): return bytes(b)
@op("cflts")
def _(a, i): return mk([max(-2**31, min(2**31 - 1, int(x * 2.0 ** (173 - i)))) for x in f32(a)])
@op("cuflt")
def _(a, i): return fromf([w / 2.0 ** (155 - i) for w in words(a)])

@op("roti")
def _(a, i): n = i & 31; return mk([((w << n) | (w >> (32 - n))) & 0xFFFFFFFF if n else w for w in words(a)])
@op("rothmi")
def _(a, i):
    n = -i & 31
    return b"".join(struct.pack(">H", (struct.unpack_from(">H", a, 2*k)[0] >> n) if n < 16 else 0) for k in range(8))
@op("clgti")
def _(a, i): return mk([0xFFFFFFFF if w > (i & 0xFFFFFFFF) else 0 for w in words(a)])
@op("ceqh")
def _(a, b): return b"".join(struct.pack(">H", 0xFFFF if a[2*k:2*k+2] == b[2*k:2*k+2] else 0) for k in range(8))
@op("fcmeq")
def _(a, b): return mk([0xFFFFFFFF if abs(x) == abs(y) else 0 for x, y in zip(f32(a), f32(b))])
@op("mpyui")
def _(a, i): return mk([(w & 0xFFFF) * (i & 0xFFFF) for w in words(a)])
@op("mpyi")
def _(a, i): return mk([sext(w & 0xFFFF, 16) * sext(i & 0xFFFF, 16) for w in words(a)])

# ---- exact SPU single precision: extended range, truncation, no denormals ----
from fractions import Fraction
FREST_LUT = [8387552, 8357798, 7401330, 7375680, 6523666, 6503146, 5739204, 5720736, 5032574, 5018210, 4393540, 4380202, 3812882, 3800570, 3282404, 3273170, 2795966, 2786732, 2348442, 2342286, 1934710, 1934710, 1551706, 1551706, 1197379, 1197379, 866605, 866605, 558362, 558362, 269575, 269575]
def sval(w):
    e = (w >> 23) & 0xFF
    if e == 0: return Fraction(0)
    v = Fraction((1 << 23) | (w & 0x7FFFFF), 1 << 23) * (Fraction(2) ** (e - 127))
    return -v if w >> 31 else v
def spack(v):
    if v == 0: return 0
    sign = 0x80000000 if v < 0 else 0; v = abs(v)
    e = 127
    while v >= 2: v /= 2; e += 1
    while v < 1: v *= 2; e -= 1
    if e > 255: return sign | 0x7FFFFFFF
    if e < 1: return 0
    frac = int((v - 1) * (1 << 23))       # truncate toward zero
    return sign | (e << 23) | frac
def fl(f): return lambda *a: mk([f(*[x if isinstance(x, int) else W(x, k) for x in a]) for k in range(4)])
OPS["fm"] = fl(lambda x, y: spack(sval(x) * sval(y)))
OPS["fa"] = fl(lambda x, y: spack(sval(x) + sval(y)))
OPS["fs"] = fl(lambda x, y: spack(sval(x) - sval(y)))
OPS["fma"] = fl(lambda x, y, z: spack(sval(x) * sval(y) + sval(z)))
OPS["fnms"] = fl(lambda x, y, z: spack(sval(z) - sval(x) * sval(y)))
OPS["fms"] = fl(lambda x, y, z: spack(sval(x) * sval(y) - sval(z)))
OPS["fcgt"] = fl(lambda x, y: 0xFFFFFFFF if sval(x) > sval(y) else 0)
OPS["fcmeq"] = fl(lambda x, y: 0xFFFFFFFF if abs(sval(x)) == abs(sval(y)) else 0)
def _frest(w):
    e = (w >> 23) & 0xFF
    rexp = 255 if e == 0 else (0 if e >= 253 else 253 - e)
    return (w & 0x80000000) | (rexp << 23) | (FREST_LUT[(w >> 18) & 0x1F] & 0x7FFFFF)
OPS["frest"] = fl(_frest)
def _fi(a, est):
    sign = est & 0x80000000; e = (est >> 23) & 0xFF
    base = (est >> 10) & 0x1FFF; step = est & 0x3FF; y = a & 0x7FFFF
    if e == 0: return 0
    m = (((1 << 13) | base) << 19) - step * y
    if m < (1 << 32):
        m <<= 1; e -= 1
        if e == 0: return 0
    return sign | (e << 23) | ((m >> 9) & 0x7FFFFF)
OPS["fi"] = fl(_fi)
def _cfl(x, scale):
    if (x >> 23) & 0xFF == 0: return 0
    v = sval(x) * (Fraction(2) ** scale)
    i = int(v)   # truncation toward zero
    return max(-2**31, min(2**31 - 1, i)) & 0xFFFFFFFF
OPS["cflts"] = lambda a, i: mk([_cfl(W(a, k), 173 - (i & 0xFF)) for k in range(4)])
OPS["cuflt"] = lambda a, i: mk([spack(Fraction(W(a, k)) / (Fraction(2) ** (155 - (i & 0xFF)))) for k in range(4)])

FRSQEST_LUT = [3473760, 3467604, 3119421, 3119421, 2794787, 2794787, 2496781, 2496781, 2221305, 2221305, 1966313, 1966313, 1727705, 1727705, 1505483, 1505483, 1297600, 1297600, 1100979, 1100979, 917674, 917674, 743585, 743585, 578712, 578712, 423057, 423057, 274569, 274569, 134275, 134275, 8388084, 8376798, 7887304, 7880122, 7427484, 7427484, 7005564, 7005564, 6616417, 6616417, 6254921, 6254921, 5918003, 5918003, 5603615, 5603615, 5309711, 5309711, 5032190, 5032190, 4772080, 4772080, 4526308, 4526308, 4292823, 4292823, 4072652, 4072652, 3863747, 3863747, 3664058, 3664058]
def _frsqest(w):
    e = (w >> 23) & 0xFF
    rexp = 0xFF if e == 0 else (190 - (e + 1) // 2)
    return ((rexp & 0xFF) << 23) | (FRSQEST_LUT[(w >> 18) & 0x3F] & 0x7FFFFF)
OPS["frsqest"] = fl(_frsqest)
def _rotm(a, b):
    n = (-b) & 0x3F
    return (a >> n) if n < 32 else 0
OPS["rotm"] = fl(_rotm)
OPS["mpya"] = fl(lambda x, y, z: (sext(x & 0xFFFF, 16) * sext(y & 0xFFFF, 16) + z) & 0xFFFFFFFF)

@op("cwx")
def _(a, b):
    t = (W(a, 0) + W(b, 0)) & 0xC
    ctl = bytearray(range(0x10, 0x20))
    ctl[t:t + 4] = bytes([0, 1, 2, 3])
    return bytes(ctl)
@op("shlqbii")
def _(a, i):
    n = i & 7; v = int.from_bytes(a, "big")
    return ((v << n) & ((1 << 128) - 1)).to_bytes(16, "big")
@op("rotqmbii")
def _(a, i):
    n = -i & 7; v = int.from_bytes(a, "big")
    return (v >> n).to_bytes(16, "big")
@op("orx")
def _(a): return mk([W(a, 0) | W(a, 1) | W(a, 2) | W(a, 3), 0, 0, 0])

class Emu:
    def __init__(s, gpr_le, ls):
        s.r = []
        for n in range(128):  # host u128: _u32[k] = SPU word k, little-endian storage
            s.r.append(mk(struct.unpack_from("<4I", gpr_le, n * 16)))
        s.ls = bytearray(ls); s.trace = None
    def val(s, t):
        t = t.strip()
        m = re.fullmatch(r"r\[(\d+)\]", t)
        if m: return s.r[int(m[1])]
        return int(t, 0)
    def addr(s, t):
        t = t.replace("ctx->gpr", "r")
        tot = 0
        for part in re.split(r"\s*\+\s*", t.strip()):
            m = re.fullmatch(r"r\[(\d+)\]\._u32\[0\]", part)
            tot += W(s.r[int(m[1])], 0) if m else int(part, 0)
        return tot & 0x3FFF0
    def run(s, pc, max_steps=200000, stop=None):
        steps = 0
        while pc and pc != stop:
            lines = [l.replace("ctx->gpr", "r") for l in func(pc)]
            labels = {l[:-1]: i for i, l in enumerate(lines) if re.fullmatch(r"loc_\w+:", l)}
            i = 0; nxt = None
            while i < len(lines):
                l = lines[i]; i += 1; steps += 1
                if steps > max_steps: raise RuntimeError("too many steps")
                if l.startswith("/*") or l.startswith("loc_"): continue
                m = re.fullmatch(r"r\[(\d+)\] = spu_ls_read128\(ctx, (.*)\);", l)
                if m: s.r[int(m[1])] = bytes(s.ls[s.addr(m[2]):s.addr(m[2]) + 16]); continue
                m = re.fullmatch(r"spu_ls_write128\(ctx, (.*), r\[(\d+)\]\);", l)
                if m:
                    a = s.addr(m[1]); s.ls[a:a + 16] = s.r[int(m[2])]
                    if s.trace is not None: s.trace.append(("W", a, bytes(s.r[int(m[2])])))
                    continue
                m = re.fullmatch(r"r\[(\d+)\] = spu_(\w+)\((.*)\);", l)
                if m:
                    args = [s.val(x) for x in m[3].split(",")]
                    s.r[int(m[1])] = OPS[m[2]](*args); continue
                m = re.fullmatch(r"if \(r\[(\d+)\]\._u(32|16)\[0\] (==|!=) 0\) (.*)", l)
                if m:
                    v = W(s.r[int(m[1])], 0); v = v & 0xFFFF if m[2] == "16" else v
                    if (v == 0) == (m[3] == "=="):
                        l = m[4]
                    else: continue
                m = re.fullmatch(r"goto (loc_\w+);", l)
                if m: i = labels[m[1]]; continue
                m = re.search(r"g_spu_trampoline_fn = " + PREFIX_RE + r"(\w+);", l)
                if m: nxt = m[1]; break
                if l.startswith("SPU_RET"): return steps
                raise ValueError("unhandled: " + l)
            pc = nxt
        return steps

if __name__ == "__main__":
    p = Path(sys.argv[1]); b = p.read_bytes(); G = 2048
    e = Emu(b[:G], b[G:G + LS]); e.trace = []
    n = e.run("60E8")
    want = b[2 * G + LS:]
    diff = [i for i in range(LS) if e.ls[i] != want[i]]
    print(p.name, "steps", n, "LS bytes differing from dump:", len(diff), "writes:", len(e.trace))
