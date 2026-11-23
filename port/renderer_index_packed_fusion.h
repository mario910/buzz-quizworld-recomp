#pragma once
#include "rsx_vertex_compact.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

#if defined(__x86_64__)
#include <immintrin.h>
#define BUZZ_INDEX_RANK_SIMD 1
/* Big-endian u16 indices -> native u16, with min and max. The Deck's Zen 2
 * always has SSSE3/SSE4.1; the build baseline is SSE2, so only this helper
 * is compiled for them. */
__attribute__((target("ssse3,sse4.1")))
static void buzz_index_swap_minmax(const u8* src,u32 n,u16* dst,u32* lo_out,u32* hi_out)
{
    const __m128i swap=_mm_setr_epi8(1,0,3,2,5,4,7,6,9,8,11,10,13,12,15,14);
    __m128i vmin=_mm_set1_epi16(-1),vmax=_mm_setzero_si128();
    u32 i=0;
    for(;i+8<=n;i+=8) {
        const __m128i v=_mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(src+2u*i)),swap);
        _mm_storeu_si128((__m128i*)(dst+i),v);
        vmin=_mm_min_epu16(vmin,v);vmax=_mm_max_epu16(vmax,v);
    }
    vmin=_mm_minpos_epu16(vmin);
    u32 lo=(u32)_mm_extract_epi16(vmin,0);
    vmax=_mm_xor_si128(vmax,_mm_set1_epi16(-1));
    vmax=_mm_minpos_epu16(vmax);
    u32 hi=(u32)(0xFFFFu^(u32)_mm_extract_epi16(vmax,0));
    if(n<8){lo=65535;hi=0;}
    for(;i<n;++i) {
        const u32 index=((u32)src[2*i]<<8)|src[2*i+1];
        dst[i]=(u16)index;lo=index<lo?index:lo;hi=index>hi?index:hi;
    }
    if(*lo_out>lo)*lo_out=lo;
    if(*hi_out<hi)*hi_out=hi;
}

/* Per four memberships, prefix gives the sorted ordinal within the group
 * and offset lists the selected vertices in order. */
static const struct { u8 prefix[4], offset[4], count; } buzz_rank_group[16] = {
    {{0,0,0,0},{0,0,0,0},0},
    {{0,1,1,1},{0,0,0,0},1},
    {{0,0,1,1},{1,0,0,0},1},
    {{0,1,2,2},{0,1,0,0},2},
    {{0,0,0,1},{2,0,0,0},1},
    {{0,1,1,2},{0,2,0,0},2},
    {{0,0,1,2},{1,2,0,0},2},
    {{0,1,2,3},{0,1,2,0},3},
    {{0,0,0,0},{3,0,0,0},1},
    {{0,1,1,1},{0,3,0,0},2},
    {{0,0,1,1},{1,3,0,0},2},
    {{0,1,2,2},{0,1,3,0},3},
    {{0,0,0,1},{2,3,0,0},2},
    {{0,1,1,2},{0,2,3,0},3},
    {{0,0,1,2},{1,2,3,0},3},
    {{0,1,2,3},{0,1,2,3},4},
};

__attribute__((target("sse4.1")))
static u32 buzz_index_sparse_ranks(u8* mark, u32 range, u32 lo, u32 base,
                                  u32* rank, rsx_vertex_ref* output, u32 count)
{
    u32 unique = 0, k = 0;
    const __m128i zero = _mm_setzero_si128(), bases = _mm_set1_epi32(base);
    /* Four-wide stores may overlap the following group. Reserve enough
     * output for all sixteen lanes, even if the last group selects none. */
    for (; k + 16 <= range && unique + 16 <= count; k += 16) {
        __m128i values = _mm_loadu_si128((const __m128i*)(mark + k));
        unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpgt_epi8(values, zero));
        _mm_storeu_si128((__m128i*)(mark + k), zero);
        if (!mask) continue;
        if (mask == 0xffff) {
            for (unsigned j=0;j<16;++j) {
                rank[k+j]=unique+j;
                output[unique+j]=(rsx_vertex_ref){lo+k+j,base};
            }
            unique+=16;
            continue;
        }
        for (unsigned g = 0; g < 16; g += 4, mask >>= 4) {
            unsigned m = mask & 15;
            if (!m) continue;
            __m128i prefix = _mm_cvtepu8_epi32(_mm_loadu_si32(buzz_rank_group[m].prefix));
            _mm_storeu_si128((__m128i*)(rank + k + g), _mm_add_epi32(prefix,_mm_set1_epi32(unique)));
            __m128i offsets = _mm_cvtepu8_epi32(_mm_loadu_si32(buzz_rank_group[m].offset));
            __m128i ids = _mm_add_epi32(offsets,_mm_set1_epi32(lo+k+g));
            _mm_storeu_si128((__m128i*)(output+unique),_mm_unpacklo_epi32(ids,bases));
            _mm_storeu_si128((__m128i*)(output+unique+2),_mm_unpackhi_epi32(ids,bases));
            unique += buzz_rank_group[m].count;
        }
    }
    for (; k < range; ++k) {
        if (mark[k]) {
            mark[k] = 0;
            rank[k] = unique;
            output[unique++] = (rsx_vertex_ref){lo + k, base};
        }
    }
    return unique;
}
#endif

