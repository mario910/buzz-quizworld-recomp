"""Pinned original skin direction kernels, with derived code kept private."""
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed skin directions source")
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0xbd4c,0xbd54,0xbd58,0xbe20,0xbe28,0xbe30,0xbf7c,0xbf84,0xbf88}
    refs={int(pc,16) for entry in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[entry])}
    if refs-selected!={0xb868}:raise SystemExit("Unreviewed skin direction boundary")
    output="namespace skin_directions_oracle {\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(refs|selected))
    output+=f"\nstatic void {PREFIX}0000B868(spu_context*){{abort();}}\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output+="\nstatic buzz_geometry_job_fn roots[]={"+",".join(f"{PREFIX}{pc:08X}" for pc in (0xbd4c,0xbe20,0xbf7c))+"};\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-skin-directions-reference.inc").write_text(output,encoding="utf-8")
    print("Pinned skin directions oracle:9 complete original blocks;1/2/3 streams; exact B868 boundary trapped")


if __name__=="__main__":main()
