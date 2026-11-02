"""Pinned complete blend-scatter descriptor codec oracle, with private derived output."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed blend-scatter source")
    functions = {int(pc,16): body for pc,body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",raw.decode())}
    selected = {0xc750,0xc7a8}
    refs = {int(pc,16) for start in selected for pc in re.findall(PREFIX+r"([0-9A-F]+)",functions[start])}
    if refs-selected:
        raise SystemExit("Unreviewed blend-scatter boundaries")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(refs|selected))
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    path = ROOT/"analysis/native-geometry/phase3/test-blend-scatter-reference.inc"
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(output,encoding="utf-8")
    fixture = (ROOT/"analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if len(fixture)!=264192 or hashlib.sha256(fixture).hexdigest()!="99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":
        raise SystemExit("Unreviewed private blend-scatter fixture")
    (path.parent/"test-blend-scatter-fixture.bin").write_bytes(fixture)
    print("Pinned blend-scatter codec oracle: 2 complete original blocks;root link return trapped")


if __name__ == "__main__":
    main()
