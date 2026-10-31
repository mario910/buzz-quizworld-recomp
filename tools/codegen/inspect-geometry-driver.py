"""Inventory the pinned image-41 driver; keep derived evidence private."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "analysis/native-geometry/phase3/driver-audit.json")
    args = parser.parse_args()
    raw = SOURCE.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != PIN:
        raise SystemExit("Unreviewed image-41 lift; audit before changing the pin")
    functions = dict(re.findall(
        r"(?ms)^void buzz_job_6eba_spu_func_([0-9A-F]+)\(spu_context\* ctx\) \{(.*?)^\}",
        raw.decode()))
    blocks = {}
    for name, body in functions.items():
        pc = int(name, 16)
        if not 0x1C18 <= pc < 0x2718:
            continue
        calls = re.findall(r"host_depth\+\+; buzz_job_6eba_spu_func_([0-9A-F]+)", body)
        branches = re.findall(r"ctx->pc = (0x[0-9A-F]+)", body)
        blocks[f"0x{pc:04X}"] = dict(
            body_sha256=hashlib.sha256(body.encode()).hexdigest(),
            direct_calls=[f"0x{int(call, 16):04X}" for call in calls],
            direct_branches=branches,
            return_through_r0="SPU_RET(ctx)" in body,
            indirect_dispatch="SPU_IB_DISPATCH" in body,
            context_fields=sorted(set(re.findall(r"ctx->(\w+)", body))))
    if "0x1C18" not in blocks or "0x22A0" not in blocks:
        raise SystemExit("Pinned driver boundaries not found")
    callees = sorted({call for block in blocks.values() for call in block["direct_calls"]})
    result = dict(source_sha256=digest, workload_entry="0x0010", driver_entry="0x1C18",
                  driver_end_exclusive="0x2718", driver_blocks=len(blocks),
                  direct_callees=callees, blocks=blocks,
                  limits=["Static direct edges only; computed calls/returns need runtime evidence.",
                          "An inventory is not a native implementation or correctness proof.",
                          "Job DMA/atomics require shadow I/O, not two real executions."])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Pinned image 41: driver 1C18..2718, {len(blocks)} blocks, "
          f"{len(callees)} distinct direct callees; evidence -> {args.output}")


if __name__ == "__main__":
    main()
