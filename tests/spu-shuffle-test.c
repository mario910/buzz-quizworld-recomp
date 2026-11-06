#include "spu_shuffle.h"
#include <assert.h>
#include <stdio.h>
static unsigned rng = 0x12345678;
static unsigned next(void) { rng ^= rng << 13; rng ^= rng >> 17; return rng ^= rng << 5; }
int main(void) {
    for (unsigned iteration = 0; iteration < 200000; ++iteration) {
        u128 a,b,c;
        for (unsigned i=0;i<4;++i) {a._u32[i]=next();b._u32[i]=next();c._u32[i]=next();}
        // Cover every selector class in every position, then mixed masks.
        if(iteration < 256) memset(c._u8, iteration, 16);
        const u128 actual=spu_shufb(a,b,c), expected=spu_shufb_ref(a,b,c);
        assert(!memcmp(&actual,&expected,16));
        // Include negative and out-of-range immediate encodings. Both paths
        // mask immediates exactly as the ISA specifies.
        const int shift = (int)(iteration % 257) - 128;
        const u128 left = spu_shlqbyi(a, shift), left_ref = spu_shlqbyi_ref(a, shift);
        const u128 rotate = spu_rotqbyi(a, shift), rotate_ref = spu_rotqbyi_ref(a, shift);
        assert(!memcmp(&left,&left_ref,16));
        assert(!memcmp(&rotate,&rotate_ref,16));
    }
    puts("shuffle/shift/rotate: 200000 vectors match the scalar oracle");
}
