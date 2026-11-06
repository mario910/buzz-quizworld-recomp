#pragma once
#include "rsx_vertex_compact.h"
#include "rsx_vertex_formats.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define BUZZ_FETCH_NEON 1
#elif defined(__SSE2__) || defined(_M_X64)
#include <immintrin.h>
#define BUZZ_FETCH_SSE 1
#endif

/* Byte operations preserve float NaN payloads and signed zero. Exact-size
 * memcpy loads never cross the end of a validated guest attribute. */
static inline void buzz_fetch_float(const u8* source,u8* out,unsigned size) {
    u8 words[16]={0,0,0,0,0,0,0,0,0,0,0,0,0x3f,0x80,0,0};
    memcpy(words,source,size*4u);
#if BUZZ_FETCH_NEON
    vst1q_u8(out,vrev32q_u8(vld1q_u8((const u8*)words)));
#elif BUZZ_FETCH_SSE
    __m128i value=_mm_loadu_si128((const __m128i*)words);
#if defined(__SSSE3__) || defined(__AVX2__)
    value=_mm_shuffle_epi8(value,_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12));
#else
    value=_mm_or_si128(_mm_slli_epi16(value,8),_mm_srli_epi16(value,8));
    value=_mm_shufflelo_epi16(value,_MM_SHUFFLE(2,3,0,1));
    value=_mm_shufflehi_epi16(value,_MM_SHUFFLE(2,3,0,1));
#endif
    _mm_storeu_si128((__m128i*)out,value);
