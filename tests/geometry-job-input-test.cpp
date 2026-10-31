#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_geometry_job_input.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-input-reference.inc"

static void store32(spu_context& ctx, unsigned at, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) ctx.ls[at + i] = uint8_t(value >> (24 - i * 8));
}
static void store16(spu_context& ctx, unsigned at, uint32_t value)
{
    ctx.ls[at] = uint8_t(value >> 8); ctx.ls[at + 1] = uint8_t(value);
}

static std::unique_ptr<spu_context> synthetic(unsigned seed)
{
    auto ctx = std::make_unique<spu_context>();
    memset(ctx.get(), 0, sizeof(*ctx));
    ctx->ls = ctx->ls_store;
    ctx->image_id = 41;
    ctx->gpr[3]._u32[0] = 0x3fbc0;
    ctx->gpr[4]._u32[0] = 0x3fc00;
    ctx->gpr[0]._u32[0] = 0x3ff00;
    store32(*ctx, 0x3fbc0, 0xe800);
    store32(*ctx, 0x3fbd4, 0x2003fbf0);
    store16(*ctx, 0x3fbd8, 21);
    store32(*ctx, 0x3fbdc, 0x10800);
    store32(*ctx, 0x3fbe0, 0x10800);
    store32(*ctx, 0x3fbe4, 20);
    store16(*ctx, 0x3fc08, 0xe68);
    store16(*ctx, 0x3fc0a, 168);
    store32(*ctx, 0x3fc14, 8192);
    store16(*ctx, 0x3fc1e, 256); // 4096 B scratch + default 8192 B stack.
    ctx->gpr[1]._u32[0] = 0x137e0;
    const uint32_t constants[] = {0, 0x7fff, 0, 0, 0x04050607, 0x80808080, 0x0c0d0e0f, 0x80808080};
    for (unsigned i = 0; i < 8; ++i) store32(*ctx, 0xcd10 + i * 4, constants[i]);
    for (unsigned i = 0; i < 12; ++i) ctx->ls[0xe554 + i] = uint8_t(seed * 19 + i);
    unsigned cursor = 0xe800;
    for (unsigned i = 0; i < 21; ++i) {
        const unsigned size = i == 16 ? 16 : ((seed + i) % 5 ? (seed * 7 + i * 13) % 256 : 0);
        const unsigned ea = 0x100000 + i * 1024 + (i == 16 ? 0 : (seed + i) % 16);
        store32(*ctx, 0x3fc30 + i * 8, size | ((seed & 1) && i != 0 ? 0x80000000u : 0));
        store32(*ctx, 0x3fc34 + i * 8, ea);
        if (i == 16) {
            const uint8_t config[16] = {uint8_t(4 | ((seed & 1) ? 0x80 : 0)), 0, 0, 0xff,
                0xff, 0xff, uint8_t(0x30 | (seed & 1)), 0, 1, 0, 3, 0, 0xff, 0xff, 0xff, 0xff};
            memcpy(ctx->ls + cursor, config, sizeof config);
        }
        cursor += (size + 15) & ~15u;
    }
    store32(*ctx, 0x3fcec, (seed & 1 ? 0x80 : 0) | (seed & 2 ? 0x100 : 0));
    return ctx;
}

static void compare_setup(spu_context& ctx, const buzz::GeometryJobEntry& entry)
{
    uint8_t padding[12];
    memcpy(padding, ctx.ls + 0xe554, sizeof padding);
    ctx.host_depth = 1;
    g_spu_trampoline_fn = nullptr;
    buzz_job_6eba_spu_func_000008A8(&ctx);
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 4096);
        auto fn = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr;
        fn(&ctx);
    }
    assert(ctx.pc == ctx.gpr[0]._u32[0]);
    for (unsigned i = 0; i < 21; ++i)
        assert(buzz::geometry_job_be32(ctx.ls + 0xe500 + i * 4) == entry.input[i].lsa);
    assert(!memcmp(padding, ctx.ls + 0xe554, sizeof padding));
}

