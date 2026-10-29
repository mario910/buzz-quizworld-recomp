/* Read-only shader inspection; no game execution or GPU needed. */
#include "../ps3recomp/libs/video/rsx_fp_decompiler.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char** argv) {
    if(argc<2)return 2;
    FILE* f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 2;}
    u8 bytes[16384];u32 size=(u32)fread(bytes,1,sizeof bytes,f);fclose(f);
    static char hlsl[RSX_FP_HLSL_BUFFER_SIZE];
    u32 control=argc>2?(u32)strtoul(argv[2],NULL,0):0x40;
    int count=rsx_fp_decompile(bytes,size,control,hlsl,sizeof hlsl);
    if(count<=0)return 1;
    puts(hlsl);return 0;
}
