#define _GNU_SOURCE
#include "spu_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sched.h>
#include "spu_arm_fp_state.h"
#endif

SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* ctx) { (void)ctx; }
void buzz_audio_0002FF18_local(spu_context*);
void buzz_audio_000301D8_local(spu_context*);
void buzz_audio_0002FF18_oracle(spu_context*);
void buzz_audio_000301D8_oracle(spu_context*);
void buzz_audio_0002FF18_baseline(spu_context*);
void buzz_audio_000301D8_baseline(spu_context*);
void buzz_mstream_compressor_spu_func_0002FF18_reference(spu_context* ctx) { buzz_audio_0002FF18_oracle(ctx); }
void buzz_mstream_compressor_spu_func_000301D8_reference(spu_context* ctx) { buzz_audio_000301D8_oracle(ctx); }
#define MIXER_LEAF(address) \
void buzz_audio_##address##_local(spu_context*); \
void buzz_audio_##address##_oracle(spu_context*); \
void buzz_audio_##address##_baseline(spu_context*); \
void spu_0001_at_0071D300_spu_func_##address##_reference(spu_context* ctx) { buzz_audio_##address##_oracle(ctx); }
MIXER_LEAF(0000B078)
MIXER_LEAF(0000C758)
MIXER_LEAF(0000C8D4)
MIXER_LEAF(00004E00)
MIXER_LEAF(00004F24)
MIXER_LEAF(0000513C)
MIXER_LEAF(000051F8)
void spu_0001_at_0071D300_spu_func_0000B100(spu_context* ctx) { (void)ctx; }
void spu_0001_at_0071D300_spu_func_0000C7E0(spu_context* ctx) { (void)ctx; }
void spu_0001_at_0071D300_spu_func_0000C95C(spu_context* ctx) { (void)ctx; }
void spu_0001_at_0071D300_spu_func_00004DC8(spu_context* ctx) { (void)ctx; }
void spu_0001_at_0071D300_spu_func_00004EEC(spu_context* ctx) { (void)ctx; }
void spu_0001_at_0071D300_spu_func_00004FF0(spu_context* ctx) { (void)ctx; }
void spu_0001_at_0071D300_spu_func_00004DA8(spu_context* ctx) { (void)ctx; }
typedef void (*audio_fn)(spu_context*);
static audio_fn originals[] = {buzz_audio_0002FF18_oracle, buzz_audio_000301D8_oracle,
    buzz_audio_0000B078_oracle, buzz_audio_0000C758_oracle, buzz_audio_0000C8D4_oracle,
    buzz_audio_00004E00_oracle, buzz_audio_00004F24_oracle, buzz_audio_0000513C_oracle, buzz_audio_000051F8_oracle};
static audio_fn locals[] = {buzz_audio_0002FF18_local, buzz_audio_000301D8_local,
    buzz_audio_0000B078_local, buzz_audio_0000C758_local, buzz_audio_0000C8D4_local,
    buzz_audio_00004E00_local, buzz_audio_00004F24_local, buzz_audio_0000513C_local, buzz_audio_000051F8_local};
static audio_fn baselines[] = {buzz_audio_0002FF18_baseline, buzz_audio_000301D8_baseline,
    buzz_audio_0000B078_baseline, buzz_audio_0000C758_baseline, buzz_audio_0000C8D4_baseline,
    buzz_audio_00004E00_baseline, buzz_audio_00004F24_baseline, buzz_audio_0000513C_baseline, buzz_audio_000051F8_baseline};

static spu_context reference, candidate, seed;
static unsigned rng = 0xa0d10;
static const unsigned boundaries[] = {0,0x80000000u,0x00000001u,0x007fffffu,
    0x00800000u,0x3f800000u,0xbf800000u,0x7f7fffffu,0x7f800000u,0xffffffffu};
