"""Pinned 9F8 controller oracle with explicit, adversarial data-call boundaries."""
import hashlib
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
PIN="5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX="buzz_job_6eba_spu_func_"


def main():
    raw=(ROOT/"port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:raise SystemExit("Unreviewed skin dispatch source")
    functions={int(pc,16):body for pc,body in re.findall(r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected={0x9f8,0xa28,0xa84,0xb18,0xb48,0xb54,0xb80,0xb9c}
    references={int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    calls={0x2750,0x2778,0x2780,0xc518,0x2c00}
    if references-selected != calls|{0xaf18,0xbf8}:raise SystemExit("Unreviewed skin dispatch closure")
    # Namespace avoids clashes with the outer-driver and other private oracles.
    output="namespace skin_dispatch_oracle {\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected|references))
    for pc in sorted(calls|{0xaf18,0xbf8}):
        body=f"skin_dispatch_model(ctx,0x{pc:x});" if pc in calls else "abort();"
        output+=f"\nstatic void {PREFIX}{pc:08X}(spu_context* ctx) {{{body}}}\n"
    output+="\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output+="\nstatic const buzz_geometry_skin_dispatch_functions library={"+",".join(f"{PREFIX}{pc:08X}" for pc in (0x2750,0x2778,0x2780,0xc518,0x2c00,0xaf18))+"};\n}\n"
    out=ROOT/"analysis/native-geometry/phase3";out.mkdir(parents=True,exist_ok=True)
    (out/"test-skin-dispatch-reference.inc").write_text(output,encoding="utf-8")
    print("Pinned skin dispatch oracle:8 complete controller blocks;5 explicit data-call models; AF18 and unreachable BF8 trapped")


if __name__=="__main__":main()
