/* 8-lane AVX2 forms of the SPU single-precision operations stage F uses.
 *
 * Same contract as spu_native_rtz.h: the job runs with MXCSR RZ+FTZ+DAZ
 * (image 41 is SPU_NATIVE_RTZ_IMAGE), so in-range hardware results ARE the
 * SPU results. A lane whose result is not a finite value below MAX_FINITE
 * (overflow, SPU extended-range operands) is redone with the exact scalar
 * lane helper, as rtz_fix does for the 4-lane helpers. -0 becomes +0 like
 * rtz_finish. Integer-only operations (frest table, fi step, compares) follow
 * the reference formulas in spu_helpers.h exactly.
 *
 * Vectors hold host-order 32-bit words: lane k == SPU word k of a quadword,
 * lanes 4..7 == the next quadword (the next group of four).
 */
#include <immintrin.h>

typedef __m256i v8;

static inline v8 v8_set(uint32_t x) { return _mm256_set1_epi32((int)x); }
static inline v8 v8_pair(u128 lo, u128 hi)
{
    __m128i a, b; memcpy(&a, &lo, 16); memcpy(&b, &hi, 16);
    return _mm256_set_m128i(b, a);
}
static inline v8 v8_bcast(u128 v) { return v8_pair(v, v); }

/* exact repair of the lanes the hardware result does not cover */
static __attribute__((noinline, cold)) v8 v8_fix(v8 out, int valid, v8 a, v8 b, v8 c, int op)
{
    uint32_t o[8], x[8], y[8], z[8];
    _mm256_storeu_si256((__m256i*)o, out); _mm256_storeu_si256((__m256i*)x, a);
    _mm256_storeu_si256((__m256i*)y, b); _mm256_storeu_si256((__m256i*)z, c);
    for (int n = 0; n < 8; n++) if (!(valid & (1 << n)))
        o[n] = op == 0 ? spu__fa_lane(x[n], y[n]) :
               op == 1 ? spu__fm_lane(x[n], y[n]) : spu__fma_lane(x[n], y[n], z[n], 0, 0);
    return _mm256_loadu_si256((const __m256i*)o);
}
static inline v8 v8_finish(__m256 value, v8 a, v8 b, v8 c, int op)
{
    v8 bits = _mm256_castps_si256(value);
    const v8 mag = _mm256_and_si256(bits, v8_set(0x7fffffff));
    const v8 valid = _mm256_cmpgt_epi32(v8_set(0x7f7fffff), mag);
    bits = _mm256_andnot_si256(_mm256_cmpeq_epi32(_mm256_slli_epi32(bits, 1), _mm256_setzero_si256()), bits);
    const int mask = _mm256_movemask_ps(_mm256_castsi256_ps(valid));
    return mask == 0xFF ? bits : v8_fix(bits, mask, a, b, c, op);
}
static inline v8 v8_fs(v8 a, v8 b)
{
    const v8 nb = _mm256_xor_si256(b, v8_set(0x80000000u));
    return v8_finish(_mm256_add_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(nb)), a, nb, a, 0);
}
static inline v8 v8_fm(v8 a, v8 b)
{
    return v8_finish(_mm256_mul_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)), a, b, a, 1);
}
static inline v8 v8_fma(v8 a, v8 b, v8 c)
{
    return v8_finish(_mm256_fmadd_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b), _mm256_castsi256_ps(c)),
                     a, b, c, 2);
}
/* fnms(a, b, c) = c - a*b, as the 4-lane helper: fma(-a, b, c) */
static inline v8 v8_fnms(v8 a, v8 b, v8 c) { return v8_fma(_mm256_xor_si256(a, v8_set(0x80000000u)), b, c); }

/* fnms on lane MASKS: the cull feeds fnms(back, r83, clip) with operands that
 * are 0 or 0xFFFFFFFF (an SPU extended-range -MAX, never a valid hardware
 * result), so the generic form repaired every lane. For that domain there are
 * eight cases; their exact results come from the same exact lane helper, once,
 * and a permute selects them. Anything else takes the generic path. */
