#pragma once
#include "rsx_vertex_compact.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

/* Read four (vertex_id, base_index) pairs at a time, without an overread.
 * All bases must match before the dense remap can be used. No caching. */
static inline int buzz_vertex_ref_range(const rsx_vertex_ref* refs,u32 count,u32* low,u32* high)
{
    if(!refs || !count || !low || !high)return 0;
    const u32 base=refs[0].base_index;
    u32 lo=UINT_MAX,hi=0,i=0;
#if defined(__aarch64__)
    uint32x4_t minima=vdupq_n_u32(UINT_MAX),maxima=vdupq_n_u32(0);
    for(;count-i>=4;i+=4) {
        const uint32x4x2_t pairs=vld2q_u32((const u32*)(refs+i));
        if(vminvq_u32(vceqq_u32(pairs.val[1],vdupq_n_u32(base)))!=UINT_MAX)return 0;
        minima=vminq_u32(minima,pairs.val[0]);maxima=vmaxq_u32(maxima,pairs.val[0]);
    }
    lo=vminvq_u32(minima);hi=vmaxvq_u32(maxima);
#elif defined(__SSE2__) || defined(_M_X64)
    __m128i minima=_mm_set1_epi32(-1),maxima=_mm_setzero_si128();
    const __m128i sign=_mm_set1_epi32((int)0x80000000u),bases=_mm_set1_epi32((int)base);
    for(;count-i>=4;i+=4) {
        const __m128 a=_mm_castsi128_ps(_mm_loadu_si128((const __m128i*)(refs+i)));
        const __m128 b=_mm_castsi128_ps(_mm_loadu_si128((const __m128i*)(refs+i+2)));
        const __m128i ids=_mm_castps_si128(_mm_shuffle_ps(a,b,_MM_SHUFFLE(2,0,2,0)));
        const __m128i bs=_mm_castps_si128(_mm_shuffle_ps(a,b,_MM_SHUFFLE(3,1,3,1)));
        if(_mm_movemask_epi8(_mm_cmpeq_epi32(bs,bases))!=65535)return 0;
        const __m128i less=_mm_cmpgt_epi32(_mm_xor_si128(minima,sign),_mm_xor_si128(ids,sign));
        const __m128i more=_mm_cmpgt_epi32(_mm_xor_si128(ids,sign),_mm_xor_si128(maxima,sign));
        minima=_mm_or_si128(_mm_and_si128(less,ids),_mm_andnot_si128(less,minima));
        maxima=_mm_or_si128(_mm_and_si128(more,ids),_mm_andnot_si128(more,maxima));
    }
    u32 mins[4],maxs[4];_mm_storeu_si128((__m128i*)mins,minima);_mm_storeu_si128((__m128i*)maxs,maxima);
    for(u32 lane=0;lane<4;++lane){if(mins[lane]<lo)lo=mins[lane];if(maxs[lane]>hi)hi=maxs[lane];}
#endif
    for(;i<count;++i) {
        if(refs[i].base_index!=base)return 0;
        if(refs[i].vertex_id<lo)lo=refs[i].vertex_id;
        if(refs[i].vertex_id>hi)hi=refs[i].vertex_id;
    }
    *low=lo;*high=hi;return 1;
}

static inline int buzz_vertex_remap_build(rsx_vertex_remap* remap,rsx_vertex_ref* refs,u32 count,u32* unique_count)
{
    if(!remap || !refs || !unique_count || count<16)
        return rsx_vertex_remap_build(remap,refs,count,unique_count);
    u32 lo,hi;
    if(!buzz_vertex_ref_range(refs,count,&lo,&hi))return rsx_vertex_remap_build(remap,refs,count,unique_count);
    const u64 range=(u64)hi-lo+1u;
    if(range>(u64)count*4u+4096u || range>(1u<<22))
        return rsx_vertex_remap_build(remap,refs,count,unique_count);
    *unique_count=count;
    if(count>remap->occurrence_capacity) {
        u32* occurrences=(u32*)realloc(remap->occurrence_to_unique,(size_t)count*sizeof(u32));
        if(!occurrences)return 0;
        remap->occurrence_to_unique=occurrences;remap->occurrence_capacity=count;
    }
    if(range>remap->dense_capacity) {
        u32* stamp=(u32*)realloc(remap->dense_stamp,(size_t)range*sizeof(u32));
        if(!stamp)return 0;
        remap->dense_stamp=stamp;
        u32* value=(u32*)realloc(remap->dense_value,(size_t)range*sizeof(u32));
        if(!value)return 0;
        remap->dense_value=value;
        memset(stamp,0,(size_t)range*sizeof(u32));
        remap->dense_capacity=(u32)range;remap->dense_generation=0;
    }
    if(++remap->dense_generation==0) {
        memset(remap->dense_stamp,0,(size_t)remap->dense_capacity*sizeof(u32));
        remap->dense_generation=1;
    }
    const u32 gen=remap->dense_generation,base=refs[0].base_index;
    u32* stamp=remap->dense_stamp;u32* value=remap->dense_value;u32 unique=0;
    for(u32 occurrence=0;occurrence<count;++occurrence) {
        const u32 k=refs[occurrence].vertex_id-lo;
        if(stamp[k]!=gen) {
            stamp[k]=gen;value[k]=unique;
            refs[unique].vertex_id=k+lo;refs[unique].base_index=base;++unique;
        }
        remap->occurrence_to_unique[occurrence]=value[k];
    }
    *unique_count=unique;return 1;
}
