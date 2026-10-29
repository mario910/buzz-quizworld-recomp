/* Generated scalar leaf vs guarded native SIMD, all architectural state.
 * Compile reference_06d8.c WITHOUT SPU_NATIVE_RTZ and kernel_06d8.c WITH it. */
#include "../port/native_image36.h"
#include <immintrin.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

void buzz_image36_06d8_reference(spu_context*);
void buzz_image36_06d8_oracle(spu_context*);
void buzz_image36_06d8_native(spu_context*);
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* ctx) { (void)ctx; abort(); }
static void incoming(spu_context* ctx) { ctx->pc ^= 0xDEADu; } /* Not ICF-identical to abort stub. */
static spu_context a, b, c, entry;
static u128 expected_gpr[128];
static uint8_t expected_ls[SPU_LS_SIZE];
static uint32_t rng = 0x6d836231;
static uint32_t word(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void check(unsigned n)
{
    memset(&a, 0, sizeof a); a.ls = a.ls_store; a.image_id = 36;
    for (unsigned r = 0; r < 128; ++r) for (unsigned w = 0; w < 4; ++w)
        a.gpr[r]._u32[w] = word();
    for (unsigned i = 0; i < SPU_LS_SIZE; i += 4) {
        uint32_t value = word(); memcpy(a.ls + i, &value, 4);
    }
    // Alternate arbitrary bit patterns (including extended SPU FP) and normal
    // finite values. Preserve pointer/counter setup after filling the registers.
    if (n & 1) for (unsigned r = 3; r < 80; ++r) for (unsigned w = 0; w < 4; ++w)
        a.gpr[r]._u32[w] = (word() & 0x807fffffu) | ((120u + word() % 12u) << 23);
    a.gpr[2]._u32[0] = 1 + n % 4;
    a.host_depth = n & 1;
    a.gpr[0]._u32[0] = 0x2500;
    memcpy(&b, &a, sizeof b); b.ls = b.ls_store;
    memcpy(&c, &a, sizeof c); c.ls = c.ls_store;
    const unsigned saved = _mm_getcsr();
    // Production AVX2 reference uses a double-precision TwoSum fast path.
    // Its oracle environment is RN (the game's default), not arbitrary host
    // rounding. Independently check native against exact INTEGER FP helpers.
    _mm_setcsr(0x1f80);
    g_spu_trampoline_fn = incoming;
    buzz_image36_06d8_reference(&a);
    void (*expected_next)(spu_context*) = g_spu_trampoline_fn;
    _mm_setcsr(saved);
    g_spu_trampoline_fn = incoming;
    buzz_image36_06d8_oracle(&c);
    assert(!memcmp(a.gpr, c.gpr, sizeof a.gpr));
    assert(!memcmp(a.ls, c.ls, SPU_LS_SIZE));
    assert(g_spu_trampoline_fn == expected_next);
    g_spu_trampoline_fn = incoming;
    buzz_image36_mode = 1;
    buzz_image36_run(&b, buzz_image36_06d8_reference, buzz_image36_06d8_native);
    assert(_mm_getcsr() == saved);
    assert(g_spu_trampoline_fn == expected_next);
    if (memcmp(a.gpr, b.gpr, sizeof a.gpr)) {
        for (unsigned r = 0; r < 128; ++r) for (unsigned w = 0; w < 4; ++w)
            if (a.gpr[r]._u32[w] != b.gpr[r]._u32[w])
                fprintf(stderr, "case=%u mxcsr=%08X r%u.word%u reference=%08X native=%08X\n",
                        n, saved, r, w, a.gpr[r]._u32[w], b.gpr[r]._u32[w]);
    }
    assert(!memcmp(a.gpr, b.gpr, sizeof a.gpr));
    assert(!memcmp(a.ls, b.ls, SPU_LS_SIZE));
    b.ls = a.ls; /* Only the owned backing pointer intentionally differs. */
    assert(!memcmp(&a, &b, sizeof a));
}

static unsigned fault;
static void broken_native(spu_context* ctx)
{
    buzz_image36_06d8_reference(ctx);
    if (fault == 0) ctx->gpr[17]._u32[3] ^= 1; /* Volatile GPR must be checked. */
    if (fault == 1) ctx->ls[0x3ffff] ^= 1;
    if (fault == 2) ctx->stop_code ^= 1;
    if (fault == 3) ctx->pc ^= 1;
    if (fault == 4) g_spu_trampoline_fn = incoming;
}

static double seconds_now(void)
{
#ifdef _WIN32
    LARGE_INTEGER t, frequency;
    QueryPerformanceCounter(&t); QueryPerformanceFrequency(&frequency);
    return (double)t.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec t; timespec_get(&t, TIME_UTC);
    return t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

static void captured(const char* path, double* reference_time, double* native_time)
{
    FILE* file = fopen(path, "rb"); assert(file);
    memset(&entry, 0, sizeof entry); entry.ls = entry.ls_store; entry.image_id = 36;
    assert(fread(entry.gpr, sizeof entry.gpr, 1, file) == 1);
    assert(fread(entry.ls, SPU_LS_SIZE, 1, file) == 1);
    assert(fread(expected_gpr, sizeof expected_gpr, 1, file) == 1);
    assert(fread(expected_ls, SPU_LS_SIZE, 1, file) == 1);
    assert(fgetc(file) == EOF); fclose(file);
    // A,B,B,A order repeated, excluding reset/copy time. Every timed run is
    // checked against the IN-GAME captured result, including all volatile GPRs.
    for (unsigned i = 0; i < 100; ++i) {
        unsigned native = (i ^ (i >> 1)) & 1;
        memcpy(&a, &entry, sizeof a); a.ls = a.ls_store;
        _mm_setcsr(0x1f80); g_spu_trampoline_fn = incoming;
        double start = seconds_now();
        if (native) {
            buzz_image36_mode = 1;
            buzz_image36_run(&a, buzz_image36_06d8_reference, buzz_image36_06d8_native);
        } else buzz_image36_06d8_reference(&a);
        double elapsed = seconds_now() - start;
        *(native ? native_time : reference_time) += elapsed;
        assert(!memcmp(a.gpr, expected_gpr, sizeof expected_gpr));
        assert(!memcmp(a.ls, expected_ls, SPU_LS_SIZE));
    }
}

int main(int argc, char** argv)
{
    const unsigned original = _mm_getcsr();
    for (unsigned n = 0; n < 4000; ++n) {
        _mm_setcsr((original & ~0xe07fu) | 0x1f80u | ((n % 4) << 13) |
                   ((n & 4) ? 0x8040u : 0) | (n & 0x3fu));
        check(n);
    }
    _mm_setcsr(original);
    assert(!buzz_image36_mismatches());
    double reference_time = 0, native_time = 0;
    for (int i = 1; i < argc; ++i) captured(argv[i], &reference_time, &native_time);
    if (argc > 1)
        printf("Captured image36 leaf: %d cases x100, reference %.2f us, native %.2f us\n",
               argc - 1, reference_time * 1e6 / ((argc - 1) * 50),
               native_time * 1e6 / ((argc - 1) * 50));
    _mm_setcsr(original);
    for (fault = 0; fault < 5; ++fault) {
        memset(&a, 0, sizeof a); a.ls = a.ls_store; a.gpr[2]._u32[0] = 1;
        buzz_image36_mode = 2;
        g_spu_trampoline_fn = NULL;
        buzz_image36_run(&a, buzz_image36_06d8_reference, broken_native);
        assert(buzz_image36_mismatches() == fault + 1);
        assert(_mm_getcsr() == original);
    }
    puts("PASS image36: 4000 full states, 1..4 iterations, host-depth/rounding/MXCSR, 5 injected faults caught");
}