static inline v8 v8_is_mask(v8 x)
{
    return _mm256_or_si256(_mm256_cmpeq_epi32(x, _mm256_setzero_si256()), _mm256_cmpeq_epi32(x, v8_set(-1)));
}
static uint32_t v8_fnms_mask_table[8];
__attribute__((constructor)) static void v8_fnms_mask_init(void)   /* before any SPU thread */
{
    for (int i = 0; i < 8; i++)
        v8_fnms_mask_table[i] = spu__fma_lane((i & 4 ? 0xFFFFFFFFu : 0u) ^ 0x80000000u, i & 2 ? 0xFFFFFFFFu : 0u,
                                              i & 1 ? 0xFFFFFFFFu : 0u, 0, 0);
}
static inline v8 v8_fnms_masks(v8 a, v8 b, v8 c)
{
    const v8 ok = _mm256_and_si256(_mm256_and_si256(v8_is_mask(a), v8_is_mask(b)), v8_is_mask(c));
    if (_mm256_movemask_ps(_mm256_castsi256_ps(ok)) != 0xFF) return v8_fnms(a, b, c);
    const v8 table = _mm256_loadu_si256((const __m256i*)v8_fnms_mask_table);
    const v8 idx = _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(a, v8_set(4)), _mm256_and_si256(b, v8_set(2))),
                                   _mm256_and_si256(c, v8_set(1)));
    return _mm256_permutevar8x32_epi32(table, idx);
}

