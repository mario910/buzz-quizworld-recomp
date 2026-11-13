"""Image 39 (C0BC): per-function register-promoted, trampoline-free regions.

The reference lift leaves every basic block through the trampoline (about
9800 SPURS drain steps per image 39 job) and keeps every SPU register in
ctx->gpr. LS stores are byte stores into ctx->ls, which lives inside the
context, so the compiler must also reload registers after every LS store.

This generator rewrites the pinned lift mechanically, one SPU function at a
time. A region is a function entry (a brsl target, or a block nothing
transfers to) plus every block reachable from it by plain transfers; calls
stay calls. Each region becomes one host function in which

* the registers the region touches live in host locals; r0 (the link
  register, observed by LS diagnostics, SPU_RET and the drain) stays in
  ctx->gpr[0];
* an in-region transfer is a goto when no drain hook could observe the step
  (interrupts off, no step observer/lockstep/pm-flow/stack-reset); otherwise
  it leaves through the original trampoline;
* a call keeps the reference host frame and drain bracket; the region's
  registers are stored before it and reloaded after it;
* every exit (trampoline, SPU_RET, stop, halt, end of a function) stores the
  region's registers back first.

The wrapper under each region entry's original name runs the region; every
other block keeps its reference body. SPU instructions, helpers, LS accesses,
channel operations and their order are unchanged. Channel helpers read
ctx->gpr only in env-gated diagnostics, so registers are not stored around
them. Reproducible derived output: never edit port/spu.
Runtime: BUZZ_IMAGE39_FAST=0 keeps the reference lift;
BUZZ_IMAGE39_FAST_SKIP=<hex entry>,... keeps the reference for those regions.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from pathlib import Path
import collections
import hashlib
import re
import os

# BUZZ_FAST_PROMOTE=0 (generator env): keep registers in ctx->gpr; only the
# trampoline hops become gotos.
PROMOTE = os.environ.get("BUZZ_FAST_PROMOTE", "1") != "0"

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_c0bc"
DEST = ROOT / "analysis/fast-image39"
PREFIX = "buzz_job_c0bc_spu_func_"
PINNED = {"c8e0e95c527ac062d019f5385b27fdafba20d08306e32f4977fbdf313ea79611"}  # EU 1.00

raw = (SOURCE / "spu_recomp.c").read_bytes()
if hashlib.sha256(raw).hexdigest() not in PINNED:
    raise SystemExit("Image 39 reference changed: re-audit before generating")
source = raw.decode()

FUNC_RE = re.compile(rf"(?ms)^void ({PREFIX}([0-9A-F]{{8}}))\(spu_context\* ctx\) \{{\n(.*?)^\}}\n")
functions = [(m.group(1), int(m.group(2), 16), m.group(3), m) for m in FUNC_RE.finditer(source)]
if len(functions) != 199:
    raise SystemExit(f"expected 199 lifted functions, found {len(functions)}")
names = {name for name, _, _, _ in functions}
bodies = {pc: body for _, pc, body, _ in functions}

TRANSFER_RE = re.compile(r"\{ ctx->pc = 0x([0-9A-F]+); g_spu_trampoline_fn = (\w+); return; \}")
CALL_RE = re.compile(r"^ctx->gpr\[0\] = spu_link\((0x[0-9A-F]+|\d+)\); \{ int32_t _si = \(int32_t\)ctx->image_id; "
                     r"ctx->host_depth\+\+; (\w+)\(ctx\); spu_drain_call\(ctx, (0x[0-9A-F]+|\d+)\); "
                     r"ctx->host_depth--; spu_img_restore\(ctx, _si\); \}$")
STOP_RE = re.compile(r"^ctx->stop_code = (0x[0-9A-F]+u|\d+u); ctx->status = SPU_STATUS_STOPPED_BY_STOP; "
                     r"spu_stop\(ctx\); return;$")
HALT_TAIL = "ctx->status = SPU_STATUS_STOPPED_BY_HALT; spu_halt(ctx); return; }"
ALLOWED_CTX = {"gpr", "pc", "host_depth", "image_id", "status", "stop_code"}

all_text = "".join(bodies.values())
fields = set(re.findall(r"ctx->(\w+)", all_text))
if fields - ALLOWED_CTX:
    raise SystemExit(f"unexpected context fields: {sorted(fields - ALLOWED_CTX)}")
for helper in re.findall(r"\b(spu_\w+)\(ctx[,)]", all_text):
    if helper not in {"spu_ls_read128", "spu_ls_write128", "spu_rdch", "spu_wrch",
                      "spu_stop", "spu_halt", "spu_drain_call", "spu_img_restore"}:
        raise SystemExit(f"unexpected context helper {helper}")


def target_pc(name):
    return int(name[len(PREFIX):], 16)


# ---- regions ---------------------------------------------------------------
successors = {pc: [target_pc(t) for _, t in TRANSFER_RE.findall(body) if t in names]
              for pc, body in bodies.items()}
callees = collections.Counter(target_pc(c.group(2)) for body in bodies.values()
                              for line in body.split("\n") for c in [CALL_RE.match(line.strip())] if c)
predecessors = collections.Counter(t for s in successors.values() for t in s)
roots = sorted(set(callees) | {pc for pc in bodies if predecessors[pc] == 0})


def region_of(root):
    seen, stack = [root], [root]
    while stack:
        for nxt in successors[stack.pop()]:
            if nxt in bodies and nxt not in seen:
                seen.append(nxt)
                stack.append(nxt)
    return seen


def promote(text):
    if not PROMOTE:
        return text
    text = re.sub(r"ctx->gpr\[(\d+)\]", lambda m: m.group(0) if m.group(1) == "0" else f"r{m.group(1)}", text)
    text = text.replace("spu_ls_read128(ctx, ", "buzz_fast_ls_read(ls, ")
    return text.replace("spu_ls_write128(ctx, ", "buzz_fast_ls_write(ls, ")


counts = collections.Counter()


def emit_region(index, root):
    blocks = region_of(root)
    text = "".join(bodies[b] for b in blocks)
    used = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\]", text)} - {0})
    written = sorted({int(n) for n in re.findall(r"ctx->gpr\[(\d+)\] =", text)} - {0})
    if not PROMOTE:
        used, written = [], []
    flush = " ".join(f"ctx->gpr[{n}] = r{n};" for n in written)
    reload = " ".join(f"r{n} = ctx->gpr[{n}];" for n in used)
    members = set(blocks)

    def transfer(match):
        pc, target = match.group(1), match.group(2)
        if target in names and target_pc(target) in members and int(pc, 16) == target_pc(target) \
                and int(pc, 16) != 0:
            counts["hop"] += 1
            return f"{{ ctx->pc = 0x{pc}; BUZZ_HOP(L_{target_pc(target):08X}, {target}); }}"
        counts["leave"] += 1
        return f"{{ ctx->pc = 0x{pc}; pending = {target}; goto X_transfer; }}"

    lines = []
    for block in blocks:
        tag = f"{block:08X}"
        lines.append(f"L_{tag}:;")
        for line in bodies[block].split("\n"):
            s = line.strip()
            if not s or (s.startswith("/*") and s.endswith("*/;")):
                continue
            s = re.sub(r"\bloc_([0-9A-F]+)\b", rf"L_{tag}_loc_\1", s)
            call = CALL_RE.match(s)
            if call:
                if int(call.group(1), 0) != int(call.group(3), 0) or call.group(2) not in names:
                    raise SystemExit(f"{tag}: unexpected call bracket {s}")
                lines.append(f"ctx->gpr[0] = spu_link({call.group(1)}); BUZZ_FLUSH; "
                             f"{{ int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++; {call.group(2)}(ctx); "
                             f"spu_drain_call(ctx, {call.group(3)}); ctx->host_depth--; spu_img_restore(ctx, _si); }} "
                             f"BUZZ_RELOAD;")
                counts["call"] += 1
                continue
            if "host_depth" in s:
                raise SystemExit(f"{tag}: unrecognised host-frame statement {s}")
            s = TRANSFER_RE.sub(transfer, s)
            if "SPU_RET(ctx);" in s:
                s = s.replace("SPU_RET(ctx);", "goto X_ret;")
                counts["ret"] += 1
            if "spu_stop(ctx)" in s:
                stop = STOP_RE.match(s)
                if not stop:
                    raise SystemExit(f"{tag}: unexpected stop form {s}")
                s = f"ctx->stop_code = {stop.group(1)}; ctx->status = SPU_STATUS_STOPPED_BY_STOP; goto X_stop;"
                counts["stop"] += 1
            if "spu_halt(ctx)" in s:
                if not s.startswith("if (") or not s.endswith(HALT_TAIL):
                    raise SystemExit(f"{tag}: unexpected halt form {s}")
                s = s[:-len(HALT_TAIL)] + "ctx->status = SPU_STATUS_STOPPED_BY_HALT; goto X_halt; }"
                counts["halt"] += 1
            if re.search(r"\breturn;", s) and "BUZZ_HOP" not in s:
                raise SystemExit(f"{tag}: unhandled exit: {s}")
            lines.append(promote(s))
        # Falling off the end of a lifted function is a host return.
        lines.append("goto X_return;")
    declarations = "\n".join(f"    u128 r{n} = ctx->gpr[{n}];" for n in used)
    code = "\n    ".join(lines)
    return len(blocks), len(used), f'''
static void buzz_c0bc_region_{root:05X}(spu_context* ctx)
{{
#define BUZZ_FLUSH do {{ {flush} }} while (0)
#define BUZZ_RELOAD do {{ {reload} }} while (0)
    uint8_t* const ls = ctx->ls;
    void (*pending)(spu_context*) = 0;
{declarations}
    {code}
X_ret:
    BUZZ_FLUSH; SPU_RET(ctx);
X_transfer:
    BUZZ_FLUSH; g_spu_trampoline_fn = pending; return;
X_stop:
    BUZZ_FLUSH; spu_stop(ctx); return;
X_halt:
    BUZZ_FLUSH; {{ extern void spu_halt(spu_context*); spu_halt(ctx); }} return;
X_return:
    BUZZ_FLUSH; return;
#undef BUZZ_FLUSH
#undef BUZZ_RELOAD
}}
'''


region_code = []
summary = []
for index, root in enumerate(roots):
    nblocks, nregs, code = emit_region(index, root)
    region_code.append(code)
    summary.append(f"{root:05X}:{nblocks}b/{nregs}r")

forward = "\n".join(f"static void buzz_c0bc_region_{root:05X}(spu_context* ctx);" for root in roots)
dispatch = "\n".join(f"    case 0x{root:X}u: buzz_c0bc_region_{root:05X}(ctx); return 1;" for root in roots)
fast = f'''/* Generated by tools/codegen/generate-image39-fast.py -- do not edit. */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern void (*g_spu_step_observer)(spu_context*, uint32_t);
extern volatile int g_yz_lockstep_on;
extern volatile unsigned g_spu_stack_reset_n;
extern void* volatile g_pm_flow_ctx;
int spu_fast_lift_allowed(void);

/* Same bytes and masking as spu_ls_read128/spu_ls_write128 with the debug
 * probes disarmed (admission requires that). */
static inline u128 buzz_fast_ls_read(const uint8_t* ls, uint32_t lsa)
{{
    u128 v; uint32_t w[4];
    memcpy(w, ls + (lsa & (SPU_LS_MASK & ~0xFu)), 16);
    v._u32[0] = SPU_BSWAP32(w[0]); v._u32[1] = SPU_BSWAP32(w[1]);
    v._u32[2] = SPU_BSWAP32(w[2]); v._u32[3] = SPU_BSWAP32(w[3]);
    return v;
}}
static inline void buzz_fast_ls_write(uint8_t* ls, uint32_t lsa, u128 val)
{{
    uint32_t w[4] = {{ SPU_BSWAP32(val._u32[0]), SPU_BSWAP32(val._u32[1]),
                       SPU_BSWAP32(val._u32[2]), SPU_BSWAP32(val._u32[3]) }};
    memcpy(ls + (lsa & (SPU_LS_MASK & ~0xFu)), w, 16);
}}

/* A drain step whose hooks would all be no-ops, and no interrupt to take. */
static inline int buzz_fast_hop_ok(const spu_context* ctx)
{{
    return !ctx->int_enable && !g_spu_step_observer && g_yz_lockstep_on == 0 &&
           !g_spu_stack_reset_n && g_pm_flow_ctx != (const void*)ctx;
}}
#define BUZZ_HOP(label, fn) do {{ \\
        if (__builtin_expect(buzz_fast_hop_ok(ctx), 1)) {{ ctx->steps++; goto label; }} \\
        pending = fn; goto X_transfer; }} while (0)

{forward}

static int buzz_c0bc_mode = -1;
static uint32_t buzz_c0bc_skip[16];
static unsigned buzz_c0bc_nskip;

static void buzz_c0bc_init(void)
{{
    const char* e = getenv("BUZZ_IMAGE39_FAST");
    int mode = e ? atoi(e) : 1;
    /* The LS debug probes act only when one of these is set. */
    if (getenv("SPU_LS_WATCH") || getenv("SPU_LS_LOWREAD") || getenv("SPU_SMC_WATCH")) mode = 0;
    const char* s = getenv("BUZZ_IMAGE39_FAST_SKIP");
    while (s && *s && buzz_c0bc_nskip < 16) {{
        char* end; unsigned long v = strtoul(s, &end, 16);
        if (end == s) break;
        buzz_c0bc_skip[buzz_c0bc_nskip++] = (uint32_t)v;
        s = *end == ',' ? end + 1 : end;
    }}
    fprintf(stderr, "[image39-fast] mode=%d regions={len(roots)} skipped=%u\\n", mode, buzz_c0bc_nskip);
    __atomic_store_n(&buzz_c0bc_mode, mode, __ATOMIC_RELEASE);
}}

/* Returns 1 when the region at `entry` ran, 0 when the caller must run the
 * reference body. */
int buzz_c0bc_fast(spu_context* ctx, uint32_t entry)
{{
    int mode = __atomic_load_n(&buzz_c0bc_mode, __ATOMIC_ACQUIRE);
    if (mode < 0) {{ buzz_c0bc_init(); mode = buzz_c0bc_mode; }}
    if (!mode || (ctx->image_id == 2 && ctx->policy_mode) || !spu_fast_lift_allowed() ||
        !buzz_fast_hop_ok(ctx))
        return 0;
    for (unsigned i = 0; i < buzz_c0bc_nskip; ++i)
        if (buzz_c0bc_skip[i] == entry) return 0;
    switch (entry) {{
{dispatch}
    default: return 0;
    }}
}}
{"".join(region_code)}'''

# Reference TU: region entries get a wrapper under their original name (the
# function table and every trampoline/call keep using it); every other block
# is unchanged.
out = []
last = 0
for name, pc, body, m in functions:
    if pc not in roots:
        continue
    out.append(source[last:m.start()])
    out.append(f"static void {name}_reference(spu_context* ctx) {{\n{body}}}\n"
               f"void {name}(spu_context* ctx) {{\n"
               f"    if (!buzz_c0bc_fast(ctx, 0x{pc:X}u)) {name}_reference(ctx);\n}}\n")
    last = m.end()
out.append(source[last:])
reference = "".join(out)
anchor = '#include "spu_helpers.h"'
reference = reference.replace(anchor, anchor + "\nint buzz_c0bc_fast(spu_context* ctx, uint32_t entry);\n", 1)

DEST.mkdir(parents=True, exist_ok=True)
(DEST / "spu_recomp.c").write_text(reference)
(DEST / "fast.c").write_text(fast)
(DEST / "spu_recomp.h").write_text((SOURCE / "spu_recomp.h").read_text())
print(f"image39 fast: {len(roots)} regions {dict(counts)}")
print("regions " + " ".join(summary))
