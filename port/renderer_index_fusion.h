#pragma once
#include "rsx_vertex_compact.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if BUZZ_RENDERER_PACKED_FUSION
#include "renderer_index_packed_fusion.h"
#endif
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

/* One contiguous, restart-free u16 index batch with a shared base. Decode
 * directly into the first-use unique list and occurrence map; never build a
 * full intermediate reference list or scan it for min/max. No guest caching. */
static int buzz_index_fuse_u16(rsx_vertex_remap* remap,rsx_vertex_ref** refs,
    u32* capacity,const u8* source,u32 count,u32 base,u32* unique_count) {
#if BUZZ_RENDERER_PACKED_FUSION
    return buzz_index_packed_u16(buzz_index_packed_storage(),remap,refs,capacity,source,count,base,unique_count);
#else
    if(!remap || !refs || !capacity || !source || !unique_count || count<4 || count>65536)
        return 0;
    if(count>*capacity) {
        u32 next=*capacity?*capacity:4096;
        while(next<count)next*=2;
        rsx_vertex_ref* p=(rsx_vertex_ref*)realloc(*refs,(size_t)next*sizeof(*p));
        if(!p)return 0;
        *refs=p;*capacity=next;
    }
    if(count>remap->occurrence_capacity) {
        u32* p=(u32*)realloc(remap->occurrence_to_unique,(size_t)count*sizeof(u32));
        if(!p)return 0;
        remap->occurrence_to_unique=p;remap->occurrence_capacity=count;
    }
    /* u16 gives an exact bound without reading the input twice. Existing SDK
     * remaps can share this storage: every call advances the generation. */
    if(remap->dense_capacity<65536) {
        u32* stamps=(u32*)realloc(remap->dense_stamp,65536*sizeof(u32));
        if(!stamps)return 0;
        remap->dense_stamp=stamps;
        u32* values=(u32*)realloc(remap->dense_value,65536*sizeof(u32));
        if(!values)return 0;
        remap->dense_value=values;
        memset(stamps,0,65536*sizeof(u32));
        remap->dense_capacity=65536;remap->dense_generation=0;
    }
    if(++remap->dense_generation==0) {
        memset(remap->dense_stamp,0,(size_t)remap->dense_capacity*sizeof(u32));
        remap->dense_generation=1;
    }
    const u32 generation=remap->dense_generation;u32 unique=0,i=0;
    while(i<count) {
        u16 indices[8];u32 n=1;
#if defined(__aarch64__)
        if(count-i>=8){vst1q_u16(indices,vreinterpretq_u16_u8(vrev16q_u8(vld1q_u8(source+2u*i))));n=8;}
        else indices[0]=(u16)((source[2u*i]<<8)|source[2u*i+1]);
#elif defined(__SSE2__) || defined(_M_X64)
        if(count-i>=8) {
            __m128i v=_mm_loadu_si128((const __m128i*)(source+2u*i));
            v=_mm_or_si128(_mm_slli_epi16(v,8),_mm_srli_epi16(v,8));
            _mm_storeu_si128((__m128i*)indices,v);n=8;
        } else indices[0]=(u16)((source[2u*i]<<8)|source[2u*i+1]);
#else
        indices[0]=(u16)((source[2u*i]<<8)|source[2u*i+1]);
#endif
        for(u32 lane=0;lane<n;lane++) {
            const u32 index=indices[lane];
            if(remap->dense_stamp[index]!=generation) {
                remap->dense_stamp[index]=generation;remap->dense_value[index]=unique;
                (*refs)[unique++]=(rsx_vertex_ref){index,base};
            }
            remap->occurrence_to_unique[i+lane]=remap->dense_value[index];
        }
        i+=n;
    }
    *unique_count=unique;return 1;
#endif
}
