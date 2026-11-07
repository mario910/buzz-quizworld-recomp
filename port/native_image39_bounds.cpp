#include "spu_context.h"
#include "spu_helpers.h"
#include "spu_pure_loop.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#if defined(__aarch64__)
#define BUZZ_NEON_RTZ_NO_REMAP
#include "spu_neon_rtz.h"
#undef BUZZ_NEON_RTZ_NO_REMAP
#include "spu_arm_fp_state.h"
struct BoundsFpScope
{
    buzz_arm_fp_state state;
    BoundsFpScope() : state(buzz_arm_fp_save())
    {
        buzz_arm_fp_enter_rtz(state);
    }
    ~BoundsFpScope()
    {
        buzz_arm_fp_restore(state);
    }
};
static inline u128 bounds_fa(u128 a, u128 b)
{
    return buzz_neon_add(a, b, 0);
}
static inline u128 bounds_fs(u128 a, u128 b)
{
    return buzz_neon_add(a, b, 0x80000000u);
}
static inline u128 bounds_fm(u128 a, u128 b)
{
    return buzz_neon_mul(a, b);
}
static inline u128 bounds_fma(u128 a, u128 b, u128 c)
{
    return buzz_neon_fused(a, b, c, 0, 0);
}
#else
#define SPU_RTZ_RESULT_GUARD 1
#include "spu_native_rtz.h"
struct BoundsFpScope
{
    unsigned state;
    BoundsFpScope() : state(_mm_getcsr())
    {
        _mm_setcsr((state & ~0x6000u) | 0x6000u | 0x8040u | 0x1f80u);
    }
    ~BoundsFpScope()
    {
        _mm_setcsr(state);
    }
};
static inline u128 bounds_fa(u128 a, u128 b)
{
    return spu_native_rtz_fa(a, b, 0);
}
static inline u128 bounds_fs(u128 a, u128 b)
{
    return spu_native_rtz_fa(a, b, 0x80000000u);
}
static inline u128 bounds_fm(u128 a, u128 b)
{
    return spu_native_rtz_fm(a, b);
}
static inline u128 bounds_fma(u128 a, u128 b, u128 c)
{
    return spu_native_rtz_fma(a, b, c, 0, 0);
}
#endif

extern "C"
{
    int buzz_image39_bounds_mode = 1;
}
namespace
{
struct BoundsInit
{
    BoundsInit()
    {
        if (const char *value = getenv("BUZZ_IMAGE39_BOUNDS"))
        {
            if ((value[0] != '0' && value[0] != '1') || value[1])
            {
                fprintf(stderr, "[image39-bounds] BUZZ_IMAGE39_BOUNDS must be 0 or 1\n");
                std::abort();
            }
            buzz_image39_bounds_mode = value[0] - '0';
        }
        fprintf(stderr, "[image39-bounds] enabled=%d profile=eu100 planes=4+1 max-items=4096\n",
                buzz_image39_bounds_mode);
    }
} bounds_init;
} // namespace

