#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../ps3recomp/runtime/spu/spu_dma.h"
#include "../port/native_geometry_context.h"
#include "../port/native_geometry_job_setup.h"
#include "../port/native_geometry_job_call.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct InputTransfer { uint32_t kind, lsa, ea, size; std::vector<uint8_t> data; };
static std::vector<InputTransfer> transfers;
static size_t consumed;
static unsigned preamble_reads;
static const char* current_case = "synthetic";

static void drain(spu_context* ctx)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 4096);
        auto fn = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr;
        fn(ctx);
    }
}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_drain_call(spu_context* ctx, uint32_t return_pc) { drain(ctx); assert(ctx->pc == return_pc); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
void spu_stop(spu_context*) { fprintf(stderr, "Unexpected STOP: %s\n", current_case); abort(); }
void spu_halt(spu_context*) { fprintf(stderr, "Unexpected HALT: %s\n", current_case); abort(); }
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value)
{
    switch (channel) {
    case MFC_LSA: ctx->mfc_lsa = value._u32[0]; break;
    case MFC_EAH: ctx->mfc_eah = value._u32[0]; break;
    case MFC_EAL: ctx->mfc_eal = value._u32[0]; break;
    case MFC_Size: ctx->mfc_size = value._u32[0]; break;
    case MFC_TagID: ctx->mfc_tag = value._u32[0]; break;
    case MFC_Cmd: {
        // The only pre-initializer I/O is 24F0's 32-byte metadata GET.
        assert(value._u32[0] == 0x40 && ctx->mfc_size == 32 && ctx->mfc_eah == 0);
        assert(consumed < transfers.size());
        const auto& t = transfers[consumed++];
        assert(t.kind == 1 && t.lsa == ctx->mfc_lsa && t.ea == ctx->mfc_eal && t.size == ctx->mfc_size);
        assert(t.lsa <= SPU_LS_SIZE - t.size);
        memcpy(ctx->ls + t.lsa, t.data.data(), t.size);
        ++preamble_reads;
        break;
    }
    default: abort();
    }
}
}
#include "../analysis/native-geometry/phase3/test-context-reference.inc"

static buzz::GeometryContextInit arguments(const spu_context& ctx)
{
    return {ctx.gpr[3]._u32[0], ctx.gpr[4]._u32[0], ctx.gpr[5]._u32[0], ctx.gpr[6]._u32[0],
            ctx.gpr[7]._u32[0], ctx.gpr[8]._u32[0], ctx.gpr[9]._u32[0], ctx.gpr[10]._u32[0],
            ctx.gpr[11]._u32[0], ctx.gpr[12]._u32[0]};
}

static void compare(spu_context& native, spu_context& reference, const buzz::GeometryContextInit& in)
{
    const auto before = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&native),
        reinterpret_cast<const uint8_t*>(&native) + sizeof(native));
    buzz::GeometryContextState state;
    const char* reason = nullptr;
    if (!buzz::initialize_geometry_context(native, in, state, &reason)) {
        fprintf(stderr, "%s: initializer rejected %s\n", current_case, reason); abort();
    }
    // Internal native operations change LS only, including no temporary GPRs
    // or controls. Their complete ABI is checked at the whole-driver boundary.
    auto unchanged = before;
    memcpy(unchanged.data() + offsetof(spu_context, ls_store), native.ls, SPU_LS_SIZE);
    assert(!memcmp(&native, unchanged.data(), unchanged.size()));
    reference.host_depth = 1;
    g_spu_trampoline_fn = nullptr;
    buzz_job_6eba_spu_func_00002F80(&reference);
    drain(&reference);
    assert(reference.pc == reference.gpr[0]._u32[0]);
    if (memcmp(native.ls, reference.ls, SPU_LS_SIZE)) {
        for (unsigned at = 0; at < SPU_LS_SIZE; ++at) if (native.ls[at] != reference.ls[at]) {
            fprintf(stderr, "%s: LS[%05X] native=%02X reference=%02X context=%05X\n",
                current_case, at, native.ls[at], reference.ls[at], in.context);
            break;
        }
        abort();
    }
    for (unsigned i = 0; i < state.table_count; ++i)
        assert(state.tables[i] == buzz::geometry_job_be32(reference.ls + in.context + 0x10 + i * 4));
}

