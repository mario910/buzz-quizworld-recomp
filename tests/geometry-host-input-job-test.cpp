#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_input_job.h"
#include "../port/native_geometry_job_call.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
using namespace buzz;
static void drain(spu_context*, uint32_t);
static std::string current_case;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c, uint32_t stop) { drain(c, stop); }
void spu_img_restore(spu_context* c, int32_t image) { assert(c->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 spu_rdch(spu_context*, uint32_t) { abort(); }
}
#include "../analysis/native-geometry/phase3/host-input-job-v1/reference.inc"
extern "C" void spu_indirect_branch(spu_context* c)
{
    auto next = host_input_reference_lookup(c->pc);
    if (!next) { fprintf(stderr, "%s missing original input PC=%X\n", current_case.c_str(), c->pc); abort(); }
    next(c);
}
static void drain(spu_context* c, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 1048576); auto next = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr; next(c);
    }
    assert(c->pc == stop);
}
int main(int argc, char** argv)
{
    assert(argc == 2);
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    buzz_test_enter_rtz();
    auto entry = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    auto before = std::make_unique<uint8_t[]>(sizeof(spu_context));
    auto data = std::make_unique<GeometryHostInputData>();
    auto fault_seed = std::make_unique<spu_context>(); bool have_fault_seed = false;
    unsigned jobs = 0, captured = 0, multi_dma = 0; uint64_t compared = 0;
    std::map<std::string, unsigned> fallback;
    for (const auto& path : std::filesystem::directory_iterator(argv[1])) {
        if (path.path().extension() != ".gz") continue;
        current_case = path.path().filename().string();
        auto* file = gzopen(path.path().string().c_str(), "rb"); assert(file);
        uint32_t header[6]; assert(gzread(file, header, sizeof header) == sizeof header);
        assert(header[0] == 0x434a5a42 && header[1] == 1 && header[2] == 41 && header[5] == SPU_LS_SIZE);
        memset(entry.get(), 0, sizeof(*entry)); entry->ls = entry->ls_store; entry->image_id = 41; entry->host_depth = 2;
        assert(gzread(file, entry->gpr, sizeof entry->gpr) == sizeof entry->gpr);
        assert(gzread(file, entry->ls, SPU_LS_SIZE) == SPU_LS_SIZE); gzclose(file);
        memcpy(before.get(), entry.get(), sizeof(*entry));
        GeometryJobEntry e; const char* reason = nullptr; assert(read_geometry_job_entry(*entry, e, &reason));
        GeometryHostInputRecipe recipe; ++jobs;
        const bool ready = prepare_geometry_host_inputs(*entry, e, recipe, *data, &reason);
        assert(!memcmp(before.get(), entry.get(), sizeof(*entry)));
        if (!ready) { ++fallback[reason ? reason : "unknown"]; continue; }
        ++captured; if (e.vertices * 12 > e.input[8].size) ++multi_dma;
        if (!have_fault_seed && recipe.fields >= 3) {
            memcpy(fault_seed.get(), entry.get(), sizeof(*entry)); fault_seed->ls = fault_seed->ls_store; have_fault_seed = true;
        }
        memcpy(reference.get(), entry.get(), sizeof(*entry)); reference->ls = reference->ls_store;
        GeometryJobSetup setup; assert(read_geometry_job_setup(*reference, setup, &reason, true));
        commit_geometry_job_setup(*reference, setup); materialize_geometry_job_initial_call(*reference, setup);
        GeometryContextState state; assert(initialize_geometry_context(*reference, setup.initialization, state, &reason));
        reference->gpr[0] = spu_link(0x1234); reference->gpr[3] = spu_il(e.geometry_context_lsa);
        reference->gpr[4] = reference->gpr[104]; reference->gpr[5] = reference->gpr[107];
        reference->gpr[6] = reference->gpr[87]; reference->gpr[7] = reference->gpr[88];
        g_spu_trampoline_fn = nullptr; buzz_job_6eba_spu_func_0000AC68(reference.get()); drain(reference.get(), 0x1234);
        assert(data->ready);
        for (unsigned field = 0; field < recipe.fields; ++field) {
            const auto& f = recipe.field[field];
            assert(reference->ls[e.geometry_context_lsa + 0x160 + field] == f.semantic);
            const unsigned source = geometry_job_be32(reference->ls + e.geometry_context_lsa + 0x10 + field * 4);
            const unsigned components = f.storage == 3 ? f.components : 4;
            for (unsigned first = 0; first < e.vertices; first += 8) for (unsigned component = 0; component < components; ++component) {
                unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<v8*>(words), data->at(recipe, field, first / 8).component[component]);
                for (unsigned lane = 0; lane < 8 && first + lane < e.vertices; ++lane) {
                    const unsigned expected = geometry_job_be32(reference->ls + source + (first + lane) * 16 + component * 4);
                    if (words[lane] != expected) {
                        fprintf(stderr, "%s field%u semantic%u storage%u vertex%u component%u actual%08X expected%08X\n",
                            current_case.c_str(), field, f.semantic, f.storage, first + lane, component, words[lane], expected); abort();
                    }
                    ++compared;
                }
            }
        }
        if (jobs % 5000 == 0) printf("Host input corpus progress: %u entries\n", jobs);
    }
    assert(jobs == 19508 && captured && have_fault_seed);
    GeometryJobEntry seed_entry; assert(read_geometry_job_entry(*fault_seed, seed_entry));
    for (unsigned fault = 0; fault < 24; ++fault) {
        memcpy(entry.get(), fault_seed.get(), sizeof(*entry)); entry->ls = entry->ls_store;
        auto e = seed_entry; auto* descriptor = entry->ls + e.input[20].lsa;
        GeometryHostInputRecipe valid; assert(read_geometry_host_input_recipe(*entry, e, valid));
        switch (fault) {
        case 0: e.config[2] = 1; break;
        case 1: e.config[0] |= 16; break;
        case 2: e.vertices = 0; break;
        case 3: e.vertices = 4097; break;
        case 4: e.config[0] &= 0xf0; break;
        case 5: e.scratch_bytes = 0; break;
        case 6: e.io_bytes = UINT32_MAX; break;
        case 7: for (unsigned i = 8; i < 11; ++i) e.input[i].size = 0; break;
        case 8: e.input[8].lsa |= 1; break;
        case 9: e.config[3] = 0; break;
        case 10: e.input[20].lsa |= 1; break;
        case 11: e.input[20].size = 8; break;
        case 12: descriptor[0] = 17; break;
        case 13: descriptor[1] = 0; break;
        case 14: descriptor[9] = 4; break;
        case 15: descriptor[12] = 0; break;
        case 16: descriptor[18] = 4; break; // half2 width no longer matches component count
        case 17: entry->ls[0xd250] ^= 1; break;
        case 18: entry->ls[0xd600] ^= 1; break;
        case 19: entry->ls[0xd8b0] ^= 1; break;
        case 20: entry->ls[0xddf0] ^= 1; break;
        case 21: {
            const auto& field = valid.field[1]; assert(field.storage == 9);
            auto* at = entry->ls + field.source + (e.vertices - 1) * field.stride + 2;
            *at = uint8_t((*at & 0xf0) | 12); break;
        }
        case 22: e.input[11].size = 1; e.input[12].size = e.input[13].size = 0; break;
        case 23: descriptor[11] = 0; break;
        }
        memcpy(before.get(), entry.get(), sizeof(*entry));
        GeometryHostInputRecipe output; output.fields = 123; data->ready = true; const char* reason = nullptr;
        if (prepare_geometry_host_inputs(*entry, e, output, *data, &reason) || !reason) {
            fprintf(stderr, "Host input fault%u unexpectedly accepted\n", fault); abort();
        }
        assert(!data->ready && output.fields == 123 && !memcmp(before.get(), entry.get(), sizeof(*entry)));
    }
    for (const auto& [reason, count] : fallback) printf("Whole-job input fallback: %s = %u\n", reason.c_str(), count);
    printf("PASS host input job capture: %u real entries; %u original-attribute exact captures; %u multi-DMA primary streams; %llu component values; entry LS/GPR/control unchanged\n",
        jobs, captured, multi_dma, static_cast<unsigned long long>(compared));
    puts("PASS host input admission:24 unchanged-state metadata/payload rejects;late invalid selector discards host data;no stale readiness or partially published recipe");
}
