#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_streams.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
static void drain(spu_context*, uint32_t);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_drain_call(spu_context* ctx, uint32_t stop) { drain(ctx, stop); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
}
#include "../analysis/native-geometry/phase3/test-half-stream-reference.inc"
static void drain(spu_context* ctx, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 65536); auto next = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; next(ctx);
    }
    assert(ctx->pc == stop);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    auto fixture = std::make_unique<unsigned char[]>(2048 + SPU_LS_SIZE);
    auto* file = fopen("analysis/native-geometry/phase3/test-half-stream-fixture.bin", "rb");
    assert(file && fread(fixture.get(), 2048 + SPU_LS_SIZE, 1, file) == 1 && fgetc(file) == EOF); fclose(file);
    auto reference = std::make_unique<spu_context>(); auto& c = *reference;
    memset(&c, 0, sizeof c); c.ls = c.ls_store; c.image_id = 41; c.host_depth = 2;
    memcpy(c.ls, fixture.get() + 2048, 0xe680);
    c.gpr[1] = spu_il(0x3d000); c.gpr[107] = spu_il(1);
    c.gpr[106] = spu_il(16); c.gpr[42] = spu_il(8);
    c.gpr[80] = spu_il(0x24070); c.gpr[81] = spu_il(64); c.gpr[83] = spu_il(16);
    constexpr unsigned source[] = {0x10000, 0x14000, 0x18000, 0x1c000};
    constexpr unsigned base_register[] = {118, 117, 116, 124};
    for (unsigned row = 0; row < 4; ++row) {
        c.gpr[41]._u32[row] = 0x24000 + row * 16;
        c.gpr[82]._u32[row] = 0x3c000 + row * 16;
        c.gpr[base_register[row]] = spu_il(source[row]);
        spu_ls_write128(&c, 0x3d070 - row * 16, spu_il(source[row] + 16));
    }
    u128 initial_registers[128]; memcpy(initial_registers, c.gpr, sizeof initial_registers);
    unsigned compared = 0;
    for (unsigned prefix = 0; prefix < 256; ++prefix) for (unsigned first = 0; first < 65536; first += 32) {
        // This bank belongs only to the independent lifted oracle. Native
        // input arithmetic has no context, bank, call frame or scratch.
        memcpy(c.gpr, initial_registers, sizeof initial_registers); c.pc = 0x8378;
        unsigned encoded[4][8];
        for (unsigned lane = 0; lane < 8; ++lane) {
            auto* at = c.ls + source[lane % 4] + (lane / 4) * 16;
            at[0] = uint8_t(prefix);
            for (unsigned component = 0; component < 4; ++component) {
                const unsigned half = first + lane * 4 + component;
                encoded[component][lane] = half;
                at[8 + component * 2] = uint8_t(half >> 8); at[9 + component * 2] = uint8_t(half);
            }
        }
        GeometryHostSoA8 input;
        for (unsigned component = 0; component < 4; ++component)
            input.component[component] = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(encoded[component]));
        const auto actual = decode_geometry_host_halves8(input, v8_set(prefix), 4);
        g_spu_trampoline_fn = nullptr; buzz_job_6eba_spu_func_00008378(&c);
        assert(c.pc == 0x83c8 && g_spu_trampoline_fn == buzz_job_6eba_spu_func_000083C8);
        g_spu_trampoline_fn = nullptr;
        for (unsigned component = 0; component < 4; ++component) {
            unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), actual.component[component]);
            for (unsigned lane = 0; lane < 8; ++lane) {
                const auto* at = c.ls + 0x24000 + lane * 16 + component * 4;
                const unsigned expected = unsigned(at[0]) << 24 | unsigned(at[1]) << 16 | unsigned(at[2]) << 8 | at[3];
                if (words[lane] != expected) {
                    fprintf(stderr, "Host half prefix%02X half%04X lane%u component%u actual%08X expected%08X\n",
                        prefix, encoded[component][lane], lane, component, words[lane], expected); abort();
                }
                ++compared;
            }
        }
        // The UV consumer requests two components; the other components
        // must not accidentally retain data from a previous attribute.
        if (first == 0) for (unsigned components = 0; components <= 4; ++components) {
            const auto partial = decode_geometry_host_halves8(input, v8_set(prefix), components);
            for (unsigned component = 0; component < 4; ++component)
                assert(_mm256_movemask_epi8(_mm256_cmpeq_epi32(partial.component[component],
                    component < components ? actual.component[component] : _mm256_setzero_si256())) == -1);
        }
    }
    printf("PASS host half input: %u values against complete original decoder output;all65536halves x all256quad-prefix bytes;component0..4 masks;no GPR/scratch acceptance gate\n", compared);
}