static uint32_t random_word(uint32_t& seed)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
static void fill(spu_context& ctx, uint32_t seed)
{
    memset(&ctx, 0, sizeof ctx);
    ctx.ls = ctx.ls_store; ctx.image_id = 41;
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx.ls[i] = uint8_t(random_word(seed));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(seed);
    ctx.gpr[0]._u32[0] = 0x2088; ctx.gpr[1]._u32[0] = 0x2ffe0;
}
static void assign(spu_context& ctx, const buzz::GeometryContextInit& in)
{
    const uint32_t args[] = {in.context, in.configuration, in.scratch, in.scratch_bytes,
        in.workspace, in.workspace_bytes, in.dma_tag, in.viewport, in.object, in.custom};
    for (unsigned i = 0; i < 10; ++i) ctx.gpr[3 + i]._u32[0] = args[i];
}
static void clone(spu_context& destination, const spu_context& source)
{
    memcpy(&destination, &source, sizeof source); destination.ls = destination.ls_store;
}

static void compare_preamble(spu_context& native, spu_context& reference)
{
    buzz::GeometryJobEntry entry;
    assert(buzz::read_geometry_job_entry(native, entry));
    clone(reference, native);
    buzz::GeometryJobSetup setup;
    const char* reason = nullptr;
    const auto before = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&native),
        reinterpret_cast<uint8_t*>(&native) + sizeof native);
    if (!buzz::prepare_geometry_job(native, setup, &reason)) {
        fprintf(stderr, "%s: native preamble rejected %s\n", current_case, reason); abort();
    }
    auto unchanged = before;
    memcpy(unchanged.data() + offsetof(spu_context, ls_store), native.ls, SPU_LS_SIZE);
    assert(!memcmp(&native, unchanged.data(), unchanged.size()));
    reference.host_depth = 1; g_spu_trampoline_fn = nullptr;
    buzz_job_6eba_spu_func_00001C18(&reference); drain(&reference);
    assert(reference.pc == 0x2f80);
    if (memcmp(native.ls, reference.ls, SPU_LS_SIZE)) {
        for (unsigned at = 0; at < SPU_LS_SIZE; ++at) if (native.ls[at] != reference.ls[at]) {
            fprintf(stderr, "%s: preamble LS[%05X] native=%02X reference=%02X flags=%08X frame=%X\n",
                current_case, at, native.ls[at], reference.ls[at], entry.flags, entry.frame_lsa); break;
        }
        abort();
    }
    const auto in = arguments(reference);
    assert(!memcmp(&in, &setup.initialization, sizeof in));
    assert(in.context == entry.geometry_context_lsa && in.configuration == entry.input[16].lsa);
    assert(in.scratch == entry.scratch_lsa && in.scratch_bytes == entry.scratch_bytes);
    assert(in.workspace == entry.workspace_lsa && in.workspace_bytes == entry.workspace_bytes);
    auto call_before = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&native),
        reinterpret_cast<uint8_t*>(&native) + sizeof native);
    buzz::materialize_geometry_job_initial_call(native, setup);
    memcpy(call_before.data() + offsetof(spu_context, pc), &native.pc, sizeof native.pc);
    assert(native.pc == reference.pc);
    for (unsigned reg = 0; reg < 128; ++reg) {
        if (!(reg <= 1 || (reg >= 3 && reg <= 12) || (reg >= 80 && reg <= 109) || reg == 126)) continue;
        if (memcmp(&native.gpr[reg], &reference.gpr[reg], 16)) {
            fprintf(stderr, "%s: initial-call r%u native=%08X,%08X,%08X,%08X reference=%08X,%08X,%08X,%08X\n",
                current_case, reg, native.gpr[reg]._u32[0], native.gpr[reg]._u32[1], native.gpr[reg]._u32[2], native.gpr[reg]._u32[3],
                reference.gpr[reg]._u32[0], reference.gpr[reg]._u32[1], reference.gpr[reg]._u32[2], reference.gpr[reg]._u32[3]);
            abort();
        }
        memcpy(call_before.data() + offsetof(spu_context, gpr) + reg * 16, &native.gpr[reg], 16);
    }
    assert(!memcmp(call_before.data(), &native, sizeof native));
    compare(native, reference, in);
}

