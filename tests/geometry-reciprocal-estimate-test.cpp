#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_data.h"
#include <cassert>
#include <cstdio>

int main() {
    unsigned state=0x812739ab, cases=0;
    auto next=[&]() { state^=state<<13;state^=state>>17;state^=state<<5;return state; };
    auto check=[&](const uint32_t* input) {
        uint32_t actual[8];
        _mm256_storeu_si256(reinterpret_cast<v8*>(actual),v8_frest(_mm256_loadu_si256(reinterpret_cast<const v8*>(input))));
        for(unsigned half=0;half<2;++half){
            u128 words;memcpy(&words,input+half*4,16);
            const auto expected=spu_frest(words);
            for(unsigned lane=0;lane<4;++lane)assert(actual[half*4+lane]==expected._u32[lane]);
        }
        cases+=8;
    };
    // Every exponent, sign and table bucket, with both ends of the ignored
    // fraction range. Includes zero/denormals and SPU extended exponents.
    for(unsigned sign=0;sign<2;++sign)for(unsigned exponent=0;exponent<256;++exponent)
    for(unsigned bucket=0;bucket<32;++bucket){
        uint32_t words[8];
        for(unsigned lane=0;lane<8;++lane)
            words[lane]=(sign<<31)|(exponent<<23)|(((bucket+lane)&31)<<18)|((lane&1)?0x3ffff:0);
        check(words);
    }
    for(unsigned trial=0;trial<100000;++trial){uint32_t words[8];for(auto& word:words)word=next();check(words);}
    printf("PASS exact reciprocal estimates: %u lanes, all exponent/sign/table buckets and random mixed lanes\n",cases);
}
