#include "../ps3recomp/libs/video/rsx_vertex_upload.h"
#include <assert.h>
#include <stdio.h>
static uint32_t rng=0x5672fe10;
static uint32_t word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint8_t source[257*256],actual[257*256+64],expected[257*256+64];
static void check(unsigned count,unsigned mask)
{
    for(unsigned n=0;n<count*256u;n+=4){uint32_t x=word();memcpy(source+n,&x,4);}
    memset(actual,0xcd,sizeof actual);memset(expected,0xcd,sizeof expected);
    unsigned cursor=16;RsxVertexUpload upload;
    assert(rsx_vertex_upload_pack(actual,sizeof actual,&cursor,source,count*256u,count,mask,&upload));
    unsigned at=16;
    if(!mask){memcpy(expected+at,source,count*256u);at+=count*256u;}
    else for(unsigned a=0;a<16;++a)if(mask&(1u<<a)) {
        assert(rsx_vertex_upload_stream(&upload,count,a)==at);
        for(unsigned v=0;v<count;++v){memcpy(expected+at,source+v*256u+a*16u,16);at+=16;}
    }
    assert(upload.offset==16 && upload.mask==mask && upload.bytes==at-16 && cursor==at);
    assert(!memcmp(actual,expected,sizeof actual));
}
int main(void)
{
    for(unsigned mask=0;mask<65536;++mask)check(1,mask);
    for(unsigned n=0;n<4000;++n)check(1+word()%257,word()&65535);
    check(257,0);check(257,65535);check(257,0x8000);check(257,0x101);
    memset(actual,0xcd,sizeof actual);memcpy(expected,actual,sizeof actual);
    unsigned cursor=16;RsxVertexUpload upload={123,456,789};
    assert(!rsx_vertex_upload_pack(actual,31,&cursor,source,256,1,1,&upload));
    assert(!rsx_vertex_upload_pack(actual,sizeof actual,&cursor,source,255,1,1,&upload));
    assert(!rsx_vertex_upload_pack(actual,sizeof actual,&cursor,source,sizeof source,0,1,&upload));
    assert(!rsx_vertex_upload_pack(actual,sizeof actual,&cursor,source,sizeof source,1,65536,&upload));
    assert(!rsx_vertex_upload_pack(actual,sizeof actual,&cursor,source,sizeof source,0xffffffffu,1,&upload));
    cursor=0xfffffff0u;
    assert(!rsx_vertex_upload_pack(actual,0xffffffffu,&cursor,source,256,1,1,&upload));
    cursor=17;
    assert(!rsx_vertex_upload_pack(actual,sizeof actual,&cursor,source,256,1,1,&upload));
    assert(upload.offset==123 && upload.bytes==456 && upload.mask==789);
    assert(!memcmp(actual,expected,sizeof actual));
    puts("PASS: all 65536 masks, 4000 random vertex batches, exact byte streams, fallback, canaries and nonmutating bounds failures");
}
