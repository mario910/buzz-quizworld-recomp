#include "rsx_vertex_compact.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif
static unsigned fail_at,allocations;
static void* checked_realloc(void* ptr,size_t n){if(fail_at && ++allocations==fail_at)return NULL;return realloc(ptr,n);}
#define realloc checked_realloc
#include "renderer_index_packed_fusion.h"
static buzz_index_packed_scratch scratch;
static int buzz_index_fuse_u16(rsx_vertex_remap* m,rsx_vertex_ref** r,u32* cap,const u8* b,u32 n,u32 base,u32* unique) {return buzz_index_packed_u16(&scratch,m,r,cap,b,n,base,unique);}
#undef realloc
static unsigned seed=0x9e3779b9;
static unsigned rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static rsx_vertex_ref original[65536];
static u8 input[131072];
static void compare(rsx_vertex_remap* reference,rsx_vertex_remap* fused,rsx_vertex_ref** refs,u32* capacity,const u8* bytes,u32 n,u32 base) {
    for(u32 i=0;i<n;i++)original[i]=(rsx_vertex_ref){(bytes[2*i]<<8)|bytes[2*i+1],base};
    u32 a,b;
    assert(rsx_vertex_remap_build(reference,original,n,&a));
    assert(buzz_index_fuse_u16(fused,refs,capacity,bytes,n,base,&b));
    assert(a==b && !memcmp(original,*refs,a*sizeof(*original)));
    assert(!memcmp(reference->occurrence_to_unique,fused->occurrence_to_unique,n*sizeof(u32)));
    buzz_index_span spans[256];u32 at=0,nb=0;
    while(at<n && nb<255) {
        const u32 left=n-at,take=left>257?257:left;
        spans[nb++]=(buzz_index_span){bytes+2*at,take};at+=take;
    }
    spans[nb++]=(buzz_index_span){bytes+2*at,n-at};
    assert(buzz_index_packed_spans(&scratch,fused,refs,capacity,spans,nb,base,&b));
    assert(a==b && !memcmp(original,*refs,a*sizeof(*original)));
    assert(!memcmp(reference->occurrence_to_unique,fused->occurrence_to_unique,n*sizeof(u32)));
}
int main(void) {
    rsx_vertex_remap reference={0},fused={0};rsx_vertex_ref* refs=NULL;u32 capacity=0;
    const u32 counts[]={4,5,6,7,8,9,15,16,17,31,64,127,256,1024,65535,65536};
    for(unsigned draw=0;draw<2000;draw++) {
        const u32 n=counts[draw%16],base=rnd();
        for(u32 i=0;i<n;i++){const u32 v=draw%3?rnd()%127:rnd()%65536;input[2*i]=(u8)(v>>8);input[2*i+1]=(u8)v;}
        if(draw%19==0)scratch.generation=65535;
        compare(&reference,&fused,&refs,&capacity,input,n,base);
        // Alternate SDK and packed remaps sharing output buffers but private stamps.
        for(u32 i=0;i<32;i++)refs[i]=(rsx_vertex_ref){(i*3)%7,base};
        u32 unique;assert(rsx_vertex_remap_build(&fused,refs,32,&unique));
        compare(&reference,&fused,&refs,&capacity,input,n,base);
    }
    for(u32 i=0;i<65536;++i){input[2*i]=(u8)(i>>8);input[2*i+1]=(u8)i;}
    compare(&reference,&fused,&refs,&capacity,input,65536,0);
    scratch.generation=65535;compare(&reference,&fused,&refs,&capacity,input,65536,UINT_MAX);
    memset(input,255,sizeof input);compare(&reference,&fused,&refs,&capacity,input,17,UINT_MAX);
    for(unsigned fail=1;fail<=2;fail++) {
        rsx_vertex_remap m={0};rsx_vertex_ref* r=NULL;u32 cap=0,n=0x12345678;
        fail_at=fail;allocations=0;
        assert(!buzz_index_fuse_u16(&m,&r,&cap,input,17,0,&n) && n==0x12345678);
        fail_at=0;compare(&reference,&m,&r,&cap,input,17,0);rsx_vertex_remap_destroy(&m);free(r);
    }
#ifndef _WIN32
    const size_t page=sysconf(_SC_PAGESIZE);u8* pages=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(pages!=MAP_FAILED && !mprotect(pages+page,page,PROT_NONE));
    for(u32 n=4;n<70;n++){u8* p=pages+page-2*n;for(u32 i=0;i<2*n;i++)p[i]=(u8)rnd();compare(&reference,&fused,&refs,&capacity,p,n,0xfffffff0);}
    assert(!munmap(pages,page*2));
#endif
    buzz_index_span bad[2]={{input,4},{NULL,4}};u32 untouched=77;
    assert(!buzz_index_packed_spans(&scratch,&fused,&refs,&capacity,bad,2,0,&untouched) && untouched==77);
    bad[1]=(buzz_index_span){input,65536};
    assert(!buzz_index_packed_spans(&scratch,&fused,&refs,&capacity,bad,2,0,&untouched) && untouched==77);
    bad[0]=(buzz_index_span){NULL,0};bad[1]=(buzz_index_span){input,4};
    assert(buzz_index_packed_spans(&scratch,&fused,&refs,&capacity,bad,2,0,&untouched));
    u32 n=77;assert(!buzz_index_fuse_u16(&fused,&refs,&capacity,input,3,0,&n) && n==77);
    assert(!buzz_index_fuse_u16(&fused,&refs,&capacity,input,65537,0,&n));
    rsx_vertex_remap_destroy(&reference);rsx_vertex_remap_destroy(&fused);free(refs);
    puts("PASS packed fused u16 indices: exact unique order/map, 2000 changing draws, SDK interleaving, generation wrap, two allocation failures, guarded tails and 0xffff as data");
}
