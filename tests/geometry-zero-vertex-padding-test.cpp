#define SPU_LS_DEBUG_HOOKS 0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../port/native_geometry_host_job.h"
#include "geometry-test-memory.h"
#include <memory>
#include <map>
#include <string>
static void compare_zero_padding(const spu_context&,spu_context&,const buzz::GeometryJobEntry&,
    const buzz::GeometryHostInputRecipe&,const buzz::GeometryHostSkinRecipe&,
    const buzz::GeometryHostInputData&,const buzz::GeometryHostSkinWorkspace&,unsigned);
static void report_zero_padding();
#define BUZZ_HOST_VERTEX_OUTPUT_CHECK compare_zero_padding
#define BUZZ_HOST_VERTEX_OUTPUT_REPORT report_zero_padding
#define BUZZ_HOST_FINAL_OUTPUT_ONLY 1
#define BUZZ_HOST_CULL_REFERENCE "../analysis/native-geometry/phase3/host-vertex-job-v1/reference.inc"
#include "geometry-host-index-output-test.cpp"

static unsigned jobs,new_tail_admissions,full_jobs,full_new_admissions,padded_jobs;
static uint64_t logical_bytes,padding_bytes,changed_padding_bytes;
static std::map<std::string,unsigned> prior_reasons;
static void check_zero_tail_rejects(const GeometryHostInputRecipe& input,const GeometryHostVertexRecipe& recipe) {
    for(unsigned fault=0;fault<9;++fault) {
        auto i=input;auto r=recipe;GeometryHostVertexTail tail;tail.ready=true;const char* why=nullptr;
        switch(fault) {
        case 0:r.ready=false;break;
        case 1:i.vertices=0;break;
        case 2:i.vertices=4097;break;
        case 3:++r.output.vertices;break;
        case 4:r.output.stride=0;break;
        case 5:r.output.stride=65;break;
        case 6:r.output.bytes=i.vertices*r.output.stride-1;break;
        case 7:r.output.bytes=i.vertices*r.output.stride+32;break;
        case 8:r.output.bytes|=1;break;
        }
        assert(!prepare_geometry_host_zero_vertex_tail(i,r,tail,&why)&&why&&!tail.ready);
    }
}
static void compare_zero_padding(const spu_context& entry,spu_context& reference,const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input,const GeometryHostSkinRecipe& skin,
    const GeometryHostInputData& data,const GeometryHostSkinWorkspace& workspace,unsigned visible)
{
    if(!visible)return;
    static auto before=std::make_unique<spu_context>();memcpy(before.get(),&entry,sizeof entry);
    GeometryHostVertexRecipe recipe;const char* why=nullptr;
    if(!read_geometry_host_vertex_recipe(entry,e,input,recipe,&why))return;
    GeometryHostVertexTail strict,zero;
    const bool prior=prepare_geometry_host_vertex_tail(entry,e,input,skin,workspace,recipe,strict,&why);
    if(!prior){assert(why);++new_tail_admissions;++prior_reasons[why];}
    assert(prepare_geometry_host_zero_vertex_tail(input,recipe,zero,&why)&&!why);
    static uint8_t* storage=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x42000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    static bool guarded=false;
    if(!guarded){assert(storage);DWORD previous;assert(VirtualProtect(storage+0x41000,0x1000,PAGE_NOACCESS,&previous));guarded=true;check_zero_tail_rejects(input,recipe);}
    auto* destination=storage+0x41000-recipe.output.bytes;
    memset(destination-32,0xa5,recipe.output.bytes+32);
    assert(emit_geometry_host_vertices(input,data,recipe,zero,destination,recipe.output.bytes,&why));
    const unsigned index_source=geometry_job_be32(reference.ls+e.geometry_context_lsa+0x50);
    const unsigned source=geometry_job_be32(reference.ls+e.geometry_context_lsa+0x58);
    reference.gpr[0]=spu_link(0x1244);reference.gpr[3]=spu_il(e.geometry_context_lsa);
    g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_000054E8(&reference);drain(&reference,0x1244);
    const unsigned logical=e.vertices*recipe.output.stride;
    if(memcmp(destination,reference.ls+source,logical)) {
        fprintf(stderr,"%s logical vertex output differs under zero-padding policy\n",current_case.c_str());abort();
    }
    for(unsigned at=logical;at<recipe.output.bytes;++at){assert(destination[at]==0);changed_padding_bytes+=reference.ls[source+at]!=0;}
    for(unsigned at=0;at<32;++at)assert(destination[-32+int(at)]==0xa5);
    assert(!emit_geometry_host_vertices(input,data,recipe,zero,destination,recipe.output.bytes-1,&why)&&why);
    assert(!memcmp(before.get(),&entry,sizeof entry));
    static auto full=std::make_unique<GeometryHostJobWorkspace>();
    const char* full_reason=nullptr;
    if(prepare_geometry_host_job_impl<true,true,true,true>(entry,e,*full,&full_reason)) {
        assert(full->indices.maximum_index<e.vertices && full->indices.triangles*3==visible);
        uint8_t indices[GeometryHostIndexData::capacity*6+16];
        assert(emit_geometry_host_indices(full->indices,indices,sizeof indices));
        assert(!memcmp(indices,reference.ls+index_source,full->index_bytes));
        memset(destination,0xa5,recipe.output.bytes);
        assert(emit_geometry_host_vertices(full->input,full->data,full->vertices,full->tail,destination,recipe.output.bytes));
        assert(!memcmp(destination,reference.ls+source,logical));
        for(unsigned at=logical;at<recipe.output.bytes;++at)assert(!destination[at]);
        ++full_jobs;full_new_admissions+=!prior;
    }
    assert(!memcmp(before.get(),&entry,sizeof entry));
    ++jobs;logical_bytes+=logical;padding_bytes+=recipe.output.bytes-logical;padded_jobs+=recipe.output.bytes!=logical;
}
static void report_zero_padding() {
    assert(jobs>=7167 && new_tail_admissions>=632 && full_new_admissions>0 && changed_padding_bytes>0);
    printf("PASS zero vertex alignment: %u visible jobs, %llu exact logical vertex bytes, %llu zero padding bytes, %u padded jobs, %u prior tail-only rejects admitted; guarded ends and nine layout rejects\n",
        jobs,(unsigned long long)logical_bytes,(unsigned long long)padding_bytes,padded_jobs,new_tail_admissions);
    printf("PASS full zero-padding producer: %u original index/logical-vertex outputs, %u newly admitted; no architectural writes; differences confined to %llu unindexed alignment bytes\n",full_jobs,full_new_admissions,(unsigned long long)changed_padding_bytes);
    for(const auto& [why,n]:prior_reasons)printf("Prior padding fallback: %s=%u\n",why.c_str(),n);
}
