#pragma once
#include "rsx_vertex_compact.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Private to one renderer consumer. A u16 index can address at most 65536
 * unique vertices, so stamp and first-use ordinal fit in one 32-bit entry.
 * Nothing here caches guest memory: every source index is read on every draw.
 * The generation is reset before its 16-bit wrap, including the all-unique
 * case where ordinal 65535 is a valid value, not an empty sentinel. */
typedef struct {
    u32 entries[65536];
    u32 generation;
} buzz_index_packed_scratch;

typedef struct { const u8* bytes; u32 count; } buzz_index_span;

static inline buzz_index_packed_scratch* buzz_index_packed_storage(void)
{
    static buzz_index_packed_scratch scratch;
    return &scratch;
}

static int buzz_index_packed_spans(buzz_index_packed_scratch* scratch,
    rsx_vertex_remap* remap, rsx_vertex_ref** refs, u32* capacity,
    const buzz_index_span* spans, u32 span_count, u32 base, u32* unique_count)
{
    if(!scratch || !remap || !refs || !capacity || !spans || !unique_count)return 0;
    u32 count=0;
    for(u32 b=0;b<span_count;++b) {
        if((spans[b].count && !spans[b].bytes) || spans[b].count>65536 ||
           count>65536-spans[b].count)return 0;
        count+=spans[b].count;
    }
    if(count<4)return 0;
    if(count>*capacity) {
        u32 next=*capacity?*capacity:4096;
        while(next<count)next*=2;
        rsx_vertex_ref* p=(rsx_vertex_ref*)realloc(*refs,(size_t)next*sizeof(*p));
        if(!p)return 0;
        *refs=p;*capacity=next;
    }
    if(count>remap->occurrence_capacity) {
        u32* p=(u32*)realloc(remap->occurrence_to_unique,(size_t)count*sizeof(*p));
        if(!p)return 0;
        remap->occurrence_to_unique=p;remap->occurrence_capacity=count;
    }
    if(scratch->generation>=65535) {
        memset(scratch->entries,0,sizeof scratch->entries);
        scratch->generation=0;
    }
    const u32 stamp=++scratch->generation<<16;
    u32* const entries=scratch->entries;
    u32* const occurrences=remap->occurrence_to_unique;
    rsx_vertex_ref* const output=*refs;
    u32 unique=0,offset=0;
    for(u32 b=0;b<span_count;++b) {
      const u8* source=spans[b].bytes;
      for(u32 i=0;i<spans[b].count;++i) {
        const u32 index=((u32)source[2*i]<<8)|source[2*i+1];
        const u32 entry=entries[index];
        u32 ordinal=entry&65535u;
        if((entry&0xffff0000u)!=stamp) {
            ordinal=unique++;
            entries[index]=stamp|ordinal;
            output[ordinal].vertex_id=index;
            output[ordinal].base_index=base;
        }
        occurrences[offset+i]=ordinal;
      }
      offset+=spans[b].count;
    }
    *unique_count=unique;
    return 1;
}

static int buzz_index_packed_u16(buzz_index_packed_scratch* scratch,
    rsx_vertex_remap* remap, rsx_vertex_ref** refs, u32* capacity,
    const u8* source, u32 count, u32 base, u32* unique_count)
{
    const buzz_index_span span={source,count};
    return buzz_index_packed_spans(scratch,remap,refs,capacity,&span,1,base,unique_count);
}
