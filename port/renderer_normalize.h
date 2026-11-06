#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef BUZZ_RENDERER_NORMAL_MULTIPLY
#define BUZZ_RENDERER_NORMAL_MULTIPLY 0
#endif

/* Exact only for the decoded CMP domains: [-1024,1023]/1023 and
 * [-512,511]/511. In round-to-nearest, reciprocal multiplication is one bit
 * too small in magnitude for 48 and 96 inputs respectively. The predicates
 * below identify precisely those output mantissas; adding one bit repairs
 * them for either sign. All input encodings are exhaustively tested.
 * Other rounding modes keep the original divisions. This is not a general
 * replacement for floating-point division or approximate normal conversion. */
static inline int buzz_normal_multiply_enabled(void) {
#if BUZZ_RENDERER_NORMAL_MULTIPLY
    static int enabled=-1;
    if(enabled<0) {
        const char* e=getenv("BUZZ_NORMAL_MULTIPLY");enabled=!(e && !strcmp(e,"0"));
        fprintf(stderr,"[rsx engine] exact CMP multiply=%d\n",enabled);
    }
    if(!enabled)return 0;
#if BUZZ_FETCH_NEON
    uint64_t control;__asm__ volatile("mrs %0, fpcr":"=r"(control));
    return !(control&(3ull<<22));
#elif BUZZ_FETCH_SSE
    return !(_mm_getcsr()&0x6000u);
#else
    return 0;
#endif
#else
    return 0;
#endif
}
#if BUZZ_RENDERER_NORMAL_MULTIPLY && BUZZ_FETCH_NEON
static inline uint32x4_t buzz_normal_multiply4(float32x4_t v,int z) {
    const uint32x4_t q=vreinterpretq_u32_f32(vmulq_f32(v,vdupq_n_f32(z?0x1.00804p-9f:0x1.00401p-10f)));
    const uint32x4_t mask=vdupq_n_u32(z?0x0007c3e1u:0x001fe7f9u);
    const uint32x4_t match=vceqq_u32(vandq_u32(q,mask),vdupq_n_u32(z?0x00024120u:0x00082208u));
    return vaddq_u32(q,vandq_u32(match,vdupq_n_u32(1)));
}
#elif BUZZ_RENDERER_NORMAL_MULTIPLY && BUZZ_FETCH_SSE
static inline __m128i buzz_normal_multiply4(__m128 v,int z) {
    const __m128i q=_mm_castps_si128(_mm_mul_ps(v,_mm_set1_ps(z?0x1.00804p-9f:0x1.00401p-10f)));
    const __m128i mask=_mm_set1_epi32(z?0x0007c3e1u:0x001fe7f9u);
    const __m128i match=_mm_cmpeq_epi32(_mm_and_si128(q,mask),_mm_set1_epi32(z?0x00024120u:0x00082208u));
    return _mm_add_epi32(q,_mm_and_si128(match,_mm_set1_epi32(1)));
}
#endif
