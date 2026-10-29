"""Fail-closed extraction checks using synthetic code, no game assets needed."""
import importlib.util
from pathlib import Path
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("image36_generator", root / "generate-image36-native.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
tail = ("        SPU_RET(ctx);\n"
        "        ctx->stop_code = 0x0u; ctx->status = SPU_STATUS_STOPPED_BY_STOP; spu_stop(ctx); return;\n}")
body = (f"void {generator.NAME}(spu_context* ctx) {{\nloc_000006D8:\n"
        "        ctx->gpr[2] = spu_ai(ctx->gpr[2], -1);\n"
        "        if (ctx->gpr[2]._u32[0] != 0) goto loc_000006D8;\n")
assert "spu_stop" not in generator.extract(body + tail)[3]
for bad in ("spu_wrch(ctx, 1, ctx->gpr[2]);", "spu_rdch(ctx, 1);", "spu_dfma(x,y,z);",
            "spu_drain_call(ctx, 0);", "g_spu_trampoline_fn = other;", "goto another;"):
    try:
        generator.extract(body + bad + "\n" + tail)
    except ValueError:
        pass
    else:
        raise AssertionError(bad)
try:
    generator.extract(body + "SPU_RET(ctx);\n}")
except ValueError:
    pass
else:
    raise AssertionError("unrecognized return trailer")
print("PASS image36 generator: channels/double/calls/extra exits rejected")
