#pragma once
#include "renderer_vertex_remap.h"

/* Per-consumer scratch, never guest data. A generation and its mapped index
 * occupy one aligned word, avoiding a second dependent load on repeated refs.
 * No allocation/lifetime changes to the SDK remap; its ordinary fallback and
 * destroy remain valid when this path is enabled or disabled between draws. */
typedef struct {
    u64 entries[4096];
    u32 generation;
} buzz_packed_remap;

static inline int buzz_packed_remap_build(buzz_packed_remap* scratch,
    rsx_vertex_remap* remap,rsx_vertex_ref* refs,u32 count,u32* unique_count)
{
    if(!scratch || !remap || !refs || !unique_count || count<16)
        return rsx_vertex_remap_build(remap,refs,count,unique_count);
    u32 lo,hi;
    if(!buzz_vertex_ref_range(refs,count,&lo,&hi) || (u64)hi-lo>=4096)
        return rsx_vertex_remap_build(remap,refs,count,unique_count);
    *unique_count=count;
    if(count>remap->occurrence_capacity) {
        u32* occurrences=(u32*)realloc(remap->occurrence_to_unique,(size_t)count*sizeof(u32));
        if(!occurrences)return 0;
        remap->occurrence_to_unique=occurrences;remap->occurrence_capacity=count;
    }
    if(++scratch->generation==0) {
        memset(scratch->entries,0,sizeof scratch->entries);scratch->generation=1;
    }
    const u64 generation=(u64)scratch->generation<<32;
    const u32 base=refs[0].base_index;
    u32* occurrences=remap->occurrence_to_unique;
    u32 unique=0;
    for(u32 i=0;i<count;++i) {
        const u32 id=refs[i].vertex_id,k=id-lo;
        const u64 entry=scratch->entries[k];
        u32 mapped=(u32)entry;
        if((entry&0xffffffff00000000ull)!=generation) {
            mapped=unique++;
            scratch->entries[k]=generation|mapped;
            refs[mapped].vertex_id=id;refs[mapped].base_index=base;
        }
        occurrences[i]=mapped;
    }
    *unique_count=unique;return 1;
}
