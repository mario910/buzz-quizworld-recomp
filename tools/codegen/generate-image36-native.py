"""Scoped native SIMD for image36's hot 0x06D8 leaf loop.

The pinned original remains the oracle. Only the leaf is built with the
audited RTZ helpers; no native arithmetic flag applies to the whole image.
Output in ignored analysis/native-image36 derives from the user's lift.
The EU 1.00 search adapter is separately runtime-gated and uses integer helpers.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from pathlib import Path
import argparse
import hashlib
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
NAME = "buzz_job_14b4_spu_func_000006D8"
PIN = "7b435dbe9136d2ca0786fea35783f0c0fed1955b11df562496cdd0d8d5bf360c"
DISC_PIN = "f193509244fecb411b85adb97af264278f6eae17c3e991a90c9cd6cd800b1794"
SHARED_BODY_PIN = "84bc11a243a7acdf20fc1b8454e9b66368de9bb39bdb8af3caad1a0ae048242e"
LEAVES = {PIN: "000006D8", DISC_PIN: "000006C8"}


def extract(source, name=NAME, loop_pc="000006D8"):
    match = re.search(rf"(?m)^void {name}\(spu_context\* ctx\) \{{", source)
    if not match:
        raise ValueError("Missing profile-bound image36 leaf")
    end = source.index("\n}", match.end())
    body = source[match.end():end]
    # The lifter appends an unreachable STOP after unconditional SPU_RET.
    tail = ("SPU_RET(ctx);\n"
            "        ctx->stop_code = 0x0u; ctx->status = SPU_STATUS_STOPPED_BY_STOP; spu_stop(ctx); return;")
    if not body.strip().endswith(tail):
        raise ValueError("Unexpected leaf return: re-audit MXCSR scope")
    core = body[:body.index("SPU_RET(ctx);")] + "SPU_RET(ctx);\n"
    if re.search(r"spu_(?:df\w*|fesd|frds|fscr\w*|wrch|rdch|rchcnt|stop|halt|mfc)|spu_drain_call|g_spu_trampoline_fn", core):
        raise ValueError("Leaf acquired double/channel/call effects: RTZ scope unsafe")
    if core.count("SPU_RET(ctx);") != 1 or re.findall(r"goto (\w+)", core) != [f"loc_{loop_pc}"]:
        raise ValueError("Unexpected leaf control flow")
    return match, end, body, core


def bind_search(source):
    """Retarget only entry edges, retaining original loop bodies/PGO identities."""
    prefix = "buzz_job_14b4_spu_func_0000"
    original = prefix + "15BC"
    branch = ("if (ctx->gpr[17]._u32[0] == 0) { ctx->pc = 0x15BC; "
              f"g_spu_trampoline_fn = {original}; return; }}")
    table = f'{{ 0x000015BCu, {original}, "{original}" }}'
    include = '#include "spu_helpers.h"'
    if (source.count(original) != 5 or source.count(branch) != 1 or
            source.count(table) != 1 or source.count(include) != 1):
        raise ValueError("Image36 search entry edges changed; re-audit before binding")
    blocks = []
    for pc in ("15BC", "15A8"):
        matches = list(re.finditer(rf"(?ms)^void {prefix}{pc}\(spu_context\* ctx\) \{{\n.*?^\}}", source))
        if len(matches) != 1:
            raise ValueError("Image36 search oracle block missing or duplicated")
        blocks.append(matches[0].group())
    includes = "".join(line + "\n" for line in source.splitlines() if line.startswith("#include"))
    oracle = includes + "\n".join(blocks) + "\n"
    source = source.replace(branch, branch.replace(original, "buzz_image36_search_15bc"))
    source = source.replace(table, table.replace(original + ",", "buzz_image36_search_15bc,"))
    source = source.replace(include, include + '\n#include "native_image36_search.h"')
    return source, oracle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, default=ROOT, help="Checkout or prepared workspace with the SPU lift")
    workspace = parser.parse_args().workspace
    source_dir = workspace / "port/spu/buzz_job_14b4"
    out = workspace / "analysis/native-image36"
    raw = (source_dir / "spu_recomp.c").read_bytes()
    source_hash = hashlib.sha256(raw).hexdigest()
    if source_hash not in LEAVES:
        raise SystemExit("Image36 lift changed: re-audit before generating")
    pc = LEAVES[source_hash]
    name = "buzz_job_14b4_spu_func_" + pc
    source = raw.decode("utf-8")
    match, end, body, core = extract(source, name, pc)
    if body.count(pc) != body.count("loc_" + pc):
        raise SystemExit("Entry PC became a non-label operand; re-audit the cross-version contract")
    if hashlib.sha256(body.replace("loc_" + pc, "loc_ENTRY").encode()).hexdigest() != SHARED_BODY_PIN:
        raise SystemExit("Leaf no longer matches the shared audited instruction/ABI contract")
    wrapper = f"""
extern int buzz_image36_mode;
extern void buzz_image36_06d8_native(spu_context*);
extern void buzz_image36_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*));
void {name}(spu_context* ctx) {{
    if (!buzz_image36_mode) {{ {name}_reference(ctx); return; }}
    buzz_image36_run(ctx, {name}_reference, buzz_image36_06d8_native);
}}
"""
    lift = (source[:match.start()] + f"void {name}_reference(spu_context* ctx) {{"
            + body + "\n}\n" + wrapper + source[end + 2:])
    search_reference = None
    if source_hash == DISC_PIN:
        lift, search_reference = bind_search(lift)
    includes = "".join(line + "\n" for line in source.splitlines() if line.startswith("#include"))
    exact = "".join(f"#define spu_{op} spu_{op}_ref\n" for op in ("fa", "fs", "fm", "fma", "fms", "fnms"))
    neon = '#ifdef BUZZ_IMAGE36_NEON\n#include "spu_neon_rtz.h"\n#endif\n'
    out.mkdir(parents=True, exist_ok=True)
    for filename, text in (("spu_recomp.c", lift),
                           ("kernel_06d8.c", includes + neon + "\nvoid buzz_image36_06d8_native(spu_context* ctx) {" + core + "}\n"),
                           ("reference_06d8.c", includes + "\nvoid buzz_image36_06d8_reference(spu_context* ctx) {" + core + "}\n"),
                           ("oracle_06d8.c", includes + exact + "\nvoid buzz_image36_06d8_oracle(spu_context* ctx) {" + core + "}\n")):
        (out / filename).write_text(text, encoding="utf-8", newline="\n")
    shutil.copyfile(source_dir / "spu_recomp.h", out / "spu_recomp.h")
    search_headers = ("image36_search.h", "native_image36_search.h", "spu_pure_loop.h")
    if search_reference is not None:
        (out / "reference_search.c").write_text(search_reference, encoding="utf-8", newline="\n")
        for filename in search_headers:
            shutil.copyfile(ROOT / "port" / filename, out / filename)
    else:
        # An older supported FP leaf has no audited search binding. Never leave
        # a stale EU 1.00 search oracle that could appear to validate that lift.
        for filename in (*search_headers, "reference_search.c"):
            (out / filename).unlink(missing_ok=True)
    print(f"Image36: profile-bound leaf 0x{int(pc,16):04X}, shared checked native RTZ -> {out}")


if __name__ == "__main__":
    main()
