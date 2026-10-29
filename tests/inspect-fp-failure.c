/* Offline reproduction of the production decode/ABI adaptation stages. */
#include "../ps3recomp/libs/video/rsx_fp_decompiler.h"
#include "../ps3recomp/libs/video/rsx_d3d12_fp_abi.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv) {
    if(argc<2)return 2;
    FILE* file=fopen(argv[1],"rb"); if(!file)return 2;
    static u8 bytes[16384]; static char hlsl[RSX_FP_HLSL_BUFFER_SIZE];
    u32 size=(u32)fread(bytes,1,sizeof bytes,file); fclose(file);
    u32 ctrl=argc>2?(u32)strtoul(argv[2],NULL,0):0;
    u32 cube=argc>3?(u32)strtoul(argv[3],NULL,0):0;
    rsx_fp_constant_block constants;
    int collected=rsx_fp_collect_constants(bytes,size,&constants);
    u32 count=0;
    int n=rsx_fp_decompile_buffered_ex(bytes,size,ctrl,cube,hlsl,sizeof hlsl,&count);
    int bound=n>0?rsx_d3d12_fp_bind_constants(hlsl,sizeof hlsl,count):-2;
    int scaled=bound>=0?rsx_d3d12_fp_texture_scales(hlsl,sizeof hlsl,cube):-2;
    int alpha=scaled>=0?rsx_d3d12_fp_alpha_test(hlsl,sizeof hlsl):-2;
    printf("bytes=%u program=%u collected=%d partial_constants=%u decoded=%d constants=%u bound=%d scales=%d alpha=%d\n",
           size,rsx_fp_program_size(bytes,size),collected,constants.count,n,count,bound,scaled,alpha);
    return n>0 && bound>=0 && scaled>=0 && alpha>=0?0:1;
}
