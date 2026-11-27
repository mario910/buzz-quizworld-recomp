#pragma once

#include <stdint.h>
#include <string.h>

/* Common input sizes in the 21-entry geometry list. Constant sizes let
 * the NDK emit fixed loads and stores. The inline builtin prevents LLVM from
 * merging these arms back into one variable-size libc call. The caller
 * validates the span and handles padding. */
static inline void buzz_spurs_geometry_input_copy(uint8_t* dst, const uint8_t* src, uint32_t size)
{
    switch (size) {
    case 16:  __builtin_memcpy_inline(dst, src, 16);  break;
    case 32:  __builtin_memcpy_inline(dst, src, 32);  break;
    case 48:  __builtin_memcpy_inline(dst, src, 48);  break;
    case 64:  __builtin_memcpy_inline(dst, src, 64);  break;
    case 128: __builtin_memcpy_inline(dst, src, 128); break;
    case 208: __builtin_memcpy_inline(dst, src, 208); break;
    case 240: __builtin_memcpy_inline(dst, src, 240); break;
    default:  memcpy(dst, src, size); break;
    }
}

/* Image 41 uses exactly 21 entries. The adapter supplies the SDK reader,
 * including its 32-bit guest address arithmetic. Return aligned bytes used. */
static inline uint32_t buzz_spurs_stage_geometry_inputs(
    uint8_t* ls, const uint8_t* vm, uint32_t io, uint32_t ls_size,
    uint32_t list, int lazy_zero, uint64_t (*read_entry)(uint32_t))
{
    if (!io || io > ls_size) return 0;
    uint32_t cursor = io;
#pragma clang loop unroll_count(3)
    for (unsigned i = 0; i < 21; ++i) {
        const uint64_t entry = read_entry(list + 8u * i);
        const uint32_t size = (entry >> 32) & 0x7fffu;
        const uint32_t ea = (uint32_t)entry;
        if (!size || !ea) continue;
        if ((uint64_t)cursor + size > ls_size) break;
        const uint32_t aligned = (size + 15u) & ~15u;
        buzz_spurs_geometry_input_copy(ls + cursor, vm + ea, size);
        if (lazy_zero && aligned != size)
            memset(ls + cursor + size, 0, aligned - size);
        cursor += aligned;
    }
    return cursor - io;
}
