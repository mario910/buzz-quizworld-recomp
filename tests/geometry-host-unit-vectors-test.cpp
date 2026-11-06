#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_unit_vectors.h"
#include "../port/native_geometry_context.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_drain_call(spu_context*, uint32_t) { abort(); }
void spu_img_restore(spu_context*, int32_t) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-unit-vectors-reference.inc"

static unsigned random_word(unsigned& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
int main()
{
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    buzz_test_enter_rtz();
    auto fixture = std::make_unique<unsigned char[]>(2048 + SPU_LS_SIZE);
    auto* file = fopen("analysis/native-geometry/phase3/test-unit-vectors-fixture.bin", "rb");
    assert(file && fread(fixture.get(), 2048 + SPU_LS_SIZE, 1, file) == 1 && fgetc(file) == EOF); fclose(file);
    auto reference = std::make_unique<spu_context>();
    constexpr unsigned source[] = {0x10000, 0x14000, 0x18000, 0x1c000};
    constexpr unsigned base_register[] = {118, 117, 116, 124};
    constexpr unsigned constants[] = {96, 87, 85, 86, 89, 88, 97, 109, 112, 108, 102, 101, 110};
    constexpr unsigned coordinate[] = {0, 1, 2, 3, 63, 127, 255, 383, 511, 512, 639, 767, 895, 1021, 1022, 1023};
    unsigned state = 0x723bc9d1, vertices = 0;
    for (unsigned trial = 0; trial < 7168; ++trial) {
        auto& c = *reference;
        memset(&c, 0, sizeof c); c.ls = c.ls_store; c.image_id = 41; c.pc = 0x9114; c.host_depth = 2;
        memcpy(c.ls, fixture.get() + 2048, 0xe680);
        for (unsigned reg : constants) memcpy(&c.gpr[reg], fixture.get() + reg * 16, 16);
        c.gpr[1] = spu_il(0x3d000); c.gpr[121] = c.gpr[107] = spu_il(1);
        c.gpr[106] = spu_il(3); c.gpr[42] = spu_il(0);
        c.gpr[80] = spu_il(0x24070); c.gpr[81] = spu_il(64); c.gpr[83] = spu_il(16);
        unsigned packed[8];
        for (unsigned lane = 0; lane < 8; ++lane) {
            const unsigned index = trial < 3072 ? trial : random_word(state);
            const unsigned u = trial < 3072 ? coordinate[index % 16] : index & 1023;
            const unsigned v = trial < 3072 ? coordinate[(index / 16) % 16] : (index >> 10) & 1023;
            const unsigned selector = trial < 3072 ? index / 256 : (index >> 20) % 12;
            packed[lane] = (u << 14) | (v << 4) | selector;
        }
        for (unsigned row = 0; row < 4; ++row) {
            c.gpr[41]._u32[row] = 0x24000 + row * 16;
            c.gpr[82]._u32[row] = 0x3c000 + row * 16;
            c.gpr[base_register[row]] = spu_il(source[row]);
            spu_ls_write128(&c, 0x3d070 - row * 16, spu_il(source[row] + 16));
            // Only the first eight vertices are observed. Fill valid data
            // for the retained decoder's speculative groups as well.
            for (unsigned group = 0; group < 6; ++group) {
                const unsigned value = packed[(group % 2) * 4 + row];
                auto* at = c.ls + source[row] + group * 16;
                at[0] = uint8_t(value >> 16); at[1] = uint8_t(value >> 8); at[2] = uint8_t(value);
            }
        }
        spu_ls_write128(&c, 0x3d0c0, spu_il(0xddf0));
        const auto actual = decode_geometry_host_unit8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(packed)));
        g_spu_trampoline_fn = nullptr; buzz_job_6eba_spu_func_00009114(&c);
        for (unsigned steps = 0; c.pc != 0x83c8; ++steps) {
            assert(steps < 65536 && g_spu_trampoline_fn);
            const auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(&c);
        }
        for (unsigned component = 0; component < 4; ++component) {
            unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), actual.component[component]);
            for (unsigned lane = 0; lane < 8; ++lane) {
                const unsigned expected = geometry_job_be32(c.ls + 0x24000 + lane * 16 + component * 4);
                if (words[lane] != expected) {
                    fprintf(stderr, "Host unit trial%u lane%u component%u packed%06X actual%08X expected%08X\n",
                        trial, lane, component, packed[lane], words[lane], expected); abort();
                }
            }
        }
        vertices += 8;
    }
    printf("PASS host unit SoA: %u vertices against complete original decoder output;all12 selectors/grid corners/random mixed lanes;no GPR or scratch acceptance gate\n", vertices);
}
