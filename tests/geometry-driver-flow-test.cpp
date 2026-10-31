#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_pipeline.h"
#include "../port/native_function_check.h"
#include "../ps3recomp/runtime/spu/spu_dma.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <immintrin.h>

struct Call {
    unsigned pc, link;
    std::array<u128, 10> arguments{};
    std::array<u128, 31> nonvolatile{};
};
static std::vector<Call> calls;
static unsigned scenario;
static const char* current_case = "synthetic";
static bool emit_enabled = false, full_enabled = false;
static unsigned emit_references = 0, emit_native[4]{}, emit_fallback[4]{};
static bool routing_probe = false, commands_enabled = false, command_publish_enabled = false;
static unsigned publication_references = 0, publication_native = 0, publication_fallback = 0;
static unsigned routed_references = 0, routed_native = 0, routed_fallback = 0;
static void routing_inside(spu_context*);
static void drain(spu_context* ctx, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 4096);
        if (g_spu_trampoline_fn == spu_indirect_branch && ctx->pc == stop) {
            g_spu_trampoline_fn = nullptr; break;
        }
        auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(ctx);
    }
    assert(ctx->pc == stop);
}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_drain_call(spu_context* ctx, uint32_t link) { drain(ctx, link); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_halt(spu_context*) { abort(); }
void spu_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 spu_rdch(spu_context*, uint32_t) { abort(); }
void buzz_geometry_job_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 buzz_geometry_job_rdch(spu_context*, uint32_t) { abort(); }
int buzz_geometry_job_publish_enabled() { return 0; }
void buzz_geometry_job_publish_result(int) { abort(); }
int buzz_geometry_job_layout_enabled() { return 0; }
int buzz_geometry_job_vertex_enabled() { return 0; }
int buzz_geometry_job_commands_enabled() { return commands_enabled; }
int buzz_geometry_job_full_commands_enabled() { return full_enabled; }
int buzz_geometry_job_emit_enabled() { return emit_enabled; }
int buzz_geometry_job_command_publish_enabled() { return command_publish_enabled; }
void buzz_geometry_job_output_result(unsigned stage, int native, int) {
    if (stage >= BUZZ_GEOMETRY_JOB_COMMAND_BIND) {
        assert(routing_probe && stage <= BUZZ_GEOMETRY_JOB_FULL_COMMANDS);
        if (native) ++emit_native[stage - BUZZ_GEOMETRY_JOB_COMMAND_BIND];
        else ++emit_fallback[stage - BUZZ_GEOMETRY_JOB_COMMAND_BIND];
        return;
    }
    if (stage == BUZZ_GEOMETRY_JOB_COMMAND_PUBLISH) {
        assert(routing_probe);
        if (native) ++publication_native; else ++publication_fallback;
        return;
    }
    assert(routing_probe && stage == BUZZ_GEOMETRY_JOB_COMMAND_BUFFER);
    if (native) ++routed_native; else ++routed_fallback;
}
void buzz_geometry_job_native_fail(spu_context*, const char* reason) {
    fprintf(stderr, "%s: native rejection: %s\n", current_case, reason); abort();
}
}

static unsigned arguments_of(unsigned pc)
{
    switch (pc) {
    case 0xac68: case 0x37c0: case 0x32c8: case 0x27a0: return 7;
    case 0xbf8: return 8;
    case 0x9f8: return 10;
    case 0xaea0: case 0x6608: case 0x2b80: return 4;
    case 0x2dc0: return 6;
    case 0x54e8: return 3;
    case 0x35f0: return 5;
    case 0x12f0: case 0x2e8: return 12;
    default: abort();
    }
}
static void model_call(spu_context* ctx, unsigned pc)
{
    assert(ctx->host_depth == 1);
    if (routing_probe && pc == 0xac68) routing_inside(ctx);
    Call record{}; record.pc = pc; record.link = ctx->gpr[0]._u32[0];
    uint32_t hash = pc ^ uint32_t(calls.size() * 0x173b);
    for (unsigned reg = 3; reg <= arguments_of(pc); ++reg) {
        record.arguments[reg - 3] = ctx->gpr[reg];
        for (auto word : ctx->gpr[reg]._u32) hash = (hash ^ word) * 0x1000193u;
    }
    for (unsigned i = 0; i < 30; ++i) record.nonvolatile[i] = ctx->gpr[80 + i];
    record.nonvolatile[30] = ctx->gpr[126];
    calls.push_back(record);
    // A legal callee may spill these complete vectors. Exercise that contract
    // separately from argument observation and real-library corpus replay.
    for (unsigned i = 0; i < 30; ++i)
        spu_ls_write128(ctx, ctx->gpr[1]._u32[0] - 0x200 - i * 16, ctx->gpr[80 + i]);
    const unsigned frame = ctx->gpr[1]._u32[0];
    const u128 value = spu_ilhu(uint16_t(hash));
    if (pc == 0x2dc0) spu_ls_write128(ctx, frame + 0x50, value);
    if (pc == 0x35f0) spu_ls_write128(ctx, frame + 0x40, value);
    if (pc == 0x37c0) spu_ls_write128(ctx, frame + 0x90, value);
    if (pc == 0x32c8) spu_ls_write128(ctx, frame + 0x60, value);
    for (unsigned reg = 2; reg < 80; ++reg)
        for (unsigned lane = 0; lane < 4; ++lane) ctx->gpr[reg]._u32[lane] = hash ^ (reg * 0x11701u + lane * 0x137u);
    if (pc == 0x6608) ctx->gpr[3]._u32[0] = scenario % 3 ? 3 + (scenario % 97) * 3 : 0;
    if (pc == 0x2b80) ctx->gpr[3]._u32[0] = 16 * (1 + scenario % 59);
    if (pc == 0x37c0) ctx->gpr[3]._u32[0] = scenario & 1 ? 1 : 0x100;
    ctx->pc = record.link;
}
#include "../analysis/native-geometry/phase3/test-driver-reference.inc"

