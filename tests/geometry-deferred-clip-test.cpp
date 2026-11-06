#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_clip_job.h"
#include <cassert>
#include <cstdio>
#include <chrono>
using namespace buzz;
static uint32_t rng=0x941ecc3;
static uint32_t next() {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static volatile uint32_t checksum;
template<bool Deferred> __attribute__((noinline)) static GeometryHostSoA8 project(
    const GeometryHostClipParameters& p,const GeometryHostSoA8& v)
{return geometry_host_clip8_impl<Deferred>(p,v);}
int main(int argc,char**) {
    buzz_test_enter_rtz();
    const uint32_t corner[]={0,0x80000000,1,0x807fffff,0x800000,0x80800000,
        0x3f800000,0xbf800000,0x7f7ffffe,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    GeometryHostClipParameters p{};GeometryHostSoA8 v{};
    for(unsigned trial=0;trial<30000;trial++) {
        auto bits=[&](){uint32_t n=next();return trial<10000?(n&0x80ffffff)|0x3f000000:
            trial<20000?corner[n%(sizeof(corner)/4)]:n;};
        for(auto& row:p.row)for(auto& x:row)x=bits();
        for(auto& x:p.minimum)x=bits();for(auto& x:p.maximum)x=bits();p.quantization_mask=trial&1?0xfffffffe:0xfffffffc;
        for(auto& component:v.component){uint32_t x[8];for(auto& a:x)a=bits();component=_mm256_loadu_si256((const v8*)x);}
        auto a=project<false>(p,v),b=project<true>(p,v);
        if(memcmp(&a,&b,sizeof(a))){fprintf(stderr,"Mismatch trial=%u\n",trial);return 1;}
    }
    puts("PASS: 30000 projection blocks / 960000 component words; ordinary, extended, zero, denormal and random bits");
    if(argc>1){
        for(auto& row:p.row)for(auto& x:row)x=0x3f000000;
        for(auto& x:p.minimum)x=0;for(auto& x:p.maximum)x=0x447a0000;
        for(auto& component:v.component)component=v8_set(0x3f800000);
        for(unsigned round=0;round<6;round++) {
            auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<200000;i++) {
                p.row[0][0]=0x3f000000+(i&255);
                auto x=round&1?project<true>(p,v):project<false>(p,v);
                checksum^=(uint32_t)_mm256_extract_epi32(x.component[0],0);
            }
            printf("%s %.3f ns/block\n",round&1?"deferred":"reference",
                std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/200000);fflush(stdout);
        }
    }
}
