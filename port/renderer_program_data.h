#pragma once
#include "rsx_fp_decompiler.h"
#include <string.h>

typedef struct { u32 sampled, gamma, expand; } buzz_fp_usage;

/* Same half-word-swapped BE encoding as rsx_fp_read_word, inlined across the
 * port/SDK boundary. Byte loads also admit unaligned and page-end inputs. */
static inline u32 buzz_fp_word(const u8* p)
{
    return ((u32)p[2]<<24)|((u32)p[3]<<16)|((u32)p[0]<<8)|p[1];
}

/* The three SDK queries deliberately have different opcode/END rules.
 * Preserve those rules while reading each instruction only once. */
static inline buzz_fp_usage buzz_fp_usage_read(const u8* code, u32 bytes)
{
    buzz_fp_usage result = {0,0,0};
    if (!code) return result;
    for (u32 off=0; off<=bytes && bytes-off>=16;) {
        const u32 w0=buzz_fp_word(code+off), w1=buzz_fp_word(code+off+4);
        const u32 w2=buzz_fp_word(code+off+8), w3=buzz_fp_word(code+off+12);
        const u32 op=(w0>>24)&63u, bit=1u<<((w0>>17)&15u);
        const int branch=(w2>>31)!=0;
        const int basic=op==0x17 || op==0x18 || op==0x31 || op==0x2f;
        if (!branch && (basic || op==0x19 || op==0x33 || op==0x34)) {
            result.sampled |= bit;
            if (!(w0&(1u<<21))) result.gamma |= bit;
            if (basic) result.expand |= bit;
        }
        off+=16;
        if (!branch && ((w1&3u)==2 || (w2&3u)==2 || (w3&3u)==2)) off+=16;
        if (w0&1u) return result;
    }
    /* texture_units returns partial usage; gamma/expand require END. */
    result.gamma=result.expand=0;
    return result;
}

static inline u32 buzz_fp_usage_mask(u32 candidates, const u8 masks[16])
{
    u32 result=0;
    if (masks) for (u32 unit=0;unit<16;++unit)
        if (masks[unit]) result |= candidates&(1u<<unit);
    return result;
}

enum { BUZZ_BYTE_HASH_SLOTS=16, BUZZ_PROGRAM_BYTES=8192 };
typedef struct { u64 seed,hash; u32 bytes; u8 code[BUZZ_PROGRAM_BYTES]; } buzz_byte_hash_entry;
typedef struct { buzz_byte_hash_entry entry[BUZZ_BYTE_HASH_SLOTS]; } buzz_byte_hash_cache;

static inline u64 buzz_program_hash(const u8* code,u32 bytes,u64 seed)
{
    for (u32 i=0;i<bytes;++i) { seed^=code[i];seed*=1099511628211ull; }
    return seed;
}

/* FIFO-owned. A sampled key selects a slot, never proves a cache hit. */
static inline u64 buzz_program_hash_cached(buzz_byte_hash_cache* cache,const u8* code,u32 bytes,u64 seed)
{
    if (!cache || !code || bytes<16 || bytes>BUZZ_PROGRAM_BYTES)
        return buzz_program_hash(code,bytes,seed);
    u32 first,middle,last;
    memcpy(&first,code,4);memcpy(&middle,code+bytes/2,4);memcpy(&last,code+bytes-4,4);
    u32 key=(first^(middle*0x9e3779b9u)^last^bytes^(u32)seed^(u32)(seed>>32))*0x85ebca6bu;
    key^=key>>16;
    buzz_byte_hash_entry* entry=&cache->entry[key&(BUZZ_BYTE_HASH_SLOTS-1)];
    if (entry->bytes==bytes && entry->seed==seed && !memcmp(entry->code,code,bytes)) return entry->hash;
    const u64 result=buzz_program_hash(code,bytes,seed);
    memcpy(entry->code,code,bytes);entry->bytes=bytes;entry->seed=seed;entry->hash=result;
    return result;
}

static inline u32 buzz_renderer_sanitize_constant(u32 word)
{
    float value;memcpy(&value,&word,sizeof value);
    if (value==value && value<=3.0e38f && value>=-3.0e38f) return word;
    ++g_rsx_fp_nonfinite_constants;
    return 0;
}

/* Only for the renderer's private, zero-initialized block. Its unused tail
 * remains zero after EVERY call, including malformed input and shrinking
 * programs. Refresh/sanitize every active value, and clear only the old live
 * suffix. No constant or sanitizer side effect is cached. */
static inline void buzz_renderer_clear_constants(rsx_fp_constant_block* out)
{
    if(out->count>RSX_FP_MAX_INLINE_CONSTANTS) {memset(out,0,sizeof(*out));return;}
    memset(out->values,0,(size_t)out->count*sizeof(out->values[0]));
    out->count=0;
}

static inline int buzz_renderer_collect_constants(const u8* code,u32 bytes,rsx_fp_constant_block* out)
{
    if (!code || !out || bytes>65536 || out->count>RSX_FP_MAX_INLINE_CONSTANTS)
        return rsx_fp_collect_constants(code,bytes,out);
    const u32 previous=out->count;
    out->count=0;
    int result=-1;
    for (u32 off=0;off<=bytes && bytes-off>=16;) {
        const u32 w0=buzz_fp_word(code+off),w1=buzz_fp_word(code+off+4);
        const u32 w2=buzz_fp_word(code+off+8),w3=buzz_fp_word(code+off+12);
        off+=16;
        /* Unlike texture-usage queries, the SDK collector checks constant
         * source types on branch instructions too. */
        if ((w1&3u)==2 || (w2&3u)==2 || (w3&3u)==2) {
            if (bytes-off<16 || out->count>=RSX_FP_MAX_INLINE_CONSTANTS) break;
            for (u32 c=0;c<4;++c)
                out->values[out->count][c]=buzz_renderer_sanitize_constant(buzz_fp_word(code+off+c*4));
            out->count++;off+=16;
        }
        if (w0&1u) { result=(int)out->count;break; }
    }
    if (previous>out->count)
        memset(out->values+out->count,0,(previous-out->count)*sizeof(out->values[0]));
    return result;
}
