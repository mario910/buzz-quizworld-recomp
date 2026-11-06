#pragma once
#include "rsx_vp_decompiler.h"
#include <string.h>

/* Renderer-owned cache: the FIFO engine serializes all callers. Addresses and
 * sampled hashes are only hints; a hit requires equality of EVERY code byte.
 * Reusing or modifying a guest address therefore cannot preserve stale masks.
 * Bounded storage, no allocation and no guest-memory ownership. */
enum { BUZZ_VP_CACHE_SLOTS = 16, BUZZ_VP_CACHE_BYTES = 8192 };
typedef struct {
    u32 bytes;
    int result;
    rsx_vp_input_analysis analysis;
    u8 code[BUZZ_VP_CACHE_BYTES];
} buzz_vp_cache_entry;
typedef struct { buzz_vp_cache_entry entry[BUZZ_VP_CACHE_SLOTS]; } buzz_vp_cache;

static inline int buzz_vp_analyze_cached(buzz_vp_cache* cache, const u8* code,
                                        u32 bytes, rsx_vp_input_analysis* output)
{
    if (!cache || !code || !output || bytes < 16 || bytes > BUZZ_VP_CACHE_BYTES)
        return rsx_vp_analyze_inputs(code, bytes, output);
    u32 first, middle, last;
    memcpy(&first, code, 4);
    memcpy(&middle, code + bytes / 2, 4);
    memcpy(&last, code + bytes - 4, 4);
    u32 key = (first ^ (middle * 0x9e3779b9u) ^ last ^ bytes) * 0x85ebca6bu;
    key ^= key >> 16;
    buzz_vp_cache_entry* entry = &cache->entry[key & (BUZZ_VP_CACHE_SLOTS - 1)];
    if (entry->bytes == bytes && !memcmp(entry->code, code, bytes)) {
        *output = entry->analysis;
        return entry->result;
    }
    const int result = rsx_vp_analyze_inputs(code, bytes, output);
    memcpy(entry->code, code, bytes);
    entry->analysis = *output;
    entry->result = result;
    entry->bytes = bytes;
    return result;
}
