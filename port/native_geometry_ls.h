#ifndef BUZZ_NATIVE_GEOMETRY_LS_H
#define BUZZ_NATIVE_GEOMETRY_LS_H
/* Only for the audited pure image41 kernels, after their entry guard rejects
 * watch/probe modes and any LS not owned by the private spu_context. No DMA,
 * channels, callbacks or ownership changes occur inside these kernels.
 * Addresses still wrap to 256 KiB and align DOWN to a whole quadword. */
static inline __attribute__((always_inline)) u128 buzz_geometry_ls_read(const uint8_t* memory,uint32_t address)
{
    u128 result;address&=SPU_LS_MASK&~15u;
#if defined(__SSSE3__)
    const __m128i order=_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
    __m128i v=_mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(memory+address)),order);
    memcpy(&result,&v,16);
#else
    for(unsigned n=0;n<4;++n){uint32_t v;memcpy(&v,memory+address+n*4,4);result._u32[n]=SPU_BSWAP32(v);}
#endif
    return result;
}
static inline __attribute__((always_inline)) void buzz_geometry_ls_write(uint8_t* memory,uint32_t address,u128 value)
{
    address&=SPU_LS_MASK&~15u;
#if defined(__SSSE3__)
    const __m128i order=_mm_setr_epi8(3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12);
    __m128i v;memcpy(&v,&value,16);_mm_storeu_si128((__m128i*)(memory+address),_mm_shuffle_epi8(v,order));
#else
    for(unsigned n=0;n<4;++n){uint32_t v=SPU_BSWAP32(value._u32[n]);memcpy(memory+address+n*4,&v,4);}
#endif
}
#endif
