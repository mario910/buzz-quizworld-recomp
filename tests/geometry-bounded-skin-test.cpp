#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include <cassert>
#include <cstdio>
using namespace buzz;
int main() {
    buzz_test_enter_rtz(); uint32_t state=0x45321;
    auto random=[&]() {state^=state<<13;state^=state>>17;state^=state<<5;return state;};
    const uint32_t edge[]={0,0x80000000,1,0x007fffff,0x00800000,0x80800000,
        0x3f800000,0xbf800000,0x4e800000,0xce800000,0x4e800001,0x7f7fffff,
        0xff7fffff,0x7f800000,0xff800000,0xffffffff};
    unsigned compared=0;
    for(unsigned trial=0;trial<20000;++trial) {
        v8 matrix[12]; GeometryHostSoA8 a,b;
        for(auto& coefficient:matrix) {
            uint32_t word[8];
            for(auto& w:word) {auto r=random();w=(r&0x80000000)|((r&0x7fffffff)%0x4f800001u);}
            coefficient=_mm256_loadu_si256(reinterpret_cast<const v8*>(word));
        }
        for(auto& component:a.component) {
            uint32_t word[8];
            for(auto& w:word) {auto r=random();w=trial%4==0?edge[r%16]:(r&0x80000000)|((r&0x7fffffff)%0x4e800001u);}
            component=_mm256_loadu_si256(reinterpret_cast<const v8*>(word));
        }
        b=a;
        const auto mask=_mm256_cmpgt_epi32(v8_set(trial%8+1),_mm256_setr_epi32(0,1,2,3,4,5,6,7));
        transform_geometry_host_weighted_value<false>(a,matrix,trial%2,mask);
        transform_geometry_host_weighted_value<true>(b,matrix,trial%2,mask);
        for(unsigned component=0;component<4;++component) {
            uint32_t x[8],y[8];
            _mm256_storeu_si256(reinterpret_cast<v8*>(x),a.component[component]);
            _mm256_storeu_si256(reinterpret_cast<v8*>(y),b.component[component]);
            for(unsigned lane=0;lane<8;++lane) {
                if(x[lane]!=y[lane]) {printf("trial=%u component=%u lane=%u words=%08X/%08X\n",trial,component,lane,x[lane],y[lane]);return 1;}
                ++compared;
            }
        }
    }
    printf("PASS bounded weighted skin: %u component words, points/directions/tails/zeros/extended input fallback\n",compared);
}
