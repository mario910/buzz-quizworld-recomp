/* Compare against the pinned engine's actual private mask helpers, not a
 * second implementation of their rules. Unused engine sections are discarded. */
#include "../ps3recomp/libs/video/rsx_draw_engine.c"
#include "renderer_texture_snapshot.h"
#include <assert.h>
static u32 random_state=0x8791b65u;
static u32 next(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static u32 create3d(void* user,rsx_be_format fmt,u32 w,u32 h,u32 depth,u32 mips,u32 remap,u32 rsx_fmt){return 1;}
static void upload3d(void* user,u32 texture,u32 z,u32 mip,u32 w,u32 h,const void* src,u32 row_bytes){}
int main(void) {
    rsx_draw_backend backend={0};g.be=&backend;
    buzz_texture_snapshot snapshot;
    unsigned cubes=0,volumes=0,unnormalized=0;
    for(unsigned round=0;round<10000;++round) {
        /* Include every capability combination and changes between draws. */
        backend.texture_create_3d=(round&1)?create3d:NULL;
        backend.texture_upload_3d=(round&2)?upload3d:NULL;
        for(u32 unit=0;unit<16;++unit) {
            const u32 base=(0x1a00+unit*32)/4;
            for(unsigned word=0;word<8;++word)g.rsx.regs[base+word]=next();
            g.rsx.regs[(0x1840+unit*4)/4]=next();
            /* Random-only inputs rarely produce valid cubes/volumes. */
            if(round%3==0)g.rsx.regs[base+6]=(4u<<16)|4u;
            if(round%3==1)g.rsx.regs[base+1]=(next()&~0xf4u)|(3u<<4);
            if(round%7==0)g.rsx.regs[base+6]=0;
        }
        buzz_texture_snapshot_init(&snapshot,&g.rsx,
            backend.texture_create_3d && backend.texture_upload_3d);
        assert(snapshot.cube==eng_cube_mask());
        assert(snapshot.volume==eng_volume_mask());
        u32 actual[16][2],expected[16][2];
        assert(buzz_texture_snapshot_dimensions(&snapshot,actual)==eng_unnorm_units(expected));
        assert(!memcmp(actual,expected,sizeof actual));
        for(u32 unit=0;unit<16;++unit) {
            rsx_dsp_texture reference;rsx_dsp_get_texture(&g.rsx,unit,&reference);
            assert(!memcmp(&reference,&snapshot.texture[unit],sizeof reference));
        }
        cubes+=!!snapshot.cube;volumes+=!!snapshot.volume;unnormalized+=!!snapshot.unnormalized;
    }
    assert(cubes && volumes && unnormalized);
    puts("PASS texture snapshot: 10000 changing register sets, all 16 units, four backend capability combinations, SDK mask oracle");
}
