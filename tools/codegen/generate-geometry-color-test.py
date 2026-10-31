"""Pinned complete U8 descriptor codec oracle, with private derived output."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed color source")
    functions = {int(pc,16): body for pc,body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected = {0x89b0,0x89e8,0x8a60,0x8af0,0x8b08}
    refs = {int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if refs-selected != {0x83c8}:
        raise SystemExit("Unreviewed color boundaries")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(refs|selected))
    output += "\nstatic void "+PREFIX+"000083C8(spu_context*) { abort(); }\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    path = ROOT/"analysis/native-geometry/phase3/test-color-reference.inc"
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(output,encoding="utf-8")
    print("Pinned color codec oracle: 5 complete original blocks;83C8 continuation trapped")


if __name__ == "__main__":
    main()
