#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include "../port/native_geometry_host_clip_job.h"
#include "../port/native_geometry_host_cull_job.h"
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
static uint32_t original_cull[5];static bool observed_cull;
static void observe_original_cull(spu_context* c,unsigned pc)
{
    if(pc==0x6e08){observed_cull=false;return;}
    if(observed_cull)return;observed_cull=true;
    const unsigned regs[]={53,70,80,82,10};for(unsigned i=0;i<5;++i)original_cull[i]=c->gpr[regs[i]]._u32[0];
}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c, uint32_t stop) { drain(c, stop); }
void spu_img_restore(spu_context* c, int32_t image) { assert(c->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 spu_rdch(spu_context*, uint32_t) { abort(); }
}
#ifndef BUZZ_HOST_CULL_REFERENCE
#define BUZZ_HOST_CULL_REFERENCE "../analysis/native-geometry/phase3/host-cull-job-v1/reference.inc"
#endif
#include BUZZ_HOST_CULL_REFERENCE
extern "C" void spu_indirect_branch(spu_context* c)
{
    auto next = host_cull_reference_lookup(c->pc);
    if (!next) { fprintf(stderr, "%s missing original skin PC=%X\n", current_case.c_str(), c->pc); abort(); }
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
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _mm_setcsr((_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | 0x8040);
    auto entry = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    auto before = std::make_unique<uint8_t[]>(sizeof(spu_context));
    auto data = std::make_unique<GeometryHostInputData>(); auto workspace = std::make_unique<GeometryHostSkinWorkspace>();
    auto weighted_seed = std::make_unique<spu_context>(), object_seed = std::make_unique<spu_context>();
    auto clips = std::make_unique<GeometryHostClipData>(); uint64_t clip_values = 0;
    auto indices = std::make_unique<GeometryHostIndexData>();
    std::map<unsigned,unsigned> formats,modes;
    uint64_t kept_indices=0,padding_bytes=0,nonzero_padding=0,padding_equal_index=0,padding_equal_entry=0;unsigned fully_culled=0;
    for(unsigned bits=0;bits<8;++bits){
        const uint32_t back=bits&4?0xffffffffu:0,enabled=bits&2?0xffffffffu:0,common=bits&1?0xffffffffu:0;
        const unsigned actual=spu__fma_lane(back^0x80000000u,enabled,common,0,0)&1;
        assert(actual==unsigned(bool(common)||(bool(back)&&bool(enabled))));
    }
    auto prepare_cull=[&](const GeometryJobEntry& e){
        const char* reason=nullptr;
        assert(decode_geometry_host_indices(*entry,e,*indices,&reason));
        assert(cull_geometry_host_triangles(*entry,e,*clips,*indices,&reason));
    };
    bool have_weighted = false, have_object = false;
    unsigned jobs = 0, captured = 0, skinned = 0, objects = 0; uint64_t compared = 0, skinned_vertices = 0, object_vertices = 0;
    std::map<std::string, unsigned> fallback;
    auto compare_original = [&](const GeometryJobEntry& e, const GeometryHostInputRecipe& input, const GeometryHostSkinRecipe& skin) {
        uint64_t values = 0;
        const char* reason = nullptr;
        memcpy(reference.get(), entry.get(), sizeof(*entry)); reference->ls = reference->ls_store;
        GeometryJobSetup setup; assert(read_geometry_job_setup(*reference, setup, &reason, true));
        commit_geometry_job_setup(*reference, setup); materialize_geometry_job_initial_call(*reference, setup);
        GeometryContextState state; assert(initialize_geometry_context(*reference, setup.initialization, state, &reason));
        const unsigned reference_mode = reference->gpr[103]._u32[0];
        reference->gpr[0] = spu_link(0x1234); reference->gpr[3] = spu_il(e.geometry_context_lsa);
        reference->gpr[4] = reference->gpr[104]; reference->gpr[5] = reference->gpr[107];
        reference->gpr[6] = reference->gpr[87]; reference->gpr[7] = reference->gpr[88];
        g_spu_trampoline_fn = nullptr; buzz_job_6eba_spu_func_0000AC68(reference.get()); drain(reference.get(), 0x1234);
        reference->gpr[0] = spu_link(0x1238); reference->gpr[3] = spu_il(e.geometry_context_lsa);
        reference->gpr[4] = spu_il(skin.object ? 0 : e.input[4].lsa); reference->gpr[5] = spu_il(skin.object ? 0 : e.input[6].lsa);
        reference->gpr[6] = spu_il((e.input[4].size_flags + e.input[5].size_flags) / 48);
        reference->gpr[7] = spu_il(e.frame_lsa + 0xe0); reference->gpr[8] = spu_il(e.frame_lsa + 0x120);
        reference->gpr[9] = spu_il(skin.object); reference->gpr[10] = spu_il(e.position_only());
        g_spu_trampoline_fn = nullptr; buzz_job_6eba_spu_func_000009F8(reference.get()); drain(reference.get(), 0x1238);
        assert(data->ready);
        for (unsigned field = 0; field < input.fields; ++field) {
            const auto& f = input.field[field];
            const unsigned source = geometry_job_be32(reference->ls + e.geometry_context_lsa + 0x10 + field * 4);
            const unsigned components = f.storage == 3 ? f.components : 4;
            for (unsigned first = 0; first < e.vertices; first += 8) for (unsigned component = 0; component < components; ++component) {
                unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<v8*>(words), data->at(input, field, first / 8).component[component]);
                for (unsigned lane = 0; lane < 8 && first + lane < e.vertices; ++lane) {
                    const unsigned expected = geometry_job_be32(reference->ls + source + (first + lane) * 16 + component * 4);
                    if (words[lane] != expected) {
                        fprintf(stderr, "%s skin%u field%u semantic%u vertex%u component%u actual%08X expected%08X\n",
                            current_case.c_str(), skin.active, field, f.semantic, first + lane, component, words[lane], expected); abort();
                    }
                    ++values;
                }
            }
        }
        assert(clips->ready && clips->vertices == e.vertices);
        reference->gpr[0] = spu_link(0x123c); reference->gpr[3] = spu_il(e.geometry_context_lsa);reference->gpr[4]=spu_il(e.input[2].lsa);
        g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_0000AEA0(reference.get());drain(reference.get(),0x123c);
        const unsigned decoded_base=geometry_job_be32(reference->ls+e.geometry_context_lsa+0x50),host_bytes=indices->triangles*6;
        uint8_t prior_index_padding[16]{};const unsigned host_padding=((host_bytes+15)&~15u)-host_bytes;
        memcpy(prior_index_padding,reference->ls+decoded_base+host_bytes,host_padding);
        GeometryHostCullParameters params;assert(read_geometry_host_cull_parameters(*entry,e,params,&reason));assert(params.mode==reference_mode);
        reference->gpr[0]=spu_link(0x1240);reference->gpr[3]=spu_il(e.geometry_context_lsa);reference->gpr[4]=spu_il(reference_mode);
        g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_00006608(reference.get());drain(reference.get(),0x1240);
        assert(observed_cull&&original_cull[0]==params.winding&&original_cull[1]==0&&original_cull[2]==0x80000000u&&
            original_cull[3]==(params.mode==1?0:0xffffffffu));
        const unsigned count=geometry_job_be16(reference->ls+e.geometry_context_lsa+0x180);
        if(!indices->ready||count!=indices->triangles*3||reference->gpr[3]._u32[0]!=count){
            fprintf(stderr,"%s cull mode%u actual%u expected%u newWinding%08X original53=%08X zero70=%08X mask80=%08X enable82=%08X vertices=%X\n",current_case.c_str(),params.mode,indices->triangles*3,count,
                params.winding,original_cull[0],original_cull[1],original_cull[2],original_cull[3],original_cull[4]);
            unsigned differences=0;for(unsigned vertex=0;vertex<e.vertices;++vertex)for(unsigned component=0;component<4;++component){
                const unsigned expected=geometry_job_be32(reference->ls+original_cull[4]+vertex*16+component*4);
                if(clips->component[component][vertex]!=expected&&differences++<12)fprintf(stderr,"clip%u.%u %08X/%08X\n",vertex,component,clips->component[component][vertex],expected);
            }fprintf(stderr,"clip differences=%u\n",differences);abort();
        }
        const unsigned index_source=geometry_job_be32(reference->ls+e.geometry_context_lsa+0x50);
        for(unsigned triangle=0;triangle<indices->triangles;++triangle)for(unsigned corner=0;corner<3;++corner){
            const unsigned expected=geometry_job_be16(reference->ls+index_source+triangle*6+corner*2);
            if(indices->component[corner][triangle]!=expected){fprintf(stderr,"%s cull triangle%u corner%u actual%u expected%u\n",
                current_case.c_str(),triangle,corner,indices->component[corner][triangle],expected);abort();}
        }
        const unsigned end=geometry_job_be32(reference->ls+e.geometry_context_lsa+0x58),bytes=(count*2+15)&~15u;
        assert(end==index_source+bytes);
#ifdef BUZZ_HOST_INDEX_OUTPUT_CHECK
        BUZZ_HOST_INDEX_OUTPUT_CHECK(*indices,reference->ls+index_source,bytes);
#endif
        for(unsigned at=count*2;at<bytes;++at){
            const unsigned value=reference->ls[index_source+at];nonzero_padding+=value!=0;++padding_bytes;
            padding_equal_index+=value==prior_index_padding[at-count*2];padding_equal_entry+=value==entry->ls[index_source+at];
        }
        kept_indices+=count;if(!count)++fully_culled;
        const unsigned destination = geometry_job_be32(reference->ls + e.geometry_context_lsa + 0x10 + (e.config[0] & 15) * 4);
        for (unsigned vertex = 0; vertex < e.vertices; ++vertex) for (unsigned component = 0; component < 4; ++component) {
            const unsigned expected = geometry_job_be32(reference->ls + destination + vertex * 16 + component * 4);
            if (clips->component[component][vertex] != expected) {
                fprintf(stderr, "%s clip vertex%u component%u actual%08X expected%08X\n", current_case.c_str(), vertex, component,
                    clips->component[component][vertex], expected); abort();
            }
        }
        clip_values += e.vertices * 4;
        return values;
    };
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
        GeometryHostInputRecipe input; ++jobs;
        if (!prepare_geometry_host_inputs(*entry, e, input, *data, &reason)) { ++fallback[reason]; continue; }
        GeometryHostSkinRecipe skin;
        if (!read_geometry_host_skin_recipe(*entry, e, input, skin, &reason) ||
            !skin_geometry_host_inputs(*entry, e, input, *data, *workspace, &reason)) {
            ++fallback[reason]; assert(!memcmp(before.get(), entry.get(), sizeof(*entry))); continue;
        }
        if (!project_geometry_host_inputs(*entry, e, input, *data, *clips, &reason)) {
            ++fallback[reason]; assert(!memcmp(before.get(), entry.get(), sizeof(*entry))); continue;
        }
        ++formats[entry->ls[e.input[14].lsa + 112]];
        assert(!memcmp(before.get(), entry.get(), sizeof(*entry))); ++captured;
        if (skin.active) { ++skinned; skinned_vertices += e.vertices; }
        if (skin.object) { ++objects; object_vertices += e.vertices; }
        if (skin.active && !have_weighted && skin.matrices < 256 && e.vertices >= 8) {
            memcpy(weighted_seed.get(), entry.get(), sizeof(*entry)); weighted_seed->ls = weighted_seed->ls_store; have_weighted = true;
        }
        if (skin.object && !have_object && skin.field[1] != 16 && skin.field[2] != 16 && e.vertices >= 8) {
            memcpy(object_seed.get(), entry.get(), sizeof(*entry)); object_seed->ls = object_seed->ls_store; have_object = true;
        }
        prepare_cull(e);GeometryHostCullParameters cull_params;assert(read_geometry_host_cull_parameters(*entry,e,cull_params,&reason));++modes[cull_params.mode];
        compared += compare_original(e, input, skin);
        if (jobs % 5000 == 0) printf("Host skin corpus progress: %u entries\n", jobs);
    }
    const uint64_t corpus_clip_values = clip_values;
    const uint64_t corpus_kept_indices=kept_indices,corpus_padding_bytes=padding_bytes,corpus_nonzero_padding=nonzero_padding,
        corpus_equal_index=padding_equal_index,corpus_equal_entry=padding_equal_entry;const unsigned corpus_fully_culled=fully_culled;
    assert(jobs == 19508 && skinned && have_weighted && have_object);
    constexpr uint32_t special[] = {0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,0x7f7fffff,
        0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff,0x7fc12345,0x3f000000,0xbf000000};
    uint64_t corner_values = 0;
    for (unsigned kind = 0; kind < 2; ++kind) for (unsigned trial = 0; trial < 48; ++trial) {
        const auto* seed = kind ? object_seed.get() : weighted_seed.get();
        memcpy(entry.get(), seed, sizeof(*entry)); entry->ls = entry->ls_store;
        current_case = std::string(kind ? "object FP corner " : "weighted FP corner ") + std::to_string(trial);
        GeometryJobEntry e; const char* reason = nullptr; assert(read_geometry_job_entry(*entry, e));
        for (unsigned lane = 0; lane < 8; ++lane) for (unsigned axis = 0; axis < 3; ++axis)
            geometry_job_put32(entry->ls + e.input[8].lsa + lane * 12 + axis * 4, special[(trial + lane * 3 + axis) % 16]);
        const unsigned palette = kind ? e.input[15].lsa : e.input[4].lsa;
        const unsigned matrices = kind ? 1 : (e.input[4].size_flags + e.input[5].size_flags) / 48;
        if (trial >= 16) for (unsigned matrix = 0; matrix < matrices; ++matrix) for (unsigned coefficient = 0; coefficient < 12; ++coefficient)
            geometry_job_put32(entry->ls + palette + matrix * 48 + coefficient * 4, special[(trial + matrix + coefficient) % 16]);
        if (!kind) for (unsigned lane = 0; lane < 8; ++lane) for (unsigned influence = 0; influence < 4; ++influence) {
            auto* at = entry->ls + e.input[6].lsa + lane * 8 + influence * 2;
            at[0] = uint8_t(trial % 4 == 0 ? 0 : trial % 4 == 1 ? (influence == lane % 4 ? 255 : 0) :
                trial % 4 == 2 ? 255 : trial * 19 + lane * 31 + influence * 73);
        }
        memcpy(before.get(), entry.get(), sizeof(*entry));
        GeometryHostInputRecipe input; GeometryHostSkinRecipe skin;
        assert(prepare_geometry_host_inputs(*entry, e, input, *data, &reason));
        assert(read_geometry_host_skin_recipe(*entry, e, input, skin, &reason));
        assert(skin_geometry_host_inputs(*entry, e, input, *data, *workspace, &reason));
        assert(project_geometry_host_inputs(*entry, e, input, *data, *clips, &reason));
        assert(!memcmp(before.get(), entry.get(), sizeof(*entry)));
        prepare_cull(e);corner_values += compare_original(e, input, skin);
    }
    uint64_t parameter_corner_values = 0;
    for (unsigned trial = 0; trial < 64; ++trial) {
        memcpy(entry.get(), trial & 1 ? object_seed.get() : weighted_seed.get(), sizeof(*entry)); entry->ls = entry->ls_store;
        current_case = "clip parameter FP corner " + std::to_string(trial);
        GeometryJobEntry e; const char* reason = nullptr; assert(read_geometry_job_entry(*entry,e));
        auto* viewport = entry->ls + e.input[14].lsa;
        for (unsigned half = 0; half < 4; ++half) {
            const unsigned value = (trial * 65521u + half * 17179u) & 65535;
            viewport[half * 2] = uint8_t(value >> 8); viewport[half * 2 + 1] = uint8_t(value);
        }
        geometry_job_put32(viewport + 8, special[trial % 16]);
        geometry_job_put32(viewport + 12, special[(trial + 7) % 16]);
        viewport[112] = trial & 2 ? 3 : 0; // Both actually observed formats, no cold format expansion.
        for (unsigned word = 4; word < 28; ++word)
            geometry_job_put32(viewport + word * 4, special[(trial + word) % 16]);
        if (trial >= 32) for (unsigned word = 0; word < 12; ++word)
            geometry_job_put32(entry->ls + 0xcd30 + word * 4, special[(trial + word * 3) % 16]);
        memcpy(before.get(),entry.get(),sizeof(*entry));
        GeometryHostInputRecipe input; GeometryHostSkinRecipe skin;
        assert(prepare_geometry_host_inputs(*entry,e,input,*data,&reason));
        assert(read_geometry_host_skin_recipe(*entry,e,input,skin,&reason));
        assert(skin_geometry_host_inputs(*entry,e,input,*data,*workspace,&reason));
        assert(project_geometry_host_inputs(*entry,e,input,*data,*clips,&reason));
        assert(!memcmp(before.get(),entry.get(),sizeof(*entry)));
        prepare_cull(e);parameter_corner_values += compare_original(e,input,skin);
    }
    for (unsigned fault = 0; fault < 14; ++fault) {
        const bool object = fault >= 9;
        memcpy(entry.get(), object ? object_seed.get() : weighted_seed.get(), sizeof(*entry)); entry->ls = entry->ls_store;
        GeometryJobEntry e; const char* reason = nullptr; assert(read_geometry_job_entry(*entry, e));
        GeometryHostInputRecipe input; assert(prepare_geometry_host_inputs(*entry, e, input, *data, &reason));
        GeometryHostSkinRecipe skin; assert(read_geometry_host_skin_recipe(*entry, e, input, skin, &reason));
        switch (fault) {
        case 0: data->ready = false; break;
        case 1: e.config[6] = uint8_t((e.config[6] & 0xf0) | 2); break;
        case 2: e.config[0] &= ~128u; break;
        case 3: e.input[6].size = 0; break;
        case 4: e.input[6].lsa |= 1; break;
        case 5: e.input[4].lsa |= 1; break;
        case 6: e.input[4].size_flags = UINT32_MAX; break;
        case 7: {
            auto* at = entry->ls + skin.weights + (e.vertices - 1) * 8 + 6;
            at[0] = 0; at[1] = uint8_t(skin.matrices); break; // Even zero weights read their palette entry.
        }
        case 8: entry->ls[0xda10] ^= 1; break;
        case 9: e.input[15].size = 47; break;
        case 10: e.input[15].lsa |= 1; break;
        case 11: entry->ls[0xd930] ^= 1; break;
        case 12: entry->ls[0xd920] ^= 1; break;
        case 13: entry->ls[0xcd60] ^= 1; break;
        }
        memcpy(before.get(), entry.get(), sizeof(*entry));
        if (skin_geometry_host_inputs(*entry, e, input, *data, *workspace, &reason) || !reason) {
            fprintf(stderr, "Skin admission fault%u unexpectedly accepted\n", fault); abort();
        }
        assert(!data->ready && !memcmp(before.get(), entry.get(), sizeof(*entry)));
    }
    for (unsigned fault = 0; fault < 9; ++fault) {
        memcpy(entry.get(), object_seed.get(), sizeof(*entry)); entry->ls = entry->ls_store;
        GeometryJobEntry e; const char* reason = nullptr; assert(read_geometry_job_entry(*entry,e));
        GeometryHostInputRecipe input; assert(prepare_geometry_host_inputs(*entry,e,input,*data,&reason));
        assert(skin_geometry_host_inputs(*entry,e,input,*data,*workspace,&reason));
        switch (fault) {
        case 0:data->ready=false;break;
        case 1:e.input[14].size=127;break;
        case 2:e.input[14].lsa|=1;break;
        case 3:e.input[14].lsa=SPU_LS_SIZE;break;
        case 4:e.config[0]|=16;break;
        case 5:e.vertices=4097;break;
        case 6:entry->ls[0xda40]^=1;break;
        case 7:entry->ls[0xcfc0]^=1;break;
        case 8:entry->ls[e.input[14].lsa+112]=5;break;
        }
        memcpy(before.get(),entry.get(),sizeof(*entry));clips->ready=true;
        if(project_geometry_host_inputs(*entry,e,input,*data,*clips,&reason) || !reason){fprintf(stderr,"Clip fault%u accepted\n",fault);abort();}
        assert(!clips->ready && !memcmp(before.get(),entry.get(),sizeof(*entry)));
    }
    auto index_before=std::make_unique<GeometryHostIndexData>();
    for(unsigned fault=0;fault<16;++fault){
        memcpy(entry.get(),object_seed.get(),sizeof(*entry));entry->ls=entry->ls_store;
        GeometryJobEntry e;const char* reason=nullptr;assert(read_geometry_job_entry(*entry,e));GeometryHostInputRecipe input;
        assert(prepare_geometry_host_inputs(*entry,e,input,*data,&reason));assert(skin_geometry_host_inputs(*entry,e,input,*data,*workspace,&reason));
        assert(project_geometry_host_inputs(*entry,e,input,*data,*clips,&reason));assert(decode_geometry_host_indices(*entry,e,*indices,&reason));
        switch(fault){
        case 0:indices->ready=false;break;
        case 1:clips->ready=false;break;
        case 2:clips->vertices=0;break;
        case 3:clips->vertices=4097;break;
        case 4:++clips->vertices;break;
        case 5:indices->triangles=GeometryHostIndexData::capacity+1;break;
        case 6:indices->triangles=0;break;
        case 7:indices->maximum_index=clips->vertices;break;
        case 8:e.config[0]|=16;break;
        case 9:e.flags&=~8u;e.input[0].size_flags=e.input[0].size=1;break;
        case 10:e.flags&=~8u;e.input[0].size_flags=4;e.input[0].size=4;geometry_job_put32(entry->ls+e.input[0].lsa,1);break;
        case 11:entry->ls[0xd050]^=1;break;
        case 12:entry->ls[0xdaf0]^=1;break;
        case 13:entry->ls[0xdcf0]^=1;break;
        case 14:indices->maximum_index=65535;break;
        case 15:e.index_count=1;break;
        }
        memcpy(before.get(),entry.get(),sizeof(*entry));memcpy(index_before.get(),indices.get(),sizeof(*indices));
        if(cull_geometry_host_triangles(*entry,e,*clips,*indices,&reason)||!reason){fprintf(stderr,"Cull fault%u accepted\n",fault);abort();}
        assert(!indices->ready&&!memcmp(before.get(),entry.get(),sizeof(*entry)));
        assert(!memcmp(indices->component,index_before->component,sizeof indices->component)&&indices->triangles==index_before->triangles&&indices->maximum_index==index_before->maximum_index&&indices->final_group_kept==index_before->final_group_kept);
    }
    assert(captured==19399 && jobs==19508 && fallback.size()==1);
    for(const auto& [mode,count]:modes)printf("Observed cull mode%u:%u jobs\n",mode,count);
    printf("PASS host cull composed pipeline:%u jobs/%llu kept indices/%u fully culled;stable order;8 exact mask-FNMS truth cases\n",captured,
        static_cast<unsigned long long>(corpus_kept_indices),corpus_fully_culled);
#ifdef BUZZ_HOST_INDEX_OUTPUT_CHECK
    printf("PASS original index padding:%llu alignment bytes/%llu nonzero exact;whole-job DMA gate remains open\n",static_cast<unsigned long long>(corpus_padding_bytes),static_cast<unsigned long long>(corpus_nonzero_padding));
#else
    printf("Observed original cull producer padding:%llu alignment bytes/%llu nonzero;not a final DMA gate\n",static_cast<unsigned long long>(corpus_padding_bytes),static_cast<unsigned long long>(corpus_nonzero_padding));
#endif
    printf("Padding byte provenance observations:equal pre-cull decoded indices=%llu;equal entry LS=%llu;16 cull rejects preserve source and owned index arrays\n",
        static_cast<unsigned long long>(corpus_equal_index),static_cast<unsigned long long>(corpus_equal_entry));
    for (const auto& [reason, count] : fallback) printf("Host skin unsupported: %s = %u\n", reason.c_str(), count);
    printf("PASS host cull composed producer: %u real entries; %u original full skin producer exact captures; %u weighted jobs/%llu vertices; %llu logical component values; entry LS/GPR/control unchanged\n",
        jobs, captured, skinned, static_cast<unsigned long long>(skinned_vertices), static_cast<unsigned long long>(compared));
    printf("PASS preceding object producer: %u jobs/%llu vertices;original matrix transform and direction normalization exact\n", objects, static_cast<unsigned long long>(object_vertices));
    printf("PASS composed FP corners:96 modified hot-family cases/%llu logical values;zeros,denormals,extended exponents,sparse/dense/non-normalized weights;14 unchanged-state rejects\n", static_cast<unsigned long long>(corner_values));
    for(const auto& [format,count]:formats)printf("Observed clip format%u: %u jobs\n",format,count);
    printf("PASS clip parameter corners:64 viewport/default-matrix special-value cases;unsigned16 viewport sums/wrap;reversed/denormal depth bounds; %llu preceding logical component values exact\n",static_cast<unsigned long long>(parameter_corner_values));
    printf("PASS host clip records: %llu corpus component values; %llu FP corner component values across160 cases;9 unchanged-state/ready rejects\n",
        static_cast<unsigned long long>(corpus_clip_values),static_cast<unsigned long long>(clip_values-corpus_clip_values));
}
