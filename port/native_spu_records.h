#pragma once
#include <immintrin.h>

/* These helpers operate on owned LS with observation disabled. Each raw load
 * is captured at its original position, including when stores alias the source. */
static inline __m128i record_quad(const spu_context* ctx, unsigned address)
{
    return _mm_loadu_si128((const __m128i*)(ctx->ls + (address & (SPU_LS_MASK & ~15u))));
}
static inline u128 record_vector(__m128i bytes)
{
    u128 result;
    memcpy(&result, &bytes, sizeof result);
    return result;
}
#if defined(__SSE4_1__)
/* A CWD/CWX selector inserts source word zero into a single destination word. */
static inline u128 record_insert_word(u128 a, u128 b, unsigned word)
{
    __m128 src, dst;
    memcpy(&src, &a, sizeof src);
    memcpy(&dst, &b, sizeof dst);
    switch (word) {
    case 0: dst = _mm_insert_ps(dst, src, 0x00); break;
    case 1: dst = _mm_insert_ps(dst, src, 0x10); break;
    case 2: dst = _mm_insert_ps(dst, src, 0x20); break;
    default: dst = _mm_insert_ps(dst, src, 0x30); break;
    }
    return record_vector(_mm_castps_si128(dst));
}
#endif
static inline __m128i record_word_order(void)
{
    return _mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
}
static inline u128 record_rotated_quad(__m128i raw, unsigned shift)
{
    const __m128i index = _mm_add_epi8(record_word_order(), _mm_set1_epi8((char)(shift & 15)));
    return record_vector(_mm_shuffle_epi8(raw, index));
}

/* A radix pass repeatedly selects bytes from one quad. Bit 4 of a normal
 * selector is immaterial with identical sources. High selectors produce a
 * constant zero, all ones, or 0x80 according to the SPU SHUFB encoding. */
typedef struct { __m128i index, fill; } RadixSelector;
static inline RadixSelector radix_selector(u128 mask)
{
    __m128i c;
    memcpy(&c, &mask, sizeof c);
    const __m128i kind = _mm_and_si128(c, _mm_set1_epi8(-32));
    const __m128i ones = _mm_cmpeq_epi8(kind, _mm_set1_epi8(-64));
    const __m128i high = _mm_and_si128(_mm_cmpeq_epi8(kind, _mm_set1_epi8(-32)), _mm_set1_epi8(-128));
    const RadixSelector result = {_mm_xor_si128(c, _mm_set1_epi8(3)), _mm_or_si128(ones, high)};
    return result;
}
static inline u128 radix_select(u128 input, RadixSelector selector)
{
    __m128i bytes;
    memcpy(&bytes, &input, sizeof bytes);
    return record_vector(_mm_or_si128(_mm_shuffle_epi8(bytes, selector.index), selector.fill));
}
