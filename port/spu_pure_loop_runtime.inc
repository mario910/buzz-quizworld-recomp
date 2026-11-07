/* Included after the SDK drain hooks; the SDK's private history remains the
 * single source of truth. No SPU state/layout changes or duplicate TLS bank. */
int buzz_spu_native_request_allowed(const spu_context* ctx)
{
    static int diagnostics = -1;
    int active = __atomic_load_n(&diagnostics, __ATOMIC_RELAXED);
    if (active < 0) {
        active = getenv("SPU_PCHIST") || getenv("SPU_CHHIST") ? 1 : 0;
        __atomic_store_n(&diagnostics, active, __ATOMIC_RELAXED);
    }
    return !active && !g_spu_step_observer && !g_yz_lockstep_on && !g_spu_stack_reset_n &&
        g_pm_flow_ctx != (const void*)ctx && !spu_launch_check_slow();
}

unsigned buzz_spu_pure_loop_budget(const spu_context* ctx, uint32_t back_pc, uint32_t body_pc)
{
    static int diagnostics = -1;
    int diagnostic_flags = __atomic_load_n(&diagnostics, __ATOMIC_RELAXED);
    if (diagnostic_flags < 0) {
        diagnostic_flags = getenv("SPU_PCHIST") || getenv("BUZZ_SPU_PURE_LOOPS_OFF") ? 1 : 0;
        __atomic_store_n(&diagnostics, diagnostic_flags, __ATOMIC_RELAXED);
    }
    const uint32_t link = ctx->gpr[0]._u32[0] & SPU_LS_MASK;
    const uint32_t ret = ctx->drain_ret_pc & SPU_LS_MASK;
    if (diagnostic_flags || (ctx->int_enable && ctx->event_mask) || g_spu_step_observer || g_yz_lockstep_on ||
        g_spu_stack_reset_n || g_pm_flow_ctx == (const void*)ctx || spu_launch_check_slow() ||
        !back_pc || !body_pc || back_pc == link || body_pc == link || back_pc == ret || body_pc == ret)
        return 0;
    return 256;
}

void buzz_spu_pure_loop_hops(spu_context* ctx, uint32_t back_pc, uint32_t body_pc, unsigned pairs)
{
    const unsigned count = pairs * 2;
    if (!count) return;
    static int announced;
    if (!__atomic_load_n(&announced, __ATOMIC_RELAXED) &&
        !__atomic_exchange_n(&announced, 1, __ATOMIC_RELAXED))
        fprintf(stderr, "[spu-pure-loop] bounded loop active (%05X -> %05X, %u internal transfers)\n",
                back_pc, body_pc, count);
    if (!spu_launch_no_ls0()) ctx->steps += count;
    // Only the final ring contents are observable after a pure, bounded loop.
    // Preserve unsigned counter wrap and chronological order exactly.
    for (unsigned i = count > 32 ? count - 32 : 0; i < count; ++i)
        t_recent[(t_recent_n + i) & 31] = (i & 1) ? body_pc : back_pc;
    t_recent_n += count;
    for (unsigned i = count > 8 ? count - 8 : 0; i < count; ++i)
        g_spu_pch[(g_spu_pch_n + i) & 7] = (i & 1) ? body_pc : back_pc;
    g_spu_pch_n += count;
}
