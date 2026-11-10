#pragma once
// Eight host-order lanes are two native NEON registers. No AVX emulation runtime.
#include <arm_neon.h>
#include <stdint.h>
#include <string.h>
typedef uint32x4_t __m128i;
typedef struct { uint32x4_t lo, hi; } __m256i;
typedef struct { float32x4_t lo, hi; } __m256;
#define BNI static inline __attribute__((always_inline))
BNI __m256i _mm256_set_m128i(__m128i h,__m128i l){return {l,h};}
BNI __m128i _mm_loadu_si128(const __m128i* p){return vld1q_u32((const uint32_t*)p);}
BNI void _mm_storeu_si128(__m128i* p,__m128i a){vst1q_u32((uint32_t*)p,a);}
BNI __m256i _mm256_loadu_si256(const __m256i* p){return {vld1q_u32((const uint32_t*)p),vld1q_u32((const uint32_t*)p+4)};}
BNI void _mm256_storeu_si256(__m256i* p,__m256i a){vst1q_u32((uint32_t*)p,a.lo);vst1q_u32((uint32_t*)p+4,a.hi);}
// AVX mask load/store must not touch masked-off lanes, including the tail
// past a bounded clip/curve stream. A full NEON load followed by a select
// would overread that stream (and can cross a protected page).
BNI __m256i _mm256_maskload_epi32(const int* p,__m256i mask){
    uint32_t bits[8],value[8]{};
    _mm256_storeu_si256((__m256i*)bits,mask);
    for(unsigned lane=0;lane<8;++lane)if(bits[lane]>>31)memcpy(value+lane,p+lane,4);
    return _mm256_loadu_si256((const __m256i*)value);
}
BNI void _mm256_maskstore_epi32(int* p,__m256i mask,__m256i value){
    uint32_t bits[8],words[8];
    _mm256_storeu_si256((__m256i*)bits,mask);
    _mm256_storeu_si256((__m256i*)words,value);
    for(unsigned lane=0;lane<8;++lane)if(bits[lane]>>31)memcpy(p+lane,words+lane,4);
}
#define _mm256_load_si256 _mm256_loadu_si256
#define _mm256_store_si256 _mm256_storeu_si256
BNI __m256i _mm256_set1_epi32(int x){return {vdupq_n_u32(x),vdupq_n_u32(x)};}
BNI __m256i _mm256_setzero_si256(){return _mm256_set1_epi32(0);}
BNI __m128i _mm_setzero_si128(){return vdupq_n_u32(0);}
BNI __m128i _mm_max_epu16(__m128i a,__m128i b){return vreinterpretq_u32_u16(vmaxq_u16(vreinterpretq_u16_u32(a),vreinterpretq_u16_u32(b)));}
BNI __m128i _mm_set1_epi32(int x){return vdupq_n_u32(x);}
BNI __m256 _mm256_set1_ps(float x){return {vdupq_n_f32(x),vdupq_n_f32(x)};}
BNI __m256 _mm256_castsi256_ps(__m256i a){return {vreinterpretq_f32_u32(a.lo),vreinterpretq_f32_u32(a.hi)};}
BNI __m256i _mm256_castps_si256(__m256 a){return {vreinterpretq_u32_f32(a.lo),vreinterpretq_u32_f32(a.hi)};}
BNI __m128i _mm256_castsi256_si128(__m256i a){return a.lo;}
BNI __m128i _mm256_extracti128_si256(__m256i a,int n){return n?a.hi:a.lo;}
BNI __m256i _mm256_broadcastsi128_si256(__m128i a){return {a,a};}
BNI __m256i _mm256_setr_epi32(int a,int b,int c,int d,int e,int f,int g,int h){uint32_t v[8]={(uint32_t)a,(uint32_t)b,(uint32_t)c,(uint32_t)d,(uint32_t)e,(uint32_t)f,(uint32_t)g,(uint32_t)h};return _mm256_loadu_si256((const __m256i*)v);}
BNI int _mm256_extract_epi32(__m256i a,int n){uint32_t v[8];_mm256_storeu_si256((__m256i*)v,a);return v[n&7];}
BNI __m256i _mm256_and_si256(__m256i a,__m256i b){return {vandq_u32(a.lo,b.lo),vandq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_or_si256(__m256i a,__m256i b){return {vorrq_u32(a.lo,b.lo),vorrq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_xor_si256(__m256i a,__m256i b){return {veorq_u32(a.lo,b.lo),veorq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_andnot_si256(__m256i a,__m256i b){return {vbicq_u32(b.lo,a.lo),vbicq_u32(b.hi,a.hi)};}
BNI __m256i _mm256_add_epi32(__m256i a,__m256i b){return {vaddq_u32(a.lo,b.lo),vaddq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_sub_epi32(__m256i a,__m256i b){return {vsubq_u32(a.lo,b.lo),vsubq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_mullo_epi32(__m256i a,__m256i b){return {vmulq_u32(a.lo,b.lo),vmulq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_cmpeq_epi32(__m256i a,__m256i b){return {vceqq_u32(a.lo,b.lo),vceqq_u32(a.hi,b.hi)};}
BNI __m256i _mm256_cmpgt_epi32(__m256i a,__m256i b){return {vcgtq_s32(vreinterpretq_s32_u32(a.lo),vreinterpretq_s32_u32(b.lo)),vcgtq_s32(vreinterpretq_s32_u32(a.hi),vreinterpretq_s32_u32(b.hi))};}
BNI __m256i _mm256_cmpeq_epi16(__m256i a,__m256i b){return {vreinterpretq_u32_u16(vceqq_u16(vreinterpretq_u16_u32(a.lo),vreinterpretq_u16_u32(b.lo))),vreinterpretq_u32_u16(vceqq_u16(vreinterpretq_u16_u32(a.hi),vreinterpretq_u16_u32(b.hi)))};}
BNI __m256 _mm256_add_ps(__m256 a,__m256 b){return {vaddq_f32(a.lo,b.lo),vaddq_f32(a.hi,b.hi)};}
BNI __m256 _mm256_mul_ps(__m256 a,__m256 b){return {vmulq_f32(a.lo,b.lo),vmulq_f32(a.hi,b.hi)};}
BNI __m256 _mm256_fmadd_ps(__m256 a,__m256 b,__m256 c){return {vfmaq_f32(c.lo,a.lo,b.lo),vfmaq_f32(c.hi,a.hi,b.hi)};}
BNI __m256i _mm256_slli_epi32(__m256i a,int n){int32x4_t k=vdupq_n_s32(n>=32?32:n);return {vshlq_u32(a.lo,k),vshlq_u32(a.hi,k)};}
BNI __m256i _mm256_srli_epi32(__m256i a,int n){int32x4_t k=vdupq_n_s32(n>=32?-32:-n);return {vshlq_u32(a.lo,k),vshlq_u32(a.hi,k)};}
BNI __m256i _mm256_srai_epi32(__m256i a,int n){int32x4_t k=vdupq_n_s32(n>=31?-31:-n);return {vreinterpretq_u32_s32(vshlq_s32(vreinterpretq_s32_u32(a.lo),k)),vreinterpretq_u32_s32(vshlq_s32(vreinterpretq_s32_u32(a.hi),k))};}
BNI __m256i _mm256_sllv_epi32(__m256i a,__m256i n){return {vshlq_u32(a.lo,vreinterpretq_s32_u32(vminq_u32(n.lo,vdupq_n_u32(32)))),vshlq_u32(a.hi,vreinterpretq_s32_u32(vminq_u32(n.hi,vdupq_n_u32(32))))};}
BNI __m256i _mm256_srlv_epi32(__m256i a,__m256i n){return {vshlq_u32(a.lo,vnegq_s32(vreinterpretq_s32_u32(vminq_u32(n.lo,vdupq_n_u32(32))))),vshlq_u32(a.hi,vnegq_s32(vreinterpretq_s32_u32(vminq_u32(n.hi,vdupq_n_u32(32)))))};}
BNI int _mm256_movemask_ps(__m256 a){
    const uint32_t weights[4]={1,2,4,8};
    const uint32x4_t w=vld1q_u32(weights);
    return vaddvq_u32(vmulq_u32(vshrq_n_u32(vreinterpretq_u32_f32(a.lo),31),w)) |
        (vaddvq_u32(vmulq_u32(vshrq_n_u32(vreinterpretq_u32_f32(a.hi),31),w))<<4);
}
BNI unsigned buzz_neon_byte_mask(uint32x4_t a){
    const uint8_t weights[16]={1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
    const uint8x16_t bits=vmulq_u8(vshrq_n_u8(vreinterpretq_u8_u32(a),7),vld1q_u8(weights));
    return unsigned(vaddv_u8(vget_low_u8(bits))) | (unsigned(vaddv_u8(vget_high_u8(bits)))<<8);
}
BNI int _mm256_movemask_epi8(__m256i a){return int(buzz_neon_byte_mask(a.lo)|(buzz_neon_byte_mask(a.hi)<<16));}
BNI __m256i _mm256_blend_epi32(__m256i a,__m256i b,int mask){uint32_t m[8];for(int i=0;i<8;i++)m[i]=mask&(1<<i)?~0u:0;return {vbslq_u32(vld1q_u32(m),b.lo,a.lo),vbslq_u32(vld1q_u32(m+4),b.hi,a.hi)};}
BNI __m256i _mm256_blendv_epi8(__m256i a,__m256i b,__m256i m){return {vreinterpretq_u32_u8(vbslq_u8(vreinterpretq_u8_s8(vshrq_n_s8(vreinterpretq_s8_u32(m.lo),7)),vreinterpretq_u8_u32(b.lo),vreinterpretq_u8_u32(a.lo))),vreinterpretq_u32_u8(vbslq_u8(vreinterpretq_u8_s8(vshrq_n_s8(vreinterpretq_s8_u32(m.hi),7)),vreinterpretq_u8_u32(b.hi),vreinterpretq_u8_u32(a.hi)))};}
BNI __m256i _mm256_i32gather_epi32(const int* base,__m256i idx,int scale){int32_t k[8];uint32_t v[8];_mm256_storeu_si256((__m256i*)k,idx);for(int i=0;i<8;i++)memcpy(v+i,(const char*)base+(int64_t)k[i]*scale,4);return _mm256_loadu_si256((const __m256i*)v);}
BNI __m256i _mm256_mask_i32gather_epi32(__m256i src,const int* base,__m256i idx,__m256i mask,int scale){int32_t k[8],m[8];uint32_t v[8];_mm256_storeu_si256((__m256i*)k,idx);_mm256_storeu_si256((__m256i*)m,mask);_mm256_storeu_si256((__m256i*)v,src);for(int i=0;i<8;i++)if(m[i]<0)memcpy(v+i,(const char*)base+(int64_t)k[i]*scale,4);return _mm256_loadu_si256((const __m256i*)v);}
BNI __m256i _mm256_permutevar8x32_epi32(__m256i a,__m256i idx){
    // Convert eight word indices into two byte-table lookups across both halves.
    uint8x16x2_t table={{vreinterpretq_u8_u32(a.lo),vreinterpretq_u8_u32(a.hi)}};
    uint32x4_t lo=vaddq_u32(vmulq_u32(vandq_u32(idx.lo,vdupq_n_u32(7)),vdupq_n_u32(0x04040404)),vdupq_n_u32(0x03020100));
    uint32x4_t hi=vaddq_u32(vmulq_u32(vandq_u32(idx.hi,vdupq_n_u32(7)),vdupq_n_u32(0x04040404)),vdupq_n_u32(0x03020100));
    return {vreinterpretq_u32_u8(vqtbl2q_u8(table,vreinterpretq_u8_u32(lo))),vreinterpretq_u32_u8(vqtbl2q_u8(table,vreinterpretq_u8_u32(hi)))};
}
BNI __m256i _mm256_shuffle_epi32(__m256i a,int k){return _mm256_permutevar8x32_epi32(a,_mm256_setr_epi32(k&3,(k>>2)&3,(k>>4)&3,(k>>6)&3,4+(k&3),4+((k>>2)&3),4+((k>>4)&3),4+((k>>6)&3)));}
BNI __m128i _mm_shuffle_epi8(__m128i a,__m128i k){uint8x16_t idx=vandq_u8(vreinterpretq_u8_u32(k),vdupq_n_u8(0x8f));return vreinterpretq_u32_u8(vqtbl1q_u8(vreinterpretq_u8_u32(a),idx));}
BNI __m256i _mm256_shuffle_epi8(__m256i a,__m256i k){return {_mm_shuffle_epi8(a.lo,k.lo),_mm_shuffle_epi8(a.hi,k.hi)};}
BNI __m256i _mm256_cvtepu16_epi32(__m128i a){return {vmovl_u16(vget_low_u16(vreinterpretq_u16_u32(a))),vmovl_u16(vget_high_u16(vreinterpretq_u16_u32(a)))};}
BNI __m256 _mm256_cvtepi32_ps(__m256i a){return {vcvtq_f32_s32(vreinterpretq_s32_u32(a.lo)),vcvtq_f32_s32(vreinterpretq_s32_u32(a.hi))};}
BNI __m256i _mm256_cvttps_epi32(__m256 a){return {vreinterpretq_u32_s32(vcvtq_s32_f32(a.lo)),vreinterpretq_u32_s32(vcvtq_s32_f32(a.hi))};}
#define _CMP_GE_OQ 29
BNI __m256 _mm256_cmp_ps(__m256 a,__m256 b,int predicate){return _mm256_castsi256_ps({vcgeq_f32(a.lo,b.lo),vcgeq_f32(a.hi,b.hi)});}
BNI __m128i _mm_setr_epi8(int a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,int a9,int a10,int a11,int a12,int a13,int a14,int a15){uint8_t v[16]={(uint8_t)a0,(uint8_t)a1,(uint8_t)a2,(uint8_t)a3,(uint8_t)a4,(uint8_t)a5,(uint8_t)a6,(uint8_t)a7,(uint8_t)a8,(uint8_t)a9,(uint8_t)a10,(uint8_t)a11,(uint8_t)a12,(uint8_t)a13,(uint8_t)a14,(uint8_t)a15};return _mm_loadu_si128((const __m128i*)v);}
BNI __m256i _mm256_setr_epi8(int a0,int a1,int a2,int a3,int a4,int a5,int a6,int a7,int a8,int a9,int a10,int a11,int a12,int a13,int a14,int a15,int a16,int a17,int a18,int a19,int a20,int a21,int a22,int a23,int a24,int a25,int a26,int a27,int a28,int a29,int a30,int a31){uint8_t v[32]={(uint8_t)a0,(uint8_t)a1,(uint8_t)a2,(uint8_t)a3,(uint8_t)a4,(uint8_t)a5,(uint8_t)a6,(uint8_t)a7,(uint8_t)a8,(uint8_t)a9,(uint8_t)a10,(uint8_t)a11,(uint8_t)a12,(uint8_t)a13,(uint8_t)a14,(uint8_t)a15,(uint8_t)a16,(uint8_t)a17,(uint8_t)a18,(uint8_t)a19,(uint8_t)a20,(uint8_t)a21,(uint8_t)a22,(uint8_t)a23,(uint8_t)a24,(uint8_t)a25,(uint8_t)a26,(uint8_t)a27,(uint8_t)a28,(uint8_t)a29,(uint8_t)a30,(uint8_t)a31};return _mm256_loadu_si256((const __m256i*)v);}
BNI int _mm_popcnt_u32(unsigned a){return __builtin_popcount(a);}
#define _MM_SHUFFLE(a,b,c,d) (((a)<<6)|((b)<<4)|((c)<<2)|(d))
BNI __m128i _mm_blend_epi32(__m128i a,__m128i b,int m){return _mm256_blend_epi32({a,a},{b,b},m).lo;}
BNI __m128i _mm_set1_epi8(int a){return vreinterpretq_u32_u8(vdupq_n_u8(a));}
BNI __m128i _mm_add_epi8(__m128i a,__m128i b){return vreinterpretq_u32_u8(vaddq_u8(vreinterpretq_u8_u32(a),vreinterpretq_u8_u32(b)));}
BNI __m128i _mm_alignr_epi8(__m128i a,__m128i b,int n){uint8x16x2_t t={{vreinterpretq_u8_u32(b),vreinterpretq_u8_u32(a)}};uint8_t i[16];for(int k=0;k<16;k++)i[k]=n+k<32?n+k:255;return vreinterpretq_u32_u8(vqtbl2q_u8(t,vld1q_u8(i)));}
BNI __m128i _mm_srli_si128(__m128i a,int n){return _mm_alignr_epi8(vdupq_n_u32(0),a,n);}
#undef BNI
