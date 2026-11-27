#pragma once
#if defined(__aarch64__)
#include <arm_neon.h>
#else
#include <immintrin.h>
#endif
#include <cstring>

namespace buzz {
// Guest VMX VPERM selects one of 32 raw bytes. Preserve all sources before
// writing the destination, which is allowed to alias any of the three.
template<bool SameSource, class V>
inline void ppu_permute_bytes(V& out, const V& left, const V& right, const V& control)
{
#if defined(__aarch64__)
    const auto a = vld1q_u8(reinterpret_cast<const uint8_t*>(&left));
    const auto mask = vld1q_u8(reinterpret_cast<const uint8_t*>(&control));
    uint8x16_t result;
    if constexpr (SameSource) {
        result = vqtbl1q_u8(a, vandq_u8(mask, vdupq_n_u8(15)));
    } else {
        const uint8x16x2_t table = {{a, vld1q_u8(reinterpret_cast<const uint8_t*>(&right))}};
        result = vqtbl2q_u8(table, vandq_u8(mask, vdupq_n_u8(31)));
    }
    vst1q_u8(reinterpret_cast<uint8_t*>(&out), result);
#else
    __m128i a, mask;
    std::memcpy(&a, &left, sizeof a);
    std::memcpy(&mask, &control, sizeof mask);
    const __m128i index = _mm_and_si128(mask, _mm_set1_epi8(15));
    __m128i result = _mm_shuffle_epi8(a, index);
    if constexpr (!SameSource) {
        __m128i b;
        std::memcpy(&b, &right, sizeof b);
        result = _mm_blendv_epi8(result, _mm_shuffle_epi8(b, index), _mm_slli_epi16(mask, 3));
    }
    std::memcpy(&out, &result, sizeof result);
#endif
}
}
