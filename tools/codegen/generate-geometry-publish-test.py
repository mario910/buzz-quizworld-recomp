"""Generate the original index publisher as a private, pinned test oracle."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed index publisher source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected = {pc for pc in functions if 0x2dc0 <= pc <= 0x2f58}
    references = {int(pc, 16) for start in selected for pc in re.findall(PREFIX + r"([0-9A-F]+)", functions[start])}
    stubs = references - selected
    if stubs != {0x298, 0xcc70, 0x2f80}:
        raise SystemExit(f"Unreviewed publisher boundary: {sorted(stubs)}")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | stubs))
    output += "\n" + "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{ abort(); }}" for pc in sorted(stubs))
    output += "\n" + "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    destination = ROOT / "analysis/native-geometry/phase3/test-publish-reference.inc"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(output)
    print(f"Pinned index publisher oracle: {len(selected)} original blocks -> {destination}")


if __name__ == "__main__":
    main()
