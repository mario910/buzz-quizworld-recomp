// Native CPU benchmark using the SDK's synthetic recording backend. No assets
// or GPU: measures complete draw preparation, not game frame rate.
#define main sdk_renderer_tests_main
#include "../ps3recomp/libs/video/tests/test_rsx_draw_engine.c"
#undef main
#include <assert.h>
#include <time.h>

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1000+(double)t.tv_nsec/1000000;
}
int main(int argc,char** argv) {
    const int profile=argc>1?atoi(argv[1]):0;
    assert(profile>=0&&profile<=3);
    vm_base=(u8*)calloc(1,GUEST_BYTES); assert(vm_base);ppu_vm_size=GUEST_BYTES;
    for(u32 n=0;n<12;++n){u8* p=vm_base+VTX_OFFSET+n*4;p[0]=0x3f;p[1]=0x80;}
    const unsigned sizes[]={1,16,64,128};
    for(unsigned shape=0;shape<4;++shape){
        engine_up();
        u8* fp=vm_base+0x30000;memset(fp,0,8192);
        for(unsigned n=0;n<sizes[shape];++n){
            program_word(fp+n*32,(1u<<24)|(0xfu<<9)|(n+1==sizes[shape]));
            program_word(fp+n*32+4,2u|(1u<<11)|(2u<<13)|(3u<<15)|(7u<<18)|(1u<<23)|(2u<<25)|(3u<<27));
            for(unsigned c=0;c<4;++c)program_float(fp+n*32+16+c*4,0.25f*(c+1));
        }
        program_vertex_input(0);m(0x08e4,0x30000|2u);m(0x1d60,0x40);
        if(profile==2) {
            program_word(fp,(0x17u<<24)|(0xfu<<9)|(sizes[shape]==1));
            bind_texture_unit(RSX_LOCATION_MAIN,TEX_OFFSET);
            memset(vm_base+TEX_OFFSET,0x55,64);
        }
        if(profile==3) {
            const u32 source=2u|(1u<<12)|(2u<<10)|(3u<<8);
            m(0x1e9c,0);
            for(unsigned n=0;n<64;++n) {
                m(0x0b80,(1u<<30)|(63u<<15));m(0x0b84,(1u<<22)|((source>>9)&0xff));
                m(0x0b88,(source&0x1ff)<<23);m(0x0b8c,(n==63)|(0xfu<<13));
            }
        }
        draw_triangle();rsx_draw_engine_present();assert(stub.n_pipeline_create==1);
        for(unsigned pass=0;pass<5;++pass){
            const int start_draws=stub.n_draw;const double start=now_ms();
            for(unsigned n=0;n<10000;++n){
                if(profile==1)program_float(fp+16,(n&1)?0.5f:0.25f);
                draw_triangle();if(!(n&127))rsx_draw_engine_present();
            }
            const double elapsed=now_ms()-start;
            assert(stub.n_draw-start_draws==10000&&stub.n_pipeline_create==1);
            assert(stub.first_fp_constant[0]==(profile==1?0.5f:0.25f));
            printf("profile=%d instructions=%u pass=%u draws=10000 ms=%.3f\n",profile,sizes[shape],pass,elapsed);fflush(stdout);
        }
        engine_down();
    }
    free(vm_base);return 0;
}
