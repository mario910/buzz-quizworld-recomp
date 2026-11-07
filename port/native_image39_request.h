/* EU 1.00 C0BC descriptor requests. Admission/host planning precede all writes;
 * unsupported callers retain the entire 0x3A10 routine, including its frame.
 * Integer SIMD helpers preserve packed guest fields on both x86 and NEON. */
#pragma once
#ifndef BUZZ_IMAGE39_NATIVE_REQUEST
#define BUZZ_IMAGE39_NATIVE_REQUEST 0
#endif

#if BUZZ_IMAGE39_NATIVE_REQUEST && BUZZ_IMAGE39_REQUEST_PROFILE == 100
#include "spu_pure_loop.h"
#include <stdio.h>
#include <stdlib.h>

int buzz_image39_request_mode = 1;
static int buzz_image39_request_stats;
static unsigned long long buzz_image39_request_calls, buzz_image39_request_native;

static void buzz_image39_request_report(void)
{
    if (buzz_image39_request_stats)
        fprintf(stderr, "[image39-request] calls=%llu native=%llu\n",
            __atomic_load_n(&buzz_image39_request_calls, __ATOMIC_RELAXED),
            __atomic_load_n(&buzz_image39_request_native, __ATOMIC_RELAXED));
}

__attribute__((constructor)) static void buzz_image39_request_init(void)
{
    const char* mode = getenv("BUZZ_IMAGE39_REQUEST");
    if (mode) {
        if (mode[0] < '0' || mode[0] > '1' || mode[1]) {
            fprintf(stderr, "[image39-request] BUZZ_IMAGE39_REQUEST must be 0 or 1\n");
            abort();
        }
        buzz_image39_request_mode = mode[0] - '0';
    }
    const char* stats = getenv("BUZZ_IMAGE39_REQUEST_STATS");
    buzz_image39_request_stats = stats && atoi(stats) != 0;
    if (buzz_image39_request_stats) atexit(buzz_image39_request_report);
    fprintf(stderr, "[image39-request] enabled=%d stats=%d profile=eu100\n",
        buzz_image39_request_mode, buzz_image39_request_stats);
}

typedef struct {
    unsigned descriptor, output, table, stack;
    u128 tick, used, selected, record, output_record;
    u128 address, ea, stride, tag, pending;
    int miss;
} buzz_image39_request_plan;

static inline int buzz_image39_request_span(unsigned address, unsigned bytes)
{
    return !(address & 15) && address < SPU_LS_SIZE && bytes <= SPU_LS_SIZE - address;
}

static inline int buzz_image39_request_overlap(unsigned a, unsigned n, unsigned b, unsigned m)
{
    return a < b + m && b < a + n;
}

