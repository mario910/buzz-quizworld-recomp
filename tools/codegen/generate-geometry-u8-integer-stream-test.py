"""Pinned complete u8-integer-stream descriptor codec oracle, with private derived output."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed u8-integer-stream source")
    functions = {int(pc,16): body for pc,body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected = {0x8ea8,0x8ee0,0x8f48,0x8fcc,0x8fe0}
    refs = {int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if refs-selected != {0x83c8}:
        raise SystemExit("Unreviewed u8-integer-stream boundaries")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(refs|selected))
    output += "\n#ifndef BUZZ_GEOMETRY_TEST_CONTINUATION_83C8_DEFINED\nstatic void "+PREFIX+"000083C8(spu_context*) { abort(); }\n#endif\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    path = ROOT/"analysis/native-geometry/phase3/test-u8-integer-stream-reference.inc"
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(output,encoding="utf-8")
    fixture = (ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if len(fixture)!=264192 or hashlib.sha256(fixture).hexdigest()!="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":
        raise SystemExit("Unreviewed private u8-integer-stream fixture")
    (path.parent/"test-u8-integer-stream-fixture.bin").write_bytes(fixture)
    print("Pinned u8-integer-stream codec oracle: 5 complete original blocks;83C8 continuation trapped")


if __name__ == "__main__":
    main()