static void routing_reference(spu_context* ctx)
{
    ++routed_references; ctx->gpr[2] = spu_il(-1); ctx->pc = ctx->gpr[0]._u32[0];
}
static void publication_reference(spu_context* ctx)
{
    ++publication_references; ctx->pc = ctx->gpr[0]._u32[0];
}
static void emit_reference(spu_context* ctx) { ++emit_references; ctx->pc = ctx->gpr[0]._u32[0]; }
static void routing_inside(spu_context* ctx)
{
    std::array<u128, 128> saved;
    memcpy(saved.data(), ctx->gpr, sizeof ctx->gpr); const auto pc = ctx->pc;
    const uint32_t context = ctx->gpr[3]._u32[0];
    buzz::geometry_job_put32(ctx->ls + context + 0x58, 0x20000);
    ctx->gpr[4] = spu_ila(0x28000); ctx->gpr[5] = spu_ila(0x10000);
    ctx->gpr[6] = spu_il(0); ctx->gpr[7] = spu_il(0);
    buzz_geometry_job_prepare_commands(ctx, routing_reference);
    assert(routed_native == 1 && routed_references == 1);
    assert(buzz::geometry_job_be32(ctx->ls + 0x28000) == 0x20080 &&
           buzz::geometry_job_be32(ctx->ls + 0x28004) == 0x20084);
    auto foreign = std::make_unique<spu_context>(); memcpy(foreign.get(), ctx, sizeof(*ctx)); foreign->ls = foreign->ls_store;
    buzz_geometry_job_prepare_commands(foreign.get(), routing_reference);
    assert(routed_references == 2 && routed_native == 1);
    commands_enabled = false; buzz_geometry_job_prepare_commands(ctx, routing_reference); commands_enabled = true;
    assert(routed_references == 3 && routed_native == 1);
    ctx->gpr[4]._u32[0] = SPU_LS_SIZE - 1;
    buzz_geometry_job_prepare_commands(ctx, routing_reference);
    assert(routed_references == 4 && routed_fallback == 1);
    ctx->gpr[5] = spu_il(0); // The no-destination path needs no LS or DMA.
    buzz_geometry_job_publish_commands(ctx, publication_reference);
    assert(publication_native == 1 && publication_references == 1);
    buzz_geometry_job_publish_commands(foreign.get(), publication_reference);
    command_publish_enabled = false;
    buzz_geometry_job_publish_commands(ctx, publication_reference);
    command_publish_enabled = true;
    ctx->gpr[5] = spu_il(1); // Unsupported before mutation.
    buzz_geometry_job_publish_commands(ctx, publication_reference);
    assert(publication_references == 4 && publication_fallback == 1 && publication_native == 1);
    ctx->gpr[3] = spu_ila(context); ctx->gpr[4] = spu_ila(0x28000);
    ctx->gpr[5] = spu_ila(0x28020); ctx->gpr[6] = spu_il(1);
    buzz::geometry_job_put32(ctx->ls + context + 0x74, 0x2a000); ctx->ls[0x2a000] = 0;
    buzz_geometry_job_command_bindings(ctx, emit_reference, 0);
    ctx->gpr[3] = spu_ila(context); ctx->gpr[4] = spu_ila(0x28000); ctx->gpr[5] = spu_il(1);
    buzz_geometry_job_command_bindings(ctx, emit_reference, 1);
    assert(emit_native[0] == 1 && emit_native[1] == 1);
    buzz_geometry_job_command_bindings(foreign.get(), emit_reference, 0);
    buzz_geometry_job_emit_position(foreign.get(), emit_reference, nullptr);
    buzz_geometry_job_emit_full(foreign.get(), emit_reference, nullptr);
    emit_enabled = full_enabled = false;
    buzz_geometry_job_command_bindings(ctx, emit_reference, 0);
    buzz_geometry_job_emit_position(ctx, emit_reference, nullptr);
    buzz_geometry_job_emit_full(ctx, emit_reference, nullptr);
    emit_enabled = full_enabled = true;
    ctx->gpr[3]._u32[0] = SPU_LS_SIZE - 1;
    buzz_geometry_job_command_bindings(ctx, emit_reference, 0);
    ctx->gpr[1]._u32[0] = 3;
    buzz_geometry_job_emit_position(ctx, emit_reference, nullptr);
    buzz_geometry_job_emit_full(ctx, emit_reference, nullptr);
    assert(emit_fallback[0] == 1 && emit_fallback[2] == 1);
    memcpy(ctx->gpr, saved.data(), sizeof ctx->gpr); ctx->pc = pc;
}

