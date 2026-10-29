#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include <assert.h>
#include <stdio.h>
static uint32_t state = 0x12abcdef;
static uint32_t random_word(void) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
static void equal(u128 a, u128 b) { assert(!memcmp(&a, &b, sizeof a)); }
int main(void) {
    for (unsigned trial = 0; trial < 8192; ++trial) {
        u128 a, b;
        for (unsigned lane = 0; lane < 4; ++lane) {
            a._u32[lane] = random_word(); b._u32[lane] = random_word();
        }
        equal(spu_cntb(a), spu_cntb_ref(a));
        for (unsigned control = 0; control < 256; ++control) {
            b._u32[0] = (random_word() & 0xffffff00u) | control;
            equal(spu_shlqby(a,b), spu_shlqby_ref(a,b));
            equal(spu_rotqby(a,b), spu_rotqby_ref(a,b));
            equal(spu_shlqbybi(a,b), spu_shlqbybi_ref(a,b));
            equal(spu_rotqbybi(a,b), spu_rotqbybi_ref(a,b));
            equal(spu_rotqmby(a,b), spu_rotqmby_ref(a,b));
            equal(spu_rotqmbybi(a,b), spu_rotqmbybi_ref(a,b));
            int immediate = (int)control - 128;
            equal(spu_shlqbyi(a,immediate), spu_shlqbyi_ref(a,immediate));
            equal(spu_rotqbyi(a,immediate), spu_rotqbyi_ref(a,immediate));
            equal(spu_rotqmbyi(a,immediate), spu_rotqmbyi_ref(a,immediate));
        }
    }
    // Every possible byte value, including zero and all bits set.
    for (unsigned byte = 0; byte < 256; ++byte) {
        u128 a; memset(&a, (int)byte, sizeof a);
        equal(spu_cntb(a), spu_cntb_ref(a));
    }
    puts("PASS: 18,874,368 byte-shift comparisons and population-count cases");
}
