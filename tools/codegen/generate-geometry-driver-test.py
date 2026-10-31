"""Private pinned outer-driver oracle with explicit mock library boundaries."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"
LIBRARY = (0xac68, 0xbf8, 0x9f8, 0xaea0, 0x6608, 0x2b80, 0x37c0,
           0x2dc0, 0x54e8, 0x35f0, 0x12f0, 0x2e8, 0x32c8, 0x27a0)


def main():
    raw = SOURCE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed outer-driver source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected = {pc for pc in functions if 0x8a8 <= pc < 0x9f8 or
                0x1ad8 <= pc <= 0x2718 or 0x2f80 <= pc <= 0x3268}
    references = {int(target, 16) for pc in selected for target in re.findall(PREFIX + r"([0-9A-F]+)", functions[pc])}
    stubs = references - selected
    if stubs != set(LIBRARY) | {0x2728, 0x2738}:
        raise SystemExit(f"Unreviewed driver library boundaries: {sorted(stubs)}")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | stubs))
    for pc in sorted(stubs):
        body = f"model_call(ctx, 0x{pc:x});" if pc in LIBRARY else "abort();"
        output += f"\nstatic void {PREFIX}{pc:08X}(spu_context* ctx) {{ {body} }}\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}\n" for pc in sorted(selected))
    output += "\nstatic const buzz_geometry_job_functions model_library = {" + ",".join(
        f"{PREFIX}{pc:08X}" for pc in LIBRARY) + "};\n"
    destination = ROOT / "analysis/native-geometry/phase3/test-driver-reference.inc"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(output)
    print(f"Pinned outer-driver oracle: {len(selected)} original blocks, 14 explicit library mocks -> {destination}")


if __name__ == "__main__":
    main()
