"""Pinned 7118/7298 run-list root oracle; generated code remains private."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed run-list source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    # The lexical fallthrough after this unconditional return is unreachable.
    # It belongs to the next function, not the 7118 root's call graph.
    marker = "        SPU_RET(ctx);"
    if functions[0x7298].count(marker) != 1:
        raise SystemExit("Unreviewed run-list return")
    functions[0x7298] = functions[0x7298].split(marker)[0] + marker + "\n"
    selected = {0x7118, 0x7298}
    refs = {int(pc, 16) for start in selected for pc in re.findall(PREFIX + r"([0-9A-F]+)", functions[start])}
    if refs - selected:
        raise SystemExit("Unreviewed run-list boundaries")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected))
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    path = ROOT / "analysis/native-geometry/phase3/test-run-lists-reference.inc"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(output, encoding="utf-8")
    fixture = (ROOT / "analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if len(fixture) != 264192 or hashlib.sha256(fixture).hexdigest() != "99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":
        raise SystemExit("Unreviewed run-list fixture")
    (path.parent / "test-run-lists-fixture.bin").write_bytes(fixture)
    print("Pinned run-list oracle: 2 original blocks, no reachable external callees")


if __name__ == "__main__":
    main()
