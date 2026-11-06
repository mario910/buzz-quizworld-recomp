#include "../port/native_geometry_index_delta.h"
#include <cassert>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
static uint32_t random_state=0x8ad6c391;
static uint32_t random_word(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void check(const uint8_t* bytes,unsigned available,unsigned width,unsigned bias){
    uint16_t actual[10],expected[8];
    actual[0]=0x1579;actual[9]=0x2468;
    for(unsigned lane=0;lane<8;++lane){
        const uint16_t previous=uint16_t(random_word());actual[lane+1]=previous;
        unsigned raw=0;
        for(unsigned at=lane*width;at<(lane+1)*width;++at)raw=raw*2+((bytes[at/8]>>(7-at%8))&1);
        expected[lane]=uint16_t(previous+raw-bias);
    }
    buzz::geometry_host_index_delta8(bytes,available,width,bias,actual+1);
    assert(actual[0]==0x1579&&actual[9]==0x2468);
    assert(!memcmp(actual+1,expected,sizeof expected));
}
int main(){
    uint8_t bytes[64];
    for(unsigned width=2;width<=11;++width)for(unsigned n=0;n<1000;++n){
        for(auto& b:bytes)b=uint8_t(n==0?0:n==1?255:random_word());
        for(unsigned offset=0;offset<16;++offset)for(unsigned available:{width,16u,32u})
            check(bytes+offset,available,width,random_word()&65535);
    }
#ifdef _WIN32
    SYSTEM_INFO info;GetSystemInfo(&info);const size_t page=info.dwPageSize;
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
    DWORD old;assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&old));
#else
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
    auto* memory=static_cast<uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));assert(memory!=MAP_FAILED);
    assert(!mprotect(memory+page,page,PROT_NONE));
#endif
    for(unsigned width=2;width<=11;++width)for(unsigned bias:{0u,1u,32767u,32768u,65535u}){
        auto* end=memory+page-width;
        for(unsigned i=0;i<width;++i)end[i]=uint8_t(random_word());
        check(end,width,width,bias);
    }
#ifdef _WIN32
    assert(VirtualFree(memory,0,MEM_RELEASE));
#else
    assert(!munmap(memory,page*2));
#endif
    puts("PASS eight-delta extraction/carry: widths 2..11, arbitrary alignment, modulo wrap and guard-page tails");
}
