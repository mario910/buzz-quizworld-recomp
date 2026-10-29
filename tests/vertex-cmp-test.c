#include "../ps3recomp/libs/video/rsx_vertex_fetch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u8 memory[4096];
u8* vm_base=memory;
static int last_location;
u32 cellGcmResolveLocated(int location,u32 offset){last_location=location;return offset;}
u32 cellGcmResolveOffset(u32 offset){return offset;}
static void put(u8* p,u32 word){p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;}
static unsigned tested;
static void check(u32 word) {
    u8 bytes[4];float got[4];put(bytes,word);rsx_rd_cmp_be(bytes,got);
    const unsigned shifts[]={0,11,22},widths[]={11,11,10};
    for(unsigned k=0;k<3;++k) {
        unsigned expanded=((word>>shifts[k])&((1u<<widths[k])-1))<<(16-widths[k]);
        int signed16=expanded>=32768?(int)expanded-65536:(int)expanded;
        float expected=(float)signed16/32767.f;
        assert(isfinite(got[k]) && got[k]==expected);
    }
    assert(got[3]==1.f);++tested;
}
int main(void) {
    for(u32 v=0;v<2048;++v){check(v);check(v<<11);}
    for(u32 v=0;v<1024;++v)check(v<<22);
    u32 seed=17;
    for(unsigned n=0;n<100000;++n){seed=seed*1664525u+1013904223u;check(seed);}
    check(0x7FC00000u);check(0xFFC00000u); // valid CMP words, NaNs if read as float
    rsx_state* state=(rsx_state*)calloc(1,sizeof *state);assert(state);
    state->vertex_attribs[2].enabled=1;state->vertex_attribs[2].type=6;
    state->vertex_attribs[2].size=1;state->vertex_attribs[2].stride=4;
    state->vertex_attribs[2].offset=0x100;state->vertex_attribs[2].frequency=1;
    put(memory+0x100,0xFFC00000u);put(memory+0x104,0x3FFu|(0x400u<<11)|(0x1FFu<<22));
    float got[4],expected[4];rsx_rd_cmp_be(memory+0x104,expected);
    rsx_fetch_attrib(state,2,1,got);
    assert(last_location==1 && !memcmp(got,expected,sizeof got));
    state->vertex_attribs[2].offset|=0x80000000u;
    rsx_fetch_attrib(state,2,1,got);
    assert(last_location==0 && !memcmp(got,expected,sizeof got));
    state->vertex_attribs[2].enabled=0;
    state->vertex_data4f[2][0]=.25f;state->vertex_data4f[2][3]=.75f;
    rsx_fetch_attrib(state,2,1,got);assert(got[0]==.25f && got[3]==.75f);
    free(state);
    printf("PASS: %u packed normal cases, BE decoding, size=1 expansion, stride, LOCAL/MAIN and disabled attributes\n",tested);
}
