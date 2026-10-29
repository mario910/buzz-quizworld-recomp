"""Fuse the hot geometry loops of image 41 into single host loops.

In the reference lift every iteration of these loops leaves its function
through the trampoline (one SPU_DRAIN step per iteration, ~540 per job) and
round-trips every SPU register through ctx->gpr. Here a loop body K whose
back edge goes to a short pre-block H (which only falls through to K) becomes

    load registers into locals
    top: K body; if (back edge) { H body; goto top; }
    store written registers; leave exactly as the reference does

The SPU instructions, helpers, LS accesses and every exit are unchanged; only
the host control flow and register storage differ. Reproducible derived
output: never edit port/spu. BUZZ_FUSED_GEOMETRY=0 reference (default),
1 fused, 2 fused + full-state comparison against the reference.
"""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "port/spu/buzz_job_6eba"
DEST = ROOT / "analysis/fused-geometry"
PREFIX = "buzz_job_6eba_spu_func_0000"
# (loop body, pre-block that falls through into it)
LOOPS = (("6E00", "6DF8"), ("9158", "9008"))
options = argparse.ArgumentParser()
options.add_argument("--with-63e8", action="store_true",
                     help="also keep the self-looping 63E8 kernel in locals")
args = options.parse_args()

source = (SOURCE / "spu_recomp.c").read_text()


def body_of(pc):
    match = re.search(rf"(?ms)^void {PREFIX}{pc}\(spu_context\* ctx\) \{{(.*?)^\}}", source)
    if not match:
        raise ValueError(f"missing block {pc}")
    body = match.group(1)
    fields = set(re.findall(r"ctx->(\w+)", body))
    if fields - {"gpr", "pc"} or re.search(r"spu_(?:wrch|rdch|stop|indirect|drain)\b", body):
        raise ValueError(f"unexpected side effect in {pc}: {fields}")
    return match, body


def tail_jump(pc, target):
    return (f"{{ ctx->pc = 0x{target}; g_spu_trampoline_fn = {PREFIX}{target}; return; }}")