// Experimental EU 1.00 image39 0x05C0/0x0750 object-bounds driver.
// Only the pinned caller's live ABI is published: complete r3, unchanged
// r0/r1/r80..127, full LS and return PC. Scratch registers are not emulated.
// Direct object-bounds test: four planes per SIMD group, no SPU scratch bank.
struct BoundsPlanes
{
    u128 x, y, z, d;
    unsigned lanes;
};
static u128 bounds_splat(unsigned bits)
{
    u128 v;
    for (auto &w : v._u32)
        w = bits;
    return v;
}
static u128 bounds_abs(u128 v)
{
    for (auto &w : v._u32)
        w &= 0x7fffffffu;
    return v;
}
static u128 bounds_dot(const BoundsPlanes &p, u128 v)
{
    return bounds_fma(
        bounds_splat(v._u32[2]), p.z,
        bounds_fma(bounds_splat(v._u32[1]), p.y, bounds_fm(bounds_splat(v._u32[0]), p.x)));
}
static bool bounds_span(unsigned at, unsigned bytes, unsigned alignment = 16)
{
    return !(at & (alignment - 1)) && at < SPU_LS_SIZE && bytes <= SPU_LS_SIZE - at;
}
static bool bounds_overlap(unsigned a, unsigned n, unsigned b, unsigned m)
{
    return a < b + m && b < a + n;
}
static bool bounds_constants(const spu_context *ctx)
{
    static const unsigned addresses[] = {0x5c30, 0x5c40, 0x5c80, 0x5d00,
                                         0x5d10, 0x5d20, 0x5d40, 0x5d50};
    static const unsigned char expected[][16] = {
        {8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11},
        {0, 1, 2, 3, 16, 17, 18, 19, 128, 128, 128, 128, 128, 128, 128, 128},
        {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
        {12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15},
        {8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31},
        {4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7},
        {0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23},
        {0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 128, 128, 128, 128}};
    for (unsigned i = 0; i < 8; ++i)
        if (memcmp(ctx->ls + addresses[i], expected[i], 16))
            return false;
    return true;
}
extern "C" int buzz_image39_bounds(spu_context *ctx)
{
#if SPU_LS_DEBUG_HOOKS
    (void)ctx;
    return 0;
#else
    if (!buzz_image39_bounds_mode || !ctx || ctx->image_id != 39 || ctx->ls != ctx->ls_store ||
        !ctx->host_depth || ctx->policy_mode || ctx->resident_ovl || ctx->irq_saved ||
        ctx->irq_frame || ctx->event_mask || ctx->list_stall_mask || ctx->int_enable ||
        ctx->gpr[0]._u32[0] != 0x2178 || !buzz_spu_native_request_allowed(ctx))
        return 0;
    const unsigned count = ctx->gpr[3]._u32[0], table_bounds = ctx->gpr[5]._u32[0],
                   table_matrix = ctx->gpr[6]._u32[0];
    const unsigned output = ctx->gpr[13]._u32[0], output_begin = output & ~15u;
    if (!count || count > 4096 || !bounds_span(output, count * 4, 4) ||
        !bounds_span(table_bounds, count * 16) || !bounds_span(table_matrix, count * 16) ||
        !bounds_constants(ctx))
        return 0;
    const unsigned output_bytes = ((output + count * 4 + 15) & ~15u) - output_begin;
    if (bounds_overlap(output_begin, output_bytes, table_bounds, count * 16) ||
        bounds_overlap(output_begin, output_bytes, table_matrix, count * 16) ||
        bounds_overlap(output_begin, output_bytes, 0x5c30, 0x130))
        return 0;
    // Preflight before mutation: omitted prefetches and reordered pure arithmetic
    // are valid only while output cannot change any input or shuffle constant.
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned b = spu_ls_read128(ctx, table_bounds + i * 16)._u32[0];
        const unsigned m = spu_ls_read128(ctx, table_matrix + i * 16)._u32[0];
        if (!bounds_span(b, 32) || !bounds_span(m, 64) ||
            bounds_overlap(output_begin, output_bytes, b, 32) ||
            bounds_overlap(output_begin, output_bytes, m, 64))
            return 0;
    }
    BoundsPlanes planes[2]{};
    planes[0].lanes = 4;
    planes[1].lanes = 1;
    for (unsigned i = 0; i < 5; ++i)
    {
        BoundsPlanes &p = planes[i / 4];
        const unsigned lane = i % 4;
        p.x._u32[lane] = ctx->gpr[7 + i]._u32[0];
        p.y._u32[lane] = ctx->gpr[7 + i]._u32[1];
        p.z._u32[lane] = ctx->gpr[7 + i]._u32[2];
        p.d._u32[lane] = ctx->gpr[7 + i]._u32[3];
    }
    BoundsFpScope fp;
    unsigned kept = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned b = spu_ls_read128(ctx, table_bounds + i * 16)._u32[0];
        const unsigned m = spu_ls_read128(ctx, table_matrix + i * 16)._u32[0];
        const u128 center = spu_ls_read128(ctx, b),
                   extent = bounds_fm(spu_ls_read128(ctx, b + 16), ctx->gpr[12]);
        u128 column[4];
        for (unsigned row = 0; row < 4; ++row)
        {
            const u128 v = spu_ls_read128(ctx, m + row * 16);
            for (unsigned c = 0; c < 4; ++c)
                column[c]._u32[row] = v._u32[c];
        }
        const u128 world =
            bounds_fma(column[2], bounds_splat(center._u32[2]),
                       bounds_fma(column[1], bounds_splat(center._u32[1]),
                                  bounds_fma(column[0], bounds_splat(center._u32[0]), column[3])));
        // The original emits the candidate even when it is rejected. Rejected
        // candidates overwrite the current slot; that final tail word is observable.
        const unsigned at = output + kept * 4;
        u128 slot = spu_ls_read128(ctx, at);
        slot._u32[(at & 15) / 4] = i;
        spu_ls_write128(ctx, at, slot);
        bool outside = false;
        for (const BoundsPlanes &p : planes)
        {
            const u128 ex =
                bounds_abs(bounds_fm(bounds_dot(p, column[0]), bounds_splat(extent._u32[0])));
            const u128 ey =
                bounds_abs(bounds_fm(bounds_dot(p, column[1]), bounds_splat(extent._u32[1])));
            const u128 ez =
                bounds_abs(bounds_fm(bounds_dot(p, column[2]), bounds_splat(extent._u32[2])));
            const u128 radius = bounds_fa(ez, bounds_fa(ex, ey));
            const u128 distance =
                bounds_fma(bounds_splat(world._u32[2]), p.z,
                           bounds_fma(bounds_splat(world._u32[1]), p.y,
                                      bounds_fma(bounds_splat(world._u32[0]), p.x, p.d)));
            const u128 rejected = spu_fcgt(bounds_fs(spu_zero(), radius), distance);
            for (unsigned lane = 0; lane < p.lanes; ++lane)
                outside |= rejected._u32[lane] != 0;
            if (outside)
                break;
        }
        kept += !outside;
    }
    ctx->gpr[3] = bounds_splat(count);
    ctx->gpr[3]._u32[0] = kept;
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = nullptr;
    return 1;
#endif
}