#else
    for(unsigned c=0;c<4;++c) {
        const u8* p=words+c*4;
        const u32 v=((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
        memcpy(out+c*4,&v,4);
    }
#endif
}

/* SDK semantics flush half subnormals to signed zero, while retaining NaN
 * payload/signaling bits. Native half-to-float instructions do neither. */
static inline void buzz_fetch_half(const u8* source,u8* out,unsigned size) {
    u8 words[8]={0,0,0,0,0,0,0x3c,0};
    memcpy(words,source,size*2u);
#if BUZZ_FETCH_NEON
    const uint32x4_t h=vmovl_u16(vreinterpret_u16_u8(vrev16_u8(vld1_u8((const u8*)words))));
    const uint32x4_t e=vandq_u32(h,vdupq_n_u32(0x7c00));
    const uint32x4_t sign=vshlq_n_u32(vandq_u32(h,vdupq_n_u32(0x8000)),16);
    uint32x4_t body=vshlq_n_u32(vandq_u32(h,vdupq_n_u32(0x7fff)),13);
    body=vaddq_u32(body,vdupq_n_u32(0x38000000));
    body=vbslq_u32(vceqq_u32(e,vdupq_n_u32(0x7c00)),
        vorrq_u32(vshlq_n_u32(vandq_u32(h,vdupq_n_u32(0x3ff)),13),vdupq_n_u32(0x7f800000)),body);
    body=vbicq_u32(body,vceqq_u32(e,vdupq_n_u32(0)));
    vst1q_u8(out,vreinterpretq_u8_u32(vorrq_u32(sign,body)));
#elif BUZZ_FETCH_SSE
    __m128i h=_mm_loadl_epi64((const __m128i*)words);
    h=_mm_or_si128(_mm_slli_epi16(h,8),_mm_srli_epi16(h,8));
    h=_mm_unpacklo_epi16(h,_mm_setzero_si128());
    const __m128i e=_mm_and_si128(h,_mm_set1_epi32(0x7c00));
    const __m128i sign=_mm_slli_epi32(_mm_and_si128(h,_mm_set1_epi32(0x8000)),16);
    __m128i body=_mm_add_epi32(_mm_slli_epi32(_mm_and_si128(h,_mm_set1_epi32(0x7fff)),13),_mm_set1_epi32(0x38000000));
    const __m128i exceptional=_mm_cmpeq_epi32(e,_mm_set1_epi32(0x7c00));
    const __m128i special=_mm_or_si128(_mm_slli_epi32(_mm_and_si128(h,_mm_set1_epi32(0x3ff)),13),_mm_set1_epi32(0x7f800000));
    body=_mm_or_si128(_mm_and_si128(exceptional,special),_mm_andnot_si128(exceptional,body));
    body=_mm_andnot_si128(_mm_cmpeq_epi32(e,_mm_setzero_si128()),body);
    _mm_storeu_si128((__m128i*)out,_mm_or_si128(sign,body));
#else
    for(unsigned c=0;c<4;++c) {
        const u8* p=words+c*2;const u32 h=((u32)p[0]<<8)|p[1];
        const u32 e=h&0x7c00,sign=(h&0x8000)<<16;
        const u32 v=sign|(e==0?0:e==0x7c00?0x7f800000|((h&0x3ff)<<13):((h&0x7fff)<<13)+0x38000000);
        memcpy(out+c*4,&v,4);
    }
#endif
}

static inline void buzz_fetch_cmp(const u8* source,u8* out) {
    const u32 word=((u32)source[0]<<24)|((u32)source[1]<<16)|((u32)source[2]<<8)|source[3];
    s32 xyz[4]={(s32)(word&0x7ff),(s32)((word>>11)&0x7ff),(s32)(word>>22),1};
    if(xyz[0]&0x400)xyz[0]-=0x800;
    if(xyz[1]&0x400)xyz[1]-=0x800;
    if(xyz[2]&0x200)xyz[2]-=0x400;
#if BUZZ_FETCH_NEON
    const float denominators[4]={1023,1023,511,1};
    vst1q_u8(out,vreinterpretq_u8_f32(vdivq_f32(vcvtq_f32_s32(vld1q_s32(xyz)),vld1q_f32(denominators))));
#elif BUZZ_FETCH_SSE
    const __m128 value=_mm_div_ps(_mm_cvtepi32_ps(_mm_loadu_si128((const __m128i*)xyz)),_mm_setr_ps(1023,1023,511,1));
    _mm_storeu_si128((__m128i*)out,_mm_castps_si128(value));
#else
    float v[4]={xyz[0]/1023.0f,xyz[1]/1023.0f,xyz[2]/511.0f,1};memcpy(out,v,16);
#endif
}

#if BUZZ_RENDERER_FETCH_PROFILE
static void buzz_vertex_fetch_profile(const rsx_vertex_fetch_plan* plan, u32 count) {
    static uint64_t formats[8][5], frequency[3], calls;
    if(!plan)return;
    for(u32 slot=0;slot<plan->layout.count;++slot) {
        const rsx_vertex_fetch_attr* f=&plan->attr[plan->layout.attrs[slot]];
        if(f->desc.type<8 && f->desc.size<5)formats[f->desc.type][f->desc.size]+=count;
        frequency[f->desc.frequency<=1?0:f->stable_base?1:2]+=count;
    }
    if((++calls & 65535u)==0) {
        fprintf(stderr,"[vertex fetch] calls=%llu frequency plain=%llu instanced-stable=%llu other=%llu\n",
            (unsigned long long)calls,(unsigned long long)frequency[0],
            (unsigned long long)frequency[1],(unsigned long long)frequency[2]);
        for(u32 type=0;type<8;++type)for(u32 size=0;size<5;++size)if(formats[type][size])
            fprintf(stderr,"[vertex fetch] type=%u size=%u values=%llu\n",type,size,(unsigned long long)formats[type][size]);
    }
}
#endif
static inline int buzz_vertex_fetch_many(const rsx_vertex_fetch_plan* plan,
    const rsx_vertex_ref* refs,u32 count,u8* out) {
    if(!plan || !refs || !out || count<4 || !plan->layout.count)
        return rsx_vertex_fetch_many(plan,refs,count,out);
    /* Validate the whole draw BEFORE writing output. Unsupported/failing
     * draws retain the SDK's resolver order, partial writes and return code. */
    for(u32 slot=0;slot<plan->layout.count;++slot) {
        const rsx_vertex_fetch_attr* f=&plan->attr[plan->layout.attrs[slot]];
        if(!f->desc.type || !f->desc.size)continue;
        if(!f->stable_base || f->desc.frequency>1 || f->desc.type>7 || f->desc.size>4)
            return rsx_vertex_fetch_many(plan,refs,count,out);
    }
    u32 first=UINT_MAX,last=0;
    for(u32 i=0;i<count;++i) {
        const u32 element=(refs[i].vertex_id+refs[i].base_index)&0xfffffu;
        if(element<first)first=element;if(element>last)last=element;
    }
    for(u32 slot=0;slot<plan->layout.count;++slot) {
        const rsx_vertex_fetch_attr* f=&plan->attr[plan->layout.attrs[slot]];
        if(f->desc.type && f->desc.size && (first<f->stable_first || last>f->stable_last))
            return rsx_vertex_fetch_many(plan,refs,count,out);
    }
    // The usual position/normal/UV layout writes one complete output vertex
    // at a time. Reuse the element index and keep all three stores together.
    if(plan->layout.count==3 && plan->layout.stride==48) {
        const rsx_vertex_fetch_attr *position=NULL,*normal=NULL,*uv=NULL;
        u32 po=0,no=0,uo=0;
        for(u32 slot=0;slot<3;++slot) {
            const rsx_vertex_fetch_attr* f=&plan->attr[plan->layout.attrs[slot]];
            if(f->desc.type==RSX_VERTEX_TYPE_F && f->desc.size==3){position=f;po=slot*16;}
            if(f->desc.type==RSX_VERTEX_TYPE_CMP && f->desc.size==1){normal=f;no=slot*16;}
            if(f->desc.type==RSX_VERTEX_TYPE_SF && f->desc.size==2){uv=f;uo=slot*16;}
        }
        if(position && normal && uv) {
            for(u32 i=0;i<count;++i) {
                const u32 element=(refs[i].vertex_id+refs[i].base_index)&0xfffffu;
                u8* vertex=out+(u64)i*48;
                buzz_fetch_float(position->stable_base+(u64)(element-position->stable_first)*position->stride,vertex+po,3);
                buzz_fetch_cmp(normal->stable_base+(u64)(element-normal->stable_first)*normal->stride,vertex+no);
                buzz_fetch_half(uv->stable_base+(u64)(element-uv->stable_first)*uv->stride,vertex+uo,2);
            }
            return 2;
        }
    }
    for(u32 slot=0;slot<plan->layout.count;++slot) {
        const rsx_vertex_fetch_attr* f=&plan->attr[plan->layout.attrs[slot]];
        u8* column=out+slot*16u;
        if(!f->desc.type || !f->desc.size) {
            for(u32 i=0;i<count;++i)memcpy(column+(u64)i*plan->layout.stride,f->default_value,16);
            continue;
        }
#define BUZZ_FETCH_LOOP(operation) do { \
        for(u32 i=0;i<count;++i) { \
            const u32 element=(refs[i].vertex_id+refs[i].base_index)&0xfffffu; \
            const u8* source=f->stable_base+(u64)(element-f->stable_first)*f->stride; \
            u8* destination=column+(u64)i*plan->layout.stride; \
            operation; \
        } \
    } while(0)
#define BUZZ_FETCH_SIZES(function) switch(f->desc.size) { \
        case 1: BUZZ_FETCH_LOOP(function(source,destination,1));break; \
        case 2: BUZZ_FETCH_LOOP(function(source,destination,2));break; \
        case 3: BUZZ_FETCH_LOOP(function(source,destination,3));break; \
        case 4: BUZZ_FETCH_LOOP(function(source,destination,4));break; \
    }
        switch(f->desc.type) {
        case RSX_VERTEX_TYPE_F: BUZZ_FETCH_SIZES(buzz_fetch_float);break;
        case RSX_VERTEX_TYPE_SF: BUZZ_FETCH_SIZES(buzz_fetch_half);break;
        case RSX_VERTEX_TYPE_CMP: BUZZ_FETCH_LOOP(buzz_fetch_cmp(source,destination));break;
        default: BUZZ_FETCH_LOOP(float value[4];rsx_vertex_decode_element(f->desc.type,f->desc.size,source,value);memcpy(destination,value,16));break;
        }
#undef BUZZ_FETCH_SIZES
#undef BUZZ_FETCH_LOOP
    }
    return 2;
}