static void synthetic_job(spu_context& ctx, unsigned trial)
{
    fill(ctx, trial + 1);
    ctx.gpr[3]._u32[0] = 0x3fbc0; ctx.gpr[4]._u32[0] = 0x3fc00;
    ctx.gpr[1]._u32[0] = 0x137e0;
    auto put = [&](unsigned at, uint32_t value) { buzz::geometry_job_put32(ctx.ls + at, value); };
    memset(ctx.ls + 0x3fbc0, 0, 0x140);
    put(0x3fbc0, 0xe800); put(0x3fbd4, 0x2003fbf0);
    put(0x3fbd8, 21 << 16); put(0x3fbdc, 0x10800); put(0x3fbe0, 0x10800); put(0x3fbe4, 20);
    put(0x3fc08, 0x0e6800a8); put(0x3fc14, 8192); put(0x3fc1c, 256);
    const uint32_t constants[] = {0, 0x7fff, 0, 0, 0x04050607, 0x80808080, 0x0c0d0e0f, 0x80808080};
    for (unsigned i = 0; i < 8; ++i) put(0xcd10 + i * 4, constants[i]);
    unsigned cursor = 0xe800;
    for (unsigned i = 0; i < 21; ++i) {
        const unsigned size = i == 0 ? 64 : i == 11 && (trial & 64) ? 0 : i == 14 ? 128 : i == 15 ? 64 : i == 16 ? 16 : 32;
        put(0x3fc30 + i * 8, size); put(0x3fc34 + i * 8, 0x100000 + i * 256);
        cursor += size;
    }
    static const uint32_t modes[] = {0, 2304, 2305, 17};
    put(0xe800, trial / 128 ? 1 : 0);
    put(0xe824, modes[trial / 128]);
    static const uint32_t flags[] = {0x10000000, 0x80, 0x100, 8, 32, 64};
    uint32_t flag_word = 0;
    for (unsigned i = 0; i < 6; ++i) if (trial & (1 << i)) flag_word |= flags[i];
    put(0x3fcec, flag_word);
}

