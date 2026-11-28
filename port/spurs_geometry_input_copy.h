#pragma once

#include <stdint.h>
#include <string.h>

// Keep the variable-length copy shared across specialized descriptor slots.
static __attribute__((noinline)) void buzz_spurs_geometry_variable_copy(
    uint8_t* dst,const uint8_t* src,uint32_t size)
{
    if (size>=1024) {
        while (size>=128) {
            __builtin_memcpy_inline(dst,src,128);
            dst+=128; src+=128; size-=128;
        }
        if (size) memcpy(dst,src,size);
        return;
    }
    // Copy unlisted small aligned lengths in fixed-size pieces.
    if (size>256 || (size&15u)) {
        memcpy(dst,src,size);
        return;
    }
    if (size==256) {
        __builtin_memcpy_inline(dst,src,256);
        return;
    }
    // Each set bit contributes one disjoint chunk of the requested length.
    if (size&128) { __builtin_memcpy_inline(dst,src,128); dst+=128; src+=128; }
    if (size&64)  { __builtin_memcpy_inline(dst,src,64);  dst+=64;  src+=64;  }
    if (size&32)  { __builtin_memcpy_inline(dst,src,32);  dst+=32;  src+=32;  }
    if (size&16)    __builtin_memcpy_inline(dst,src,16);
}

/* Common input sizes in the 21-entry geometry list. Constant sizes let
 * the NDK emit fixed loads and stores. The inline builtin prevents LLVM from
 * merging these arms back into one variable-size libc call. The caller
 * validates the span and handles padding. */
static __attribute__((noinline)) void buzz_spurs_geometry_input_copy(uint8_t* dst, const uint8_t* src, uint32_t size)
{
    switch (size) {
    case 16:  __builtin_memcpy_inline(dst, src, 16);  break;
    case 32:  __builtin_memcpy_inline(dst, src, 32);  break;
    case 48:  __builtin_memcpy_inline(dst, src, 48);  break;
    case 64:  __builtin_memcpy_inline(dst, src, 64);  break;
    case 128: __builtin_memcpy_inline(dst, src, 128); break;
    case 208: __builtin_memcpy_inline(dst, src, 208); break;
    case 240: __builtin_memcpy_inline(dst, src, 240); break;
    default: buzz_spurs_geometry_variable_copy(dst,src,size); break;
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
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < 21; ++i) {
        const uint64_t entry = read_entry(list + 8u * i);
        const uint32_t size = (entry >> 32) & 0x7fffu;
        const uint32_t ea = (uint32_t)entry;
        if (!size || !ea) continue;
        if ((uint64_t)cursor + size > ls_size) break;
        // These slots have stable sizes in this job family. Always check the live descriptor.
        if (i==0 && size==208) __builtin_memcpy_inline(ls+cursor,vm+ea,208);
        else if ((i==1 || i==20) && size==32) __builtin_memcpy_inline(ls+cursor,vm+ea,32);
        else if ((i==2 || i==16) && size==16) __builtin_memcpy_inline(ls+cursor,vm+ea,16);
        else if (i==14 && size==128) __builtin_memcpy_inline(ls+cursor,vm+ea,128);
        else if (i==15 && size==64) __builtin_memcpy_inline(ls+cursor,vm+ea,64);
        else buzz_spurs_geometry_input_copy(ls + cursor, vm + ea, size);
        cursor+=size;
        const uint32_t padding=(-size)&15u;
        if (padding) {
            if (lazy_zero) memset(ls+cursor,0,padding);
            cursor+=padding;
        }
    }
    return cursor - io;
}
