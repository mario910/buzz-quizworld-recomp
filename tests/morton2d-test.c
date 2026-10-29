#include "../ps3recomp/libs/video/rsx_morton2d.h"
#include <assert.h>
#include <stdio.h>
static uint32_t reference(uint32_t x, uint32_t y, uint32_t lw, uint32_t lh)
{
    uint32_t n = lw < lh ? lw : lh, out = 0;
    for (uint32_t i = 0; i < n; ++i)
        out |= (((x >> i) & 1u) << (2*i)) | (((y >> i) & 1u) << (2*i+1));
    if (lw > lh) out |= (x >> n) << (2*n);
    else if (lh > lw) out |= (y >> n) << (2*n);
    return out;
}
int main(void)
{
    unsigned tested = 0;
    for (unsigned lw = 0; lw <= 16; ++lw) for (unsigned lh = 0; lh <= 16; ++lh) {
        unsigned w = 1u << lw, h = 1u << lh;
        unsigned limitx = w < 128 ? w : 128, limity = h < 128 ? h : 128;
        for (unsigned y = 0; y < limity; ++y) for (unsigned x = 0; x < limitx; ++x) {
            assert(rsx_morton2d(x,y,lw,lh) == reference(x,y,lw,lh));
            assert((rsx_morton2d(x,0,lw,lh) | rsx_morton2d(0,y,lw,lh)) == reference(x,y,lw,lh));
            ++tested;
        }
        uint32_t s = 0x1234567;
        for (unsigned n = 0; n < 10000; ++n) {
            s = s * 1664525u + 1013904223u; unsigned x = s & (w-1);
            s = s * 1664525u + 1013904223u; unsigned y = s & (h-1);
            assert(rsx_morton2d(x,y,lw,lh) == reference(x,y,lw,lh));
            assert((rsx_morton2d(x,0,lw,lh) | rsx_morton2d(0,y,lw,lh)) == reference(x,y,lw,lh));
            ++tested;
        }
    }
    printf("PASS: %u rectangular Morton addresses and separable row/column offsets\n", tested);
}