int main(int argc, char** argv)
{
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    static const uint16_t counts[] = {0, 1, 7, 8, 9, 408, 4095, 65535};
    for (unsigned trial = 0; trial < 1024; ++trial) {
        fill(*native, trial + 1);
        buzz::GeometryContextInit in{0x30000, 0x1000, 0x20000, 0x8000, 0x10000, 0x8000,
            trial & 31, trial & 1 ? 0x2000u : 0, trial & 2 ? 0x3000u : 0, trial & 4 ? 0x4000u : 0};
        native->ls[in.configuration] = uint8_t((trial / 8) % 16 | ((trial & 1) ? 0x80 : 0));
        const unsigned count = counts[trial / 128];
        native->ls[in.configuration + 8] = uint8_t(count >> 8);
        native->ls[in.configuration + 9] = uint8_t(count);
        assign(*native, in); clone(*reference, *native);
        compare(*native, *reference, in);
    }
    for (unsigned fault = 0; fault < 13; ++fault) {
        fill(*native, 1);
        buzz::GeometryContextInit in{0x30000, 0x1000, 0x20000, 0x8000, 0x10000, 0x8000, 20, 0x2000, 0x3000, 0x4000};
        switch (fault) {
        case 0: in.context += 4; break;
        case 1: in.context = 0x3fe80; break;
        case 2: native->ls = nullptr; break;
        case 3: in.configuration += 1; break;
        case 4: in.configuration = in.context; break;
        case 5: in.viewport = in.context - 16; break;
        case 6: in.object = 0x3fff0; break;
        case 7: in.custom = UINT32_MAX - 15; break;
        case 8: in.scratch_bytes = UINT32_MAX; break;
        case 9: in.workspace_bytes = UINT32_MAX; break;
        case 10: in.configuration = 0x40000; break;
        case 11: in.scratch = 0x40001; break;
        case 12: in.workspace = 0xffffffff; break;
        }
        buzz::GeometryContextState state; memset(&state, 0xa5, sizeof state);
        auto output_before = state;
        const auto before = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(native.get()),
            reinterpret_cast<uint8_t*>(native.get()) + sizeof(*native));
        const char* reason = nullptr;
        assert(!buzz::initialize_geometry_context(*native, in, state, &reason) && reason);
        assert(!memcmp(native.get(), before.data(), before.size()));
        assert(!memcmp(&state, &output_before, sizeof state));
    }
    for (unsigned trial = 0; trial < 512; ++trial) {
        synthetic_job(*native, trial);
        compare_preamble(*native, *reference);
    }
    for (unsigned fault = 0; fault < 7; ++fault) {
        synthetic_job(*native, 0);
        auto put = [&](unsigned at, uint32_t value) { buzz::geometry_job_put32(native->ls + at, value); };
        switch (fault) {
        case 0: put(0x3fcf4, 0x100000); break;
        case 1: native->ls[0x3fcfa] = 1; break;
        case 2: put(0x3fc30 + 15 * 8, 32); break;
        case 3: put(0x3fc30 + 14 * 8, 64); break;
        case 4: put(0x3fc34 + 15 * 8, 0x100001); break;
        case 5: put(0x3fc34 + 14 * 8, 0x100001); break;
        case 6: put(0x3fcec, 0x80); put(0x3fc30 + 1 * 8, 0); break;
        }
        buzz::GeometryJobSetup setup; memset(&setup, 0xa5, sizeof setup);
        const auto output_before = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&setup),
            reinterpret_cast<uint8_t*>(&setup) + sizeof setup);
        const auto before = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(native.get()),
            reinterpret_cast<uint8_t*>(native.get()) + sizeof(*native));
        const char* reason = nullptr;
        assert(!buzz::prepare_geometry_job(*native, setup, &reason) && reason);
        assert(!memcmp(&setup, output_before.data(), output_before.size()));
        assert(!memcmp(native.get(), before.data(), before.size()));
    }
    unsigned corpus = 0;
    if (argc > 1) for (const auto& path : std::filesystem::directory_iterator(argv[1])) {
        if (path.path().extension() != ".gz" || path.path().string().find(".bjc.gz") == std::string::npos) continue;
        const auto name = path.path().string(); current_case = name.c_str();
        gzFile file = gzopen(name.c_str(), "rb"); assert(file);
        uint32_t header[6], count;
        assert(gzread(file, header, sizeof header) == sizeof header);
        assert(header[0] == 0x434a5a42 && header[1] == 1 && header[2] == 41 && header[5] == SPU_LS_SIZE);
        memset(native.get(), 0, sizeof(*native)); native->ls = native->ls_store; native->image_id = 41;
        assert(gzread(file, native->gpr, sizeof native->gpr) == sizeof native->gpr);
        assert(gzread(file, native->ls, SPU_LS_SIZE) == SPU_LS_SIZE);
        assert(gzread(file, &count, sizeof count) == sizeof count && count < 65536);
        transfers.clear(); consumed = 0;
        for (unsigned i = 0; i < count; ++i) {
            InputTransfer t;
            assert(gzread(file, &t.kind, 16) == 16 && t.size <= 0x10000);
            t.data.resize(t.size);
            assert(gzread(file, t.data.data(), t.size) == t.size);
            transfers.push_back(std::move(t));
        }
        gzclose(file);
        compare_preamble(*native, *reference);
        ++corpus;
    }
    if (argc > 1) assert(corpus);
    printf("Native setup/context: 1024 initializer cases, 512 preamble/call flag/secondary/command cases, 13+7 unchanged-state rejections, %u real native preambles/initializers and call vectors exact, %u recorded metadata GETs PASS\n", corpus, preamble_reads);
}
