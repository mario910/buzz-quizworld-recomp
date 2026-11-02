"""Private complete original index graph for logical host-index comparisons."""
import hashlib
import importlib.util
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PREFIX = "buzz_job_6eba_spu_func_"


def main():
    raw = (ROOT / "port/spu/buzz_job_6eba/spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Unreviewed host index reference source")
    spec = importlib.util.spec_from_file_location("index_audit", ROOT / "tools/inspect/geometry-index-audit.py")
    audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
    audit.source_graph(raw)
    functions = {int(pc,16):body for pc,body in re.findall(
        r"(?ms)^void " + PREFIX + r"([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}", raw.decode())}
    selected, errors = audit.NORMAL, audit.ERRORS
    output = "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*);" for pc in sorted(selected | errors)) + "\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context*) {{ abort(); }}" for pc in sorted(errors)) + "\n"
    output += "\n".join(f"static void {PREFIX}{pc:08X}(spu_context* ctx) {{{audit.reachable_text(functions[pc])}}}" for pc in sorted(selected))
    output += "\nstatic void (*host_index_reference_lookup(unsigned pc))(spu_context*) { switch(pc) {\n"
    output += "\n".join(f"case 0x{pc:x}:return {PREFIX}{pc:08X};" for pc in sorted(selected))
    output += "\ndefault:return nullptr;} }\n"
    out = ROOT / "analysis/native-geometry/phase3/host-index-job-v1"; out.mkdir(parents=True,exist_ok=True)
    (out / "reference.inc").write_text(output,encoding="utf-8")
    print("Pinned complete original index decoder:22 normal blocks;4 error traps;no native/model callbacks")


if __name__ == "__main__":
    main()
