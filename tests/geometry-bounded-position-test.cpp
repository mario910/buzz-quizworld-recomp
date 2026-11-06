#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace buzz;

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
static NOINLINE GeometryHostSoA8 transform(GeometryHostSoA8 value, const v8* matrix, v8 mask, bool fast) {
    if (!fast || !transform_geometry_bounded_position(value,matrix,mask))
        transform_geometry_host_object_value(value,matrix,false,mask);
    return value;
}
int main(int argc,char**) {
    buzz_test_enter_rtz();
    uint32_t rng=0x752eb951;
    auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    const uint32_t edge[]={0,0x80000000,1,0x80000001,0x7fffff,0x807fffff,0x800000,
        0x80800000,0x3f800000,0xbf800000,0x4e7fffff,0xce7fffff,0x4e800000,0xce800000,
        0x4e800001,0x7f800000,0xff800000,0x7fffffff};
    uint64_t admitted=0,rejected=0,compared=0;
    for(unsigned trial=0;trial<100000;++trial){
        uint32_t coeff[12],input[4][8],masks[8];v8 matrix[12];GeometryHostSoA8 value{};
        for(auto& c:coeff)c=trial%3==0?edge[next()%18]:trial%6==1?edge[next()%14]:((next()&0x80000000u)|(next()%0x4e800001u));
        for(unsigned c=0;c<12;++c)matrix[c]=v8_set(coeff[c]);
        bool eligible=geometry_object_matrix_bounded(coeff);
        for(unsigned c=0;c<4;++c){
            for(auto& word:input[c]){
                word=trial%3==0?edge[next()%18]:trial%6==1?edge[next()%14]:((next()&0x80000000u)|(next()%0x4e800001u));
                eligible&=(word&0x7fffffff)<=0x4e800000u;
            }
            value.component[c]=_mm256_loadu_si256((const v8*)input[c]);
        }
        for(unsigned i=0;i<8;++i)masks[i]=i<trial%9?~0u:0u;
        const v8 mask=_mm256_loadu_si256((const v8*)masks);
        auto actual=value;
        const bool accepted=geometry_object_matrix_bounded(coeff)&&transform_geometry_bounded_position(actual,matrix,mask);
        assert(accepted==eligible);
        if(!accepted){assert(!memcmp(&actual,&value,sizeof value));++rejected;continue;}
        ++admitted;
        auto generic=transform(value,matrix,mask,false);
        assert(!memcmp(&actual,&generic,sizeof actual));
        uint32_t output[4][8];for(unsigned c=0;c<4;++c)_mm256_storeu_si256((v8*)output[c],actual.component[c]);
        for(unsigned row=0;row<3;++row)for(unsigned lane=0;lane<8;++lane){
            uint32_t expected=spu__fm_lane(input[row][lane],coeff[row*4+row]);
            for(unsigned term=1;term<4;++term){const unsigned c=(row+term)%4;
                expected=spu__fma_lane(input[c][lane],coeff[row*4+c],expected,0,0);}
            assert(output[row][lane]==(expected&masks[lane]));++compared;
        }
        assert(!memcmp(input[3],output[3],sizeof input[3]));
    }
    assert(admitted>60000&&rejected>30000);
    printf("PASS bounded positions: %llu admitted, %llu rejected, %llu exact scalar components\n",
        (unsigned long long)admitted,(unsigned long long)rejected,(unsigned long long)compared);
    if(argc<2)return 0;
    std::array<GeometryHostSoA8,1024> values{};
    for(auto& value:values)for(auto& c:value.component)c=v8_set(0x3f000000+(next()&0x7fffff));
    v8 matrix[12];for(auto& c:matrix)c=v8_set(0x3f000000+(next()&0x7fffff));
    const auto mask=v8_set(~0u);volatile uint32_t sink=0;
    for(unsigned repeat=0;repeat<5;++repeat){double ms[2]{};
        for(unsigned mode=0;mode<2;++mode){const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<1000000;++i){auto result=transform(values[i%values.size()],matrix,mask,mode!=0);
                uint32_t words[8];_mm256_storeu_si256((v8*)words,result.component[0]);sink=sink+words[i%8];}
            ms[mode]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
        printf("generic_ms=%.3f bounded_ms=%.3f speedup=%.2f checksum=%u\n",ms[0],ms[1],ms[0]/ms[1],unsigned(sink));
    }
}
