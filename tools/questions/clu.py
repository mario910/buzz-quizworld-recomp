"""Read/write Buzz! Quiz World compiled Lua (.clu): Lua 5.1, big-endian,
4-byte int/size_t/Instruction, float32 lua_Number.

Used to patch constants in pack scripts (e.g. AddPackInfo's pack id/dir) and
to emit simple straight-line call scripts for generated packs.

  python -I clu.py roundtrip <file.clu|dir>   # parse + rewrite, compare bytes
  python -I clu.py consts <file.clu>          # list constants per function
"""
import struct
import sys
from pathlib import Path

HEADER = b"\x1bLuaQ\x00\x00\x04\x04\x04\x04\x00"
NIL, BOOL, NUM, STR = 0, 1, 3, 4


class Proto:
    def __init__(self):
        self.source = None
        self.line_defined = self.last_line = 0
        self.nups = self.nparams = self.vararg = self.maxstack = 0
        self.code = []          # u32 instructions
        self.consts = []        # None | bool | float | str (latin-1 bytes as str)
        self.protos = []
        self.lineinfo = []
        self.locvars = []       # (name, startpc, endpc)
        self.upvalues = []


class _R:
    def __init__(self, d):
        self.d, self.p = d, 0

    def take(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise ValueError("truncated .clu")
        self.p += n
        return v

    def u8(self): return self.take(1)[0]
    def i32(self): return struct.unpack(">i", self.take(4))[0]
    def f32(self): return struct.unpack(">f", self.take(4))[0]

    def string(self):
        n = struct.unpack(">I", self.take(4))[0]
        if n == 0:
            return None
        s = self.take(n)
        if s[-1] != 0:
            raise ValueError("unterminated string")
        return s[:-1].decode("latin-1")


def _read_proto(r):
    f = Proto()
    f.source = r.string()
    f.line_defined, f.last_line = r.i32(), r.i32()
    f.nups, f.nparams, f.vararg, f.maxstack = r.u8(), r.u8(), r.u8(), r.u8()
    f.code = [struct.unpack(">I", r.take(4))[0] for _ in range(r.i32())]
    for _ in range(r.i32()):
        t = r.u8()
        if t == NIL: f.consts.append(None)
        elif t == BOOL: f.consts.append(bool(r.u8()))
        elif t == NUM: f.consts.append(r.f32())
        elif t == STR: f.consts.append(r.string())
        else: raise ValueError(f"constant type {t}")
    f.protos = [_read_proto(r) for _ in range(r.i32())]
    f.lineinfo = [r.i32() for _ in range(r.i32())]
    f.locvars = [(r.string(), r.i32(), r.i32()) for _ in range(r.i32())]
    f.upvalues = [r.string() for _ in range(r.i32())]
    return f


def load(data):
    if data[:12] != HEADER:
        raise ValueError("not a Buzz .clu (Lua 5.1 BE float32)")
    r = _R(data)
    r.p = 12
    f = _read_proto(r)
    if r.p != len(data):
        raise ValueError("trailing bytes")
    return f


def _w_str(out, s):
    if s is None:
        out += struct.pack(">I", 0)
    else:
        b = s.encode("latin-1") + b"\0"
        out += struct.pack(">I", len(b)) + b


def _write_proto(out, f):
    _w_str(out, f.source)
    out += struct.pack(">ii4B", f.line_defined, f.last_line, f.nups, f.nparams, f.vararg, f.maxstack)
    out += struct.pack(">i", len(f.code)) + b"".join(struct.pack(">I", c) for c in f.code)
    out += struct.pack(">i", len(f.consts))
    for c in f.consts:
        if c is None: out += bytes([NIL])
        elif isinstance(c, bool): out += bytes([BOOL, int(c)])
        elif isinstance(c, (int, float)): out += bytes([NUM]) + struct.pack(">f", float(c))
        else: out += bytes([STR]); _w_str(out, c)
    out += struct.pack(">i", len(f.protos))
    for p in f.protos:
        _write_proto(out, p)
    out += struct.pack(">i", len(f.lineinfo)) + b"".join(struct.pack(">i", x) for x in f.lineinfo)
    out += struct.pack(">i", len(f.locvars))
    for name, a, b in f.locvars:
        _w_str(out, name)
        out += struct.pack(">ii", a, b)
    out += struct.pack(">i", len(f.upvalues))
    for u in f.upvalues:
        _w_str(out, u)


def dump(f):
    out = bytearray(HEADER)
    _write_proto(out, f)
    return bytes(out)


def replace_consts(f, mapping):
    """Replace constants equal to a key (exact type/value) in all functions.
    Returns the number of replacements."""
    n = 0
    for i, c in enumerate(f.consts):
        for old, new in mapping.items():
            if type(c) is type(old) and c == old:
                f.consts[i] = new
                n += 1
                break
    return n + sum(replace_consts(p, mapping) for p in f.protos)


_OP = {"GETGLOBAL": 5, "GETTABLE": 6, "LOADK": 1, "CALL": 28, "RETURN": 30}


def _abc(op, a, b, c): return _OP[op] | a << 6 | c << 14 | b << 23
def _abx(op, a, bx): return _OP[op] | a << 6 | bx << 14


def call_script(calls):
    """Compile straight-line calls into a main chunk.

    calls: [(("PackageData", "NewPackage"), ["q_people"]), ...]; the first
    name is a global, the rest are table fields. Args are str/float/bool.
    Equivalent to the game's generated scripts (results discarded)."""
    f = Proto()
    f.vararg, f.maxstack = 2, 2

    def k(v):
        for i, c in enumerate(f.consts):
            if type(c) is type(v) and c == v:
                return i
        f.consts.append(v)
        return len(f.consts) - 1

    for names, args in calls:
        f.code.append(_abx("GETGLOBAL", 0, k(names[0])))
        for field in names[1:]:
            kf = k(field)
            if kf > 255:
                raise ValueError("too many constants for RK operand")
            f.code.append(_abc("GETTABLE", 0, 0, 256 + kf))
        for i, a in enumerate(args, 1):
            f.code.append(_abx("LOADK", i, k(float(a) if isinstance(a, int) and not isinstance(a, bool) else a)))
        f.maxstack = max(f.maxstack, len(args) + 1)
        f.code.append(_abc("CALL", 0, len(args) + 1, 1))
    f.code.append(_abc("RETURN", 0, 1, 0))
    return f


def _main(argv):
    if len(argv) >= 2 and argv[0] == "roundtrip":
        p = Path(argv[1])
        files = sorted(p.rglob("*.clu")) if p.is_dir() else [p]
        bad = 0
        for fp in files:
            d = fp.read_bytes()
            if dump(load(d)) != d:
                bad += 1
                print("MISMATCH", fp)
        print(f"{len(files)} files, {bad} mismatches")
        return 1 if bad else 0
    if len(argv) >= 2 and argv[0] == "consts":
        def walk(f, depth=0):
            print("  " * depth + f"function params={f.nparams} consts={f.consts}")
            for p in f.protos:
                walk(p, depth + 1)
        walk(load(Path(argv[1]).read_bytes()))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
