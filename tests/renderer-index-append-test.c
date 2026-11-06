#include <stdlib.h>
static int allocation_failure;
static void* checked_realloc(void* p,size_t bytes) {return allocation_failure?NULL:realloc(p,bytes);}
#define realloc checked_realloc
#include "renderer_index_append.h"
#undef realloc
#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif
static u32 random_state=0x71323456;
static u32 random32(void){random_state^=random_state<<13;random_state^=random_state>>17;return random_state^=random_state<<5;}
static void check(const u8* source,u32 count,int wide,u32 base) {
    rsx_vertex_ref* out=NULL;u32 capacity=0,length=0;
    for(u32 repeat=0;repeat<3;++repeat) {
        const u32 previous=length;
        assert(buzz_index_append(&out,&capacity,&length,source,count,base,wide));
        assert(length==previous+count && capacity>=length);
        for(u32 i=0;i<length;++i) {
            const u8* p=source+(size_t)(i%count)*(wide?4:2);
            const u32 id=wide?((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]:((u32)p[0]<<8)|p[1];
            assert(out[i].vertex_id==id && out[i].base_index==base);
        }
    }
    free(out);
}
int main(void) {
    u8* source=malloc(65536*4+16);assert(source);
    for(u32 i=0;i<65536*4+16;++i)source[i]=(u8)random32();
    for(u32 wide=0;wide<2;++wide) {
        for(u32 offset=0;offset<16;++offset)for(u32 count=4;count<=137;++count)
            check(source+offset,count,wide,random32());
        for(u32 n=4094;n<=4098;++n)check(source,n,wide,UINT_MAX);
        check(source,65536,wide,0);
    }
    rsx_vertex_ref* out=NULL;u32 capacity=0,length=0;
    allocation_failure=1;
    assert(!buzz_index_append(&out,&capacity,&length,source,16,0,0));
    assert(!out && !capacity && !length);allocation_failure=0;
    assert(!buzz_index_append(&out,&capacity,&length,source,3,0,0));
    assert(!buzz_index_append(&out,&capacity,&length,source,65537,0,0));
    length=UINT_MAX-3;
    assert(!buzz_index_append(&out,&capacity,&length,source,4,0,0));
    assert(length==UINT_MAX-3 && !out && !capacity);
#ifndef _WIN32
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    u8* pages=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(pages!=MAP_FAILED && !mprotect(pages+page,page,PROT_NONE));
    for(u32 wide=0;wide<2;++wide)for(u32 count=4;count<=65;++count) {
        const u32 bytes=count*(wide?4:2);u8* p=pages+page-bytes;
        memcpy(p,source,bytes);check(p,count,wide,0xfffffffdu);
    }
    assert(!munmap(pages,page*2));
#endif
    free(source);puts("PASS index append: u16/u32, misalignment, tails, growth, prefixes, allocation failure, overflow, guard pages");
    return 0;
}
