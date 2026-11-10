// ARM64 masked tails must match AVX semantics without speculative overreads.
#include "../port/geometry_neon_vectors.h"
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

int main()
{
    alignas(32) uint32_t source[8],destination[8],mask[8],result[8];
    for(unsigned bits=0;bits<256;++bits) {
        for(unsigned lane=0;lane<8;++lane) {
            source[lane]=0x12345678u+lane;
            destination[lane]=0xdeadbeefu;
            // Only the sign bit selects: test noncanonical masks too.
            mask[lane]=(bits&(1u<<lane)?0x80000000u:0u)|0x13579bdu;
        }
        const auto selected=_mm256_loadu_si256((const __m256i*)mask);
        const auto value=_mm256_maskload_epi32((const int*)source,selected);
        _mm256_storeu_si256((__m256i*)result,value);
        _mm256_maskstore_epi32((int*)destination,selected,value);
        for(unsigned lane=0;lane<8;++lane) {
            const bool active=bits&(1u<<lane);
            assert(result[lane]==(active?source[lane]:0u));
            assert(destination[lane]==(active?source[lane]:0xdeadbeefu));
        }
    }
    const auto page=size_t(sysconf(_SC_PAGESIZE));
    auto* memory=(uint8_t*)mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(memory!=MAP_FAILED);
    assert(mprotect(memory+page,page,PROT_NONE)==0);
    for(unsigned tail=0;tail<=8;++tail) {
        auto* p=(uint32_t*)(memory+page-tail*4);
        for(unsigned lane=0;lane<8;++lane)mask[lane]=lane<tail?~0u:0u;
        for(unsigned lane=0;lane<tail;++lane)p[lane]=0xabcdef00u+lane;
        const auto selected=_mm256_loadu_si256((const __m256i*)mask);
        const auto value=_mm256_maskload_epi32((const int*)p,selected);
        _mm256_storeu_si256((__m256i*)result,value);
        _mm256_maskstore_epi32((int*)p,selected,value);
        for(unsigned lane=0;lane<8;++lane)assert(result[lane]==(lane<tail?0xabcdef00u+lane:0u));
    }
    assert(munmap(memory,page*2)==0);
    std::puts("PASS NEON masked memory: 256 sign masks, 0..8 protected-page tails");
}
