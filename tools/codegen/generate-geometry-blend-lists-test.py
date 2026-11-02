"""Pinned BF8 list/blend oracle; all derived lift fragments stay private."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed list/blend source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected = {pc for pc in functions if 0xbf8 <= pc <= 0x12e8}
    callees = {int(pc, 16) for start in selected for pc in re.findall(PREFIX + r"([0-9A-F]+)", functions[start])} - selected
    if callees != {0x7118, 0x7df8, 0x2780, 0x2c00}:
        raise SystemExit(f"Unreviewed list/blend boundaries {callees}")
    # The lift contains inline suffixes following interrupt thunks. Computed
    # r2+8 dispatch reaches these original instructions, not the thunk again.
    # Expose private oracle entries; do not invent native reference behavior.
    interiors = {0xde8: (0xde0, "        ctx->gpr[6] = spu_ilhu"),
                 0xee8: (0xee0, "        ctx->gpr[47] = spu_ila"),
                 0xf48: (0xf40, "        ctx->gpr[81] = spu_il"),
                 0x111c: (0x1114, "        ctx->gpr[11] = spu_il"),
                 0x1240: (0x1238, "        { ctx->pc = 0x1160"),
                 0x12c0: (0x12b8, "        { ctx->pc = 0x1018")}
    for pc, (parent, marker) in interiors.items():
        if pc in functions or functions[parent].count(marker) != 1:
            raise SystemExit(f"Unreviewed original interior {pc:x}")
        functions[pc] = "\n" + functions[parent][functions[parent].index(marker):]
    selected |= interiors.keys()
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | callees))
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output += "\nstatic buzz_geometry_job_fn list_reference_lookup(uint32_t pc) { switch(pc) {\n"
    output += "\n".join(f"case 0x{pc:x}: return {PREFIX}{pc:08X};" for pc in sorted(selected))
    output += "\ndefault: return nullptr; } }\n"
    output = re.sub(r"\bspu_wrch\(", "buzz_geometry_job_wrch(", output)
    output = re.sub(r"\bspu_rdch\(", "buzz_geometry_job_rdch(", output)
    path = ROOT / "analysis/native-geometry/phase3/test-blend-lists-reference.inc"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(output, encoding="utf-8")
    fixture = (ROOT / "analysis/native-geometry/snapshots/p1-all/pc_9148.bin").read_bytes()
    if len(fixture) != 264192 or hashlib.sha256(fixture).hexdigest() != "99eea4d670679a82083ece36a8fdfdfde2b09a772d5d3bfca4da9cefd560fe1f":
        raise SystemExit("Unreviewed list/blend fixture")
    (path.parent / "test-blend-lists-fixture.bin").write_bytes(fixture)
    print(f"Pinned list/blend oracle: {len(selected)} original blocks/interiors; four explicit data callees")


if __name__ == "__main__":
    main()
