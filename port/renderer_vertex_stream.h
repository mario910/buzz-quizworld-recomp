#pragma once
#include "renderer_vertex_fetch.h"
#include "renderer_normalize.h"

/* Four independent vertices per vector. Integer half expansion preserves the
 * SDK's subnormal flush and NaN payloads; normal conversion preserves the
 * division result, including rounding modes. No source padding read or
 * cross-draw cache. Optional exact CMP arithmetic is in renderer_normalize.h. */
#if BUZZ_FETCH_NEON
typedef uint32x4_t buzz_stream_bits;
static inline buzz_stream_bits buzz_stream_be4(const u32 words[4]) {
    return vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8((const u8*)words)));
}
static inline buzz_stream_bits buzz_stream_half4(buzz_stream_bits h) {
    const uint32x4_t e=vandq_u32(h,vdupq_n_u32(0x7c00));
    const uint32x4_t sign=vshlq_n_u32(vandq_u32(h,vdupq_n_u32(0x8000)),16);
    uint32x4_t body=vaddq_u32(vshlq_n_u32(vandq_u32(h,vdupq_n_u32(0x7fff)),13),vdupq_n_u32(0x38000000));
    body=vbslq_u32(vceqq_u32(e,vdupq_n_u32(0x7c00)),
        vorrq_u32(vshlq_n_u32(vandq_u32(h,vdupq_n_u32(0x3ff)),13),vdupq_n_u32(0x7f800000)),body);
    return vorrq_u32(sign,vbicq_u32(body,vceqq_u32(e,vdupq_n_u32(0))));
}
static inline void buzz_stream_store4(u8* out,u32 stride,buzz_stream_bits x,buzz_stream_bits y,buzz_stream_bits z,buzz_stream_bits w) {
    const uint32x4x2_t xy=vzipq_u32(x,y),zw=vzipq_u32(z,w);
    vst1q_u8(out,vreinterpretq_u8_u32(vcombine_u32(vget_low_u32(xy.val[0]),vget_low_u32(zw.val[0]))));
    vst1q_u8(out+stride,vreinterpretq_u8_u32(vcombine_u32(vget_high_u32(xy.val[0]),vget_high_u32(zw.val[0]))));
    vst1q_u8(out+2u*stride,vreinterpretq_u8_u32(vcombine_u32(vget_low_u32(xy.val[1]),vget_low_u32(zw.val[1]))));
    vst1q_u8(out+3u*stride,vreinterpretq_u8_u32(vcombine_u32(vget_high_u32(xy.val[1]),vget_high_u32(zw.val[1]))));
}
static inline void buzz_stream_decode4(const u32 normal[4],const u32 uv[4],u8* no,u8* uo,u32 stride) {
    const uint32x4_t n=buzz_stream_be4(normal),h=buzz_stream_be4(uv);
    const float32x4_t x=vcvtq_f32_s32(vshrq_n_s32(vreinterpretq_s32_u32(vshlq_n_u32(n,21)),21));
    const float32x4_t y=vcvtq_f32_s32(vshrq_n_s32(vreinterpretq_s32_u32(vshlq_n_u32(n,10)),21));
    const float32x4_t z=vcvtq_f32_s32(vshrq_n_s32(vreinterpretq_s32_u32(n),22));
    const uint32x4_t one=vdupq_n_u32(0x3f800000);
#if BUZZ_RENDERER_NORMAL_MULTIPLY
    if(buzz_normal_multiply_enabled())
        buzz_stream_store4(no,stride,buzz_normal_multiply4(x,0),buzz_normal_multiply4(y,0),buzz_normal_multiply4(z,1),one);
    else
#endif
    buzz_stream_store4(no,stride,vreinterpretq_u32_f32(vdivq_f32(x,vdupq_n_f32(1023))),
        vreinterpretq_u32_f32(vdivq_f32(y,vdupq_n_f32(1023))),vreinterpretq_u32_f32(vdivq_f32(z,vdupq_n_f32(511))),one);
    buzz_stream_store4(uo,stride,buzz_stream_half4(vshrq_n_u32(h,16)),
        buzz_stream_half4(vandq_u32(h,vdupq_n_u32(0xffff))),vdupq_n_u32(0),one);
}
#elif BUZZ_FETCH_SSE
typedef __m128i buzz_stream_bits;
static inline buzz_stream_bits buzz_stream_be4(const u32 words[4]) {
    __m128i v=_mm_loadu_si128((const __m128i*)words);
#if defined(__SSSE3__) || defined(__AVX2__)
    return _mm_shuffle_epi8(v,_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12));
#else
    v=_mm_or_si128(_mm_slli_epi16(v,8),_mm_srli_epi16(v,8));
    return _mm_shufflehi_epi16(_mm_shufflelo_epi16(v,_MM_SHUFFLE(2,3,0,1)),_MM_SHUFFLE(2,3,0,1));
