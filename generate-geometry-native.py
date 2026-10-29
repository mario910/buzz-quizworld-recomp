"""Promote the three audited geometry loops' register state to native locals.

Reproducible derived output; never edit port/spu. Instructions, FP helpers,
LS accesses, loop predicates and architectural outputs remain unchanged.
"""
from pathlib import Path
import re
import shutil
import argparse
from collections import Counter

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "port/spu/buzz_job_6eba"
options = argparse.ArgumentParser()
options.add_argument("--direct-ls", action="store_true", help="Guard probes once, use private LS directly inside pure kernels")
options.add_argument("--shuffle-plans", action="store_true", help="Decode invariant, reused shuffle masks once per kernel")
args = options.parse_args()
if args.direct_ls and args.shuffle_plans:
    options.error("Keep the LS and shuffle experiments independent")
DEST = ROOT / ("analysis/native-geometry-plans" if args.shuffle_plans else "analysis/native-geometry-direct-ls" if args.direct_ls else "analysis/native-geometry")
PCS = ("63E8", "6E00", "9158")
source = (SOURCE / "spu_recomp.c").read_text()
test_kernels = []
support = r'''
#include <stdatomic.h>
#ifdef _WIN32
#include <malloc.h>
#endif
static int buzz_geometry_mode;
__attribute__((constructor)) static void buzz_geometry_init(void) {
    const char* option = getenv("BUZZ_NATIVE_GEOMETRY");
    buzz_geometry_mode = option ? atoi(option) : 0;
    fprintf(stderr, "[native-geometry] mode=%d (0 reference, 1 native locals, 2 compare)\n", buzz_geometry_mode);
}
typedef void (*BuzzGeometryFn)(spu_context*);
static __attribute__((noinline)) void buzz_geometry_compare(
    spu_context* ctx, BuzzGeometryFn native, BuzzGeometryFn reference, unsigned pc)
{
#ifdef _WIN32
    spu_context* copy = (spu_context*)_aligned_malloc(sizeof(spu_context), 16);
#else
    spu_context* copy = (spu_context*)aligned_alloc(16, sizeof(spu_context));
#endif
    if (!copy) { reference(ctx); return; }
    memcpy(copy, ctx, sizeof(*copy)); copy->ls = copy->ls_store;
    BuzzGeometryFn incoming = g_spu_trampoline_fn;
    native(ctx);
    BuzzGeometryFn outgoing = g_spu_trampoline_fn;
    g_spu_trampoline_fn = incoming;
    reference(copy);
    if (memcmp(ctx->gpr, copy->gpr, sizeof(ctx->gpr)) ||
        memcmp(ctx->ls, copy->ls, SPU_LS_SIZE) || ctx->pc != copy->pc ||
        ctx->status != copy->status || outgoing != g_spu_trampoline_fn) {
        fprintf(stderr, "[native-geometry] MISMATCH block=%04X\n", pc);
        abort();
    }
    g_spu_trampoline_fn = outgoing;
#ifdef _WIN32
    _aligned_free(copy);
#else
    free(copy);
#endif
}
'''
source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
if args.shuffle_plans:
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n#include "../../port/native_geometry_shuffle.h"', 1)
if args.direct_ls:
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n#include "../../port/native_geometry_ls.h"', 1)
for pc in PCS:
    name = f"buzz_job_6eba_spu_func_0000{pc}"
    pattern = rf"(?ms)^void {name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    match = re.search(pattern, source)
    if not match:
        raise ValueError(f"Missing audited block {pc}")
    body = match.group(1)
    fields = set(re.findall(r"ctx->(\w+)", body))
    if fields - {"gpr", "pc"} or re.search(r"spu_(?:wrch|rdch|stop|indirect|drain)", body):
        raise ValueError(f"Unexpected side effect in {pc}: {fields}")
    used = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\]", body)})
    written = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\] =", body)})
    if 0 in written:
        raise ValueError("LS diagnostic helpers observe r0; cannot defer its writes")
    local = re.sub(r"ctx->gpr\[(\d+)\]", r"r\1", body)
    flush = " ".join(f"ctx->gpr[{n}] = r{n};" for n in written)
    local = local.replace("return;", flush + " return;")
    local = local.replace("SPU_RET(ctx);", flush + " SPU_RET(ctx);")
    declarations = "\n".join(f"    u128 r{n} = ctx->gpr[{n}];" for n in used)
    if args.shuffle_plans:
        calls = re.findall(r"spu_shufb\(r\d+, r\d+, r(\d+)\)", local)
        plans = sorted(int(n) for n,count in Counter(calls).items() if count>=2 and int(n) not in written)
        for n in plans:
            local = re.sub(rf"spu_shufb\((r\d+), (r\d+), r{n}\)",rf"buzz_geometry_shuffle_apply(\1, \2, shuffle{n})",local)
            declarations += f"\n    const BuzzGeometryShuffle shuffle{n} = buzz_geometry_shuffle_prepare(r{n});"
        print(f"{pc}: invariant shuffle plans for {plans}")
    if args.direct_ls:
        # Pure kernels cannot change LS ownership or call diagnostics. The
        # wrapper below keeps every probe/external-LS case on the reference.
        local = local.replace("spu_ls_read128(ctx,", "buzz_geometry_ls_read(memory,")
        local = local.replace("spu_ls_write128(ctx,", "buzz_geometry_ls_write(memory,")
        declarations = "    uint8_t* const __restrict memory = ctx->ls_store;\n" + declarations
    kernels = (f"static void {name}_reference(spu_context* ctx) {{{body}}}\n"
        f"static void {name}_native(spu_context* ctx) {{\n{declarations}{local}}}\n")
    test_kernels.append(kernels)
    replacement = kernels + f'''void {name}(spu_context* ctx) {{
    static atomic_uint comparisons;
    if (!buzz_geometry_mode || ctx->ls != ctx->ls_store ||
        g_spu_ls_probe || g_spu_ls_watch_n != 0 || g_spu_smc_watch != 0) {{
        {name}_reference(ctx); return;
    }}
    if (buzz_geometry_mode == 2 && atomic_load(&comparisons) < 256) {{
        unsigned n = atomic_fetch_add(&comparisons, 1);
        buzz_geometry_compare(ctx, {name}_native, {name}_reference, 0x{pc});
        if (n == 255) fprintf(stderr, "[native-geometry] compared 256 full states block={pc} PASS\\n");
    }} else {name}_native(ctx);
}}'''
    source = source[:match.start()] + replacement + source[match.end():]
DEST.mkdir(exist_ok=True)
(DEST / "spu_recomp.c").write_text(source)
(DEST / "test-kernels.inc").write_text(("#include \"../../port/native_geometry_shuffle.h\"\n" if args.shuffle_plans else "#include \"../../port/native_geometry_ls.h\"\n" if args.direct_ls else "") + "\n".join(test_kernels))
shutil.copyfile(SOURCE / "spu_recomp.h", DEST / "spu_recomp.h")
print(f"Generated native register-local kernels {', '.join(PCS)} in {DEST}")