support = r'''
#include <stdatomic.h>
#ifdef _WIN32
#include <malloc.h>
#endif
static int buzz_fused_mode;
__attribute__((constructor)) static void buzz_fused_init(void) {
    const char* option = getenv("BUZZ_FUSED_GEOMETRY");
    buzz_fused_mode = option ? atoi(option) : 0;
    fprintf(stderr, "[fused-geometry] mode=%d (0 reference, 1 fused, 2 compare)\n", buzz_fused_mode);
}
typedef void (*BuzzFusedFn)(spu_context*);
static __attribute__((noinline)) void buzz_fused_compare(
    spu_context* ctx, BuzzFusedFn fused, BuzzFusedFn reference, unsigned pc)
{
#ifdef _WIN32
    spu_context* copy = (spu_context*)_aligned_malloc(sizeof(spu_context), 16);
#else
    spu_context* copy = (spu_context*)aligned_alloc(16, sizeof(spu_context));
#endif
    if (!copy) { reference(ctx); return; }
    memcpy(copy, ctx, sizeof(*copy)); copy->ls = copy->ls_store;
    BuzzFusedFn incoming = g_spu_trampoline_fn;
    fused(ctx);
    BuzzFusedFn outgoing = g_spu_trampoline_fn;
    /* The reference runs its iterations one trampoline step at a time. */
    g_spu_trampoline_fn = incoming;
    reference(copy);
    while (g_spu_trampoline_fn == buzz_fused_back_edge(pc)) {
        BuzzFusedFn step = g_spu_trampoline_fn; g_spu_trampoline_fn = 0; step(copy);
        step = g_spu_trampoline_fn; g_spu_trampoline_fn = 0; step(copy);
    }
    if (memcmp(ctx->gpr, copy->gpr, sizeof(ctx->gpr)) ||
        memcmp(ctx->ls, copy->ls, SPU_LS_SIZE) || ctx->pc != copy->pc ||
        ctx->status != copy->status || outgoing != g_spu_trampoline_fn) {
        fprintf(stderr, "[fused-geometry] MISMATCH block=%04X\n", pc);
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

kernels_out = []
back_edges = []
replacements = []
for body_pc, head_pc in LOOPS:
    kmatch, kbody = body_of(body_pc)
    _, hbody = body_of(head_pc)
    head_exit = tail_jump(head_pc, body_pc)
    if head_exit not in hbody or "goto" in hbody or "if (" in hbody:
        raise ValueError(f"{head_pc} is not a plain fall-through into {body_pc}")
    head_statements = hbody.replace(head_exit, "").strip()
    back = re.search(rf"if \((.*?)\) {re.escape(tail_jump(body_pc, head_pc))}", kbody)
    if not back:
        raise ValueError(f"{body_pc} has no back edge to {head_pc}")
    if kbody.count(f"g_spu_trampoline_fn = {PREFIX}{head_pc};") != 1:
        raise ValueError(f"{body_pc} reaches {head_pc} more than once")
    back_edges.append((body_pc, head_pc))
    combined = kbody + "\n" + head_statements
    used = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\]", combined)})
    written = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\] =", combined)})
    if 0 in written:
        raise ValueError("LS diagnostic helpers observe r0; cannot defer its writes")
    flush = " ".join(f"ctx->gpr[{n}] = r{n};" for n in written)
    # Interrupts are sampled by the drain between steps; keep that boundary
    # whenever one could be taken, by leaving through the original back edge.
    loop_back = (f"if ({back.group(1)}) {{ if (ctx->int_enable) "
                 f"{{ FLUSH ctx->pc = 0x{head_pc}; g_spu_trampoline_fn = {PREFIX}{head_pc}; return; }} "
                 f"HEAD goto fused_top; }}")
    fused = kbody.replace(back.group(0), loop_back)
    fused = re.sub(r"ctx->gpr\[(\d+)\]", r"r\1", fused)
    head_local = re.sub(r"ctx->gpr\[(\d+)\]", r"r\1", head_statements)
    fused = fused.replace("HEAD ", head_local + " ").replace("FLUSH ", flush + " ")
    fused = fused.replace("return;", "FLUSHRET").replace("SPU_RET(ctx);", "FLUSHSPURET")
    fused = fused.replace("FLUSHRET", flush + " return;").replace("FLUSHSPURET", flush + " SPU_RET(ctx);")
    declarations = "\n".join(f"    u128 r{n} = ctx->gpr[{n}];" for n in used)
    name = f"{PREFIX}{body_pc}"
    kernels_out.append(f"static void {name}_reference(spu_context* ctx) {{{kbody}}}\n"
                       f"static void {name}_fused(spu_context* ctx) {{\n{declarations}\nfused_top:;{fused}}}\n")
    replacements.append((kmatch, name, body_pc))

if args.with_63e8:
    raise SystemExit("--with-63e8 is reserved for a later experiment")

edge_cases = "\n".join(f"    if (pc == 0x{b}) return {PREFIX}{h};" for b, h in back_edges)
support = support.replace("typedef void (*BuzzFusedFn)(spu_context*);",
    "typedef void (*BuzzFusedFn)(spu_context*);\n" +
    "\n".join(f"void {PREFIX}{h}(spu_context* ctx);" for _, h in back_edges) +
    "\nstatic BuzzFusedFn buzz_fused_back_edge(unsigned pc) {\n" + edge_cases + "\n    return 0;\n}")
for (kmatch, name, body_pc), kernel in sorted(zip(replacements, kernels_out),
                                             key=lambda item: -item[0][0].start()):
    wrapper = kernel + f'''void {name}(spu_context* ctx) {{
    {{ static atomic_int seen; if (!atomic_exchange(&seen, 1))
        fprintf(stderr, "[fused-geometry] first call {body_pc} mode=%d int_enable=%d\\n",
                buzz_fused_mode, (int)ctx->int_enable); }}
    if (!buzz_fused_mode || ctx->int_enable) {{ {name}_reference(ctx); return; }}
    if (buzz_fused_mode == 2) buzz_fused_compare(ctx, {name}_fused, {name}_reference, 0x{body_pc});
    else {name}_fused(ctx);
}}'''
    source = source[:kmatch.start()] + wrapper + source[kmatch.end():]
source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
DEST.mkdir(parents=True, exist_ok=True)
(DEST / "spu_recomp.c").write_text(source)
(DEST / "spu_recomp.h").write_text((SOURCE / "spu_recomp.h").read_text())
print(f"Generated fused loops {', '.join(b for b, _ in LOOPS)} in {DEST}")
