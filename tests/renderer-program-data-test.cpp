#include "../port/renderer_program_data.h"
#include "geometry-test-memory.h"
#include <array>
#include <cassert>
#include <cstdio>
static unsigned rng=0x612972ba;
static buzz_byte_hash_cache hash_cache;
static rsx_fp_constant_block reference_constants{},actual_constants{};
static unsigned next() { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static void word(u8* p,u32 value) {
    value=(value<<16)|(value>>16);
    for(unsigned i=0;i<4;++i)p[i]=u8(value>>(24-i*8));
}
static void usage_check(const u8* code,unsigned bytes) {
    u8 masks[16];for(auto& mask:masks)mask=u8(next());
    masks[next()%16]=0;
    const auto actual=buzz_fp_usage_read(code,bytes);
    assert(actual.sampled==rsx_fp_texture_units(code,bytes));
    assert(buzz_fp_usage_mask(actual.gamma,masks)==rsx_fp_texture_gamma_units(code,bytes,masks));
    assert(buzz_fp_usage_mask(actual.expand,masks)==rsx_fp_texture_expand_units(code,bytes,masks));
    assert(buzz_fp_usage_mask(actual.gamma,nullptr)==0);
}
static void hash_check(const u8* code,unsigned bytes,u64 seed) {
    u64 expected=seed;
    for(unsigned i=0;i<bytes;++i)expected=(expected^code[i])*1099511628211ull;
    assert(buzz_program_hash_cached(&hash_cache,code,bytes,seed)==expected);
    assert(buzz_program_hash_cached(&hash_cache,code,bytes,seed)==expected);
    assert(buzz_program_hash_cached(nullptr,code,bytes,seed)==expected);
}
static void constants_check(const u8* code,unsigned bytes) {
    g_rsx_fp_nonfinite_constants=0;
    const int expected=rsx_fp_collect_constants(code,bytes,&reference_constants);
    const auto repairs=g_rsx_fp_nonfinite_constants;
    for(unsigned repeat=0;repeat<2;++repeat) {
        g_rsx_fp_nonfinite_constants=0;
        assert(buzz_renderer_collect_constants(code,bytes,&actual_constants)==expected);
        assert(!memcmp(&reference_constants,&actual_constants,sizeof actual_constants));
        assert(g_rsx_fp_nonfinite_constants==repairs);
    }
    rsx_fp_constant_block cleared=actual_constants;
    buzz_renderer_clear_constants(&cleared);
    const rsx_fp_constant_block empty{};
    assert(!memcmp(&cleared,&empty,sizeof empty));
}
int main() {
    std::array<u8,16416> code{};
    for(unsigned op=0;op<64;++op)for(unsigned unit=0;unit<16;++unit)
    for(unsigned flags=0;flags<16;++flags){
        for(unsigned i=0;i<code.size();i+=4)word(code.data()+i,next());
        word(code.data(),(op<<24)|(unit<<17)|((flags&1)<<21)|((flags>>1)&1));
        word(code.data()+4,(flags&4)?2:0);
        word(code.data()+8,(flags&8)?0x80000000:0);
        word(code.data()+12,0);
        for(unsigned size:{0u,15u,16u,17u,31u,32u,48u,256u})usage_check(code.data(),size);
    }
    for(unsigned trial=0;trial<12000;++trial){
        code[next()%code.size()]^=u8(next());const auto size=next()%code.size();
        usage_check(code.data(),size);hash_check(code.data(),size,(u64(next())<<32)|next());constants_check(code.data(),size);
    }
    for(unsigned bytes:{16u,128u,1024u,8192u})for(u64 seed:{u64(0),u64(1),u64(1469598103934665603ull),~u64(0)})
        for(unsigned at=0;at<bytes;++at){
            code[at]^=0x81;hash_check(code.data(),bytes,seed);
            code[at]^=0x81;hash_check(code.data(),bytes,seed);
        }
    hash_check(nullptr,0,7);
    std::array<u8,65568> long_program{};
    for(unsigned count:{2048u,1u,512u,0u,16u,1u,2049u,2u}) {
        for(unsigned i=0;i<count;++i) {
            auto* p=long_program.data()+i*32;
            word(p,(1u<<24)|(i+1==count));word(p+4,2);word(p+8,(i&1)?0x80000000u:0);
            word(p+12,0);
            for(unsigned c=0;c<4;++c)word(p+16+c*4,(i&1)?next():(0x7f800000u+c));
        }
        constants_check(long_program.data(),count*32);
        if(count)constants_check(long_program.data(),count*32-1);
        constants_check(nullptr,32);
    }
    usage_check(nullptr,0);usage_check(nullptr,8192);
    rsx_fp_constant_block invalid;
    memset(&invalid,0xff,sizeof invalid);
    buzz_renderer_clear_constants(&invalid);
    const rsx_fp_constant_block zero{};
    assert(!memcmp(&invalid,&zero,sizeof zero));
    SYSTEM_INFO info{};GetSystemInfo(&info);
    auto* memory=(u8*)VirtualAlloc(nullptr,32768+info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(memory);DWORD old;assert(VirtualProtect(memory+32768,info.dwPageSize,PAGE_NOACCESS,&old));
    for(unsigned bytes:{1u,15u,16u,17u,32u,112u,8192u,16384u}){
        auto* at=memory+32768-bytes;memcpy(at,code.data(),bytes);usage_check(at,bytes);hash_check(at,bytes,71);constants_check(at,bytes);
    }
    assert(VirtualFree(memory,0,MEM_RELEASE));
    puts("PASS shader usage: all opcodes/units/branch/EXP/END combinations, truncated constants, random mutations and guarded ends");
    puts("PASS vertex byte hash: all-byte mutations, seeds, repeated hits, malformed/oversized lengths and guarded ends");
    puts("PASS constants: complete block bytes, sanitizer counts, growing/shrinking/zero/truncated/branch programs, maximum and overflow slot counts");
}