#endif
}
static inline buzz_stream_bits buzz_stream_half4(buzz_stream_bits h) {
    const __m128i e=_mm_and_si128(h,_mm_set1_epi32(0x7c00));
    const __m128i sign=_mm_slli_epi32(_mm_and_si128(h,_mm_set1_epi32(0x8000)),16);
    __m128i body=_mm_add_epi32(_mm_slli_epi32(_mm_and_si128(h,_mm_set1_epi32(0x7fff)),13),_mm_set1_epi32(0x38000000));
    const __m128i exceptional=_mm_cmpeq_epi32(e,_mm_set1_epi32(0x7c00));
    const __m128i special=_mm_or_si128(_mm_slli_epi32(_mm_and_si128(h,_mm_set1_epi32(0x3ff)),13),_mm_set1_epi32(0x7f800000));
    body=_mm_or_si128(_mm_and_si128(exceptional,special),_mm_andnot_si128(exceptional,body));
    return _mm_or_si128(sign,_mm_andnot_si128(_mm_cmpeq_epi32(e,_mm_setzero_si128()),body));
}
static inline void buzz_stream_store4(u8* out,u32 stride,buzz_stream_bits xi,buzz_stream_bits yi,buzz_stream_bits zi,buzz_stream_bits wi) {
    __m128 x=_mm_castsi128_ps(xi),y=_mm_castsi128_ps(yi),z=_mm_castsi128_ps(zi),w=_mm_castsi128_ps(wi);
    _MM_TRANSPOSE4_PS(x,y,z,w);
    _mm_storeu_si128((__m128i*)out,_mm_castps_si128(x));
    _mm_storeu_si128((__m128i*)(out+stride),_mm_castps_si128(y));
    _mm_storeu_si128((__m128i*)(out+2u*stride),_mm_castps_si128(z));
    _mm_storeu_si128((__m128i*)(out+3u*stride),_mm_castps_si128(w));
}
static inline void buzz_stream_decode4(const u32 normal[4],const u32 uv[4],u8* no,u8* uo,u32 stride) {
    const __m128i n=buzz_stream_be4(normal),h=buzz_stream_be4(uv);
    const __m128 x=_mm_cvtepi32_ps(_mm_srai_epi32(_mm_slli_epi32(n,21),21));
    const __m128 y=_mm_cvtepi32_ps(_mm_srai_epi32(_mm_slli_epi32(n,10),21));
    const __m128 z=_mm_cvtepi32_ps(_mm_srai_epi32(n,22));
    const __m128i one=_mm_set1_epi32(0x3f800000);
#if BUZZ_RENDERER_NORMAL_MULTIPLY
    if(buzz_normal_multiply_enabled())
        buzz_stream_store4(no,stride,buzz_normal_multiply4(x,0),buzz_normal_multiply4(y,0),buzz_normal_multiply4(z,1),one);
    else
#endif
    buzz_stream_store4(no,stride,_mm_castps_si128(_mm_div_ps(x,_mm_set1_ps(1023))),
        _mm_castps_si128(_mm_div_ps(y,_mm_set1_ps(1023))),_mm_castps_si128(_mm_div_ps(z,_mm_set1_ps(511))),one);
    buzz_stream_store4(uo,stride,buzz_stream_half4(_mm_srli_epi32(h,16)),
        buzz_stream_half4(_mm_and_si128(h,_mm_set1_epi32(0xffff))),_mm_setzero_si128(),one);
}
#endif

static inline int buzz_vertex_stream(const rsx_vertex_fetch_plan* plan,const rsx_vertex_ref* refs,u32 count,u8* out) {
#if BUZZ_FETCH_NEON || BUZZ_FETCH_SSE
    if(!plan || !refs || !out || count<4 || plan->layout.count!=3 || plan->layout.stride!=48)
        return buzz_vertex_fetch_many(plan,refs,count,out);
    const rsx_vertex_fetch_attr *p=NULL,*n=NULL,*uv=NULL;
    u32 po=0,no=0,uo=0;
    for(u32 slot=0;slot<3;++slot) {
        const rsx_vertex_fetch_attr* f=&plan->attr[plan->layout.attrs[slot]];
        if(!f->stable_base || f->desc.frequency>1)return buzz_vertex_fetch_many(plan,refs,count,out);
        if(f->desc.type==RSX_VERTEX_TYPE_F && f->desc.size==3){p=f;po=slot*16;}
        if(f->desc.type==RSX_VERTEX_TYPE_CMP && f->desc.size==1){n=f;no=slot*16;}
        if(f->desc.type==RSX_VERTEX_TYPE_SF && f->desc.size==2){uv=f;uo=slot*16;}
    }
    if(!p || !n || !uv)return buzz_vertex_fetch_many(plan,refs,count,out);
    u32 first=UINT_MAX,last=0;
    for(u32 i=0;i<count;++i) {
        const u32 element=(refs[i].vertex_id+refs[i].base_index)&0xfffffu;
        if(element<first)first=element;if(element>last)last=element;
    }
    if(first<p->stable_first || last>p->stable_last || first<n->stable_first || last>n->stable_last ||
       first<uv->stable_first || last>uv->stable_last)return buzz_vertex_fetch_many(plan,refs,count,out);
    u32 i=0;
    for(;count-i>=4;i+=4) {
        u32 normal[4],texcoord[4];
        for(u32 lane=0;lane<4;++lane) {
            const u32 element=(refs[i+lane].vertex_id+refs[i+lane].base_index)&0xfffffu;
            buzz_fetch_float(p->stable_base+(u64)(element-p->stable_first)*p->stride,out+(u64)(i+lane)*48+po,3);
            memcpy(&normal[lane],n->stable_base+(u64)(element-n->stable_first)*n->stride,4);
            memcpy(&texcoord[lane],uv->stable_base+(u64)(element-uv->stable_first)*uv->stride,4);
        }
        buzz_stream_decode4(normal,texcoord,out+(u64)i*48+no,out+(u64)i*48+uo,48);
    }
    for(;i<count;++i) {
        const u32 element=(refs[i].vertex_id+refs[i].base_index)&0xfffffu;
        u8* vertex=out+(u64)i*48;
        buzz_fetch_float(p->stable_base+(u64)(element-p->stable_first)*p->stride,vertex+po,3);
        buzz_fetch_cmp(n->stable_base+(u64)(element-n->stable_first)*n->stride,vertex+no);
        buzz_fetch_half(uv->stable_base+(u64)(element-uv->stable_first)*uv->stride,vertex+uo,2);
    }
    return 2;
#else
    return buzz_vertex_fetch_many(plan,refs,count,out);
#endif
}
