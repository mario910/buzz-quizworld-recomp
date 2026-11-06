#include "spu_context.h"
#include "spu_arm_fp_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static spu_context seed, reference, native;
static int admitted;
static uint32_t random_state = 0x41544746;
#ifndef TEST_SPU_PURE_LOOPS
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
#else
volatile int g_yz_lockstep_on;
volatile unsigned g_spu_stack_reset_n;
#endif
int g_spu_ls_watch_n;
int buzz_stagef_mode = 1;
int buzz_geometry_debug;
unsigned buzz_geometry_active;

void buzz_job_6eba_spu_func_00002C00(spu_context*);
void buzz_job_6eba_spu_func_0000C518(spu_context*);
void oracle_func_00002C00(spu_context*);
void oracle_func_0000C518(spu_context*);
void buzz_job_6eba_spu_func_000063E0(spu_context*);
void buzz_job_6eba_spu_func_00006E08(spu_context*);
void oracle_func_000063E0(spu_context*);
void oracle_func_00006E08(spu_context*);
void buzz_job_6eba_spu_func_00006958(spu_context*);
void oracle_func_00006958(spu_context*);

void spu_indirect_branch(spu_context* ctx)
{
    (void)ctx;
    assert(!"unexpected indirect branch");
}

void spu_drain_call(spu_context* ctx, uint32_t return_pc)
{
    (void)ctx;
    (void)return_pc;
    assert(!"unexpected nested drain");
}

void spu_img_restore(spu_context* ctx, int32_t saved_img)
{
    (void)ctx;
    (void)saved_img;
    assert(!"unexpected image switch");
}

void buzz_stagef_run(spu_context* ctx, void (*ref)(spu_context*),
                     int (*fast)(spu_context*), int kernel)
{
    (void)ref;
    assert(kernel == 1 || kernel == 2);
    admitted = fast(ctx);
}

static unsigned run_until(spu_context* ctx, void (*entry)(spu_context*), uint32_t exit_pc)
{
    ctx->host_depth = 1;
    entry(ctx);
    unsigned steps = 1;
    while (ctx->pc != exit_pc && g_spu_trampoline_fn) {
        assert(steps < 4096);
        void (*next)(spu_context*) = g_spu_trampoline_fn;
        g_spu_trampoline_fn = NULL;
#ifdef TEST_SPU_PURE_LOOPS
        ++ctx->steps;
        g_spu_pch[g_spu_pch_n++ & 7] = ctx->pc & SPU_LS_MASK;
#endif
        next(ctx);
        ++steps;
    }
    assert(ctx->pc == exit_pc);
    ctx->host_depth = 0;
    return steps;
}

