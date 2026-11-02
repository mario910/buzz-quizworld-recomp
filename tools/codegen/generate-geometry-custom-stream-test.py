"""Pinned custom input dispatcher oracle, stopping before its data tail."""
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed custom-stream source")
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0x93f0,0x9408,0x9518,0x9580,0x95dc,0x9668}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    stubs=references-selected
    if stubs!={0x9498,0xabc8,0x7df8}:raise SystemExit(f"Unreviewed custom stream closure {stubs}")
    output="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected|stubs))
    output+="\n"+"\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{ abort(); }}" for pc in sorted(stubs))+"\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-custom-stream-reference.inc").write_text(output,encoding="utf-8")
    print("Pinned custom-stream oracle: 6 complete original blocks; exact7DF8 handoff, unreachable error/zero paths trapped")


if __name__=="__main__":main()
