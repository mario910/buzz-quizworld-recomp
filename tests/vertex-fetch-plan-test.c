#include "../ps3recomp/libs/video/rsx_vertex_fetch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static u8 memory[16*1024*1024+64];
u8* vm_base=memory;
static unsigned resolves,mapping_generation;
/* Deliberately noncontiguous main/local pages, including nonzero EA bias. */
u32 cellGcmResolveLocated(int local,u32 offset)
{
    ++resolves;u32 page=offset>>20;
    return (((page*7u+(local?3u:1u)+mapping_generation)&15u)<<20)+(offset&0xfffffu);
}
u32 cellGcmResolveOffset(u32 offset){return cellGcmResolveLocated(0,offset);}
static u32 random_word(void){static u32 s=0x31419373;s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static volatile unsigned checksum;
int main(void)
{
    rsx_state* state=calloc(1,sizeof *state);rsx_vertex_cache* cache=malloc(sizeof *cache);assert(state && cache);
    for(unsigned i=0;i<sizeof memory;++i)memory[i]=(u8)random_word();
    unsigned tested=0;
    for(unsigned round=0;round<6000;++round){
        ++mapping_generation;
        for(unsigned n=0;n<16;++n){rsx_vertex_attrib* a=&state->vertex_attribs[n];
            a->enabled=random_word()&1;a->stride=(random_word()%9)*4;
            a->type=random_word()%12;a->size=random_word()%8;
            a->frequency=random_word()%7;
            a->offset=(random_word()&0x80ffffffu);
            if(round%8==0)a->offset=(a->offset&0x80000000u)|0xffff0u; /* Cross a page, including vector loads. */
            for(unsigned k=0;k<4;++k){u32 word=random_word();memcpy(&state->vertex_data4f[n][k],&word,4);}
        }
        state->frequency_divider_op=random_word();
        rsx_vertex_draw_plan plan;rsx_vertex_draw_plan_init(&plan,state);rsx_vertex_cache_init_planned(cache,state);
        for(unsigned k=0;k<32;++k){u32 vi=k==31?0xffffffffu:random_word()%1024;
            float expected[16][4],actual[16][4],cached[16][4];
            rsx_fetch_vertex(state,vi,expected);rsx_fetch_vertex_planned(&plan,vi,actual);rsx_fetch_vertex_cached(cache,vi,cached);
            assert(!memcmp(expected,actual,sizeof actual));assert(!memcmp(expected,cached,sizeof cached));++tested;
        }
    }
    memset(state->vertex_attribs,0,sizeof state->vertex_attribs);
    for(unsigned n=0;n<3;++n){rsx_vertex_attrib* a=&state->vertex_attribs[n];a->enabled=1;a->type=2;a->size=4;a->stride=16;a->offset=n*0x10000;}
    rsx_vertex_draw_plan plan;rsx_vertex_draw_plan_init(&plan,state);
    float out[16][4],expected[16][4];resolves=0;
    for(unsigned i=0;i<1000;++i)rsx_fetch_vertex_planned(&plan,i,out);
    assert(resolves==3); /* One resolver per attribute, not per vertex. */
    unsigned before=resolves;memory[cellGcmResolveLocated(1,0)+0]^=0x80;
    rsx_fetch_vertex_planned(&plan,0,out);rsx_fetch_vertex(state,0,expected);assert(!memcmp(out,expected,sizeof out));
    assert(resolves==before+4); /* No data cache in the plan. */
    ++mapping_generation;rsx_vertex_draw_plan_init(&plan,state);rsx_fetch_vertex_planned(&plan,0,out);rsx_fetch_vertex(state,0,expected);
    assert(!memcmp(out,expected,sizeof out)); /* Mapping changes become visible at next draw. */
    vm_base=NULL;rsx_fetch_vertex_planned(&plan,0,out);assert(!memcmp(out,state->vertex_data4f,sizeof out));vm_base=memory;
    /* Isolated microbenchmark, never advertised as a game FPS measurement. */
    double times[2];
    for(unsigned mode=0;mode<2;++mode){rsx_vertex_draw_plan_init(&plan,state);clock_t start=clock();unsigned sum=0;
        for(unsigned i=0;i<500000;++i){if(mode)rsx_fetch_vertex_planned(&plan,i&4095,out);else rsx_fetch_vertex(state,i&4095,out);
            u32 bits;memcpy(&bits,out[0],4);sum^=bits;}checksum=sum;times[mode]=(double)(clock()-start)/CLOCKS_PER_SEC;}
    printf("PASS: %u bitwise full-vertex comparisons, all types, sparse constants, divisors, noncontiguous pages, wrap, draw reset and live RAM\n",tested);
    printf("Isolated fetch benchmark: reference %.3f ms, plan %.3f ms (not game FPS)\n",times[0]*1000,times[1]*1000);
    free(cache);free(state);return 0;
}
