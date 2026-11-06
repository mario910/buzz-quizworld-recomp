#include "renderer_vertex_remap.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

static u32 seed=0x613adbf1u;
static u32 random_u32(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static rsx_vertex_ref input[2049], reference[2049], actual[2049];
static rsx_vertex_remap a,b;
static void compare(u32 count) {
    memcpy(reference,input,sizeof input);memcpy(actual,input,sizeof input);
    u32 na=0,nb=0;
    assert(rsx_vertex_remap_build(&a,reference,count,&na)==buzz_vertex_remap_build(&b,actual,count,&nb));
    assert(na==nb && !memcmp(reference,actual,sizeof actual));
    for(u32 i=0;i<count;++i)assert(rsx_vertex_remap_index(&a,i)==rsx_vertex_remap_index(&b,i));
}
static volatile u64 checksum;
static double bench(int optimized,u32 count,int changing) {
    rsx_vertex_remap remap={0};
    const clock_t start=clock();
    for(u32 n=0;n<60000;++n) {
        if(changing)input[count/3].base_index=n;
        memcpy(actual,input,(size_t)count*sizeof(*input));
        u32 unique;
        const int ok=optimized?buzz_vertex_remap_build(&remap,actual,count,&unique)
                           :rsx_vertex_remap_build(&remap,actual,count,&unique);
        assert(ok);checksum+=unique+actual[0].vertex_id;
    }
    const double ms=1000.0*(clock()-start)/CLOCKS_PER_SEC;
    rsx_vertex_remap_destroy(&remap);return ms;
}
int main(int argc,char** argv) {
#ifndef _WIN32
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    u8* pages=(u8*)mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(pages!=MAP_FAILED && !mprotect(pages+page,page,PROT_NONE));
    for(u32 n=1;n<=65;++n) {
        rsx_vertex_ref* refs=(rsx_vertex_ref*)(pages+page-(size_t)n*sizeof(*refs));
        for(u32 i=0;i<n;++i){refs[i].vertex_id=UINT_MAX-i;refs[i].base_index=13;}
        u32 lo,hi;assert(buzz_vertex_ref_range(refs,n,&lo,&hi));
        assert(lo==UINT_MAX-(n-1) && hi==UINT_MAX);
        if(n>1){refs[n-1].base_index=14;assert(!buzz_vertex_ref_range(refs,n,&lo,&hi));}
    }
    assert(!munmap(pages,page*2));
#endif
    const u32 counts[]={0,1,2,3,6,15,16,17,19,32,63,128,512,1023,1024,1025,2049};
    for(u32 iteration=0;iteration<1500;++iteration) {
        const u32 count=counts[iteration%(sizeof counts/sizeof counts[0])];
        for(u32 i=0;i<2049;++i) {
            const u32 r=random_u32();
            input[i].vertex_id=iteration%3==0?r:(r%127);
            input[i].base_index=iteration%5==0?random_u32():0xfffffffeu;
        }
        compare(count);compare(count);
        /* Repeated calls must also work with a fresh SDK remap allocation. */
        rsx_vertex_remap_destroy(&b);compare(count);
        if(count>3) {
            /* Divergent base, including an early SIMD lane. */
            input[1].base_index^=0x12345678u;compare(count);compare(count);
            input[1].vertex_id^=0x87654321u;compare(count);compare(count);
        }
        if(iteration%13==0){a.dense_generation=0xffffffffu;b.dense_generation=0xffffffffu;compare(count);}
    }
    u32 unique=7;
    assert(!buzz_vertex_remap_build(NULL,input,1,&unique));
    assert(!buzz_vertex_remap_build(&b,NULL,1,&unique));
    assert(!buzz_vertex_remap_build(&b,input,1,NULL));
    assert(buzz_vertex_remap_build(&b,NULL,0,&unique)&&unique==0);
    puts("PASS remap: full refs including tail, occurrence maps, mutations, growth, fresh remaps, generation wrap, invalid/oversize inputs");
    rsx_vertex_remap_destroy(&a);rsx_vertex_remap_destroy(&b);
    if(argc>1 && !strcmp(argv[1],"--bench"))for(u32 size=32;size<=1024;size*=4) {
        for(u32 i=0;i<size;++i){input[i].vertex_id=i%((size*2)/3);input[i].base_index=0;}
        for(int changing=0;changing<2;++changing)for(int round=0;round<2;++round) {
            const int first=round&1;
            const double t0=bench(first,size,changing),t1=bench(!first,size,changing);
            printf("bench refs=%u changing=%d order=%d%d reference_ms=%.3f simd_ms=%.3f\n",size,changing,first,!first,first?t1:t0,first?t0:t1);
        }
    }
    return 0;
}
