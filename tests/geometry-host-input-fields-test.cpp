#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_streams.h"
#include "../port/native_geometry_host_unit_vectors.h"
#include "../port/native_geometry_job_input.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
static void drain(spu_context*, uint32_t);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c, uint32_t stop) { drain(c, stop); }
void spu_img_restore(spu_context* c, int32_t image) { assert(c->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 spu_rdch(spu_context*, uint32_t) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-descriptors-reference.inc"
extern "C" void spu_indirect_branch(spu_context* c)
{
    auto next = descriptors_reference_lookup(c->pc); assert(next); next(c);
}
static void drain(spu_context* c, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 65536); auto next = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; next(c);
    }
    assert(c->pc == stop);
}
static unsigned random_word(unsigned& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
    auto constants = std::make_unique<uint8_t[]>(0xe680);
    auto* file = fopen("analysis/native-geometry/phase3/test-descriptors-constants.bin", "rb");
    assert(file && fread(constants.get(), 0xe680, 1, file) == 1 && fgetc(file) == EOF); fclose(file);
    auto reference = std::make_unique<spu_context>(); auto& c = *reference;
    constexpr unsigned counts[] = {1,2,3,4,5,6,7,8,9,15,16,17,31,32,33,63,64,65};
    constexpr unsigned strides[] = {4,7,10,11,16,19,24,32};
    unsigned state = 0x839b1215, compared = 0;
    for (unsigned trial = 0; trial < 4096; ++trial) {
        const bool unit = trial & 1;
        const unsigned width = unit ? 3 : 4, components = unit ? 4 : 2;
        const unsigned count = counts[(trial / 2) % 18], stride = strides[(trial / 36) % 8];
        const unsigned offset = (trial / 288) % (stride - width + 1);
        memset(&c, 0, sizeof c); c.ls = c.ls_store; c.image_id = 41; c.host_depth = 1;
        memcpy(c.ls, constants.get(), 0xe680);
        // Valid speculative rows belong only to the independent oracle.
        for (unsigned at = 0x10000; at < 0x14000; ++at) {
            const unsigned byte = random_word(state);
            c.ls[at] = uint8_t((byte & 0xf0) | ((byte & 15) % 12));
        }
        c.gpr[0] = spu_il(0x1380); c.gpr[1] = spu_il(0x3c000); c.gpr[3] = spu_il(0x3b000);
        c.gpr[4] = spu_il(0x10000); c.gpr[5] = spu_il(0x31000); c.gpr[6] = spu_il(0x2c000);
        c.gpr[7] = spu_il(count); c.ls[0x3b000] = 0x80;
        c.ls[0x3b008] = uint8_t(count >> 8); c.ls[0x3b009] = uint8_t(count);
        auto* table = c.ls + 0x3b010; table[0] = 0; table[1] = 1; table[2] = 0x80; table[3] = 0;
        auto* descriptor = c.ls + 0x2c000;
        descriptor[0] = 1; descriptor[1] = uint8_t(stride); descriptor[2] = 1;
        descriptor[8] = uint8_t(offset); descriptor[9] = unit ? 9 : 3;
        descriptor[10] = unit ? 3 : 2; descriptor[11] = unit ? 2 : 5; descriptor[12] = uint8_t(width);
        // Original code may inspect the following descriptor bytes; this
        // reviewed sentinel is the same as the existing descriptor oracle.
        for (unsigned i = 0; i < 4; ++i) descriptor[16 + i * 2] = 8;
        GeometryHostSoA8 output[9];
        for (unsigned first = 0; first < count; first += 8) {
            const unsigned active = count - first < 8 ? count - first : 8;
            output[first / 8] = unit ? decode_geometry_host_unit_stream8(c.ls + 0x10000 + offset + first * stride, stride, active) :
                decode_geometry_host_half_stream8(c.ls, 0x10000 + offset + first * stride, stride, 2, active);
        }
        g_spu_trampoline_fn = nullptr; buzz_job_6eba_spu_func_00007DF8(&c); drain(&c, 0x1380);
        for (unsigned first = 0; first < count; first += 8) for (unsigned component = 0; component < 4; ++component) {
            unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<v8*>(words), output[first / 8].component[component]);
            for (unsigned lane = 0; lane < 8; ++lane) {
                const unsigned expected = first + lane < count && component < components ?
                    geometry_job_be32(c.ls + 0x18000 + (first + lane) * 16 + component * 4) : 0;
                if (words[lane] != expected) {
                    fprintf(stderr, "Host input field trial%u unit%u count%u stride%u offset%u vertex%u component%u actual%08X expected%08X\n",
                        trial, unit, count, stride, offset, first + lane, component, words[lane], expected); abort();
                }
                ++compared;
            }
        }
    }
    printf("PASS host input fields: 4096 synthetic cases against complete original descriptor decoder; %u component/lane comparisons;odd/even strides,quad crossings,mixed prefixes and all8lane tails\n", compared);
}
