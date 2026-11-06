#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_output_job.h"
#include "../port/native_geometry_context.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
namespace formatter_oracle { static void dispatch(spu_context*); }
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context* c) { formatter_oracle::dispatch(c); }
void spu_stop(spu_context*) { abort(); }
void buzz_geometry_job_native_fail(spu_context*, const char* reason) { fprintf(stderr, "Host invariant: %s\n", reason); abort(); }
}
#include "../analysis/native-geometry/phase3/test-formatter-reference.inc"

static uint32_t random_word(uint32_t& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
static uint16_t scalar_half(uint32_t bits)
{
    const unsigned magnitude = (bits >> 16) & 0x7fff;
    if (magnitude < 0x3800) return 0;
    return uint16_t((magnitude >= 0x4780 ? 0x7bff : ((bits - 0x38000000u) >> 13) & 0x7fff) |
        ((bits >> 16) & 0x8000));
}
static uint32_t scalar_cmp(const uint32_t* xyz)
{
    uint32_t c[3];
    for (unsigned i = 0; i < 3; ++i)
        c[i] = spu__cfl_lane(spu__fma_lane(xyz[i], i == 2 ? 0x3f7fc000 : 0x3f7fe000,
            i == 2 ? 0xba800000 : 0xba000000, 0, 0), 31, 0);
    return ((c[0] >> 21) & 2047) | (((c[1] >> 21) & 2047) << 11) | (c[2] & 0xffc00000);
}

// SPU FMA x*32767.5-0.5, cflts scale 2^16, high halfword.
static uint16_t scalar_snorm(uint32_t bits)
{
    return uint16_t(spu__cfl_lane(spu__fma_lane(bits, 0x46ffff00, 0xbf000000, 0, 0), 16, 0) >> 16);
}

static void arithmetic()
{
    uint32_t state = 0x1231abcd, bits[8], half[8], integers[8], snorm[8];
    unsigned checked = 0;
    for (unsigned group = 0; group < 393216; ++group) {
        for (unsigned lane = 0; lane < 8; ++lane) {
            const unsigned pattern = group * 8 + lane;
            // Every high halfword, retained mantissa bucket and discarded
            // tail extreme; then randomized full-bit extended-range values.
            bits[lane] = group < 131072
                ? (((pattern % 524288) / 8) << 16) | ((pattern % 8) << 13) |
                    (pattern >= 524288 ? 8191 : 0)
                : random_word(state);
        }
        const v8 input = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bits));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(half), geometry_host_half8(input));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(integers), geometry_host_signed31_8(input));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(snorm), geometry_host_snorm16_8(input));
        for (unsigned lane = 0; lane < 8; ++lane) {
            assert(half[lane] == scalar_half(bits[lane]));
            assert(snorm[lane] == scalar_snorm(bits[lane]));
            assert(integers[lane] == spu__cfl_lane(bits[lane], 31, 0)); ++checked;
        }
    }
    constexpr uint32_t special[] = {0, 0x80000000, 1, 0x807fffff, 0x00800000,
        0x38000000, 0x38800000, 0x3f800000, 0xbf800000, 0x7f7fffff,
        0x7f800000, 0xff800000, 0x7fffffff, 0xffffffff, 0x7fc01234};
    unsigned cmp_checked = 0;
    for (unsigned group = 0; group < 131072; ++group) {
        GeometryHostSoA8 input;
        uint32_t words[3][8], actual[8];
        for (unsigned axis = 0; axis < 3; ++axis) {
            for (unsigned lane = 0; lane < 8; ++lane) {
                const unsigned sample = group * 8 + lane;
                if (sample < 3375) words[axis][lane] = special[(sample / (axis == 0 ? 1 : axis == 1 ? 15 : 225)) % 15];
                else if (sample & 1) words[axis][lane] = random_word(state);
                else {
                    const float value = float(int(random_word(state) % 16385) - 8192) / 2048.0f;
                    memcpy(&words[axis][lane], &value, 4);
                }
            }
            input.component[axis] = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(words[axis]));
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(actual), geometry_host_cmp8(input));
        for (unsigned lane = 0; lane < 8; ++lane) {
            const uint32_t xyz[] = {words[0][lane], words[1][lane], words[2][lane]};
            const uint32_t expected = scalar_cmp(xyz);
            if (actual[lane] != expected) {
                fprintf(stderr, "CMP sample %u xyz=%08X/%08X/%08X actual=%08X expected=%08X\n",
                    group * 8 + lane, xyz[0], xyz[1], xyz[2], actual[lane], expected); abort();
            }
            ++cmp_checked;
        }
    }
    printf("PASS host arithmetic: %u half/signed31 values; %u CMP vertices including all 3375 special triples\n", checked, cmp_checked);
}

