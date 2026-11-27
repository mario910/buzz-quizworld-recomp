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
