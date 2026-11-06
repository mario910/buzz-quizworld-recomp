#include "../port/vertex_program_cache.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
#include "geometry-test-memory.h"

static buzz_vp_cache cache{};
static void word(u8* p, u32 x) { for(unsigned i=0;i<4;++i)p[i]=u8(x>>(i*8)); }
static void program(u8* p,unsigned count,unsigned input) {
    const u32 source=2u|(1u<<12)|(2u<<10)|(3u<<8);
    for(unsigned n=0;n<count;++n){
        word(p+n*16,(1u<<30)|(63u<<15));
        word(p+n*16+4,(1u<<22)|(input<<8)|((source>>9)&0xff));
        word(p+n*16+8,(source&0x1ff)<<23);
        word(p+n*16+12,(n+1==count?1u:0u)|(0xfu<<13));
    }
}
static void check(const u8* p,unsigned bytes) {
    rsx_vp_input_analysis want{0x1234,7},actual=want;
    const int expected=rsx_vp_analyze_inputs(p,bytes,&want);
    assert(buzz_vp_analyze_cached(&cache,p,bytes,&actual)==expected);
    assert(actual.input_mask==want.input_mask&&actual.exact==want.exact);
}
int main(int argc,char** argv) {
    std::array<u8,BUZZ_VP_CACHE_BYTES+32> code{};
    for(unsigned count:{1u,2u,7u,32u,128u,512u})for(unsigned input=0;input<16;++input){
        program(code.data(),count,input);check(code.data(),count*16);check(code.data(),count*16);
        // Same address, length and sampled cache key; alter an unsampled input.
        if(count>=7){code[21]^=1;check(code.data(),count*16);code[21]^=1;check(code.data(),count*16);}
        code[count*16-4]&=0xfe;check(code.data(),count*16);check(code.data(),count*16);
    }
    unsigned rng=0x1987921;
    for(unsigned n=0;n<12000;++n){
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        const unsigned bytes=1+rng%code.size();
        code[rng%code.size()]^=u8(rng>>17);
        check(code.data(),bytes);check(code.data(),bytes);
    }
    check(nullptr,0);check(nullptr,16);
    assert(buzz_vp_analyze_cached(&cache,code.data(),16,nullptr)==-1);
    SYSTEM_INFO system{};GetSystemInfo(&system);
    const size_t accessible=((BUZZ_VP_CACHE_BYTES+system.dwPageSize-1)/system.dwPageSize)*system.dwPageSize;
    auto* guarded=(u8*)VirtualAlloc(nullptr,accessible+system.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(guarded);DWORD old=0;
    assert(VirtualProtect(guarded+accessible,system.dwPageSize,PAGE_NOACCESS,&old));
    for(unsigned size:{16u,32u,112u,2048u,8192u}){
        auto* at=guarded+accessible-size;program(at,size/16,2);check(at,size);check(at,size);
    }
    assert(VirtualFree(guarded,0,MEM_RELEASE));
    puts("PASS VP cache: exact hits, in-place mutations, collisions, malformed/oversized inputs and 12000 mutations");
    if(argc<2||strcmp(argv[1],"--bench"))return 0;
    volatile unsigned sink=0;
    for(unsigned count:{1u,16u,64u,128u,512u}){
        program(code.data(),count,3);check(code.data(),count*16);
        constexpr unsigned iterations=50000;
        double elapsed[2]{};
        for(unsigned mode=0;mode<2;++mode){
            const auto start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<iterations;++n){
                rsx_vp_input_analysis result{};
                if(mode)buzz_vp_analyze_cached(&cache,code.data(),count*16,&result);
                else rsx_vp_analyze_inputs(code.data(),count*16,&result);
                sink=sink+result.input_mask;
            }
            elapsed[mode]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        printf("instructions=%u reference_ms=%.3f cached_ms=%.3f speedup=%.2f\n",count,elapsed[0],elapsed[1],elapsed[0]/elapsed[1]);
    }
    printf("checksum=%u\n",unsigned(sink));
}
