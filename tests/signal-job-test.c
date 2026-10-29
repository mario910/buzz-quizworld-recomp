/* Execute the actual lifted completion job against isolated synthetic RAM. */
#define main stack_test_unused_main
#include "../ps3recomp/runtime/spu/tests/test_spurs_job_stack.c"
#undef main
extern void buzz_signal_spu_func_00000010(spu_context*);
extern void buzz_signal_spu_recomp_register(void);
#define FLAG_EA 0x40000u
#define MANAGER_EA 0x40100u
static void setup(const char* binary,int frame_completion){
    memset(g_guest_mem,0,sizeof g_guest_mem);
    FILE* f=fopen(binary,"rb");if(!f){perror(binary);exit(2);}
    size_t size=fread(g_guest_mem+BIN_EA,1,0x1000,f);fclose(f);
    put32(JOB_EA+4,BIN_EA);put16(JOB_EA+8,(uint16_t)(size>>4));
    put32(JOB_EA+0x34,FLAG_EA);
    put32(JOB_EA+0x3C,frame_completion?0:MANAGER_EA);
    put32(JOB_EA+0x44,frame_completion?0:9);
    put32(JOB_EA+0x4C,frame_completion?1:0);
    g_guest_mem[FLAG_EA+0xE]=1;
    g_guest_mem[MANAGER_EA+0xE]=1;
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    spu_begin_image(32);buzz_signal_spu_recomp_register();spu_begin_image(0);
    for(int frame=0;frame<2;++frame){
        setup(argv[1],frame);
        spu_run_spurs_job(buzz_signal_spu_func_00000010,32,JOB_EA,256);
        unsigned events=(g_guest_mem[FLAG_EA]<<8)|g_guest_mem[FLAG_EA+1];
        unsigned manager=(g_guest_mem[MANAGER_EA]<<8)|g_guest_mem[MANAGER_EA+1];
        printf("signal case=%d events=%04X manager=%04X\n",frame,events,manager);
        if(events!=(frame?2:1) || (!frame && manager!=0x200))return 1;
    }
    puts("PASS: real SPU completion job sets builtin and frame-completion flags");
    return 0;
}
