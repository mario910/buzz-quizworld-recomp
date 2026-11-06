#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include <cassert>
#include <cstdio>
using namespace buzz;
int main() {
    buzz_test_enter_rtz();unsigned rng=0x831a739b;
    auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    const uint32_t special[]={0,0x80000000,1,0x7fffff,0x800000,0x3f800000,0xbf800000,
        0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff,0x7fc12345};
    unsigned components=0;
    for(unsigned trial=0;trial<20000;++trial){
        uint32_t input[4][8],coeff[12],actual[4][8];v8 matrix[12];GeometryHostSoA8 value;
        for(unsigned c=0;c<4;++c){
            for(auto& x:input[c])x=trial%2?next():special[next()%14];
            value.component[c]=_mm256_loadu_si256((const v8*)input[c]);
        }
        for(unsigned c=0;c<12;++c){coeff[c]=trial%2?next():special[next()%14];matrix[c]=v8_set(coeff[c]);}
        const unsigned active=trial%9;
        const v8 mask=_mm256_cmpgt_epi32(v8_set(active),_mm256_setr_epi32(0,1,2,3,4,5,6,7));
        transform_geometry_host_object_value(value,matrix,true,mask);
        for(unsigned c=0;c<4;++c)_mm256_storeu_si256((v8*)actual[c],value.component[c]);
        for(unsigned lane=0;lane<8;++lane){
            uint32_t expected[3];
            for(unsigned row=0;row<3;++row){
                expected[row]=spu__fm_lane(input[row][lane],coeff[row*4+row]);
                for(unsigned term=1;term<4;++term){const unsigned c=(row+term)%4;
                    expected[row]=spu__fma_lane(input[c][lane],c==3?0:coeff[row*4+c],expected[row],0,0);}
            }
            uint32_t length=spu__fm_lane(expected[0],expected[0]);
            length=spu__fma_lane(expected[1],expected[1],length,0,0);
            length=spu__fma_lane(expected[2],expected[2],length,0,0);
            u128 len{};len._u32[0]=length;
            const uint32_t inverse=spu_fi(len,spu_frsqest(len))._u32[0];
            for(unsigned c=0;c<3;++c){
                assert(actual[c][lane]==(lane<active?spu__fm_lane(expected[c],inverse):0));++components;
            }
            assert(actual[3][lane]==input[3][lane]);
        }
    }
    printf("PASS object directions: %u exact scalar components; cyclic zero FMAs, arbitrary W, extended exponents and tails\n",components);
}
