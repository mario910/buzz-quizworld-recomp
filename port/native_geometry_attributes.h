/* Host attribute orchestration and the complete built-in float3 stream.
 * Descriptor codecs remain explicit original callees until converted. */
#pragma once
#include "native_geometry_commands.h"
#if defined(__aarch64__)
#include "geometry_neon_vectors.h"
#else
#include <immintrin.h>
#endif

namespace buzz {
struct GeometryRawStream {
    uint32_t context = 0, stack = 0, source = 0, vertices = 0, slot = 0, destination = 0, groups = 0;
};
inline bool read_geometry_raw_stream(const spu_context& ctx, GeometryRawStream& output)
{
    GeometryRawStream p;
    p.context = ctx.gpr[3]._u32[0]; p.stack = ctx.gpr[1]._u32[0]; p.source = ctx.gpr[4]._u32[0];
    if (ctx.ls != ctx.ls_store || p.context < 0xe680 || !geometry_output_range(p.context, 0x170) ||
        (p.stack & 15) || p.stack < 0xe840 || p.stack > SPU_LS_SIZE - 32 ||
        geometry_output_overlap(p.context, 0x170, p.stack - 0x1c0, 0x1e0)) return false;
    p.vertices = geometry_job_be16(ctx.ls + p.context + 8);
    if (!p.vertices) { output = p; return true; }
    if ((ctx.gpr[6]._u32[0] & 255) != 0 || geometry_job_be32(ctx.ls + 0x95c4) != 0x13c) return false;
    static constexpr uint8_t selectors[3][16] = {
        {4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19},
        {8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23},
        {12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27}};
    for (unsigned i = 0; i < 3; ++i) if (memcmp(ctx.ls + 0xd250 + i * 16, selectors[i], 16)) return false;
    for (; p.slot < 16 && ctx.ls[p.context + 0x160 + p.slot]; ++p.slot) {}
    if (p.slot == 16) return false;
    p.destination = geometry_job_be32(ctx.ls + p.context + 0x10 + p.slot * 4);
    p.groups = (p.vertices + 3) / 4;
    // The last iteration loads another complete group before its final store.
    if (p.source < 0xe680 || !geometry_output_range(p.source, (p.groups + 1) * 48) ||
        p.destination < 0xe680 || !geometry_output_range(p.destination, p.groups * 64)) return false;
    const uint32_t at[] = {p.context, p.stack - 0x1c0, p.source, p.destination};
    const uint32_t bytes[] = {0x170, 0x1e0, (p.groups + 1) * 48, p.groups * 64};
    for (unsigned a = 0; a < 4; ++a) for (unsigned b = a + 1; b < 4; ++b)
        if (geometry_output_overlap(at[a], bytes[a], at[b], bytes[b])) return false;
    output = p; return true;
}
inline void decode_geometry_raw_stream(spu_context& ctx, const GeometryRawStream& p)
{
    auto& r = ctx.gpr;
    r[12] = r[8]; r[2] = geometry_output_quad(ctx, p.context); r[13] = r[7];
    spu_ls_write128(&ctx, p.stack - 0x1a0, r[126]); r[9] = spu_ila(0x9408); r[126] = spu_link(0x9408);
    for (unsigned reg = 80; reg <= 104; ++reg) spu_ls_write128(&ctx, p.stack - (reg - 79) * 16, r[reg]);
    spu_ls_write128(&ctx, p.stack + 16, r[0]); spu_ls_write128(&ctx, p.stack - 0x1c0, r[1]);
    r[34] = r[5]; r[82] = r[3]; r[84] = r[4]; r[126] = spu_sf(r[9], r[126]); r[8] = spu_rotqbyi(r[2], 6);
    r[1] = spu_ai(r[1], -448);
    if (p.vertices) {
        r[10] = spu_andi(r[6], 255); r[28] = geometry_output_quad(ctx, 0xd1b0); r[21] = spu_ila(0x95c4);
        r[27] = geometry_output_quad(ctx, 0xd1c0); r[18] = spu_shli(r[10], 2); r[49] = spu_il(0);
        r[15] = spu_a(r[21], r[126]); r[38] = spu_fsmbi(0xf); r[11] = spu_ila(65535);
        r[87] = spu_fsmbi(0xff); r[65] = spu_ilh(0x303); r[63] = spu_fsmbi(0xf0f);
        r[17] = spu_a(r[18], r[15]); r[16] = geometry_output_quad(ctx, (r[15]._u32[0] + r[18]._u32[0]) & ~15u);
        r[11] = spu_and(r[8], r[11]); r[80] = spu_shufb(r[49], r[65], r[65]);
        r[25] = spu_ilhu(0x3800); r[20] = spu_ai(r[11], 1); r[24] = spu_ilhu(0x3780); r[19] = spu_andi(r[20], -2);
        r[72] = spu_ilhu(0x4635); r[14] = spu_rotqby(r[16], r[17]); r[36] = spu_shli(r[19], 4); r[71] = spu_ilhu(0xc635);
        r[6] = spu_ila(0xddf0); r[9] = spu_a(r[14], r[15]); r[7] = spu_clgti(r[10], 5);
        r[25] = spu_ori(r[25], 128); r[24] = spu_ori(r[24], 128); r[72] = spu_iohl(r[72], 0x2e7c); r[71] = spu_iohl(r[71], 0x3150);
        r[42] = spu_sfi(r[36], 0); r[8] = spu_a(r[6], r[126]); r[35] = spu_ai(r[3], 92); r[37] = spu_ai(r[3], 96);
        r[61] = spu_ai(r[3], 100); r[39] = spu_ai(r[3], 104); r[83] = spu_ai(r[3], 16);
        // Reserve the first free float4 table and publish its position pointer.
        r[30] = spu_il(1); r[31] = geometry_output_quad(ctx, p.context + 0x160); r[36] = spu_ceqb(r[80], r[31]);
        r[35] = spu_gbb(r[36]); r[34] = spu_clz(r[35]); r[33] = spu_ai(r[34], -16); r[80] = spu_shli(r[33], 2);
        r[32] = spu_cbd(r[33], 0); r[81] = spu_a(r[83], r[80]); r[22] = spu_shufb(r[30], r[31], r[32]);
        ctx.ls[p.context + 0x160 + p.slot] = 1;
        r[10] = geometry_output_quad(ctx, (p.context + 0x10 + p.slot * 4) & ~15u); r[4] = spu_rotqby(r[10], r[81]);
        r[0] = spu_link(0x9740); ++ctx.host_depth;
        r[5] = spu_cwd(r[1], 12); r[2] = geometry_command_word(ctx, p.context + 0x5c, p.destination, &r[6]);
        ctx.pc = r[0]._u32[0]; spu_drain_call(&ctx, 0x9740); --ctx.host_depth; spu_img_restore(&ctx, ctx.image_id);
        r[27] = spu_ila(65535); r[28] = geometry_output_quad(ctx, p.context); r[21] = spu_ilhu(0x3f80);
        r[14] = spu_ai(r[84], 48); r[23] = geometry_output_quad(ctx, (p.context + 0x10 + p.slot * 4) & ~15u);
        r[26] = spu_rotqbyi(r[28], 6); r[18] = geometry_output_quad(ctx, 0xd250);
        r[4] = spu_rotqby(r[23], r[81]); r[17] = geometry_output_quad(ctx, 0xd260); r[15] = geometry_output_quad(ctx, 0xd270);
        r[13] = spu_and(r[26], r[27]); r[22] = spu_fsmbi(0xf); r[12] = spu_ai(r[13], 3); r[5] = spu_andi(r[12], -4);
        r[3] = spu_shli(r[5], 4); r[8] = spu_a(r[3], r[4]); r[9] = spu_sfi(r[3], 0);
        r[13] = spu_ai(r[8], 16); r[12] = spu_ai(r[8], 32); r[11] = spu_ai(r[8], 48);
        // Raw bits are copied without host floating-point conversion. NaNs,
        // signed zeros and padding keep their original representation.
        const __m128i one_be = _mm_set1_epi32(0x0000803f);
        for (unsigned group = 0; group < p.groups; ++group) {
            const uint8_t* source = ctx.ls + p.source + group * 48;
            uint8_t* destination = ctx.ls + p.destination + group * 64;
            const __m128i a = _mm_loadu_si128((const __m128i*)source);
            const __m128i b = _mm_loadu_si128((const __m128i*)(source + 16));
            const __m128i c = _mm_loadu_si128((const __m128i*)(source + 32));
            _mm_storeu_si128((__m128i*)destination, _mm_blend_epi32(a, one_be, 8));
            _mm_storeu_si128((__m128i*)(destination + 16), _mm_blend_epi32(_mm_alignr_epi8(b,a,12), one_be, 8));
            _mm_storeu_si128((__m128i*)(destination + 32), _mm_blend_epi32(_mm_alignr_epi8(c,b,8), one_be, 8));
            _mm_storeu_si128((__m128i*)(destination + 48), _mm_blend_epi32(_mm_srli_si128(c,4), one_be, 8));
        }
        const uint32_t last = p.source + (p.groups - 1) * 48;
        const u128 a = geometry_output_quad(ctx, last), b = geometry_output_quad(ctx, last + 16), c = geometry_output_quad(ctx, last + 32);
        r[45] = spu_selb(a, r[21], r[22]); r[47] = spu_shufb(a, b, r[15]); r[43] = spu_shufb(b, c, r[17]);
        r[40] = spu_shufb(c, r[21], r[18]); r[44] = spu_selb(r[47], r[21], r[22]); r[41] = spu_selb(r[43], r[21], r[22]);
        r[20] = geometry_output_quad(ctx, p.source + p.groups * 48);
        r[16] = geometry_output_quad(ctx, p.source + p.groups * 48 + 16); r[10] = geometry_output_quad(ctx, p.source + p.groups * 48 + 32);
        r[38] = spu_ai(r[9], p.groups * 64); r[9] = r[38]; r[14] = spu_ai(r[14], p.groups * 48);
    }
    r[1] = spu_ai(r[1], 448); r[0] = spu_ls_read128(&ctx, p.stack + 16);
    for (unsigned reg = 80; reg <= 104; ++reg) r[reg] = spu_ls_read128(&ctx, p.stack - (reg - 79) * 16);
    r[126] = spu_ls_read128(&ctx, p.stack - 0x1a0);
}

struct GeometryAttributes {
    uint32_t context = 0, stack = 0;
};
inline bool read_geometry_attributes(const spu_context& ctx, GeometryAttributes& output)
{
    GeometryAttributes p;
    p.context = ctx.gpr[3]._u32[0]; p.stack = ctx.gpr[1]._u32[0];
    if (ctx.ls != ctx.ls_store || p.context < 0xe680 || !geometry_output_range(p.context, 0x190) ||
        (p.stack & 15) || p.stack < 0x80 || p.stack > SPU_LS_SIZE - 32 ||
        geometry_output_overlap(p.context, 0x190, p.stack - 0x80, 0xa0)) return false;
    for (unsigned reg = 4; reg <= 7; ++reg) if (ctx.gpr[reg]._u32[0] & 15) return false;
    if ((ctx.ls[p.context] & 16) && ctx.gpr[6]._u32[0]) return false; // Original assertion path.
    if ((ctx.ls[p.context] & 16) && geometry_job_be32(ctx.ls + p.context + 0x88)) return false;
    const unsigned formats[] = {ctx.ls[p.context + 2], ctx.ls[p.context + 3]};
    const uint32_t handlers[] = {geometry_job_be32(ctx.ls + p.context + 0x7c), geometry_job_be32(ctx.ls + p.context + 0x80)};
    for (unsigned i = 0; i < 2; ++i) if ((!i || ctx.gpr[6]._u32[0]) && formats[i] >= 6 && handlers[i]) return false;
    output = p; return true;
}
inline void decode_geometry_attributes(spu_context& ctx, const GeometryAttributes& p,
                                       const buzz_geometry_attribute_functions& functions)
{
    auto& r = ctx.gpr;
    r[2] = spu_andi(r[4], 15); spu_ls_write128(&ctx, p.stack - 0x60, r[126]);
    r[8] = spu_ila(0xac78); r[126] = spu_link(0xac78);
    for (unsigned reg = 80; reg <= 84; ++reg) spu_ls_write128(&ctx, p.stack - (reg - 79) * 16, r[reg]);
    spu_ls_write128(&ctx, p.stack + 16, r[0]); spu_ls_write128(&ctx, p.stack - 0x80, r[1]);
    r[80] = r[3]; r[81] = r[6]; r[126] = spu_sf(r[8], r[126]); r[82] = r[4]; r[83] = r[5]; r[84] = r[7];
    r[1] = spu_ai(r[1], -128); r[5] = spu_andi(r[81], 15); r[9] = spu_andi(r[83], 15); r[12] = spu_andi(r[84], 15);
    r[5] = r[83]; r[3] = r[80]; r[18] = geometry_output_quad(ctx, p.context + 0x50); r[4] = r[82];
    r[20] = geometry_output_quad(ctx, p.context); r[17] = geometry_output_quad(ctx, p.context + 0x60); r[16] = geometry_output_quad(ctx, p.context + 0x70);
    r[19] = spu_cwd(r[1], 4); r[6] = spu_rotqbyi(r[20], 15); r[7] = spu_rotqbyi(r[17], 12); r[8] = spu_rotqbyi(r[16], 12);
    r[15] = geometry_command_word(ctx, p.context + 0x54, r[82]._u32[0]);
    geometry_command_call(ctx, functions.stream, 0xad0c);
    if (r[81]._u32[0]) {
        r[5] = r[84]; r[6] = geometry_output_quad(ctx, p.context); r[3] = r[80];
        r[7] = geometry_output_quad(ctx, p.context + 0x70); r[4] = r[81]; r[8] = geometry_output_quad(ctx, p.context + 0x80);
        geometry_command_call(ctx, functions.stream, 0xad34);
    }
    r[23] = geometry_output_quad(ctx, p.context); r[22] = spu_rotqbyi(r[23], 13); r[21] = spu_andi(r[22], 16);
    if (!r[21]._u32[0]) {
        r[39] = spu_clgt(r[82], r[81]); r[3] = r[80]; r[37] = spu_ceqi(r[81], 0);
        r[36] = spu_selb(r[82], r[81], r[39]); r[4] = spu_selb(r[36], r[82], r[37]);
    } else {
        r[3] = r[80]; r[35] = geometry_output_quad(ctx, p.context); r[34] = spu_rotqbyi(r[35], 1); r[4] = spu_andi(r[34], 255);
        geometry_command_call(ctx, functions.stride, 0xada0);
        r[33] = geometry_output_quad(ctx, p.context); r[31] = r[3]; r[3] = r[80]; r[32] = spu_rotqbyi(r[33], 6);
        r[30] = spu_mpyu(r[31], r[32]); r[29] = spu_mpyh(r[31], r[32]); r[28] = spu_a(r[29], r[30]);
        r[27] = spu_ai(r[28], 15); r[26] = spu_andi(r[27], -16); r[4] = spu_a(r[82], r[26]);
    }
    r[1] = spu_ai(r[1], 128); r[126] = spu_ls_read128(&ctx, p.stack - 0x60); r[0] = spu_ls_read128(&ctx, p.stack + 16);
    for (unsigned reg = 80; reg <= 84; ++reg) r[reg] = spu_ls_read128(&ctx, p.stack - (reg - 79) * 16);
    r[5] = spu_cwd(r[1], 8); r[2] = geometry_command_word(ctx, p.context + 0x58, r[4]._u32[0], &r[6]);
}
} // namespace buzz
