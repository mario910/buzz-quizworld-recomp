# Minimal Lua 5.1 bytecode disassembler (big-endian, float32 numbers) for Buzz .clu
import struct,sys
OP="MOVE LOADK LOADBOOL LOADNIL GETUPVAL GETGLOBAL GETTABLE SETGLOBAL SETUPVAL SETTABLE NEWTABLE SELF ADD SUB MUL DIV MOD POW UNM NOT LEN CONCAT JMP EQ LT LE TEST TESTSET CALL TAILCALL RETURN FORLOOP FORPREP TFORLOOP SETLIST CLOSE CLOSURE VARARG".split()
class R:
    def __init__(s,d): s.d=d; s.p=0
    def u8(s): v=s.d[s.p]; s.p+=1; return v
    def i32(s): v=struct.unpack_from('>i',s.d,s.p)[0]; s.p+=4; return v
    def u32(s): v=struct.unpack_from('>I',s.d,s.p)[0]; s.p+=4; return v
    def f32(s): v=struct.unpack_from('>f',s.d,s.p)[0]; s.p+=4; return v
    def str(s):
        n=s.u32()
        if n==0: return None
        v=s.d[s.p:s.p+n-1]; s.p+=n; return v.decode('latin1')
def func(r,depth,out):
    src=r.str(); r.i32(); r.i32(); nup,npar,va,ms=r.u8(),r.u8(),r.u8(),r.u8()
    n=r.i32(); code=[r.u32() for _ in range(n)]
    n=r.i32(); K=[]
    for _ in range(n):
        t=r.u8()
        K.append(None if t==0 else bool(r.u8()) if t==1 else r.f32() if t==3 else repr(r.str()))
    n=r.i32(); protos=[]
    for _ in range(n): protos.append(lambda: None); 
    subs=[]
    for _ in range(n): sub=[]; func(r,depth+1,sub); subs.append(sub)
    n=r.i32(); [r.i32() for _ in range(n)]
    n=r.i32()
    for _ in range(n): r.str(); r.i32(); r.i32()
    n=r.i32(); [r.str() for _ in range(n)]
    ind='  '*depth
    out.append(f"{ind}function <{src}> params={npar} consts={len(K)} instrs={len(code)}")
    def rk(x): return f"K{x-256}={K[x-256]}" if x>=256 else f"R{x}"
    for pc,ins in enumerate(code):
        o=ins&63; a=(ins>>6)&255; c=(ins>>14)&511; b=(ins>>23)&511; bx=ins>>14; sbx=bx-131071
        nm=OP[o]
        if nm in('LOADK','GETGLOBAL','SETGLOBAL'): t=f"R{a} {K[bx]}"
        elif nm in('JMP','FORLOOP','FORPREP'): t=f"{sbx}"
        elif nm=='CLOSURE': t=f"R{a} proto{bx}"
        elif nm in('GETTABLE','SELF'): t=f"R{a} R{b} {rk(c)}"
        elif nm in('SETTABLE','ADD','SUB','MUL','DIV','EQ','LT','LE'): t=f"{a} {rk(b)} {rk(c)}"
        else: t=f"{a} {b} {c}"
        out.append(f"{ind}  {pc:4d} {nm:9s} {t}")
    for s in subs: out.extend(s)
d=open(sys.argv[1],'rb').read(); r=R(d); r.p=12; out=[]; func(r,0,out); print('\n'.join(out))
