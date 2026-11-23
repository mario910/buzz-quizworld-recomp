#pragma once
#include <immintrin.h>
#include <cstring>

namespace buzz {
// Guest VMX VPERM selects one of 32 raw bytes. Preserve all sources before
// writing the destination, which is allowed to alias any of the three.
template<bool SameSource, class V>
inline void ppu_permute_bytes(V& out, const V& left, const V& right, const V& control)
{
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
}
}
