/* Synthetic shaders/clear only: no retail assets, guest executable or firmware. */
#include "rsx_vulkan_backend.h"
#include "rsx_shader_spirv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned char* vm_base=NULL;
// Windows' diagnostic SPU-import backtrace expects the lifted table. This
// asset-free fixture has no lifted functions; it never imports a guest image.
struct smoke_fentry {uint64_t address;void* function;const char* name;};
const struct smoke_fentry function_table[]={{0,NULL,NULL}};
const uint64_t function_table_count=0;
static void env(const char* name,const char* value){
#ifdef _WIN32
    _putenv_s(name,value);
#else
    setenv(name,value,1);
#endif
}
static void remove_env(const char* name){
#ifdef _WIN32
    _putenv_s(name,"");
#else
    unsetenv(name);
#endif
}
int main(int argc,char** argv){
    const int no_readback=argc==2&&!strcmp(argv[1],"--window-no-readback");
    const int force_dump=argc==3&&!strcmp(argv[1],"--dump-forces-readback");
    const int window=no_readback || (argc==2&&!strcmp(argv[1],"--window"));
    const char* dump=argc==3&&(!strcmp(argv[1],"--dump-roundtrip")||force_dump)?argv[2]:NULL;
    if(argc!=1&&!window&&!dump){fprintf(stderr,"Unknown smoke test option\n");return 1;}
    if(!dump)remove_env("PS3RECOMP_VK_DUMP");
    env("PS3RECOMP_VK_DRAW_ASYNC","0");
    if(dump){
        FILE* existing=fopen(dump,"rb");
        if(existing){fclose(existing);fprintf(stderr,"Refusing to replace an existing smoke dump\n");return 1;}
        env("PS3RECOMP_VK_DUMP",dump);env("PS3RECOMP_VK_DUMP_EVERY","1");
    }
    env("PS3RECOMP_VK_WINDOW",window?"1":"0");
    env("PS3RECOMP_VK_PRESENT_READBACK",(no_readback||force_dump)?"0":"1");
    env("RSX_WINDOW_BACKGROUND","1");
    env("PS3RECOMP_VK_GUEST_PROGRAMS","0");env("PS3RECOMP_RSX_ENGINE","vtable");
    if(!rsx_hlsl_to_spirv_available()){fprintf(stderr,"Shader translator is a stub\n");return 1;}
    static const char vs[]="float4 main(float4 p : ATTR0) : SV_Position { return p; }";
    static const char ps[]="float4 main() : SV_Target0 { return float4(1, 0, 0, 1); }";
    unsigned int words[4096],count=0;char log[1024]={0};
    if(rsx_hlsl_to_spirv(vs,RSX_SHADER_STAGE_VERTEX,words,4096,&count,log,sizeof(log))||!count||words[0]!=0x07230203){fprintf(stderr,"VS translation failed: %s\n",log);return 1;}
    if(rsx_hlsl_to_spirv(ps,RSX_SHADER_STAGE_FRAGMENT,words,4096,&count,log,sizeof(log))||!count){fprintf(stderr,"PS translation failed: %s\n",log);return 1;}
    if(rsx_vulkan_backend_init(96,64,"Buzz Vulkan synthetic test"))return 1;
    if(window&&!rsx_vulkan_backend_windowed()){
        fprintf(stderr,"Windowed Vulkan test fell back to headless\n");rsx_vulkan_backend_shutdown();return 1;
    }
    rsx_backend* backend=rsx_get_backend();
    if(!backend||!backend->clear){rsx_vulkan_backend_shutdown();return 1;}
    const unsigned int colors[]={0xff123456,0xffd04020,0xff20a050};
    for(unsigned i=0;i<3;++i){
        backend->clear(backend->userdata,0xf0,colors[i],1.0f,0);
        rsx_vulkan_backend_present();
        if(rsx_vulkan_backend_presented_frames()!=(window?i+1u:0u)){
            fprintf(stderr,"Actual window presentation counter mismatch\n");rsx_vulkan_backend_shutdown();return 1;
        }
        const unsigned int pixel=rsx_vulkan_backend_presented_center();
        if(pixel!=(no_readback?0u:colors[i])){fprintf(stderr,"Presentation diagnostic mismatch: %08x\n",pixel);rsx_vulkan_backend_shutdown();return 1;}
        if(rsx_vulkan_backend_pump_messages()){rsx_vulkan_backend_shutdown();return 1;}
    }
    rsx_vulkan_backend_shutdown();
    if(dump){
        FILE* f=fopen(dump,"rb");char header[32]={0};
        const char* expected="P6\n96 64\n255\n";
        if(!f||fread(header,1,strlen(expected),f)!=strlen(expected)||strcmp(header,expected)){
            if(f)fclose(f);fprintf(stderr,"Invalid smoke PPM header\n");return 1;
        }
        for(unsigned i=0;i<96*64;++i){
            unsigned char rgb[3];
            if(fread(rgb,1,3,f)!=3||rgb[0]!=0x20||rgb[1]!=0xa0||rgb[2]!=0x50){
                fclose(f);fprintf(stderr,"Smoke RGB dump differs from the rendered clear\n");return 1;
            }
        }
        const int complete=fgetc(f)==EOF&&!ferror(f);fclose(f);
        if(!complete){fprintf(stderr,"Trailing smoke PPM data\n");return 1;}
        if(remove(dump)){fprintf(stderr,"Cannot remove synthetic smoke dump\n");return 1;}
        puts("PASS row-packed PPM has the exact dimensions, size and RGB pixels");
    }
    puts(no_readback?"PASS three actual window presents with explicit diagnostic suppression (no fake pixel)":
        "PASS native Vulkan device, three GPU clears/readbacks and built-in HLSL-to-SPIR-V compiler");return 0;
}
