#define SPU_LS_DEBUG_HOOKS 0
#include "spu_helpers.h"
#include "../port/native_stagef_avx2.h"
#include <cassert>
#include <cstdio>
int main(){
    unsigned vectors=0,random=1234567;
    auto check=[&](const uint32_t* input){
        uint32_t actual[8];
        _mm256_storeu_si256((v8*)actual,v8_frsqest(_mm256_loadu_si256((const v8*)input)));
        for(unsigned half=0;half<2;++half){
            u128 words;for(unsigned lane=0;lane<4;++lane)words._u32[lane]=input[half*4+lane];
            const auto expected=spu_frsqest(words);
            for(unsigned lane=0;lane<4;++lane)assert(actual[half*4+lane]==expected._u32[lane]);
        }
        ++vectors;
    };
    // All exponent, sign and fraction-table-index combinations. The low
    // fraction bits do not affect the estimate; include their boundaries.
    for(unsigned e=0;e<256;++e)for(unsigned fraction=0;fraction<32;++fraction){
        uint32_t input[8];
        for(unsigned lane=0;lane<8;++lane)
            input[lane]=(lane&1?0x80000000u:0u)|(e<<23)|(fraction<<18)|((lane&2)?0x3ffffu:0u);
        check(input);
    }
    for(unsigned n=0;n<100000;++n){
        uint32_t input[8];
        for(auto& x:input){random^=random<<13;random^=random>>17;random^=random<<5;x=random;}
        check(input);
    }
    printf("PASS rsqrt estimate: %u vectors, exhaustive exponent/sign/table coverage and mixed raw words\n",vectors);
}
