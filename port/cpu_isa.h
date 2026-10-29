#pragma once
#include <stdint.h>
/* Pure predicate so missing CPU/OS state can be tested without executing an
 * optional instruction. Baseline main must pass this before any lifted SPU. */
static inline int buzz_avx512_fields(uint32_t leaf1_ecx,uint32_t leaf7_ebx,uint64_t xcr0)
{
    const uint32_t avx_state=(1u<<26)|(1u<<27)|(1u<<28);
    const uint32_t isa=(1u<<5)|(1u<<16)|(1u<<30)|(1u<<31); /* AVX2, F, BW, VL */
    return (leaf1_ecx&avx_state)==avx_state && (leaf7_ebx&isa)==isa && (xcr0&0xe6u)==0xe6u;
}
#ifdef _WIN32
#include <intrin.h>
#if defined(__clang__)
__attribute__((target("xsave")))
#endif
static unsigned long long buzz_read_xcr0(void){return _xgetbv(0);}
static inline int buzz_avx512_host_supported(void)
{
    int regs[4];__cpuid(regs,0);if((unsigned)regs[0]<7)return 0;
    __cpuid(regs,1);uint32_t ecx=(uint32_t)regs[2];
    const uint32_t state=(1u<<26)|(1u<<27)|(1u<<28);
    if((ecx&state)!=state)return 0; /* XGETBV is illegal without OSXSAVE. */
    uint64_t xcr0=buzz_read_xcr0();__cpuidex(regs,7,0);
    return buzz_avx512_fields(ecx,(uint32_t)regs[1],xcr0);
}
#else
static inline int buzz_avx512_host_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("avx512f") &&
        __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vl");
}
#endif
