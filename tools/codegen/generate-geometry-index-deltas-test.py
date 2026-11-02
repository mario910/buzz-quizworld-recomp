"""Emit the pinned two-block delta oracle into private analysis only."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed index-delta source")
    fixture = ROOT / "analysis/native-geometry/phase3/index-audit-v1/pc_7730.bin"
    if hashlib.sha256(fixture.read_bytes()).hexdigest() != "6312619a2af3965da9a5882161462178f1a70db483d8688af6fecb4cf6912a51":
        raise SystemExit("Unreviewed original index-delta entry snapshot")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void " + PREFIX + r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected = {0x7730, 0x7770}
    references = {int(pc, 16) for start in selected for pc in re.findall(PREFIX + r"([0-9A-F]+)", functions[start])}
    if references - selected != {0x77f8}:
        raise SystemExit("Unreviewed index-delta boundary")
    text = "namespace index_deltas_oracle {\n"
    text += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | references))
    text += f"\nstatic void {PREFIX}000077F8(spu_context*) {{abort();}}\n"
    text += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    text += "\n}\n"
    out = ROOT / "analysis/native-geometry/phase3"
    out.mkdir(parents=True, exist_ok=True)
    (out / "test-index-deltas-reference.inc").write_text(text, encoding="utf-8")
    print("Pinned index delta oracle: 7730/7770 complete; 77F8 boundary trapped")


if __name__ == "__main__":
    main()
