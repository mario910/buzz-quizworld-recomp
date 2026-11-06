#include "renderer_vertex_stream.h"
#include <assert.h>
#include <fenv.h>
#include <stdlib.h>
#include <time.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif
static u32 seed=0xa562def1;
static u32 random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static u8 bytes[8192];
_Alignas(16) static u8 a[16384],b[16384];
static rsx_vertex_ref refs[64];
static rsx_vertex_fetch_plan plan;
static u32 callbacks;
static const u8* resolve(void* user,u32 location,u32 offset,u32 count) {
    (void)user;(void)location;callbacks++;
    return offset<=sizeof bytes && count<=sizeof bytes-offset?bytes+offset:NULL;
}
static void compare(u32 count) {
    memset(a,0xa5,sizeof a);memcpy(b,a,sizeof a);callbacks=0;
    const int reference=rsx_vertex_fetch_many(&plan,refs,count,a);
    const u32 expected_callbacks=callbacks;callbacks=0;
    const int actual=buzz_vertex_fetch_many(&plan,refs,count,b);
    assert(reference==actual && callbacks==expected_callbacks);
    assert(!memcmp(a,b,sizeof a));
    memset(b,0xa5,sizeof b);callbacks=0;
    assert(buzz_vertex_stream(&plan,refs,count,b)==reference);
    assert(callbacks==expected_callbacks && !memcmp(a,b,sizeof a));
}
static void initialize(u32 type,u32 size,u32 attrs) {
    memset(&plan,0,sizeof plan);
    rsx_vertex_layout_plan_init(&plan.layout,(1u<<attrs)-1);
    for(u32 slot=0;slot<attrs;++slot) {
        rsx_vertex_fetch_attr* f=&plan.attr[slot];
        f->desc.type=type;f->desc.size=size;f->desc.frequency=1;
        f->elem_size=rsx_vertex_attrib_size(type,size);f->stride=16;
        f->stable_base=bytes+slot*256+1;f->stable_first=0;f->stable_last=63;
        for(u32 c=0;c<4;c++){const u32 v=random32();memcpy(&f->default_value[c],&v,4);}
    }
    plan.guest_ptr=resolve;
    for(u32 i=0;i<64;i++)refs[i]=(rsx_vertex_ref){random32()%64,0};
}
static volatile u32 checksum;
static void bench(void) {
    initialize(2,3,3);plan.attr[1].desc.type=3;plan.attr[1].desc.size=2;
    plan.attr[2].desc.type=6;plan.attr[2].desc.size=1;
    const int modes[]={0,1,2,2,1,0};
    for(u32 round=0;round<6;round++) {
        const int optimized=modes[round];
        const clock_t start=clock();
        for(u32 i=0;i<300000;i++) {
            refs[0].vertex_id=i%64;
            checksum+=optimized==2?buzz_vertex_stream(&plan,refs,64,b):optimized==1?buzz_vertex_fetch_many(&plan,refs,64,b):rsx_vertex_fetch_many(&plan,refs,64,b);
            checksum+=b[i%sizeof b];
        }
        printf("mixed FLOAT3/HALF2/CMP1 optimized=%d ms=%.3f\n",optimized,1000.0*(clock()-start)/CLOCKS_PER_SEC);
    }
}
int main(int argc,char** argv) {
    for(u32 i=0;i<sizeof bytes;++i)bytes[i]=(u8)random32();
    /* All 65536 half encodings, every component width; preserve signaling NaNs. */
    for(u32 h=0;h<65536;++h)for(u32 size=1;size<=4;++size) {
        u8 source[8],actual[16];float expected[4];
        for(u32 c=0;c<4;c++){source[c*2]=(u8)(h>>8);source[c*2+1]=(u8)h;}
        assert(rsx_vertex_decode_element(3,size,source,expected));
        buzz_fetch_half(source,actual,size);assert(!memcmp(actual,expected,16));
    }
#if BUZZ_FETCH_NEON || BUZZ_FETCH_SSE
    for(u32 h=0;h<65536;++h) {
        u32 normal[4],uv[4];u8 output[192],expected[16];
        for(u32 lane=0;lane<4;++lane) {
            normal[lane]=random32();
            u8* p=(u8*)&uv[lane];const u32 first=(h+lane)&65535,second=first^0xa51b;
            p[0]=(u8)(first>>8);p[1]=(u8)first;p[2]=(u8)(second>>8);p[3]=(u8)second;
        }
        buzz_stream_decode4(normal,uv,output+16,output+32,48);
        for(u32 lane=0;lane<4;++lane) {
            buzz_fetch_half((u8*)&uv[lane],expected,2);assert(!memcmp(expected,output+lane*48+32,16));
            buzz_fetch_cmp((u8*)&normal[lane],expected);assert(!memcmp(expected,output+lane*48+16,16));
        }
    }
#endif
    const int rounding[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(u32 mode=0;mode<4;++mode) {
        assert(fesetround(rounding[mode])==0);
#if BUZZ_FETCH_NEON || BUZZ_FETCH_SSE
        /* Every signed CMP component encoding, in every SIMD lane and all
         * rounding modes. Compare the actual stream path against the SDK. */
        for(u32 n=0;n<2048;++n) {
            u32 normal[4],uv[4]={0};u8 output[192];
            for(u32 lane=0;lane<4;++lane) {
                const u32 x=(n+lane*317u)&2047u;
                const u32 y=(n*3u+lane*137u)&2047u;
                const u32 z=(n+lane*71u)&1023u;
                const u32 packed=x|(y<<11)|(z<<22);
                u8* p=(u8*)&normal[lane];
                p[0]=(u8)(packed>>24);p[1]=(u8)(packed>>16);
                p[2]=(u8)(packed>>8);p[3]=(u8)packed;
            }
            buzz_stream_decode4(normal,uv,output+16,output+32,48);
            for(u32 lane=0;lane<4;++lane) {
                float expected[4];
                assert(rsx_vertex_decode_element(6,1,(u8*)&normal[lane],expected));
                assert(!memcmp(expected,output+lane*48+16,16));
            }
        }
#endif
        for(u32 n=0;n<10000;++n) {
            u32 word=random32();u8 actual[16];float expected[4];
            assert(rsx_vertex_decode_element(6,1,(u8*)&word,expected));
            buzz_fetch_cmp((u8*)&word,actual);assert(!memcmp(expected,actual,16));
        }
    }
    assert(fesetround(FE_TONEAREST)==0);
    const u32 orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    const u32 types[3]={2,3,6},sizes[3]={3,2,1};
    for(u32 order=0;order<6;++order)for(u32 mode=0;mode<4;++mode) {
        assert(fesetround(rounding[mode])==0);
        initialize(2,3,3);
        for(u32 slot=0;slot<3;++slot) {
            plan.attr[slot].desc.type=types[orders[order][slot]];
            plan.attr[slot].desc.size=sizes[orders[order][slot]];
        }
        for(u32 count=0;count<=64;++count)compare(count);
        const rsx_vertex_fetch_attr saved[3]={plan.attr[0],plan.attr[1],plan.attr[2]};
        const u8 attributes[3]={7,2,15};
        for(u32 slot=0;slot<3;++slot){plan.layout.attrs[slot]=attributes[slot];plan.attr[attributes[slot]]=saved[slot];}
        compare(64);
        for(u32 i=0;i<64;++i)refs[i]=(rsx_vertex_ref){0xfffffff0u+i,16};
        compare(64);
        plan.attr[attributes[orders[order][0]]].stable_last=0;compare(64);
    }
    assert(fesetround(FE_TONEAREST)==0);
    for(u32 n=0;n<10000;++n) {
        const u32 type=random32()%9,size=random32()%6,attrs=1+random32()%4;
        initialize(type,size,attrs);
        for(u32 i=0;i<64;++i) {
            refs[i].base_index=(n%3)?0:0x100000u;
            if(n%7==0){refs[i].vertex_id=0xfffffff0u+i;refs[i].base_index=16;}
        }
        for(u32 slot=0;slot<attrs;++slot) {
            rsx_vertex_fetch_attr* f=&plan.attr[slot];
            if(n%5==0)f->desc.frequency=2+random32()%8;
            if(n%11==0)f->stable_base=NULL;
            if(n%13==0)f->stable_last=2;
            if(n%17==0)f->desc.offset=sizeof bytes-2;
            if(n%19==0)f->desc.type=0;
        }
        plan.divider_mask=n;
        if(n%23==0){plan.inline_data=bytes;plan.inline_bytes=sizeof bytes;}
        compare(n%65);
    }
#ifndef _WIN32
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    u8* guarded=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(guarded!=MAP_FAILED && !mprotect(guarded+page,page,PROT_NONE));
    for(u32 type=1;type<=7;type++)for(u32 size=1;size<=4;size++) {
        initialize(type,size,1);const u32 length=rsx_vertex_attrib_size(type,size);
        plan.attr[0].stable_base=guarded+page-length;plan.attr[0].stable_last=0;
        for(u32 c=0;c<length;c++)guarded[page-length+c]=(u8)random32();
        for(u32 i=0;i<64;i++)refs[i]=(rsx_vertex_ref){0,0};
        compare(64);
    }
    initialize(2,3,3);
    plan.attr[1].desc.type=6;plan.attr[1].desc.size=1;
    plan.attr[2].desc.type=3;plan.attr[2].desc.size=2;
    for(u32 slot=0;slot<3;++slot) {
        rsx_vertex_fetch_attr* f=&plan.attr[slot];
        f->stable_base=guarded+page-rsx_vertex_attrib_size(f->desc.type,f->desc.size);
        f->stable_last=0;
    }
    for(u32 i=0;i<64;++i)refs[i]=(rsx_vertex_ref){0,0};
    for(u32 count=0;count<=64;++count)compare(count);
    munmap(guarded,page*2);
#endif
    assert(buzz_vertex_fetch_many(NULL,refs,4,a)==0);
    initialize(2,4,1);compare(0);
    puts("PASS exact vertex fetch: exhaustive half, CMP rounding modes, 10000 mixed/fallback plans, byte outputs and resolver counts");
    if(argc>1 && !strcmp(argv[1],"--bench"))bench();
    return 0;
}
