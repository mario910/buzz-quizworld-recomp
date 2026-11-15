"""Fail-closed extraction checks using synthetic code, no game assets needed."""
import importlib.util
from pathlib import Path
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("image36_generator", root / "tools/codegen/generate-image36-native.py")
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

prefix = "buzz_job_14b4_spu_func_0000"
original = prefix + "15BC"
branch = ("if (ctx->gpr[17]._u32[0] == 0) { ctx->pc = 0x15BC; "
          f"g_spu_trampoline_fn = {original}; return; }}")
table = f'{{ 0x000015BCu, {original}, "{original}" }}'
search = (f'#include "spu_helpers.h"\nvoid {prefix}1560(spu_context* ctx) {{\n'
          f'    {branch}\n}}\nvoid {prefix}15A8(spu_context* ctx) {{\n'
          f'    g_spu_trampoline_fn = {original};\n}}\n'
          f'void {original}(spu_context* ctx) {{\n    ctx->pc = 0x15A8;\n}}\n'
          + table + '\n')
bound, oracle = generator.bind_search(search)
assert bound.count(original) == 3  # Body, original back edge and table label.
assert f'void {original}(spu_context* ctx)' in bound
assert f'g_spu_trampoline_fn = {original};' in bound
assert 'native_image36_search.h' in bound and 'native_image36_search.h' not in oracle
assert prefix + '1560' not in oracle and table not in oracle
for bad in (search + branch, search.replace(table, ''),
            search.replace('15A8(spu_context*', '15A9(spu_context*'),
            search.replace('#include "spu_helpers.h"', '')):
    try:
        generator.bind_search(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('changed search binding accepted')
print("PASS image36 search binding: original loop/PGO names retained, changed edges rejected")
