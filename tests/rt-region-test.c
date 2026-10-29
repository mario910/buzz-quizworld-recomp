#include "../ps3recomp/libs/video/rsx_rt_region.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    rsx_rt_rect r;
    /* The actual captured two-player atlas offsets; these are layouts, not
     * version-specific address exceptions in the renderer. */
    assert(rsx_rt_region(0x060F0000,400,200,1600,4,0x060F0190,100,100,1600,&r));
    assert(r.x==100&&r.y==0&&r.w==100&&r.h==100);
    assert(rsx_rt_region(0x060F0000,400,200,1600,4,0x060F0320,100,100,1600,&r));
    assert(r.x==200&&r.y==0);
    assert(rsx_rt_region(0x06930000,1024,256,4096,4,0x06930800,256,64,4096,&r));
    assert(r.x==512&&r.y==0);
    assert(rsx_rt_region(0x10000,400,200,1664,4,0x10000+1664*100+1200,100,100,1664,&r));
    assert(r.x==300&&r.y==100);
    assert(!rsx_rt_region(0x10000,400,200,1664,4,0x10000+1600,1,1,1664,&r)); /* row padding */
    assert(!rsx_rt_region(0x10000,400,200,1600,4,0x10001,100,100,1600,&r));
    assert(!rsx_rt_region(0x10000,400,200,1600,4,0x10000,100,100,400,&r)); /* another layout */
    assert(!rsx_rt_region(0x10000,400,200,1600,4,0xFFFF,100,100,1600,&r));
    assert(!rsx_rt_region(0x10000,400,200,1600,4,0x10000+1204,100,100,1600,&r));
    assert(!rsx_rt_region(0xFFFFFF00,400,200,1600,4,0xFFFFFF00,100,100,1600,&r));
    assert(!rsx_rt_region(0,1,1,0,0,0,1,1,0,&r));

    /* Two independent viewports/scissors share one surface. Clearing the
     * second must leave the first player's pixels untouched. */
    unsigned char atlas[1024*256];memset(atlas,7,sizeof atlas);
    r=rsx_rt_clear_rect(1024,256,256,0,256,64);
    for(unsigned y=r.y;y<r.y+r.h;++y)memset(atlas+y*1024+r.x,1,r.w);
    r=rsx_rt_clear_rect(1024,256,512,0,256,64);
    for(unsigned y=r.y;y<r.y+r.h;++y)memset(atlas+y*1024+r.x,2,r.w);
    assert(atlas[256]==1&&atlas[511]==1&&atlas[512]==2&&atlas[767]==2&&atlas[768]==7);
    r=rsx_rt_clear_rect(400,200,0,0,4096,4096);assert(r.w==400&&r.h==200);
    r=rsx_rt_clear_rect(400,200,300,100,UINT32_MAX,UINT32_MAX);assert(r.w==100&&r.h==100);
    r=rsx_rt_clear_rect(400,200,400,0,10,10);assert(!r.w&&!r.h);
    r=rsx_rt_clear_rect(400,200,0,0,0,100);assert(!r.w&&!r.h);
    puts("PASS: atlas origin/stride/bounds/overflow, independent player cells and scoped clears");
}
