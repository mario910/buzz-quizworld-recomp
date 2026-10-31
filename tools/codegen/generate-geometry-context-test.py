"""Extract pinned context initialization and its real driver entry preamble."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = SOURCE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed context initialization source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    # Stop immediately before the call: all GPR arguments and the real custom
    # stack descriptor are prepared by the original driver, not by our model.
    boundary = "ctx->gpr[0] = spu_link(0x2088);"
    if functions[0x2008].count(boundary) != 1:
        raise SystemExit("Unreviewed initializer call boundary")
    functions[0x2008] = functions[0x2008].split(boundary)[0] + boundary + " ctx->pc = 0x2f80; return;\n"
    selected = set()
    pending = [0x1c18, 0x2f80]
    while pending:
        pc = pending.pop()
        if pc in selected:
            continue
        if pc == 0x9f8:  # Unreachable textual fall-through after setup return.
            continue
        if pc not in functions:
            raise SystemExit(f"Missing oracle target {pc:X}")
        selected.add(pc)
        body = functions[pc]
        targets = [int(value, 16) for value in re.findall(PREFIX + r"([0-9A-F]+)", body)]
        pending.extend(targets)
    if any(not (0x8a8 <= pc < 0x9f8 or 0x1c18 <= pc <= 0x25c8 or 0x2f80 <= pc <= 0x3268) for pc in selected):
        raise SystemExit("Oracle escaped reviewed preamble/context intervals")
    for pc in selected:
        body = functions[pc]
        if pc != 0x24f0 and re.search(r"\bspu_(wrch|rdch|rchcnt)\(", body):
            raise SystemExit(f"Unexpected preamble I/O in {pc:X}")
        if pc >= 0x2f80 and set(re.findall(r"ctx->(\w+)", body)) - {"gpr", "pc"}:
            raise SystemExit(f"Unexpected initializer control effects in {pc:X}")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected))
    output += f"\nstatic void {PREFIX}000009F8(spu_context*) {{ abort(); }}\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n" for pc in sorted(selected))
    destination = ROOT / "analysis/native-geometry/phase3/test-context-reference.inc"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(output)
    print(f"Pinned context oracle: {len(selected)} original preamble/initializer blocks -> {destination}")


if __name__ == "__main__":
    main()