static uint32_t word(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void compare(void (*entry)(spu_context*), void (*oracle)(spu_context*))
{
    reference = seed;
    native = seed;
    reference.ls = reference.ls_store;
    native.ls = native.ls_store;
    run_until(&reference, oracle, reference.gpr[0]._u32[0]);
    entry(&native);
    assert(admitted && native.pc == reference.pc && !g_spu_trampoline_fn);
    for (unsigned offset = 0; offset < SPU_LS_SIZE; ++offset) {
        if (native.ls[offset] == reference.ls[offset]) continue;
        fprintf(stderr, "Stage F difference at %05x (r3=%x r4=%x): %02x != %02x\n",
                offset, seed.gpr[3]._u32[0], seed.gpr[4]._u32[0], native.ls[offset], reference.ls[offset]);
        assert(!"local store mismatch");
    }
    // The existing Stage F ABI allows r2 through r79 to be clobbered.
    assert(memcmp(native.gpr, reference.gpr, 2*sizeof(u128)) == 0);
    assert(memcmp(native.gpr + 80, reference.gpr + 80, 48*sizeof(u128)) == 0);
}

static void compare_block(void (*entry)(spu_context*), void (*oracle)(spu_context*),
                          uint32_t exit_pc)
{
    reference = seed;
    native = seed;
    reference.ls = reference.ls_store;
    native.ls = native.ls_store;
#ifdef TEST_SPU_PURE_LOOPS
    memset(g_spu_pch, 0, sizeof(g_spu_pch)); g_spu_pch_n=0;
#endif
    const unsigned steps = run_until(&reference, oracle, exit_pc);
#ifdef TEST_SPU_PURE_LOOPS
    uint32_t expected_history[8]; memcpy(expected_history,g_spu_pch,sizeof(g_spu_pch));
    const unsigned expected_count=g_spu_pch_n;
    memset(g_spu_pch, 0, sizeof(g_spu_pch)); g_spu_pch_n=0;
#endif
    if (exit_pc == 0x6958) assert(steps > 2);
    assert(g_spu_trampoline_fn == (exit_pc == 0x6958 ? oracle_func_00006958 : NULL));
    g_spu_trampoline_fn = NULL;
    const unsigned native_steps = run_until(&native, entry, exit_pc);
#ifdef TEST_SPU_PURE_LOOPS
    assert(native_steps <= steps);
    assert(native.steps == reference.steps);
    assert(g_spu_pch_n == expected_count && !memcmp(expected_history,g_spu_pch,sizeof(g_spu_pch)));
    if (exit_pc==0x6958 && !buzz_geometry_debug && !seed.int_enable && seed.gpr[15]._u32[0]>2)
        assert(native_steps < steps); // The optimized loop really ran.
#else
    assert(native_steps == steps);
#endif
    assert(g_spu_trampoline_fn == (exit_pc == 0x6958 ? buzz_job_6eba_spu_func_00006958 : NULL));
    g_spu_trampoline_fn = NULL;
    assert(memcmp(native.gpr, reference.gpr, sizeof(native.gpr)) == 0);
    assert(memcmp(native.ls, reference.ls, SPU_LS_SIZE) == 0);
    assert(native.status == reference.status);
}

static void set_reg(unsigned reg, uint32_t value)
{
    for (unsigned lane = 0; lane < 4; ++lane)
        seed.gpr[reg]._u32[lane] = value;
}

static void test_geometry_loops(void)
{
    for (unsigned test = 0; test < 300; ++test) {
        const unsigned groups = test < 290 ? test % 17 + 1 : 255 + test - 290;
        seed.int_enable = test % 7 == 0;
        set_reg(0, 0x100);
        set_reg(30, 0u - (groups + 3)*64u);
        set_reg(6, test % 3);
        const unsigned in[] = {11, 8, 7, 3}, out[] = {4, 12, 10, 9};
        for (unsigned lane = 0; lane < 4; ++lane) {
            set_reg(in[lane], 0xe800 + lane*16 + (groups + 3)*64u);
            set_reg(out[lane], 0xf800 + lane*16);
        }
        set_reg(26, 0x3f800000);
        set_reg(27, 0xbf800000);
        set_reg(50, 64);
        set_reg(51, 0x80000000u);
        for (unsigned reg = 52; reg <= 69; ++reg)
            set_reg(reg, (word() & 0x807fffffu) | 0x3f000000u);
        set_reg(70, 0);
        set_reg(71, 0x3f800000);
        buzz_geometry_debug = test % 5 == 0;
        compare_block(buzz_job_6eba_spu_func_000063E0, oracle_func_000063E0, 0x100);
        memset(seed.gpr, 0, sizeof(seed.gpr));
        set_reg(3, 0xda00);
        set_reg(7, 0xdc00);
        set_reg(8, 0xdd00);
        set_reg(9, 0x10);
        set_reg(10, 0xf800);
        set_reg(13, 0xe800);
        set_reg(15, groups);
        set_reg(37, 0xe800);
        set_reg(63, 6);
        set_reg(64, 0xe900);
        set_reg(81, 0xee00);
        compare_block(buzz_job_6eba_spu_func_00006E08, oracle_func_00006E08, 0x6958);
    }
    puts("PASS Stage F: 600 full-state vertex and culling comparisons");
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    FILE* image = fopen(argv[1], "rb");
    assert(image);
    assert(fread(seed.ls_store, 1, 0xe680, image) == 0xe680);
    assert(fgetc(image) == EOF);
    fclose(image);
    const buzz_arm_fp_state saved = buzz_arm_fp_save();
    buzz_arm_fp_enter_rtz(saved);
    for (unsigned test = 0; test < 600; ++test) {
        const uint32_t count = test % 127 + 1;
        for (unsigned offset = 0xe800; offset < 0xf400; offset += 4) {
            uint32_t bits = word();
            if (test & 1) bits = (bits & 0x807fffffu) | 0x3f000000u;
            bits = __builtin_bswap32(bits);
            memcpy(seed.ls_store + offset, &bits, 4);
        }
        seed.gpr[0]._u32[0] = 0x100;
        seed.gpr[3]._u32[0] = count;
        seed.gpr[4]._u32[0] = 0xe800;
        seed.gpr[5]._u32[0] = test & 2 ? 0xe800 : 0xf800;
        seed.gpr[6]._u32[0] = 0xf300;
        compare(buzz_job_6eba_spu_func_0000C518, oracle_func_0000C518);
        seed.gpr[3]._u32[0] = 0xe800;
        seed.gpr[4]._u32[0] = count;
        compare(buzz_job_6eba_spu_func_00002C00, oracle_func_00002C00);
    }
    puts("PASS Stage F: 1200 matrix and normalization states against the scalar lift");
    test_geometry_loops();
    buzz_arm_fp_restore(saved);
}