static int buzz_image39_prepare_request(spu_context* ctx, buzz_image39_request_plan* plan)
{
#if SPU_LS_DEBUG_HOOKS
    (void)ctx; (void)plan;
    return 0;
#else
    if (ctx->image_id != 39 || ctx->ls != ctx->ls_store || !ctx->host_depth ||
        ctx->policy_mode || ctx->resident_ovl || ctx->irq_saved || ctx->irq_frame ||
        ctx->event_mask || ctx->list_stall_mask || ctx->int_enable ||
        !buzz_spu_native_request_allowed(ctx)) return 0;
    // These callers consume r3 and rebuild their volatile arguments afterward.
    switch (ctx->gpr[0]._u32[0]) {
    case 0x34C4: case 0x34F0: case 0x3630: case 0x36F8:
    case 0x37C0: case 0x3950: case 0x396C: case 0x39AC: break;
    default: return 0;
    }
    const unsigned descriptor = ctx->gpr[3]._u32[0];
    const unsigned output = ctx->gpr[6]._u32[0], stack = ctx->gpr[1]._u32[0];
    if (!buzz_image39_request_span(descriptor, 96) || !buzz_image39_request_span(output, 16) ||
        stack < 144 || !buzz_image39_request_span(stack, 32)) return 0;

    const u128 key = ctx->gpr[4], tag = ctx->gpr[5];
    const u128 table = spu_ls_read128(ctx, descriptor + 16);
    const u128 stride = spu_ls_read128(ctx, descriptor + 32);
    const u128 capacity = spu_ls_read128(ctx, descriptor + 48);
    const u128 used = spu_ls_read128(ctx, descriptor + 64);
    // Empty caches retain their original initialization and transfer path.
    if (!used._u32[0] || used._u32[0] > capacity._u32[0] || capacity._u32[0] > 4096) return 0;
    const unsigned table_bytes = capacity._u32[0] * 16;
    if (!buzz_image39_request_span(table._u32[0], table_bytes) ||
        buzz_image39_request_overlap(descriptor, 96, table._u32[0], table_bytes) ||
        buzz_image39_request_overlap(output, 16, descriptor, 96) ||
        buzz_image39_request_overlap(output, 16, table._u32[0], table_bytes) ||
        buzz_image39_request_overlap(descriptor, 96, stack - 144, 176) ||
        buzz_image39_request_overlap(table._u32[0], table_bytes, stack - 144, 176) ||
        buzz_image39_request_overlap(output, 16, stack - 144, 176) ||
        buzz_image39_request_overlap(0x5D90, 16, stack - 144, 176)) return 0;

    const u128 tick = spu_ai(spu_ls_read128(ctx, descriptor + 80), 1);
    u128 oldest = tick, oldest_index = spu_il(-1), found = spu_il(-1);
    for (unsigned i = 0; i < used._u32[0]; ++i) {
        const u128 entry = spu_ls_read128(ctx, table._u32[0] + 16 * i);
        const u128 ordinal = spu_il((int)i), age = spu_shlqbyi(entry, 4);
        const u128 matching = spu_ceq(entry, key), earlier = spu_cgt(oldest, age);
        // Last matching entry wins; equal ages retain the first oldest entry.
        found = spu_selb(found, ordinal, matching);
        oldest = spu_selb(oldest, age, earlier);
        oldest_index = spu_selb(oldest_index, ordinal, earlier);
    }
    const int miss = found._u32[0] == 0xffffffffu;
    const u128 room = spu_cgt(capacity, used);
    const u128 selected = miss ? spu_selb(oldest_index, used, room) : found;
    if (selected._u32[0] >= capacity._u32[0]) return 0;
    // Preserve the original low-halfword multiply, including packed tail lanes.
    const u128 address = spu_a(spu_mpyu(selected, stride), spu_ls_read128(ctx, descriptor));
    const u128 ea = spu_shlqbyi(spu_rotqmbyi(key, -4), 4);
    if (miss && (!stride._u32[0] || stride._u32[0] > 16384 || (stride._u32[0] & 15) ||
        tag._u32[0] > 31 || (ea._u32[0] & 15) ||
        !buzz_image39_request_span(address._u32[0], stride._u32[0]) ||
        buzz_image39_request_overlap(address._u32[0], stride._u32[0], stack - 144, 176))) return 0;

    plan->descriptor = descriptor; plan->output = output; plan->table = table._u32[0]; plan->stack = stack;
    plan->tick = tick; plan->used = spu_selb(used, spu_ai(used, 1), room);
    plan->selected = selected; plan->record = spu_shufb(key, tick, spu_ls_read128(ctx, 0x5D90));
    plan->output_record = spu_shufb(key, address, spu_cwd(key, 4));
    plan->address = address; plan->ea = ea; plan->stride = stride; plan->tag = tag;
    plan->pending = miss ? spu_shl(spu_il(1), tag) : spu_il(0); plan->miss = miss;
    return 1;
#endif
}

static int buzz_image39_native_request(spu_context* ctx)
{
    if (!buzz_image39_request_mode) return 0;
    if (buzz_image39_request_stats) __atomic_fetch_add(&buzz_image39_request_calls, 1, __ATOMIC_RELAXED);
    buzz_image39_request_plan plan;
    if (!buzz_image39_prepare_request(ctx, &plan)) return 0;
    // The frame spills are dead; keep the cache-result slot and all publications.
    spu_ls_write128(ctx, plan.descriptor + 80, plan.tick);
    if (plan.miss) spu_ls_write128(ctx, plan.descriptor + 64, plan.used);
    spu_ls_write128(ctx, plan.stack - 112, plan.selected);
    spu_ls_write128(ctx, plan.table + 16 * plan.selected._u32[0], plan.record);
    spu_ls_write128(ctx, plan.output, plan.output_record);
    if (plan.miss) {
        ctx->pc = 0x3AB0;
        // Private, noninterruptible context: fill argument registers once. The
        // real command path retains validation, DMA observers, coherency/wakes,
        // tag completion and the MFC engine's state. Diagnostics use the lift.
        ctx->mfc_lsa = plan.address._u32[0]; ctx->mfc_eah = 0; ctx->mfc_eal = plan.ea._u32[0];
        ctx->mfc_size = plan.stride._u32[0]; ctx->mfc_tag = plan.tag._u32[0] & 31;
        spu_wrch(ctx, MFC_Cmd, spu_il(64));
    }
    ctx->gpr[3] = plan.pending; ctx->pc = ctx->gpr[0]._u32[0]; g_spu_trampoline_fn = 0;
    if (buzz_image39_request_stats) __atomic_fetch_add(&buzz_image39_request_native, 1, __ATOMIC_RELAXED);
    return 1;
}
#else
static int buzz_image39_native_request(spu_context* ctx) { (void)ctx; return 0; }
#endif