static unsigned word(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static double seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void clone_state(spu_context* to, const spu_context* from, int external) {
    memcpy(to, from, sizeof *to);
    /* Deliberately external, but still bounded, LS for the exact fallback. */
    static unsigned char external_ls[2][SPU_LS_SIZE];
    to->ls = external ? external_ls[to == &candidate] : to->ls_store;
    memcpy(to->ls, from->ls, SPU_LS_SIZE);
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    alarm(12); /* A malformed generated loop cannot hang a device test. */
#ifdef __ANDROID__
    cpu_set_t little; CPU_ZERO(&little); CPU_SET(0, &little);
    if (sched_setaffinity(0, sizeof little, &little)) { perror("affinity"); return 3; }
#endif
    for (unsigned entry = 0; entry < 9; ++entry) {
        audio_fn original = originals[entry], local = locals[entry];
        for (unsigned n = 0; n < 800; ++n) {
            memset(&seed, 0, sizeof seed); seed.ls = seed.ls_store;
            for (unsigned r = 0; r < 128; ++r) for (unsigned lane = 0; lane < 4; ++lane)
                seed.gpr[r]._u32[lane] = word();
            if (n % 8 == 0) for (unsigned r = 0; r < 128; ++r) for (unsigned lane = 0; lane < 4; ++lane)
                seed.gpr[r]._u32[lane] = boundaries[(r + lane + n / 8) % 10];
            for (unsigned at = 0; at < SPU_LS_SIZE; at += 4) {
                unsigned x = word();
                if (n & 1) x = (x & 0x807fffffu) | ((115 + word() % 15) << 23);
                if (n % 8 == 0) x = boundaries[(at / 4 + n / 8) % 10];
                const unsigned big_endian = SPU_BSWAP32(x);
                memcpy(seed.ls + at, &big_endian, 4);
            }
            seed.host_depth = n & 1; seed.gpr[0]._u32[0] = 0x1200;
            if (!entry) {
                seed.gpr[32]._u32[0] = 256 - 8 * (1 + n % 32);
                seed.gpr[20]._u32[0] = 0x4000;
                seed.gpr[19]._u32[0] = (n % 5 == 0) ? 0x4080 : 0x9000;
            } else if (entry == 1) {
                seed.gpr[13]._u32[0] = 0x4000;
                seed.gpr[14]._u32[0] = (n % 5 == 0) ? 0x4080 : 0x9000;
                seed.gpr[20]._u32[0] = 0x4000 + 128 * (1 + n % 32);
            } else if (entry == 5 || entry == 6) {
                for (unsigned r = 16; r <= 19; ++r) seed.gpr[r]._u32[0] = n % 5 ? 0x4000 + (r - 16) * 0x2000 : 0x4000;
                if (entry == 5) {
                    seed.gpr[23]._u32[0] = n % 8;
                    seed.gpr[21]._u32[0] = 16;
                    seed.gpr[22]._u32[0] = n & 1 ? 60 : 0;
                } else seed.gpr[22]._u32[0] = n % 8;
            } else if (entry == 7) {
                seed.gpr[15]._u32[0] = 0x3000;
                seed.gpr[18]._u32[0] = 0x3000 + 16 * (1 + n % 8);
                seed.gpr[16]._u32[0] = 0;
                seed.gpr[17]._u32[0] = n % 5 ? 0x1000 : 0;
                seed.gpr[1]._u32[0] = 0x20000;
            } else if (entry == 8) {
                seed.gpr[28]._u32[0] = 256 - 4 * (1 + n % 8);
                seed.gpr[27]._u32[0] = 0x4000;
                seed.gpr[25]._u32[0] = 0x8000;
                seed.gpr[26]._u32[0] = n % 5 ? 0xc000 : 0x8000;
            }
            clone_state(&reference, &seed, n % 9 == 0);
            clone_state(&candidate, &seed, n % 9 == 0);
            g_spu_trampoline_fn = NULL; original(&reference); const void* next = (const void*)g_spu_trampoline_fn;
            g_spu_trampoline_fn = NULL;
#ifdef __ANDROID__
            const buzz_arm_fp_state before_fp = buzz_arm_fp_save();
#endif
            local(&candidate);
#ifdef __ANDROID__
            const buzz_arm_fp_state after_fp = buzz_arm_fp_save();
            if (before_fp.fpcr != after_fp.fpcr || before_fp.fpsr != after_fp.fpsr) {
                fprintf(stderr, "FAIL FP state entry=%u case=%u\n", entry, n); return 4;
            }
#endif
            if (next != (const void*)g_spu_trampoline_fn || memcmp(reference.ls, candidate.ls, SPU_LS_SIZE)) {
                fprintf(stderr, "FAIL LS/trampoline entry=%u case=%u\n", entry, n); return 1;
            }
            candidate.ls = reference.ls;
            if (memcmp(&reference, &candidate, sizeof reference)) {
                fprintf(stderr, "FAIL context entry=%u case=%u\n", entry, n); return 2;
            }
        }
        /* Compare the original vector implementation, NOT the scalar oracle.
         * Use ABBA and reset live-in registers without copying 256 KB of LS
         * inside every timed call. This is not a full-game FPS measurement. */
        clone_state(&candidate, &seed, 0);
        if (entry < 2) for (unsigned lane = 0; lane < 4; ++lane) {
            seed.gpr[18]._u32[lane] = 0x3f000000;
            seed.gpr[23]._u32[lane] = 0x3f800000;
            seed.gpr[34]._u32[lane] = 0x42000000;
            seed.gpr[33]._u32[lane] = 0;
            if (entry == 1) seed.gpr[19]._u32[lane] = 0x3f800000;
        }
        if (entry == 5 || entry == 6) for (unsigned r = entry == 5 ? 24 : 23; r <= (entry == 5 ? 29u : 28u); ++r)
            for (unsigned lane = 0; lane < 4; ++lane) seed.gpr[r]._u32[lane] = r & 1 ? 0x3f800000 : 0x3f000000;
        audio_fn baseline = baselines[entry];
        for (unsigned pass = 0; pass < 4; ++pass) {
            const int use_local = pass == 1 || pass == 2;
            void (*fn)(spu_context*) = use_local ? local : baseline;
            double elapsed = 0;
            const unsigned repeats = 100;
            for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                memcpy(candidate.gpr, seed.gpr, sizeof seed.gpr);
                // These filters are in-place. Reusing mutated LS would time
                // progressively overflowing data rather than the same input.
                memcpy(candidate.ls, seed.ls, SPU_LS_SIZE);
                const double start = seconds();
                fn(&candidate);
                elapsed += seconds() - start;
            }
            printf("BENCH entry=%u pass=%u %s us/call=%.2f\n", entry, pass,
                   use_local ? "local" : "baseline", elapsed * 1e6 / repeats);
        }
    }
    puts("PASS audio leaves: 7200 complete states, arbitrary/finite/boundary operands, overlapping buffers, external LS, return depths/tail exits");
    return 0;
}