int main(int argc, char** argv)
{
    for (unsigned seed = 0; seed < 256; ++seed) {
        auto ctx = synthetic(seed);
        const auto initial = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(ctx.get()),
            reinterpret_cast<uint8_t*>(ctx.get()) + sizeof(*ctx));
        buzz::GeometryJobEntry entry;
        const char* reason = "not run";
        assert(buzz::read_geometry_job_entry(*ctx, entry, &reason) && !reason);
        assert(!memcmp(ctx.get(), initial.data(), initial.size()));
        assert(entry.vertices == 256 && entry.index_count == 768);
        assert(entry.position_only() == bool(seed & 1));
        assert(entry.secondary_enabled() == bool(seed & 2));
        assert(entry.uniform_tables() == 5);
        assert(entry.workspace_lsa == entry.io_lsa + entry.input[0].size);
        assert(entry.workspace_bytes == entry.io_bytes - entry.input[0].size);
        assert(entry.frame_lsa == 0x132c0 && entry.geometry_context_lsa == 0x13460);
        compare_setup(*ctx, entry);
    }
    for (unsigned fault = 0; fault < 14; ++fault) {
        auto ctx = synthetic(0);
        switch (fault) {
        case 0: ctx->image_id = 40; break;
        case 1: ctx->ls = nullptr; break;
        case 2: ctx->gpr[3]._u32[0] = 0x3fbc8; break;
        case 3: ctx->gpr[4]._u32[0] = UINT32_MAX; break;
        case 4: store16(*ctx, 0x3fc0a, 160); break;
        case 5: store32(*ctx, 0x3fc24, 8); break;
        case 6: store32(*ctx, 0x3fc14, UINT32_MAX); break;
        case 7: store32(*ctx, 0x3fbe0, 0x3ffff); break;
        case 8: ctx->gpr[1]._u32[0] = 0x11000; break;
        case 9: store32(*ctx, 0x3fc30 + 16 * 8, 0); break;
        case 10: store32(*ctx, 0x3fc34 + 16 * 8, 0x100001); break;
        case 11: store32(*ctx, 0x3fc30, 16); store32(*ctx, 0x3fc34, 0); break;
        case 12: store32(*ctx, 0x3fbd4, 0x10000000); break;
        case 13: store32(*ctx, 0x3fc30, 0x80000000); break;
        }
        buzz::GeometryJobEntry entry;
        memset(&entry, 0xa5, sizeof entry);
        const auto initial = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(ctx.get()),
            reinterpret_cast<uint8_t*>(ctx.get()) + sizeof(*ctx));
        const auto before = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&entry),
            reinterpret_cast<uint8_t*>(&entry) + sizeof entry);
        const char* reason = nullptr;
        assert(!buzz::read_geometry_job_entry(*ctx, entry, &reason) && reason);
        assert(!memcmp(&entry, before.data(), before.size()));
        assert(!memcmp(ctx.get(), initial.data(), initial.size()));
    }
    unsigned corpus = 0;
    if (argc > 1) {
        auto ctx = std::make_unique<spu_context>();
        std::vector<uint8_t> before(sizeof(*ctx));
        for (const auto& path : std::filesystem::directory_iterator(argv[1])) {
            if (path.path().string().find(".bjc.gz") == std::string::npos || path.path().extension() == ".json") continue;
            gzFile file = gzopen(path.path().string().c_str(), "rb");
            assert(file);
            uint32_t header[6];
            assert(gzread(file, header, sizeof header) == sizeof header);
            assert(header[0] == 0x434a5a42 && header[1] == 1 && header[2] == 41 && header[5] == SPU_LS_SIZE);
            memset(ctx.get(), 0, sizeof(*ctx));
            ctx->ls = ctx->ls_store;
            ctx->image_id = 41;
            assert(gzread(file, ctx->gpr, sizeof(ctx->gpr)) == sizeof(ctx->gpr));
            assert(gzread(file, ctx->ls, SPU_LS_SIZE) == SPU_LS_SIZE);
            gzclose(file);
            memcpy(before.data(), ctx.get(), before.size());
            buzz::GeometryJobEntry entry;
            const char* reason = nullptr;
            if (!buzz::read_geometry_job_entry(*ctx, entry, &reason)) {
                fprintf(stderr, "%s: rejected %s\n", path.path().string().c_str(), reason);
                return 1;
            }
            assert(!memcmp(before.data(), ctx.get(), before.size()));
            compare_setup(*ctx, entry);
            ++corpus;
        }
        assert(corpus);
    }
    printf("Native job entry: 256 setup-oracle cases, 14 unchanged-state rejections, %u real entries exact PASS\n", corpus);
}