static uint32_t random_word(uint32_t& value) { value ^= value << 13; value ^= value >> 17; value ^= value << 5; return value; }
static void synthetic(spu_context& ctx, unsigned trial)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0x1c18;
    uint32_t seed = trial + 1;
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx.ls[i] = uint8_t(random_word(seed));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(seed);
    ctx.gpr[0]._u32[0] = 0x3ff00; ctx.gpr[1]._u32[0] = 0x137e0;
    ctx.gpr[3]._u32[0] = 0x3fbc0; ctx.gpr[4]._u32[0] = 0x3fc00;
    auto put = [&](unsigned at, uint32_t value) { buzz::geometry_job_put32(ctx.ls + at, value); };
    memset(ctx.ls + 0x3fbc0, 0, 0x140);
    put(0x3fbc0, 0xe800); put(0x3fbd4, 0x2003fbf0); put(0x3fbd8, 21 << 16);
    put(0x3fbdc, 0x10800); put(0x3fbe0, 0x10800); put(0x3fbe4, 20);
    put(0x3fc08, 0x0e6800a8); put(0x3fc14, 8192); put(0x3fc1c, 256);
    const uint32_t constants[] = {0, 0x7fff, 0, 0, 0x04050607, 0x80808080, 0x0c0d0e0f, 0x80808080};
    for (unsigned i = 0; i < 8; ++i) put(0xcd10 + i * 4, constants[i]);
    unsigned cursor = 0xe800;
    for (unsigned i = 0; i < 21; ++i) {
        const unsigned size = i == 0 ? 64 : i == 11 && (trial & 64) ? 0 : i == 14 ? 128 : i == 15 ? 64 : i == 16 ? 16 : 32;
        const unsigned flags = (i == 4 && (trial & 256)) || (i == 5 && (trial & 512)) ? 0x80000000u : 0;
        put(0x3fc30 + i * 8, size | flags); put(0x3fc34 + i * 8, 0x100000 + i * 256);
        if (i == 16) {
            const uint8_t config[] = {0x82, 0, 0, 0xff, 0xff, 0xff, 0x30, 0, 0, 8, 0, 24, 0xff, 0xff, 0xff, 0xff};
            memcpy(ctx.ls + cursor, config, 16);
        }
        cursor += size;
    }
    put(0xe800, 0);
    static const uint32_t bits[] = {0x10000000, 0x80, 0x100, 8, 32, 64};
    uint32_t flags = 0;
    for (unsigned i = 0; i < 6; ++i) if (trial & (1 << i)) flags |= bits[i];
    static const uint32_t weights[] = {0, 1, 0x8000, 0xffff};
    put(0x3fcec, flags); put(0x3fce0, trial & 128 ? (weights[trial >> 8] << 16) | 1 : 0);
    put(0x3fcf0, trial * 0x19707u); put(0x3fcf8, trial * 0x193031u);
    ctx.ls[0x3fcfa] &= 0xfe;
    put(0x3fcfc, trial * 0x7913u);
}
static void compare(spu_context& native, spu_context& reference)
{
    memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    const uint32_t link = native.gpr[0]._u32[0];
    calls.clear(); g_spu_trampoline_fn = nullptr;
    buzz_job_6eba_spu_func_00001C18(&reference); drain(&reference, link);
    const auto expected = calls;
    calls.clear(); g_spu_trampoline_fn = nullptr;
    buzz_geometry_job_native_start(&native, &model_library); drain(&native, link);
    if (calls.size() != expected.size()) { fprintf(stderr, "%s: call count %zu/%zu\n", current_case, calls.size(), expected.size()); abort(); }
    for (unsigned i = 0; i < calls.size(); ++i) {
        const auto& a = calls[i]; const auto& b = expected[i];
        if (a.pc != b.pc || a.link != b.link || memcmp(a.arguments.data(), b.arguments.data(), sizeof a.arguments) ||
            memcmp(a.nonvolatile.data(), b.nonvolatile.data(), sizeof a.nonvolatile)) {
            fprintf(stderr, "%s: call %u pc=%X/%X link=%X/%X differs\n", current_case, i, a.pc, b.pc, a.link, b.link);
            for (unsigned reg = 0; reg < 31; ++reg) if (memcmp(&a.nonvolatile[reg], &b.nonvolatile[reg], 16))
                fprintf(stderr, " nonvolatile r%u %08X/%08X\n", reg == 30 ? 126 : 80 + reg, a.nonvolatile[reg]._u32[0], b.nonvolatile[reg]._u32[0]);
            abort();
        }
    }
    const auto difference = buzz::compare_function_state(native, reference);
    if (!difference.exact()) {
        fprintf(stderr, "%s: exit r%d byte%d LS=%X bytes=%u control=%s\n", current_case,
            difference.first_register, difference.first_register_byte, difference.first_ls, difference.ls_bytes,
            difference.first_state ? difference.first_state : "equal"); abort();
    }
    assert(native.host_depth == 0 && reference.host_depth == 0);
}
int main(int argc, char** argv)
{
    const unsigned previous = _mm_getcsr();
    _mm_setcsr((previous & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    routing_probe = commands_enabled = command_publish_enabled = emit_enabled = full_enabled = true;
    synthetic(*native, 0); native->host_depth = 1;
    buzz_geometry_job_prepare_commands(native.get(), routing_reference);
    buzz_geometry_job_publish_commands(native.get(), publication_reference);
    buzz_geometry_job_command_bindings(native.get(), emit_reference, 0);
    buzz_geometry_job_emit_position(native.get(), emit_reference, nullptr);
    buzz_geometry_job_emit_full(native.get(), emit_reference, nullptr);
    assert(emit_references == 3);
    assert(publication_references == 1 && !publication_native && !publication_fallback);
    assert(routed_references == 1 && !routed_native && !routed_fallback);
    synthetic(*native, 0); g_spu_trampoline_fn = nullptr;
    const uint32_t routing_link = native->gpr[0]._u32[0];
    buzz_geometry_job_native_start(native.get(), &model_library); drain(native.get(), routing_link);
    native->host_depth = 1;
    buzz_geometry_job_prepare_commands(native.get(), routing_reference);
    assert(routed_references == 5 && routed_native == 1 && routed_fallback == 1);
    buzz_geometry_job_publish_commands(native.get(), publication_reference);
    assert(publication_references == 5 && publication_native == 1 && publication_fallback == 1);
    buzz_geometry_job_command_bindings(native.get(), emit_reference, 0);
    buzz_geometry_job_emit_position(native.get(), emit_reference, nullptr);
    buzz_geometry_job_emit_full(native.get(), emit_reference, nullptr);
    assert(emit_references == 15 && emit_native[0] == 1 && emit_native[1] == 1 && !emit_native[2]);
    assert(emit_fallback[0] == 1 && !emit_fallback[1] && emit_fallback[2] == 1);
    assert(emit_fallback[3] == 1 && !emit_native[3]);
    routing_probe = commands_enabled = command_publish_enabled = emit_enabled = full_enabled = false;
    puts("Command preparation/publication/emission routing: owner native; reference/foreign/disabled/unsupported/post-scope checks PASS");
    for (scenario = 0; scenario < 1024; ++scenario) { synthetic(*native, scenario); compare(*native, *reference); }
    unsigned corpus = 0;
    if (argc > 1) for (const auto& path : std::filesystem::directory_iterator(argv[1])) {
        if (path.path().extension() != ".gz" || path.path().string().find(".bjc.gz") == std::string::npos) continue;
        const auto name = path.path().string(); current_case = name.c_str();
        gzFile file = gzopen(name.c_str(), "rb"); assert(file);
        uint32_t header[6]; assert(gzread(file, header, sizeof header) == sizeof header);
        assert(header[0] == 0x434a5a42 && header[1] == 1 && header[2] == 41 && header[5] == SPU_LS_SIZE);
        memset(native.get(), 0, sizeof(*native)); native->ls = native->ls_store; native->image_id = 41; native->pc = 0x1c18;
        assert(gzread(file, native->gpr, sizeof native->gpr) == sizeof native->gpr);
        assert(gzread(file, native->ls, SPU_LS_SIZE) == SPU_LS_SIZE); gzclose(file);
        scenario = corpus++ % 1024; compare(*native, *reference);
    }
    if (argc > 1) assert(corpus);
    _mm_setcsr(previous);
    printf("Native outer driver: 1024 mock-pipeline scenarios, %u real entries, exact call/argument/nonvolatile-spill transcript and full exit state PASS\n", corpus);
}
