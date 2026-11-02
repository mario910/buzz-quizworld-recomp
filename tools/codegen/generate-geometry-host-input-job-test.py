"""Private pinned original attribute graph, including real descriptor codecs."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed host input oracle source")
    functions = {int(pc, 16): body for pc, body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected = {pc for pc in functions if 0x7df8 <= pc < 0x93f0 or 0xac68 <= pc <= 0xada0}
    selected.update((0x93f0,0x9408,0x9498,0x9518,0x9580,0x95dc,0x9668,0x9700,0x97a8,0x2738,0x2760,0x415c,0x4164,0x416c))
    pending = [0x40c8,0xc750,0xc810,0xcac0]
    while pending:
        pc = pending.pop()
        if pc in selected or pc == 0xcc70:
            continue
        selected.add(pc)
        lines = []
        for line in functions[pc].splitlines():
            lines.append(line)
            if line == "        SPU_RET(ctx);" or re.match(r"^        \{ ctx->pc = .* return; \}$", line):
                break
        pending.extend(int(target,16) for target in re.findall(
            r"g_spu_trampoline_fn = " + PREFIX + r"([0-9A-F]+)", "\n".join(lines)))
    refs = {int(pc,16) for start in selected for pc in re.findall(PREFIX + r"([0-9A-F]+)", functions[start])}
    stubs = refs - selected
    if len(selected) != 92 or stubs != {0x2750,0x2778,0x4178,0xabc8,0xadd4,0xadfc,0xae24,0xae4c,0xae74,0xcc70}:
        raise SystemExit(f"Unreviewed host input graph: {len(selected)} blocks, stubs {sorted(stubs)}")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | stubs))
    output += "\n" + "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{ abort(); }}" for pc in sorted(stubs)) + "\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output += "\nstatic void (*host_input_reference_lookup(unsigned pc))(spu_context*) { switch(pc) {\n"
    output += "\n".join(f"case 0x{pc:x}:return {PREFIX}{pc:08X};" for pc in sorted(selected))
    output += "\ndefault:return nullptr;} }\n"
    out = ROOT / "analysis/native-geometry/phase3/host-input-job-v1"
    out.mkdir(parents=True, exist_ok=True)
    (out / "reference.inc").write_text(output, encoding="utf-8")
    print(f"Pinned original host input graph: {len(selected)} complete blocks; unsupported frontiers trapped")


if __name__ == "__main__":
    main()
