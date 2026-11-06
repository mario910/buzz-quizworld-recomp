#pragma once
#include "renderer_vertex_remap.h"

/* One validated, restart-free source span. Reserve once, expand exact-size
 * groups, and publish the new length only after all reference writes. */
static int buzz_index_append(rsx_vertex_ref** refs,u32* capacity,u32* length,
    const u8* source,u32 count,u32 base,int wide)
{
    if(!refs || !capacity || !length || !source || count<4 || count>65536 ||
       *length>UINT_MAX-count)return 0;
    const u32 wanted=*length+count;
    if(wanted>*capacity) {
        u32 next=*capacity?*capacity:4096;
        while(next<wanted) {
            if(next>UINT_MAX/2){next=wanted;break;}
            next*=2;
        }
        rsx_vertex_ref* p=(rsx_vertex_ref*)realloc(*refs,(size_t)next*sizeof(*p));
        if(!p)return 0;
        *refs=p;*capacity=next;
    }
    rsx_vertex_ref* out=*refs+*length;u32 i=0;
#if defined(__aarch64__)
    const uint32x4_t bases=vdupq_n_u32(base);
    if(wide)for(;count-i>=4;i+=4) {
        const uint32x4_t ids=vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(source+(size_t)i*4)));
        const uint32x4x2_t pairs={ids,bases};vst2q_u32((u32*)(out+i),pairs);
    }
    else for(;count-i>=8;i+=8) {
        const uint16x8_t ids=vreinterpretq_u16_u8(vrev16q_u8(vld1q_u8(source+(size_t)i*2)));
        const uint32x4x2_t low={vmovl_u16(vget_low_u16(ids)),bases},high={vmovl_u16(vget_high_u16(ids)),bases};
        vst2q_u32((u32*)(out+i),low);vst2q_u32((u32*)(out+i+4),high);
    }
#elif defined(__SSE2__) || defined(_M_X64)
    const __m128i bases=_mm_set1_epi32((int)base),zero=_mm_setzero_si128();
    if(wide)for(;count-i>=4;i+=4) {
        __m128i ids=_mm_loadu_si128((const __m128i*)(source+(size_t)i*4));
        ids=_mm_or_si128(_mm_slli_epi16(ids,8),_mm_srli_epi16(ids,8));
        ids=_mm_shufflehi_epi16(_mm_shufflelo_epi16(ids,_MM_SHUFFLE(2,3,0,1)),_MM_SHUFFLE(2,3,0,1));
        _mm_storeu_si128((__m128i*)(out+i),_mm_unpacklo_epi32(ids,bases));
        _mm_storeu_si128((__m128i*)(out+i+2),_mm_unpackhi_epi32(ids,bases));
    }
    else for(;count-i>=8;i+=8) {
        __m128i ids=_mm_loadu_si128((const __m128i*)(source+(size_t)i*2));
        ids=_mm_or_si128(_mm_slli_epi16(ids,8),_mm_srli_epi16(ids,8));
        const __m128i lo=_mm_unpacklo_epi16(ids,zero),hi=_mm_unpackhi_epi16(ids,zero);
        _mm_storeu_si128((__m128i*)(out+i),_mm_unpacklo_epi32(lo,bases));
        _mm_storeu_si128((__m128i*)(out+i+2),_mm_unpackhi_epi32(lo,bases));
        _mm_storeu_si128((__m128i*)(out+i+4),_mm_unpacklo_epi32(hi,bases));
        _mm_storeu_si128((__m128i*)(out+i+6),_mm_unpackhi_epi32(hi,bases));
    }
#endif
    for(;i<count;++i) {
        const u8* p=source+(size_t)i*(wide?4u:2u);
        out[i].vertex_id=wide?((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]
                             :((u32)p[0]<<8)|p[1];
        out[i].base_index=base;
    }
    *length=wanted;return 1;
}
