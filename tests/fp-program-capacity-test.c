#include "../ps3recomp/libs/video/rsx_fp_decompiler.h"
#include "../ps3recomp/libs/video/rsx_d3d12_fp_abi.h"
#include <assert.h>
#include <stdio.h>
static void word(u8* p,u32 h) {
    u32 be=(h<<16)|(h>>16);
    p[0]=be>>24;p[1]=be>>16;p[2]=be>>8;p[3]=be;
}
int main(void) {
    enum {COUNT=RSX_D3D12_FP_CONSTANTS,BYTES=COUNT*32};
    u8 code[BYTES]; memset(code,0,sizeof code);
    for(unsigned i=0;i<COUNT;++i) {
        word(code+i*32,(1u<<24)|(15u<<9)|(i==COUNT-1));
        word(code+i*32+4,2u|(1u<<11)|(2u<<13)|(3u<<15)|
                            (7u<<18)|(1u<<23)|(2u<<25)|(3u<<27));
        for(unsigned lane=0;lane<4;++lane)
            word(code+i*32+16+lane*4,0x3f000000u+i*4+lane);
    }
    assert(BYTES>4096 && BYTES<=RSX_D3D12_FP_CODE_BYTES);
    assert(rsx_fp_program_size(code,4096)==0); /* reproduce former truncation */
    assert(rsx_fp_program_size(code,sizeof code)==BYTES);
    rsx_fp_constant_block constants;
    assert(rsx_fp_collect_constants(code,4096,&constants)<0);
    assert(rsx_fp_collect_constants(code,sizeof code,&constants)==COUNT);
    static char hlsl[RSX_FP_HLSL_BUFFER_SIZE]; u32 count=0;
    assert(rsx_fp_decompile_buffered_ex(code,sizeof code,0x40,0,hlsl,sizeof hlsl,&count)>0);
    assert(count==COUNT && strstr(hlsl,"fp_constants[255]"));
    assert(rsx_d3d12_fp_bind_constants(hlsl,sizeof hlsl,count)==0);
    assert(rsx_d3d12_fp_texture_scales(hlsl,sizeof hlsl,0)>=0);
    assert(rsx_d3d12_fp_alpha_test(hlsl,sizeof hlsl)==0);
    struct {u32 before;float values[RSX_D3D12_FP_CONSTANTS*4];u32 after;} upload;
    upload.before=0x12345678;upload.after=0xfedcba98;
    assert(rsx_fp_extract_consts(code,sizeof code,upload.values,RSX_D3D12_FP_CONSTANTS)==COUNT);
    assert(upload.before==0x12345678 && upload.after==0xfedcba98);
    for(unsigned i=0;i<COUNT*4;++i) {
        u32 bits;memcpy(&bits,&upload.values[i],4);assert(bits==0x3f000000u+i);
    }
    assert(RSX_D3D12_FP_CB_BYTES%256==0);
    assert((5u+RSX_D3D12_FP_CONSTANTS)*16<=RSX_D3D12_FP_CB_BYTES);
    assert(rsx_d3d12_fp_guest_bound(0xfffffff0u)==16);
    assert(rsx_d3d12_fp_guest_bound(0u)==RSX_D3D12_FP_CODE_BYTES);
    u64 hash=rsx_fp_structural_hash(code,sizeof code,123);
    word(code+16,0x3f800000);
    assert(rsx_fp_structural_hash(code,sizeof code,123)==hash);
    puts("PASS: 8192-byte shader / 256 constants, exact upload, ABI, truncation and address bounds");
    return 0;
}
