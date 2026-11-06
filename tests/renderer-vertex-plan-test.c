#include "renderer_vertex_plan.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
static rsx_dispatch rsx;
static rsx_vertex_fetch_plan a,b;
static u32 rng=0x5abde71;
static u32 next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static u8 stream[16384];
static volatile u64 checksum;
static void compare_plans(void) {
    rsx_vertex_fetch_plan ac=a,bc=b;
    for(u32 attr=0;attr<16;++attr)if(!(a.layout.mask&(1u<<attr))) {
        memset(&ac.attr[attr],0,sizeof ac.attr[attr]);memset(&bc.attr[attr],0,sizeof bc.attr[attr]);
    }
    assert(!memcmp(&ac,&bc,sizeof ac));
}
int main(int argc,char**argv) {
    for(u32 mask=0;mask<65536;++mask) {
        rsx_vertex_layout_plan layout;rsx_vertex_layout_plan_init(&layout,mask);
        for(u32 attr=0;attr<16;++attr) {
            rsx.regs[(0x1740+attr*4)/4]=next();rsx.regs[(0x1680+attr*4)/4]=next();
            for(u32 c=0;c<4;++c)rsx.regs[(0x1c00+attr*16+c*4)/4]=mask%3?next():0;
        }
        rsx.regs[0x1fc0/4]=next();
        memset(&a,0xa5,sizeof a);memset(&b,0x5a,sizeof b);
        rsx_vertex_fetch_plan_init(&a,&rsx,&layout,NULL,NULL);
        buzz_vertex_plan_init(&b,&rsx,&layout,NULL,NULL);
        compare_plans();
        /* Unselected attributes still contribute to inline offsets/stride. */
        rsx_vertex_fetch_plan_set_inline(&a,&rsx,stream,sizeof stream);
        rsx_vertex_fetch_plan_set_inline(&b,&rsx,stream,sizeof stream);
        const rsx_vertex_ref refs[]={{0,0},{1,0},{15,0},{31,0}};
        rsx_vertex_fetch_plan_prepare(&a,refs,4);rsx_vertex_fetch_plan_prepare(&b,refs,4);
        compare_plans();
        u8 oa[1024],ob[1024];memset(oa,0xa5,sizeof oa);memcpy(ob,oa,sizeof oa);
        assert(rsx_vertex_fetch_many(&a,refs,4,oa)==rsx_vertex_fetch_many(&b,refs,4,ob));
        assert(!memcmp(oa,ob,sizeof oa));
    }
    puts("PASS vertex plan: all 65536 input masks, arbitrary descriptors/default bits, unused inline attributes, prepared plans and fetched bytes");
    if(argc>1 && !strcmp(argv[1],"--bench"))for(u32 mask=7;mask<=65535;mask=mask==7?255:mask==255?65535:65536) {
        rsx_vertex_layout_plan layout;rsx_vertex_layout_plan_init(&layout,mask);
        for(int round=0;round<4;++round) {
            const int fast=round==1||round==2;const clock_t start=clock();
            for(u32 n=0;n<500000;++n) {
                if(fast)buzz_vertex_plan_init(&b,&rsx,&layout,NULL,NULL);
                else rsx_vertex_fetch_plan_init(&b,&rsx,&layout,NULL,NULL);
                checksum+=b.attr[0].stride;
            }
            printf("bench attrs=%u fast=%d ms=%.3f\n",layout.count,fast,1000.0*(clock()-start)/CLOCKS_PER_SEC);
        }
    }
    return 0;
}
