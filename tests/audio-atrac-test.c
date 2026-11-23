/* Full-state gate for the pinned ATRAC blocks, not an audio-codec replacement. */
#define _GNU_SOURCE
#include "spu_context.h"
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
#include <immintrin.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __aarch64__
#include "spu_arm_fp_state.h"
#include <sched.h>
#endif
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c) { (void)c; }
#define LEAF(pc) \
void buzz_audio_##pc##_local(spu_context*); \
void buzz_audio_##pc##_oracle(spu_context*); \
void buzz_mstream_atrac_spu_func_##pc##_reference(spu_context* c) { buzz_audio_##pc##_oracle(c); }
LEAF(00036538)
LEAF(00036710)
#define TAIL(pc) void buzz_mstream_atrac_spu_func_##pc(spu_context* c) { (void)c; }
TAIL(00036710)
TAIL(000363E0)
TAIL(00036988)
TAIL(00036828)
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
LEAF(00036370)
LEAF(000368B8)
LEAF(0003614C)
LEAF(00037AE8)
LEAF(00036EB0)
LEAF(00037750)
LEAF(00037148)
LEAF(00037590)
TAIL(00036890)
TAIL(00036950)
TAIL(000360F8)
TAIL(000353C8)
TAIL(00037BE0)
TAIL(00037B0C)
TAIL(000377A8)
TAIL(00036ED8)
TAIL(00036F40)
TAIL(00037778)
TAIL(00037408)
TAIL(00037170)
TAIL(00037078)
TAIL(000375BC)
LEAF(00034410)
LEAF(000352D8)
LEAF(000341E0)
LEAF(00035030)
LEAF(00034F38)
TAIL(000344E0)
TAIL(000342D8)
TAIL(00035028)
LEAF(000358B0)
LEAF(00033DE8)
TAIL(0003405C)
LEAF(00034B58)
LEAF(00034930)
LEAF(00034608)
LEAF(000342E8)
LEAF(00035600)
TAIL(00035294)
TAIL(00034ED8)
TAIL(00034B58)
TAIL(00034770)
TAIL(00035460)
TAIL(000357E8)
LEAF(00034770)
LEAF(00034840)
LEAF(000344E0)
LEAF(000363E0)
TAIL(00034840)
TAIL(000348C4)
TAIL(00034400)
TAIL(00034608)
TAIL(00036538)
#endif
static spu_context seed,reference,candidate;
static unsigned char external[2][SPU_LS_SIZE];
static unsigned random_state=0xa7ac2026;
static unsigned word(void) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static const unsigned edge[]={0,0x80000000u,1,0x7fffff,0x800000,0x3f800000,0xbf800000,0x7f7fffff,0x7f800000,0xffffffffu};
int main(void)
{
    alarm(30);
#ifdef __aarch64__
    cpu_set_t cpus;CPU_ZERO(&cpus);CPU_SET(0,&cpus);
    assert(sched_setaffinity(0,sizeof cpus,&cpus)==0);
    const buzz_arm_fp_state original_fp=buzz_arm_fp_save();
#endif
    void (*oracle[])(spu_context*)={buzz_audio_00036538_oracle,buzz_audio_00036710_oracle,
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
        buzz_audio_00034770_oracle, buzz_audio_00034840_oracle, buzz_audio_000344E0_oracle, buzz_audio_000363E0_oracle,
        buzz_audio_00034B58_oracle, buzz_audio_00034930_oracle, buzz_audio_00034608_oracle, buzz_audio_000342E8_oracle, buzz_audio_00035600_oracle,
        buzz_audio_000358B0_oracle, buzz_audio_00033DE8_oracle,
        buzz_audio_00034410_oracle, buzz_audio_000352D8_oracle, buzz_audio_000341E0_oracle, buzz_audio_00035030_oracle, buzz_audio_00034F38_oracle,
        buzz_audio_00036370_oracle, buzz_audio_000368B8_oracle, buzz_audio_0003614C_oracle,
        buzz_audio_00037AE8_oracle, buzz_audio_00036EB0_oracle, buzz_audio_00037750_oracle, buzz_audio_00037148_oracle, buzz_audio_00037590_oracle,
#endif
    };
    void (*native[])(spu_context*)={buzz_audio_00036538_local,buzz_audio_00036710_local,
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
        buzz_audio_00034770_local, buzz_audio_00034840_local, buzz_audio_000344E0_local, buzz_audio_000363E0_local,
        buzz_audio_00034B58_local, buzz_audio_00034930_local, buzz_audio_00034608_local, buzz_audio_000342E8_local, buzz_audio_00035600_local,
        buzz_audio_000358B0_local, buzz_audio_00033DE8_local,
        buzz_audio_00034410_local, buzz_audio_000352D8_local, buzz_audio_000341E0_local, buzz_audio_00035030_local, buzz_audio_00034F38_local,
        buzz_audio_00036370_local, buzz_audio_000368B8_local, buzz_audio_0003614C_local,
        buzz_audio_00037AE8_local, buzz_audio_00036EB0_local, buzz_audio_00037750_local, buzz_audio_00037148_local, buzz_audio_00037590_local,
#endif
    };
    for(unsigned entry=0;entry<sizeof oracle / sizeof oracle[0];++entry)for(unsigned n=0;n<800;++n) {
        memset(&seed,0,sizeof seed);
        for(unsigned r=0;r<128;++r)for(unsigned lane=0;lane<4;++lane)
            seed.gpr[r]._u32[lane]=n%4?word():edge[(r+lane+n)%10];
        for(unsigned at=0;at<SPU_LS_SIZE;at+=4) {
            unsigned value=word();
            if(n&1)value=(value&0x807fffffu)|((115+word()%15)<<23);
            if(n%4==0)value=edge[(at/4+n)%10];
            value=SPU_BSWAP32(value);memcpy(seed.ls_store+at,&value,4);
        }
        seed.gpr[0]._u32[0]=0x1200;
        seed.gpr[30]._u32[0]=4*(1+n%8); // Exact finite loop trip count in 36710.
        seed.gpr[31]._u32[0]=0x8000;
        seed.gpr[32]._u32[0]=0x6000;
        seed.gpr[34]._u32[0]=0x4000;
        seed.gpr[35]._u32[0]=0x8000+16*(1+n%8);
        seed.gpr[40]._u32[0]=0x8000; // Exact finite loop trip count in 36538.
        seed.gpr[37]._u32[0]=n%3?0x1200:0x4000; // Include aliasing with output.
        seed.gpr[48]._u32[0]=n%2?0x2000:0;
        seed.gpr[55]._u32[0]=0xa000;
        if (entry == 2) seed.gpr[78]._u32[0] = seed.gpr[18]._u32[0] - 64 * (1+n%8);
        if (entry == 3) seed.gpr[76]._u32[0] = seed.gpr[12]._u32[0] - 32 * (1+n%8);
        if (entry == 4) seed.gpr[28]._u32[0] = 1+n%8;
        const unsigned trips = 1 + n % 8;
        if (entry == 6) seed.gpr[59]._u32[0] = 32 - 2 * trips;
        if (entry == 7) seed.gpr[30]._u32[0] = 16 * trips - 2;
        if (entry == 8) seed.gpr[29]._u32[0] = trips;
        if (entry == 9) seed.gpr[27]._u32[0] = 0u - 8 * trips;
        if (entry == 10) seed.gpr[16]._u32[0] = 0u - trips;
        if (entry == 11 || entry == 12) {
            seed.gpr[18]._u32[0] = n % 2 ? 0x4000 : SPU_LS_SIZE - 32;
            seed.gpr[22]._u32[0] = seed.gpr[18]._u32[0] + 32 * trips;
        }
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
        if (native[entry] == buzz_audio_00036370_local)
            seed.gpr[10]._u32[0] = 296 - 4 * trips;
        if (native[entry] == buzz_audio_000368B8_local)
            seed.gpr[14]._u32[0] = 256 - 8 * trips;
        if (native[entry] == buzz_audio_0003614C_local) {
            seed.gpr[8]._u32[0] = 0;
            seed.gpr[10]._u32[0] = 4 * (trips - 1);
        }
        if (native[entry] == buzz_audio_00037AE8_local || native[entry] == buzz_audio_00036EB0_local ||
            native[entry] == buzz_audio_00037750_local || native[entry] == buzz_audio_00037148_local ||
            native[entry] == buzz_audio_00037590_local) {
            unsigned count = native[entry] == buzz_audio_00037AE8_local ? 13 :
                             native[entry] == buzz_audio_00037148_local ? 15 : 5;
            unsigned src = count == 13 || native[entry] == buzz_audio_00037590_local ? 4 : 7;
            unsigned dst = src == 4 ? 7 : 8;
            seed.gpr[count]._u32[0] = 16 * trips + (n % 16);
            unsigned start = n % 5 == 0 ? SPU_LS_SIZE - 32 : 0x8000;
            const unsigned offsets[] = {0x2000, 0, 16, (unsigned)-16, SPU_LS_SIZE-16-0x8000};
            seed.gpr[src]._u32[0] = start + (n & 15);
            seed.gpr[dst]._u32[0] = start + offsets[n % 5] + ((n >> 4) & 15);
        }
        if (native[entry] == buzz_audio_00034410_local || native[entry] == buzz_audio_000352D8_local)
            seed.gpr[20]._u32[0] = trips;
        if (native[entry] == buzz_audio_000341E0_local)
            seed.gpr[20]._u32[0] = 0u - 8 * trips;
        if (native[entry] == buzz_audio_00034F38_local) {
            seed.gpr[4]._u32[0] = n % 3 ? 0x4000 : SPU_LS_SIZE - 64;
            seed.gpr[10]._u32[0] = seed.gpr[4]._u32[0] + 128 * trips;
        }
#endif
        memcpy(&reference,&seed,sizeof seed);memcpy(&candidate,&seed,sizeof seed);
        const int foreign=n%11==0;
        reference.ls=foreign?external[0]:reference.ls_store;
        candidate.ls=foreign?external[1]:candidate.ls_store;
        if(foreign){memcpy(reference.ls,seed.ls_store,SPU_LS_SIZE);memcpy(candidate.ls,seed.ls_store,SPU_LS_SIZE);}
        g_spu_trampoline_fn=NULL;oracle[entry](&reference);
        void (*next)(spu_context*)=g_spu_trampoline_fn;
#ifdef __aarch64__
        buzz_arm_fp_state selected=original_fp;
        selected.fpcr=(selected.fpcr&~((7ull<<22)|(0x9full<<8)))|((uint64_t)(n%8)<<22);
        selected.fpsr=n&31;
        buzz_arm_fp_restore(selected);
        const buzz_arm_fp_state before=buzz_arm_fp_save();
#endif
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
            unsigned before_mxcsr = 0x1f80u | ((n & 3) << 13) | ((n & 4) ? 0x8040u : 0u) | (n & 31);
            if (native[entry] == buzz_audio_000368B8_local && !foreign && (n & 1))
                before_mxcsr &= ~(_MM_MASK_INVALID | _MM_MASK_INEXACT | _MM_MASK_OVERFLOW);
            _mm_setcsr(before_mxcsr);
#endif
        g_spu_trampoline_fn=NULL;native[entry](&candidate);
#ifdef BUZZ_AUDIO_TEST_X86_RTZ
            if (candidate.ls == candidate.ls_store && _mm_getcsr() != before_mxcsr) abort();
            _mm_setcsr(0x1f80u);
#endif
#ifdef __aarch64__
        const buzz_arm_fp_state after=buzz_arm_fp_save();
        assert(before.fpcr==after.fpcr&&before.fpsr==after.fpsr);
#endif
        assert(next==g_spu_trampoline_fn);
        assert(memcmp(reference.ls,candidate.ls,SPU_LS_SIZE)==0);
        candidate.ls=reference.ls;
        assert(memcmp(&reference,&candidate,sizeof reference)==0);
    }
#ifdef __aarch64__
    buzz_arm_fp_restore(original_fp);
#endif
    printf("PASS ATRAC: %zu complete states, aliasing, external LS fallback, tail continuation and FP restoration\n", 800 * sizeof oracle / sizeof oracle[0]);
}
