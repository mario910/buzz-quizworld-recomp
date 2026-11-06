#pragma once
#include "rsx_fp_decompiler.h"
#include <string.h>

/* Standalone helper tests exercise the candidate. The production CMake
 * adapter always sets this explicitly; its build option defaults to OFF. */
#ifndef BUZZ_RENDERER_CPU_REFINEMENTS
#define BUZZ_RENDERER_CPU_REFINEMENTS 1
#endif

/* Owned by the serialized FIFO renderer. A sampled key only selects a slot.
 * Hits check seed/length and every instruction byte. Inline constant values
 * are deliberately absent from structural identity, just as in the SDK hash;
 * the renderer still collects and sanitizes them afresh on every draw. */
enum { BUZZ_FP_HASH_SLOTS=32, BUZZ_FP_HASH_BYTES=8192 };
typedef struct {
    u64 seed, hash;
    u32 bytes;
    u8 code[BUZZ_FP_HASH_BYTES];
#if BUZZ_RENDERER_CPU_REFINEMENTS
    u16 offsets[BUZZ_FP_HASH_BYTES/16];
    u16 instructions;
#endif
} buzz_fp_hash_entry;
typedef struct { buzz_fp_hash_entry entries[BUZZ_FP_HASH_SLOTS]; } buzz_fp_hash_cache;

#if BUZZ_RENDERER_CPU_REFINEMENTS
static inline u32 buzz_fp_cache_word(const u8* p)
{
    return ((u32)p[2]<<24)|((u32)p[3]<<16)|((u32)p[0]<<8)|p[1];
}

static inline void buzz_fp_hash_layout(buzz_fp_hash_entry* entry,const u8* code,u32 bytes)
{
    u32 instructions=0,constants=0;
    entry->instructions=0;
    for (u32 off=0;off+16<=bytes;) {
        const u32 w0=buzz_fp_cache_word(code+off),w1=buzz_fp_cache_word(code+off+4);
        const u32 w2=buzz_fp_cache_word(code+off+8),w3=buzz_fp_cache_word(code+off+12);
        entry->offsets[instructions++]=(u16)off;
        off+=16;
        if ((w1&3u)==2 || (w2&3u)==2 || (w3&3u)==2) {
            if (off+16>bytes || constants>=RSX_FP_MAX_INLINE_CONSTANTS) return;
            ++constants;off+=16;
        }
        if (w0&1u) { entry->instructions=(u16)instructions;return; }
    }
}

static inline int buzz_fp_same_instructions(const buzz_fp_hash_entry* entry,const u8* code)
{
    if (!entry->instructions) return 0;
    for (u32 i=0;i<entry->instructions;++i) {
        const u32 off=entry->offsets[i];
        if (memcmp(entry->code+off,code+off,16)) return 0;
    }
    return 1;
}
#endif

static inline u64 buzz_fp_hash_cached(buzz_fp_hash_cache* cache,const u8* code,u32 bytes,u64 seed)
{
    if(!cache||!code||bytes<16||bytes>BUZZ_FP_HASH_BYTES)
        return rsx_fp_structural_hash(code,bytes,seed);
#if BUZZ_RENDERER_CPU_REFINEMENTS
    /* First-instruction words cannot be inline constants. Changing a constant
     * must not change the selected slot. Collisions still compare real code. */
    u32 first,second,last;
    memcpy(&first,code,4);memcpy(&second,code+4,4);memcpy(&last,code+12,4);
    u32 key=(first^(second*0x9e3779b9u)^last^bytes^(u32)seed^(u32)(seed>>32))*0x85ebca6bu;
#else
    u32 first,middle,last;
    memcpy(&first,code,4);memcpy(&middle,code+bytes/2,4);memcpy(&last,code+bytes-4,4);
    u32 key=(first^(middle*0x9e3779b9u)^last^bytes^(u32)seed^(u32)(seed>>32))*0x85ebca6bu;
#endif
    key^=key>>16;
    buzz_fp_hash_entry* entry=&cache->entries[key&(BUZZ_FP_HASH_SLOTS-1)];
    if(entry->bytes==bytes&&entry->seed==seed&&
       (!memcmp(entry->code,code,bytes)
#if BUZZ_RENDERER_CPU_REFINEMENTS
        ||buzz_fp_same_instructions(entry,code)
#endif
       ))return entry->hash;
    const u64 hash=rsx_fp_structural_hash(code,bytes,seed);
    memcpy(entry->code,code,bytes);entry->seed=seed;entry->hash=hash;entry->bytes=bytes;
#if BUZZ_RENDERER_CPU_REFINEMENTS
    buzz_fp_hash_layout(entry,code,bytes);
#endif
    return hash;
}
