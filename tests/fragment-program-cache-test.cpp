#include "../port/fragment_program_cache.h"
#include "geometry-test-memory.h"
#include <array>
#include <cassert>
#include <cstdio>
static buzz_fp_hash_cache cache;
static void word(u8* p,u32 value) {
    const u32 v=(value<<16)|(value>>16);
    p[0]=u8(v>>24);p[1]=u8(v>>16);p[2]=u8(v>>8);p[3]=u8(v);
}
static void program(u8* p,unsigned instructions,bool constants) {
    const unsigned stride=constants?32:16;
    memset(p,0,instructions*stride);
    for(unsigned i=0;i<instructions;++i){
        word(p+i*stride,(1u<<24)|(0xfu<<9)|(i+1==instructions));
        word(p+i*stride+4,constants?2:1);
        if(constants)for(unsigned c=0;c<4;++c)word(p+i*stride+16+c*4,0x3f800000u+c+i);
    }
}
static void check(const u8* p,u32 bytes,u64 seed) {
    const u64 expected=rsx_fp_structural_hash(p,bytes,seed);
    assert(buzz_fp_hash_cached(&cache,p,bytes,seed)==expected);
    assert(buzz_fp_hash_cached(&cache,p,bytes,seed)==expected);
    assert(buzz_fp_hash_cached(nullptr,p,bytes,seed)==expected);
}
int main() {
    std::array<u8,16416> code{};
    for(unsigned count:{1u,2u,7u,32u,128u,256u,512u})for(bool constants:{false,true})
    for(u64 seed:{u64(0),u64(1),u64(1469598103934665603ull),~u64(0)}){
        const unsigned size=count*(constants?32:16);program(code.data(),count,constants);check(code.data(),size,seed);
        // Same allocation, keys and sizes: change every code/constant byte,
        // including bytes outside the three sampled slot-selection words.
        for(unsigned at=0;at<size;++at){code[at]^=0x81;check(code.data(),size,seed);code[at]^=0x81;check(code.data(),size,seed);}
    }
    unsigned rng=0x67192abc;
    for(unsigned trial=0;trial<12000;++trial){
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        code[rng%code.size()]^=u8(rng>>24);
        check(code.data(),1+rng%code.size(),(u64(rng)<<32)^trial);
    }
    check(nullptr,0,7);check(nullptr,16,7);
    SYSTEM_INFO system{};GetSystemInfo(&system);const unsigned size=16384;
    auto* storage=(u8*)VirtualAlloc(nullptr,size+system.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(storage);DWORD old;assert(VirtualProtect(storage+size,system.dwPageSize,PAGE_NOACCESS,&old));
    for(unsigned bytes:{16u,32u,112u,8192u,8208u,16384u}){
        auto* at=storage+size-bytes;program(at,bytes/16,false);check(at,bytes,71);
    }
    assert(VirtualFree(storage,0,MEM_RELEASE));
    puts("PASS fragment hash cache: all-byte instruction/constant mutations, seeds, collisions, malformed/oversized inputs, 12000 random cases and guarded ends");
}
