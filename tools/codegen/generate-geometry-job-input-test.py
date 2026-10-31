"""Extract the pinned DMA-free 08A8 setup as a private input-model oracle."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"


def main():
    raw = SOURCE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed image-41 input setup")
    functions = re.findall(
        r"(?ms)^void (buzz_job_6eba_spu_func_([0-9A-F]+))\(spu_context\* ctx\) \{(.*?)^\}",
        raw.decode())
    selected = [(name, body) for name, pc, body in functions if 0x8a8 <= int(pc, 16) < 0x9f8]
    if not selected or selected[0][0] != "buzz_job_6eba_spu_func_000008A8":
        raise SystemExit("Missing setup boundary")
    names = {name for name, body in selected}
    for name, body in selected:
        if set(re.findall(r"ctx->(\w+)", body)) - {"gpr", "pc"} or re.search(r"spu_(wrch|rdch|rchcnt|halt|stop)", body):
            raise SystemExit(f"Unexpected setup side effect in {name}")
        targets = set(re.findall(r"g_spu_trampoline_fn = (buzz_job_6eba_spu_func_\w+)", body))
        # 09E0 has an unreachable textual fall-through to 09F8 after return.
        if targets - names - {"buzz_job_6eba_spu_func_000009F8"}:
            raise SystemExit(f"Unreviewed setup branch in {name}")
    output = "\n".join(f"static void {name}(spu_context*);" for name in sorted(names))
    output += "\nstatic void buzz_job_6eba_spu_func_000009F8(spu_context*) { abort(); }\n"
    output += "\n".join(f"static void {name}(spu_context* ctx) {{{body}}}\n" for name, body in selected)
    destination = ROOT / "analysis/native-geometry/phase3/test-input-reference.inc"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(output)
    print(f"Pinned input setup: {len(selected)} DMA-free blocks -> {destination}")


if __name__ == "__main__":
    main()