typedef struct {
    u16 native[65536];  /* byte-swapped copy of the draw's indices */
    u8 mark[65536];     /* indexed by absolute vertex ID; clear every touched range */
    u32 rank[65536];
} buzz_index_rank_scratch;

static inline buzz_index_rank_scratch* buzz_index_rank_storage(void)
{
    static buzz_index_rank_scratch scratch;
    return &scratch;
}

static inline int buzz_index_rank_check(void)
{
    static int enabled=-1;
    if(enabled<0){const char* p=getenv("BUZZ_INDEX_RANK_CHECK");enabled=p&&!strcmp(p,"1");}
    return enabled;
}

static inline int buzz_index_rank_enabled(void)
{
    static int enabled=-1;
    if(enabled<0){const char* p=getenv("BUZZ_INDEX_RANK");enabled=!(p&&!strcmp(p,"0"));
        fprintf(stderr,"[rsx engine] ranked index remap=%d\n",enabled);}
    return enabled;
}


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
    if(count+1>*capacity) {
        u32 next=*capacity?*capacity:4096;
        while(next<count+1)next*=2;
        rsx_vertex_ref* p=(rsx_vertex_ref*)realloc(*refs,(size_t)next*sizeof(*p));
        if(!p)return 0;
        *refs=p;*capacity=next;
    }
    if(count>remap->occurrence_capacity) {
        u32* p=(u32*)realloc(remap->occurrence_to_unique,(size_t)count*sizeof(*p));
        if(!p)return 0;
        remap->occurrence_to_unique=p;remap->occurrence_capacity=count;
    }
#if BUZZ_INDEX_RANK_SIMD
    /* Rank path: unique vertices in sorted order instead of first-use order,
     * with no data-dependent branch (the first-use test mispredicts heavily).
     * Every occurrence keeps its exact vertex. */
    if(buzz_index_rank_enabled()) {
        buzz_index_rank_scratch* rs=buzz_index_rank_storage();
        u16* const native=rs->native;
        u32 lo=65535,hi=0,total=0;
        for(u32 b=0;b<span_count;++b) {
            buzz_index_swap_minmax(spans[b].bytes,spans[b].count,native+total,&lo,&hi);
            total+=spans[b].count;
        }
        const u32 range=hi-lo+1;
        if(range<=4u*count+256u) {
            u8* const mark=rs->mark+lo;u32* const rank=rs->rank+lo;
            for(u32 i=0;i<count;++i)rs->mark[native[i]]=1;
            rsx_vertex_ref* const output=*refs;
            // A fully populated interval has rank(index) = index - lo.
            const int dense = range <= count && memchr(mark, 0, range) == NULL;
            u32 unique = 0;
            if (dense) {
                unique = range;
                memset(mark, 0, range);
                if (unique != count) for (u32 k = 0; k < range; ++k)
                    output[k] = (rsx_vertex_ref){lo + k, base};
            } else unique = buzz_index_sparse_ranks(mark,range,lo,base,rank,output,count);
            u32* const occurrences=remap->occurrence_to_unique;
            if(unique==count) {
                /* No repeats: the caller draws refs in order, so keep the
                 * occurrence order. */
                for(u32 i=0;i<count;++i) {
                    output[i].vertex_id=native[i];output[i].base_index=base;
                    occurrences[i]=i;
                }
            } else if (dense) {
                for (u32 i = 0; i < count; ++i) occurrences[i] = native[i] - lo;
            } else {
                for(u32 i=0;i<count;++i)occurrences[i]=rs->rank[native[i]];
            }
            *unique_count=unique;
            if(buzz_index_rank_check()) {
                static unsigned long long checked,bad;
                int ok=1;u32 offset=0;
                for(u32 b=0;b<span_count && ok;++b) {
                    const u8* source=spans[b].bytes;
                    for(u32 i=0;i<spans[b].count;++i) {
                        const u32 index=((u32)source[2*i]<<8)|source[2*i+1];
                        const u32 o=occurrences[offset+i];
                        if(o>=unique || output[o].vertex_id!=index || output[o].base_index!=base ||
                           (unique==count && o!=offset+i)){ok=0;break;}
                        mark[index-lo]=1;
                    }
                    offset+=spans[b].count;
                }
                u32 distinct=0;
                for(u32 k=0;k<range;++k){distinct+=mark[k];mark[k]=0;}
                for(u32 u=0;u<unique && ok;++u)if(output[u].vertex_id<lo||output[u].vertex_id>hi)ok=0;
                if(distinct!=unique)ok=0;
                checked++;if(!ok)bad++;
                if(!ok || (checked&4095)==0)
                    fprintf(stderr,"[rsx engine] rank check draws=%llu bad=%llu last count=%u unique=%u range=%u\n",
                        checked,bad,count,unique,range);
            }
            return 1;
        }
    }
#endif
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
