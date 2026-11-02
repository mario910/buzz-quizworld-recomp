"""Pinned original attribute, skin and complete clip producer graph; no data models."""
import hashlib
import importlib.util
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main(output_directory="host-cull-job-v1"):
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed host cull oracle source")
    functions = {int(pc,16):body for pc,body in re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected = {pc for pc in functions if 0x7df8 <= pc < 0x93f0 or 0xac68 <= pc <= 0xada0 or 0xaf18 <= pc < 0xc750 or 0x9f8 <= pc < 0xbf8 or 0x6188 <= pc < 0x6608}
    selected.update((0x93f0,0x9408,0x9498,0x9518,0x9580,0x95dc,0x9668,0x9700,0x97a8,
        0x2738,0x2750,0x2760,0x2778,0x2780,0x2790,0x27a0,0x2c00,0x2cf8,0x415c,0x4164,0x416c))
    pending = [0x40c8,0xc750,0xc810,0xcac0]
    while pending:
        pc = pending.pop()
        if pc in selected or pc == 0xcc70:
            continue
        selected.add(pc); lines = []
        for line in functions[pc].splitlines():
            lines.append(line)
            if line == "        SPU_RET(ctx);" or re.match(r"^        \{ ctx->pc = .* return; \}$", line):
                break
        pending.extend(int(target,16) for target in re.findall(r"g_spu_trampoline_fn = " + PREFIX + r"([0-9A-F]+)", "\n".join(lines)))
    def audit_module(filename,name):
        spec=importlib.util.spec_from_file_location(name,ROOT/'tools/inspect'/filename)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.source_graph(raw);return module
    index=audit_module('geometry-index-audit.py','index_audit')
    transform=audit_module('geometry-transform-audit.py','transform_audit')
    selected.update(index.NORMAL | transform.NORMAL)
    for pc in index.NORMAL | transform.NORMAL:
        functions[pc]=index.reachable_text(functions[pc])
    for pc in (0x6e08,0x6ee8):
        functions[pc] = f"observe_original_cull(ctx,0x{pc:x});\n" + functions[pc]
    refs = {int(pc,16) for start in selected for pc in re.findall(PREFIX + r"([0-9A-F]+)", functions[start])}
    stubs = refs - selected
    if len(selected) != 191 or stubs != {0x7d54,0x7d7c,0x7da4,0x7dcc,0xbf8,0x27e8,0x2b68,0x4178,0xabc8,0xadd4,0xadfc,0xae24,0xae4c,0xae74,0xcc70}:
        raise SystemExit(f"Unreviewed cull graph: {len(selected)} blocks; stubs {sorted(stubs)}")
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | stubs))
    output += "\n" + "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{ abort(); }}" for pc in sorted(stubs)) + "\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{functions[pc]}}}" for pc in sorted(selected))
    output += "\nstatic void (*host_cull_reference_lookup(unsigned pc))(spu_context*) { switch(pc) {\n"
    output += "\n".join(f"case 0x{pc:x}:return {PREFIX}{pc:08X};" for pc in sorted(selected))
    output += "\ndefault:return nullptr;} }\n"
    out = ROOT / "analysis/native-geometry/phase3" / output_directory; out.mkdir(parents=True,exist_ok=True)
    (out / "reference.inc").write_text(output,encoding="utf-8")
    print(f"Pinned original host cull graph: {len(selected)} complete blocks; no native/model data callbacks")


if __name__ == "__main__":
    main()
