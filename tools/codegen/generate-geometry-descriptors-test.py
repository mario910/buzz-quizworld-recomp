"""Private complete descriptor oracle with original typed handlers and blend."""
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
FIXTURE_PIN="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed descriptor lift")
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={pc for pc in functions if 0x7df8<=pc<0x93f0}
    pending=[0xc750,0xc810,0xcac0]
    while pending:
        pc=pending.pop()
        if pc in selected or pc==0xcc70:continue
        selected.add(pc)
        lines=[]
        for line in functions[pc].splitlines():
            lines.append(line)
            if line=="        SPU_RET(ctx);" or re.match(r"^        \{ ctx->pc = .* return; \}$",line):break
        pending.extend(int(x,16) for x in re.findall(r"g_spu_trampoline_fn = "+PREFIX+r"([0-9A-F]+)","\n".join(lines)))
    refs={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    stubs=refs-selected
    if len(selected)!=64 or stubs!={0xcc70}:raise SystemExit("Unreviewed descriptor oracle boundaries")
    output="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected|stubs))
    output+="\n"+"\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{ abort(); }}" for pc in sorted(stubs))+"\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output+="\nstatic void (*descriptors_reference_lookup(unsigned pc))(spu_context*) { switch(pc) {\n"
    output+="\n".join(f"case 0x{pc:x}:return {PREFIX}{pc:08X};" for pc in sorted(selected))
    output+="\ndefault:return nullptr;} }\n"
    out=ROOT/"analysis/native-geometry/phase3"
    out.mkdir(parents=True,exist_ok=True)
    (out/"test-descriptors-reference.inc").write_text(output,encoding="utf-8")
    fixture=(ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if len(fixture)!=264192 or hashlib.sha256(fixture).hexdigest()!=FIXTURE_PIN:raise SystemExit("Unreviewed private constant fixture")
    (out/"test-descriptors-constants.bin").write_bytes(fixture[2048:2048+0xe680])
    print(f"Pinned descriptor oracle:{len(selected)} original blocks, stubs{sorted(stubs)}; private RO fixture pinned")


if __name__=="__main__":main()
