#include "spu_helpers.h"
#include "native_image7_simd.h"
#include <cassert>
#include <cstdio>

// Exhaustive byte-domain proof using integer SPU helpers, independently of
// the hardware float operations and clamped integer packing being tested.
int main()
{
    const unsigned saved = _mm_getcsr();
    _mm_setcsr(0xffc0);
    const uint32_t cu[3] = {0, 0xbec8b439, 0x40011687};
    const uint32_t cv[3] = {0x3fcc49ba, 0xbf5020c5, 0};
    uint32_t ys[256], chroma[256];
    for (unsigned i = 0; i < 256; ++i) {
        const uint32_t f = i ? spu__sf_pack(0, i, 0) : 0;
        ys[i] = spu__fa_lane(f, 0xc1800000);
        chroma[i] = spu__fa_lane(f, 0xc3000000);
    }
    uint64_t checked = 0;
    for (unsigned u = 0; u < 256; ++u) {
        for (unsigned v = 0; v < 256; ++v) {
            uint32_t acc[3];
            for (unsigned k = 0; k < 3; ++k)
                acc[k] = spu__fma_lane(chroma[v], cv[k],
                    spu__fma_lane(chroma[u], cu[k], 0x3f000000, 0, 0), 0, 0);
            for (unsigned y = 0; y < 256; y += 8) {
                uint32_t actual[8];
                const __m256 yv = _mm256_cvtepi32_ps(
                    _mm256_setr_epi32(y, y+1, y+2, y+3, y+4, y+5, y+6, y+7));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(actual),
                    buzz_image7_simd::color8(yv, _mm256_set1_ps(float(u)), _mm256_set1_ps(float(v))));
                for (unsigned lane = 0; lane < 8; ++lane) {
                    uint32_t expected = 0xff000000;
                    for (unsigned k = 0; k < 3; ++k) {
                        const uint32_t value = spu__fma_lane(ys[y+lane], 0x3f94fdf4, acc[k], 0, 0);
                        expected |= (spu__cfl_lane(value, 24, 1) >> 24) << (8*k);
                    }
                    if (actual[lane] != expected) {
                        fprintf(stderr, "FAIL y=%u u=%u v=%u expected=%08x actual=%08x\n",
                            y+lane, u, v, expected, actual[lane]);
                        _mm_setcsr(saved);
                        return 1;
                    }
                    ++checked;
                }
            }
        }
    }
    assert(checked == 16777216);
    assert((_mm_getcsr() & ~63u) == 0xffc0);
    _mm_setcsr(saved);
    puts("PASS all 16777216 YUV triples against integer SPU FMA/conversion oracle");
}