static void formatter_bytes()
{
    auto base = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>(), native = std::make_unique<spu_context>();
    memset(base.get(), 0, sizeof(*base)); base->ls = base->ls_store;
    auto* file = fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin", "rb");
    assert(file && fread(base->gpr, sizeof base->gpr, 1, file) == 1 &&
        fread(base->ls, SPU_LS_SIZE, 1, file) == 1 && fgetc(file) == EOF); fclose(file);
    constexpr unsigned manager = 0x30000, descriptor = 0xf000, destination = 0x22000;
    unsigned cases = 0; uint32_t state = 0x192eabcd;
    for (unsigned shape = 0; shape < 6; ++shape) for (unsigned vertices = 1; vertices <= 113; ++vertices) {
        memcpy(reference.get(), base.get(), sizeof(*base)); reference->ls = reference->ls_store;
        reference->host_depth = 1; reference->image_id = 41;
        reference->gpr[0] = spu_il(0x1234); reference->gpr[1] = spu_il(0x3e000);
        reference->gpr[3] = spu_il(manager); reference->gpr[4] = spu_il(descriptor);
        memset(reference->ls + manager, 0, 0x190);
        reference->ls[manager + 8] = vertices >> 8; reference->ls[manager + 9] = vertices;
        geometry_job_put32(reference->ls + manager + 0x54, destination);
        geometry_job_put32(reference->ls + manager + 0x58, destination);
        const unsigned fields = shape < 4 ? shape + 1 : 3;
        const unsigned stride = shape < 4 ? 12 + shape * 4 : shape == 4 ? 24 : 28;
        const unsigned table_stride = ((vertices + 7) & ~7u) * 16;
        const unsigned sources[] = {0x14000, 0x14000 + table_stride, 0x14000 + 2 * table_stride, 0x14000 + 3 * table_stride};
        const unsigned storage[] = {2, 6, 3, 3}, widths[] = {12, 4, 4, 4}, offsets[] = {0, 12, 16, 20};
        reference->ls[descriptor] = fields; reference->ls[descriptor + 1] = stride;
        for (unsigned field = 0; field < fields; ++field) {
            auto* row = reference->ls + descriptor + 8 + field * 8;
            memset(row, 0, 8); row[0] = offsets[field]; row[1] = storage[field];
            row[2] = storage[field] == 6 ? 3 : widths[field] / (storage[field] == 2 ? 4 : 2);
            row[3] = field + 1; row[4] = widths[field];
            reference->ls[manager + 0x160 + field] = field + 1;
            geometry_job_put32(reference->ls + manager + 0x10 + field * 4, sources[field]);
            for (unsigned at = 0; at < table_stride; at += 4)
                geometry_job_put32(reference->ls + sources[field] + at, random_word(state));
        }
        const unsigned bytes = (vertices * stride + 15) & ~15u;
        // Include complete padded float4 rows, but compare exactly the byte
        // range published by the job, including alignment bytes and holes.
        std::vector<uint8_t> output(((vertices + 7) & ~7u) * stride);
        memcpy(output.data(), reference->ls + destination, output.size());
        for (unsigned first = 0; first < ((vertices + 3) & ~3u); first += 8) {
            const unsigned count = ((vertices + 3) & ~3u) - first < 8 ? 4 : 8;
            for (unsigned field = 0; field < fields; ++field) {
                GeometryHostSoA8 input;
                for (unsigned component = 0; component < 4; ++component) {
                    uint32_t words[8];
                    for (unsigned lane = 0; lane < 8; ++lane)
                        words[lane] = geometry_job_be32(reference->ls + sources[field] + (first + lane) * 16 + component * 4);
                    input.component[component] = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(words));
                }
                auto* target = output.data() + first * stride + offsets[field];
                if (storage[field] == 2) geometry_host_float_field8(input, widths[field] / 4, target, stride, count);
                else if (storage[field] == 3) geometry_host_half_field8(input, widths[field] / 2, target, stride, count);
                else geometry_host_cmp_field8(input, target, stride, count);
            }
        }
        memcpy(native.get(), reference.get(), sizeof(*native)); native->ls = native->ls_store;
        GeometryJobEntry entry; entry.geometry_context_lsa = manager; entry.scratch_lsa = sources[0];
        entry.io_lsa = destination; entry.io_bytes = 0x4000; entry.vertices = vertices;
        GeometryHostOutputRecipe recipe; recipe.fields = fields; recipe.stride = stride;
        recipe.vertices = vertices; recipe.bytes = bytes;
        for (unsigned field = 0; field < fields; ++field)
            recipe.field[field] = {offsets[field], storage[field], storage[field] == 6 ? 3 : widths[field] / (storage[field] == 2 ? 4u : 2u), field};
        emit_geometry_host_output(*native, entry, recipe);
        assert(!memcmp(output.data(), native->ls + destination, bytes));
        assert(geometry_job_be32(native->ls + manager + 0x54) == destination &&
            geometry_job_be32(native->ls + manager + 0x58) == destination + bytes);
        // The adapter never materializes volatile or nonvolatile SPU values.
        assert(!memcmp(native->gpr, reference->gpr, sizeof native->gpr));
        for (unsigned at = 0; at < SPU_LS_SIZE; ++at)
            if (!(at >= destination && at < destination + bytes) && !(at >= manager + 0x54 && at < manager + 0x5c))
                assert(native->ls[at] == reference->ls[at]);
        g_spu_trampoline_fn = nullptr;
        formatter_oracle::buzz_job_6eba_spu_func_00004178(reference.get());
        for (unsigned steps = 0; reference->pc != 0x1234; ++steps) {
            assert(steps < 1048576 && g_spu_trampoline_fn);
            const auto next = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; next(reference.get());
        }
        if (memcmp(output.data(), reference->ls + destination, bytes)) {
            unsigned at = 0; while (at < bytes && output[at] == reference->ls[destination + at]) ++at;
            fprintf(stderr, "Host output shape%u vertices%u stride%u byte%u actual%02X expected%02X\n",
                shape, vertices, stride, at, output[at], reference->ls[destination + at]); abort();
        }
        ++cases;
    }
    printf("PASS host output: %u original-formatter published-byte comparisons, all tail lengths and preserved holes\n", cases);
}
static void admission()
{
    auto c = std::make_unique<spu_context>(), before = std::make_unique<spu_context>();
    memset(c.get(), 0, sizeof(*c)); c->ls = c->ls_store;
    auto* file = fopen("analysis/native-geometry/phase3/conversion-audit-v1/pc_4178.bin", "rb");
    assert(file && fread(c->gpr, sizeof c->gpr, 1, file) == 1 && fread(c->ls, SPU_LS_SIZE, 1, file) == 1); fclose(file);
    GeometryJobEntry e; e.config[0] = 3; e.config[2] = 0; e.config[3] = e.config[4] = 255;
    e.config[6] = 0x30; e.vertices = 113; e.index_count = 300;
    e.io_lsa = 0xe800; e.io_bytes = 0x10000; e.scratch_lsa = 0x20000; e.scratch_bytes = 0x4000;
    e.input[1] = {0xf000, 40, 0, 40}; e.input[2].lsa = 0xf100; e.input[8].lsa = 0x10000;
    e.input[11] = {0x12000, 0x100, 0, 0x100}; e.input[20] = {0xf080, 32, 0, 32};
    memset(c->ls + 0xf000, 0, 0x100);
    auto* descriptor = c->ls + 0xf000; descriptor[0] = 3; descriptor[1] = 20;
    const uint8_t rows[][8] = {{0,2,3,1,12,0,0,0}, {12,6,3,2,4,0,0,0}, {16,3,2,5,4,0,0,0}};
    memcpy(descriptor + 8, rows, sizeof rows);
    auto* secondary = c->ls + 0xf080; secondary[0] = 2; secondary[1] = 8;
    secondary[11] = 2; secondary[19] = 5;
    GeometryHostOutputRecipe recipe;
    assert(read_geometry_host_output_recipe(*c, e, recipe));
    assert(recipe.fields == 3 && recipe.stride == 20 && recipe.bytes == 2272 && recipe.field[2].slot == 2);
    memcpy(before.get(), c.get(), sizeof(*c));
    for (unsigned mutation = 0; mutation < 16; ++mutation) {
        auto bad = e; memcpy(c.get(), before.get(), sizeof(*c)); c->ls = c->ls_store;
        switch (mutation) {
        case 0: bad.config[2] = 1; break;
        case 1: bad.config[0] |= 16; break;
        case 2: bad.config[3] = 1; break;
        case 3: bad.config[6] = 0x32; break;
        case 4: c->ls[0xf009] = 1; break; // unsupported SNORM
        case 5: c->ls[0xf00b] = 17; break; // unavailable semantic
        case 6: c->ls[0xf018] = 12; break; // field overlap
        case 7: c->ls[0xf019] = 4; break; // unsupported UNORM
        case 8: c->ls[0xf011] = 7; break; // I16 is not CMP
        case 9: bad.input[1].size = 16; break;
        case 10: bad.input[20].lsa |= 1; break;
        case 11: bad.config[0] = 0; break; // table capacity
        case 12: bad.scratch_bytes = 16; break;
        case 13: bad.io_bytes = 16; break;
        case 14: c->ls[0xcf30] ^= 1; break;
        case 15: c->ls[0xf01c] = 3; break; // incomplete half component
        }
        auto untouched = std::make_unique<spu_context>(); memcpy(untouched.get(), c.get(), sizeof(*c));
        recipe.fields = 123; const char* reason = nullptr;
        assert(!read_geometry_host_output_recipe(*c, bad, recipe, &reason) && reason && recipe.fields == 123);
        assert(!memcmp(c.get(), untouched.get(), sizeof(*c)));
    }
    puts("PASS host recipe: recognized semantic map and 16 unchanged unsupported/alias/capacity/codec rejects");
}
int main()
{
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    buzz_test_enter_rtz();
    arithmetic(); formatter_bytes(); admission();
}
