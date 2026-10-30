/* 0988/09E0: build staged-list pointers, retaining the original final entry. */
#pragma once
static inline int buzz_pointers_native(spu_context* ctx, void (*last_entry)(spu_context*))
{
    if (ctx->ls != ctx->ls_store || buzz_geometry_debug || g_spu_ls_probe > 0 ||
        g_spu_ls_watch_n > 0 || g_spu_smc_watch > 0 ||
        (ctx->int_enable && (ctx->event_status & ctx->event_mask)) ||
        !buzz_pointers_constants_match(ctx)) return 0;
    const unsigned remaining = ctx->gpr[15]._u32[0] - ctx->gpr[13]._u32[0];
    const unsigned source = ctx->gpr[10]._u32[0], destination = ctx->gpr[12]._u32[0];
    const uint64_t input_first = (uint64_t)source + 48, input_end = input_first + remaining*8ull;
    const uint64_t output_end = (uint64_t)destination + remaining*4ull;
    if (remaining < 2 || remaining > 128 || ((source | destination) & 3) ||
        source < 0xe680 || destination < 0xe500 || input_end > SPU_LS_SIZE ||
        output_end > SPU_LS_SIZE || (destination < input_end && input_first < output_end)) return 0;
    uint32_t pointer = ctx->gpr[11]._u32[0];
    for (unsigned i = 0; i < remaining - 1; ++i) {
        uint32_t size, address;
        memcpy(&size, ctx->ls + source + 48 + i*8, 4);
        memcpy(&address, ctx->ls + source + 52 + i*8, 4);
        size = SPU_BSWAP32(size) & 0x7fff; address = SPU_BSWAP32(address);
        const uint32_t output = SPU_BSWAP32(pointer | (address & 15));
        memcpy(ctx->ls + destination + i*4, &output, 4);
        pointer += (size + 15) & ~15u; // Preserve the SPU's 32-bit word wrap.
    }
    ctx->gpr[11]._u32[0] = pointer;
    for (unsigned j = 0; j < 4; ++j) {
        ctx->gpr[10]._u32[j] += (remaining - 1)*8;
        ctx->gpr[12]._u32[j] += (remaining - 1)*4;
        ctx->gpr[13]._u32[j] += remaining - 1;
    }
    last_entry(ctx); return 1;
}
