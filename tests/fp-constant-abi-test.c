#include "../ps3recomp/libs/video/rsx_fp_decompiler.h"
#include "../ps3recomp/libs/video/rsx_d3d12_fp_abi.h"
#include <assert.h>
#include <stdio.h>
static void word(u8* p, u32 h) {
    u32 be = (h << 16) | (h >> 16);
    p[0]=be>>24; p[1]=be>>16; p[2]=be>>8; p[3]=be;
}
int main(void) {
    assert(rsx_d3d12_fp_probe_matches("A2ABE53F,9C2A3DD5",0x9C2A3DD5));
    assert(rsx_d3d12_fp_probe_matches("all",0x12345678));
    assert(!rsx_d3d12_fp_probe_matches(NULL,0));
    assert(!rsx_d3d12_fp_probe_matches("A2ABE53Fjunk",0xA2ABE53F));
    assert(!rsx_d3d12_fp_probe_matches("A2ABE53F,9C2A3DD5",0x12345678));
    u8 program[32] = {0};
    word(program, (1u<<24) | (15u<<9) | 1u); // MOV r0, inline CONST, END
    word(program+4, 2u | (0u<<9) | (1u<<11) | (2u<<13) | (3u<<15) |
                    (7u<<18) | (1u<<23) | (2u<<25) | (3u<<27));
    char red[16384], blue[16384]; u32 count=0;
    word(program+16, 0x3f800000u); word(program+28, 0x3f800000u);
    u64 first_hash = rsx_fp_structural_hash(program, sizeof program, 123);
    assert(rsx_fp_decompile_buffered_ex(program, sizeof program, 0x40, 0, red, sizeof red, &count)>0);
    assert(count==1 && strstr(red,"fp_constants[0]"));
    assert(rsx_d3d12_fp_bind_constants(red,sizeof red,count)==0);
    word(program+16,0); word(program+24,0x3f800000u);
    assert(rsx_fp_structural_hash(program,sizeof program,123)==first_hash);
    assert(rsx_fp_decompile_buffered_ex(program, sizeof program, 0x40, 0, blue, sizeof blue, &count)>0);
    assert(rsx_d3d12_fp_bind_constants(blue,sizeof blue,count)==0);
    assert(!strcmp(red,blue)); // same PSO, different per-draw colors
    assert(strstr(red,"rsx_texscale[4]") && strstr(red,"rsx_alphatest;"));
    assert(strstr(red,"fp_constants[256]"));
    char saved[16384]; strcpy(saved,red);
    assert(rsx_d3d12_fp_bind_constants(red,sizeof red,RSX_D3D12_FP_CONSTANTS+1)<0 && !strcmp(red,saved));
    assert(rsx_d3d12_fp_bind_constants(red,32,1)<0 && !strcmp(red,saved));
    assert(rsx_d3d12_fp_alpha_test(red,sizeof red)==0);
    assert(strstr(red,"_rsx_func == 7") && strstr(red,"rsx_alphatest.x != 0"));
    assert(strstr(red,"_rsx_a >= _rsx_ref") && strstr(red,"discard;"));
    assert(rsx_d3d12_fp_debug_color(red,sizeof red,1,0,1)==0);
    assert(strstr(red,"return float4(1,0,1,1);")<strstr(red,"float4 r[48]"));
    char sampling[4096] =
        "rsx_tex[0].Sample(rsx_samp[0], float2(0.1, 0.2));\n"
        "rsx_tex1.SampleLevel(rsx_samp[1], saturate(float2(0.2, 0.3)), 4.0);\n"
        "rsx_tex[2].SampleBias(rsx_samp[2], float2(0.4, 0.5), 1.0);\n"
        "rsx_tex3.Sample(rsx_samp[3], float3(0,1,0));\n";
    assert(rsx_d3d12_fp_texture_scales(sampling,sizeof sampling,8)==3);
    assert(strstr(sampling,"rsx_texscale[0].xy"));
    assert(strstr(sampling,"rsx_texscale[1].xy, 4.0"));
    assert(strstr(sampling,"rsx_texscale[2].xy, 1.0"));
    assert(!strstr(sampling,"rsx_texscale[3]"));
    char short_buffer[64]="rsx_tex[0].Sample(rsx_samp[0], float2(0,1));";
    assert(rsx_d3d12_fp_texture_scales(short_buffer,strlen(short_buffer)+1,0)<0);
    puts("PASS: color changes share structural shader; D3D12 constants start at vector 5; bounded ABI");
    return 0;
}
