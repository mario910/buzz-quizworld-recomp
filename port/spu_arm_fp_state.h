#pragma once
#include <stdint.h>

typedef struct {
    uint64_t fpcr;
    uint64_t fpsr;
} buzz_arm_fp_state;

static inline buzz_arm_fp_state buzz_arm_fp_save(void)
{
    buzz_arm_fp_state state;
    __asm__ volatile("mrs %0, fpcr; mrs %1, fpsr" : "=r"(state.fpcr), "=r"(state.fpsr) :: "memory");
    return state;
}

static inline void buzz_arm_fp_enter_rtz(buzz_arm_fp_state state)
{
    const uint64_t active = (state.fpcr & ~((7ull << 22) | (0x9full << 8) | 3ull)) | (7ull << 22);
    __asm__ volatile("msr fpcr, %0; isb" :: "r"(active) : "memory");
}

static inline void buzz_arm_fp_restore(buzz_arm_fp_state state)
{
    __asm__ volatile("msr fpcr, %0; msr fpsr, %1; isb" :: "r"(state.fpcr), "r"(state.fpsr) : "memory");
}
