"""Scoped native SIMD for image36's hot 0x06D8 leaf loop.

The pinned original remains the oracle. Only the leaf is built with the
audited RTZ helpers; no native arithmetic flag applies to the whole image.
Output in ignored analysis/native-image36 derives from the user's lift.
"""
from pathlib import Path
import hashlib
import re
import shutil

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "port/spu/buzz_job_14b4"
OUT = ROOT / "analysis/native-image36"
NAME = "buzz_job_14b4_spu_func_000006D8"
PIN = "7b435dbe9136d2ca0786fea35783f0c0fed1955b11df562496cdd0d8d5bf360c"


def extract(source):
    match = re.search(rf"(?m)^void {NAME}\(spu_context\* ctx\) \{{", source)
    if not match:
        raise ValueError("Missing image36 0x06D8")
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
    if core.count("SPU_RET(ctx);") != 1 or re.findall(r"goto (\w+)", core) != ["loc_000006D8"]:
        raise ValueError("Unexpected leaf control flow")
    return match, end, body, core


def main():
    raw = (SOURCE / "spu_recomp.c").read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise SystemExit("Image36 lift changed: re-audit before generating")
    source = raw.decode("utf-8")
    match, end, body, core = extract(source)
    wrapper = f"""
extern int buzz_image36_mode;
extern void buzz_image36_06d8_native(spu_context*);
extern void buzz_image36_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*));
void {NAME}(spu_context* ctx) {{
    if (!buzz_image36_mode) {{ {NAME}_reference(ctx); return; }}
    buzz_image36_run(ctx, {NAME}_reference, buzz_image36_06d8_native);
}}
"""
    lift = (source[:match.start()] + f"void {NAME}_reference(spu_context* ctx) {{"
            + body + "\n}\n" + wrapper + source[end + 2:])
    includes = "".join(line + "\n" for line in source.splitlines() if line.startswith("#include"))
    exact = "".join(f"#define spu_{op} spu_{op}_ref\n" for op in ("fa", "fs", "fm", "fma", "fms", "fnms"))
    OUT.mkdir(parents=True, exist_ok=True)
    for filename, text in (("spu_recomp.c", lift),
                           ("kernel_06d8.c", includes + "\nvoid buzz_image36_06d8_native(spu_context* ctx) {" + core + "}\n"),
                           ("reference_06d8.c", includes + "\nvoid buzz_image36_06d8_reference(spu_context* ctx) {" + core + "}\n"),
                           ("oracle_06d8.c", includes + exact + "\nvoid buzz_image36_06d8_oracle(spu_context* ctx) {" + core + "}\n")):
        (OUT / filename).write_text(text, encoding="utf-8", newline="\n")
    shutil.copyfile(SOURCE / "spu_recomp.h", OUT / "spu_recomp.h")
    print(f"Image36: pinned leaf + native RTZ 0x06D8 -> {OUT}")


if __name__ == "__main__":
    main()
