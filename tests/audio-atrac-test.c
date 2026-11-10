/* Full-state gate for the two pinned ATRAC blocks, not an audio-codec replacement. */
#define _GNU_SOURCE
#include "spu_context.h"
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
static spu_context seed,reference,candidate;
static unsigned char external[2][SPU_LS_SIZE];
static unsigned random_state=0xa7ac2026;
static unsigned word(void) { random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static const unsigned edge[]={0,0x80000000u,1,0x7fffff,0x800000,0x3f800000,0xbf800000,0x7f7fffff,0x7f800000,0xffffffffu};
int main(void)
{
    alarm(15);
#ifdef __aarch64__
    cpu_set_t cpus;CPU_ZERO(&cpus);CPU_SET(0,&cpus);
    assert(sched_setaffinity(0,sizeof cpus,&cpus)==0);
    const buzz_arm_fp_state original_fp=buzz_arm_fp_save();
#endif
    void (*oracle[])(spu_context*)={buzz_audio_00036538_oracle,buzz_audio_00036710_oracle};
    void (*native[])(spu_context*)={buzz_audio_00036538_local,buzz_audio_00036710_local};
    for(unsigned entry=0;entry<2;++entry)for(unsigned n=0;n<800;++n) {
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
        g_spu_trampoline_fn=NULL;native[entry](&candidate);
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
    puts("PASS ATRAC: 1600 complete states, aliasing, external LS fallback, tail continuation and ARM FP restoration");
}