/* frest: biased exponent 253-e (0 for e >= 253, 255 for e == 0), table fraction */
static inline v8 v8_frest(v8 a)
{
    const v8 e = _mm256_and_si256(_mm256_srli_epi32(a, 23), v8_set(0xFF));
    v8 rexp = _mm256_sub_epi32(v8_set(253), e);
    rexp = _mm256_andnot_si256(_mm256_cmpgt_epi32(e, v8_set(252)), rexp);          /* e >= 253 -> 0 */
    rexp = _mm256_blendv_epi8(rexp, v8_set(255), _mm256_cmpeq_epi32(e, _mm256_setzero_si256()));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(a, 18), v8_set(0x1F));
    const v8 frac = _mm256_and_si256(_mm256_i32gather_epi32((const int*)spu_frest_fraction_lut, idx, 4),
                                     v8_set(0x7FFFFF));
    return _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(a, v8_set(0x80000000u)), _mm256_slli_epi32(rexp, 23)),
                           frac);
}
/* frsqest: biased exponent 190-(e+1)/2 (255 for e == 0), table fraction, no sign */
static inline v8 v8_frsqest(v8 a)
{
    const v8 e = _mm256_and_si256(_mm256_srli_epi32(a, 23), v8_set(0xFF));
    v8 rexp = _mm256_sub_epi32(v8_set(190), _mm256_srli_epi32(_mm256_add_epi32(e, v8_set(1)), 1));
    rexp = _mm256_blendv_epi8(rexp, v8_set(255), _mm256_cmpeq_epi32(e, _mm256_setzero_si256()));
    const v8 idx = _mm256_and_si256(_mm256_srli_epi32(a, 18), v8_set(0x3F));
    const v8 frac = _mm256_and_si256(_mm256_i32gather_epi32((const int*)spu_frsqest_fraction_lut, idx, 4),
                                     v8_set(0x7FFFFF));
    return _mm256_or_si256(_mm256_slli_epi32(_mm256_and_si256(rexp, v8_set(0xFF)), 23), frac);
}
/* fi: the SSE4.1 formula of spu_helpers.h, eight lanes */
static inline v8 v8_fi(v8 a, v8 b)
{
    const v8 y = _mm256_and_si256(a, v8_set(0x7ffff));
    const v8 step = _mm256_and_si256(b, v8_set(0x3ff));
    const v8 base = _mm256_and_si256(_mm256_srli_epi32(b, 10), v8_set(0x1fff));
    const v8 low = _mm256_slli_epi32(base, 19);
    const v8 product = _mm256_mullo_epi32(step, y);
    const v8 bias = v8_set(0x80000000u);
    const v8 borrow = _mm256_cmpgt_epi32(_mm256_xor_si256(product, bias), _mm256_xor_si256(low, bias));
    const v8 difference = _mm256_sub_epi32(low, product);
    v8 fraction = _mm256_blendv_epi8(_mm256_srli_epi32(difference, 9), _mm256_srli_epi32(difference, 8), borrow);
    fraction = _mm256_and_si256(fraction, v8_set(0x7fffff));
    v8 exponent = _mm256_and_si256(_mm256_srli_epi32(b, 23), v8_set(255));
    exponent = _mm256_add_epi32(exponent, borrow);
    v8 value = _mm256_or_si256(_mm256_and_si256(b, bias), _mm256_or_si256(_mm256_slli_epi32(exponent, 23), fraction));
    return _mm256_and_si256(value, _mm256_cmpgt_epi32(exponent, _mm256_setzero_si256()));
}
/* cflts(a, 170): a * 2^3 truncated, saturating; extended-range lanes exact */
static __attribute__((noinline, cold)) v8 v8_cfl_fix(v8 out, int valid, v8 a)
{
    uint32_t o[8], x[8];
    _mm256_storeu_si256((__m256i*)o, out); _mm256_storeu_si256((__m256i*)x, a);
    for (int n = 0; n < 8; n++) if (!(valid & (1 << n))) o[n] = spu__cfl_lane(x[n], 3, 0);
    return _mm256_loadu_si256((const __m256i*)o);
}
static inline v8 v8_cflts3(v8 a)
{
    const v8 e = _mm256_and_si256(_mm256_srli_epi32(a, 23), v8_set(0xFF));
    const v8 good = _mm256_cmpgt_epi32(v8_set(255), e);
    const __m256 value = _mm256_mul_ps(_mm256_castsi256_ps(a), _mm256_set1_ps(8.0f));
    v8 result = _mm256_cvttps_epi32(value);
    const v8 overflow = _mm256_castps_si256(_mm256_cmp_ps(value, _mm256_set1_ps(2147483648.0f), _CMP_GE_OQ));
    result = _mm256_or_si256(_mm256_andnot_si256(overflow, result), _mm256_and_si256(overflow, v8_set(0x7fffffff)));
    const int mask = _mm256_movemask_ps(_mm256_castsi256_ps(good));
    return mask == 0xFF ? result : v8_cfl_fix(result, mask, a);
}
/* compares on the SPU's sign-magnitude keys (denormals are zero) */
static inline v8 v8_mag(v8 x)
{
    const v8 denorm = _mm256_cmpeq_epi32(_mm256_and_si256(x, v8_set(0x7F800000)), _mm256_setzero_si256());
    return _mm256_andnot_si256(denorm, _mm256_and_si256(x, v8_set(0x7FFFFFFF)));
}
static inline v8 v8_key(v8 x)
{
    const v8 m = v8_mag(x);
    const v8 neg = _mm256_srai_epi32(x, 31);
    return _mm256_sub_epi32(_mm256_xor_si256(m, neg), neg);
}
static inline v8 v8_fcgt(v8 a, v8 b) { return _mm256_cmpgt_epi32(v8_key(a), v8_key(b)); }
static inline v8 v8_fcmeq(v8 a, v8 b) { return _mm256_cmpeq_epi32(v8_mag(a), v8_mag(b)); }
static inline v8 v8_clgt(v8 a, v8 b)
{
    const v8 bias = v8_set(0x80000000u);
    return _mm256_cmpgt_epi32(_mm256_xor_si256(a, bias), _mm256_xor_si256(b, bias));
}
static inline v8 v8_nonzero(v8 a) { return _mm256_xor_si256(_mm256_cmpeq_epi32(a, _mm256_setzero_si256()), v8_set(-1)); }

/* big-endian LS quadword pair <-> host words */
static inline v8 v8_bswap(v8 v)
{
    const v8 swap = _mm256_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12,
                                     3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    return _mm256_shuffle_epi8(v, swap);
}
static inline v8 v8_ld2(const uint8_t* ls, uint32_t a, uint32_t b)
{
    const __m128i lo = _mm_loadu_si128((const __m128i*)(ls + (a & 0x3FFF0)));
    const __m128i hi = _mm_loadu_si128((const __m128i*)(ls + (b & 0x3FFF0)));
    return v8_bswap(_mm256_set_m128i(hi, lo));
}
static inline void v8_st2(uint8_t* ls, uint32_t a, uint32_t b, v8 v)
{
    v = v8_bswap(v);
    _mm_storeu_si128((__m128i*)(ls + (a & 0x3FFF0)), _mm256_castsi256_si128(v));
    _mm_storeu_si128((__m128i*)(ls + (b & 0x3FFF0)), _mm256_extracti128_si256(v, 1));
}
