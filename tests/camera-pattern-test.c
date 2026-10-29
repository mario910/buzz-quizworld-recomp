#include "../ps3recomp/libs/hardware/cell_camera_pattern.h"
#include <stdio.h>
#include <assert.h>
#include <time.h>
static unsigned char a[640*480],b[640*480];
static void reference(unsigned char*dst,uint32_t t){
    int bx=(t*5u)%(640-96),by=(t*3u)%(480-96);
    for(int y=0;y<480;y++)for(int x=0;x<640;x++){
        unsigned r=x*255/640,g=y*255/480,z=255-r;
        if(x>=bx&&x<bx+96&&y>=by&&y<by+96)r=g=z=250;
        dst[y*640+x]=(y&1)?((x&1)?g:z):((x&1)?r:g);
    }
}
int main(void){
    uint32_t seed=0x12345678;
    for(unsigned n=0;n<1200;++n){
        seed=seed*1664525u+1013904223u;
        uint32_t frame=n<600?n:seed;
        reference(a,frame);camera_pattern_fill(b,frame);assert(!memcmp(a,b,sizeof a));
    }
    reference(a,UINT32_MAX);camera_pattern_fill(b,UINT32_MAX);assert(!memcmp(a,b,sizeof a));
    puts("PASS: 1201 complete camera frames including uint32 wrap; all 307200 bytes equal");
    return 0;
}
