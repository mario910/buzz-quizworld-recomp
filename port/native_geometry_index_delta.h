#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif

namespace buzz {
// Eight packed deltas occupy exactly `width` bytes and update independent
// carry lanes modulo 65536. The caller admits widths 2..11 and a full block.
// Loading ahead is permitted only inside the declared payload; the final
// block uses a zero-padded local copy, not surrounding LS memory.
inline void geometry_host_index_delta8(const uint8_t* source, unsigned available,
    unsigned width, unsigned bias, uint16_t* carry)
{
    struct Tables {
        uint8_t shuffle[12][32]{};
        uint32_t shifts[12][8]{};
        constexpr Tables() {
            for (unsigned w=2;w<=11;++w) for (unsigned lane=0;lane<8;++lane) {
                const unsigned byte=lane*w/8;
                shuffle[w][lane*4]=uint8_t(byte+2);
                shuffle[w][lane*4+1]=uint8_t(byte+1);
                shuffle[w][lane*4+2]=uint8_t(byte);
                shuffle[w][lane*4+3]=255;
                shifts[w][lane]=24-w-(lane*w%8);
            }
        }
    };
    static constexpr Tables tables{};
    alignas(16) uint8_t tail[16]{};
    if (available < 16) { memcpy(tail,source,width); source=tail; }
#if defined(__aarch64__)
    const uint8x16_t packed=vld1q_u8(source);
    uint32x4_t lo=vreinterpretq_u32_u8(vqtbl1q_u8(packed,vld1q_u8(tables.shuffle[width])));
    uint32x4_t hi=vreinterpretq_u32_u8(vqtbl1q_u8(packed,vld1q_u8(tables.shuffle[width]+16)));
    lo=vshlq_u32(lo,vnegq_s32(vreinterpretq_s32_u32(vld1q_u32(tables.shifts[width]))));
    hi=vshlq_u32(hi,vnegq_s32(vreinterpretq_s32_u32(vld1q_u32(tables.shifts[width]+4))));
    const uint16x8_t raw=vandq_u16(vcombine_u16(vmovn_u32(lo),vmovn_u32(hi)),vdupq_n_u16(uint16_t((1u<<width)-1)));
    vst1q_u16(carry,vsubq_u16(vaddq_u16(vld1q_u16(carry),raw),vdupq_n_u16(uint16_t(bias))));
#elif defined(__AVX2__)
    const __m256i packed=_mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(source)));
    __m256i raw=_mm256_shuffle_epi8(packed,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(tables.shuffle[width])));
    raw=_mm256_and_si256(_mm256_srlv_epi32(raw,_mm256_loadu_si256(reinterpret_cast<const __m256i*>(tables.shifts[width]))),_mm256_set1_epi32((1u<<width)-1));
    __m256i values=_mm256_sub_epi32(_mm256_add_epi32(_mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(carry))),raw),_mm256_set1_epi32(bias));
    values=_mm256_and_si256(values,_mm256_set1_epi32(65535));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(carry),_mm_packus_epi32(_mm256_castsi256_si128(values),_mm256_extracti128_si256(values,1)));
#else
    for(unsigned lane=0;lane<8;++lane) {
        unsigned raw=0;
        for(unsigned bit=0;bit<width;++bit) {
            const unsigned at=lane*width+bit;
            raw=(raw<<1)|((source[at/8]>>(7-at%8))&1);
        }
        carry[lane]=uint16_t(carry[lane]+raw-bias);
    }
#endif
}
} // namespace buzz
