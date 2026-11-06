#define SPU_LS_DEBUG_HOOKS 0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../port/native_geometry_host_vertex_job.h"
#include "../port/native_geometry_host_clip_job.h"
#include "../port/native_geometry_host_cull_job.h"
#include "geometry-test-memory.h"
#include <map>
#include <memory>
#include <string>
static std::unique_ptr<spu_context> vertex_seed[2];
static bool vertex_numeric_fixture;
static void compare_vertex_output(const spu_context&,spu_context&,const buzz::GeometryJobEntry&,
    const buzz::GeometryHostInputRecipe&,const buzz::GeometryHostSkinRecipe&,
    const buzz::GeometryHostInputData&,const buzz::GeometryHostSkinWorkspace&,unsigned);
static void report_vertex_output();
template<class Original> static void check_vertex_corners(Original&,spu_context&,
    buzz::GeometryHostInputData&,buzz::GeometryHostSkinWorkspace&,
    buzz::GeometryHostClipData&,buzz::GeometryHostIndexData&);
#define BUZZ_HOST_VERTEX_OUTPUT_CHECK compare_vertex_output
#define BUZZ_HOST_VERTEX_OUTPUT_REPORT report_vertex_output
#define BUZZ_HOST_VERTEX_CORNER_TEST check_vertex_corners
#define BUZZ_HOST_EXTRA_FP_CASES 96
#define BUZZ_HOST_CULL_REFERENCE "../analysis/native-geometry/phase3/host-vertex-job-v1/reference.inc"
#include "geometry-host-index-output-test.cpp"

static unsigned vertex_jobs,vertex_padded_jobs,vertex_weighted,vertex_object;
static uint64_t vertex_bytes,vertex_padding;
static std::map<std::string,unsigned> vertex_fallback;
static std::map<unsigned,unsigned> vertex_strides;
static bool checked_vertex_rejects;

static void check_vertex_rejects(const spu_context& entry,const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input,const GeometryHostSkinRecipe& skin,
    const GeometryHostInputData& data,const GeometryHostSkinWorkspace& workspace,
    const GeometryHostVertexRecipe& recipe,const GeometryHostVertexTail& tail,
    uint8_t* destination,const uint8_t* expected)
{
    const char* reason=nullptr;
    for(unsigned fault=0;fault<8;++fault) {
        auto bad_input=input;auto bad_entry=e;GeometryHostVertexRecipe result;result.ready=true;
        switch(fault){
        case 0:++bad_input.vertices;break;
        case 1:bad_input.fields=17;break;
        case 2:bad_input.fields=0;break;
        case 3:++bad_input.blocks_per_field;break;
        case 4:bad_input.field[0].semantic=0;break;
        case 5:bad_input.field[0].storage=3;break;
        case 6:bad_input.field[0].components=2;break;
        case 7:bad_entry.config[0]|=16;break;
        }
        assert(!read_geometry_host_vertex_recipe(entry,bad_entry,bad_input,result,&reason)&&reason&&!result.ready);
    }
    for(unsigned fault=0;fault<9;++fault) {
        auto bad_input=input;auto bad_entry=e;auto bad_recipe=recipe;auto bad_skin=skin;
        GeometryHostVertexTail result;result.ready=true;
        switch(fault){
        case 0:bad_recipe.ready=false;break;
        case 1:++bad_input.vertices;break;
        case 2:++bad_recipe.output.vertices;break;
        case 3:bad_input.field[0].source=SPU_LS_SIZE;break;
        case 4:bad_entry.io_bytes=UINT32_MAX;break;
        case 5:bad_skin.object=false;bad_skin.active=true;bad_skin.matrices=0;break;
        case 6:bad_input.field[0].stride=UINT32_MAX;break;
        case 7:bad_skin.object=false;bad_skin.active=true;bad_skin.matrices=257;break;
        case 8:bad_skin.object=false;bad_skin.active=true;bad_skin.matrices=1;bad_skin.weights=SPU_LS_SIZE;break;
        }
        assert(!prepare_geometry_host_vertex_tail(entry,bad_entry,bad_input,bad_skin,workspace,bad_recipe,result,&reason)&&reason&&!result.ready);
    }
    auto unavailable=std::make_unique<GeometryHostInputData>();unavailable->ready=false;
    for(unsigned fault=0;fault<7;++fault) {
        auto bad_input=input;auto bad_recipe=recipe;auto bad_tail=tail;
        switch(fault){
        case 0:bad_recipe.ready=false;break;
        case 1:break; // Unready source.
        case 2:bad_tail.ready=false;break;
        case 3:bad_input.vertices=0;break;
        case 4:++bad_input.vertices;break;
        case 5:break; // Null destination.
        case 6:break; // Incomplete aligned destination.
        }
        assert(!emit_geometry_host_vertices(bad_input,fault==1?*unavailable:data,bad_recipe,bad_tail,
            fault==5?nullptr:destination,recipe.output.bytes-(fault==6),&reason)&&reason);
        assert(!memcmp(destination,expected,recipe.output.bytes));
    }
    puts("PASS host vertex admission: eight metadata/nine tail/seven output rejects; invalid readiness cleared, source and destination unchanged");
}

static void compare_vertex_output(const spu_context& entry,spu_context& reference,
    const GeometryJobEntry& e,const GeometryHostInputRecipe& input,const GeometryHostSkinRecipe& skin,
    const GeometryHostInputData& data,const GeometryHostSkinWorkspace& workspace,unsigned visible)
{
    if(!visible && !vertex_numeric_fixture) return;
    static auto before=std::make_unique<spu_context>(); memcpy(before.get(),&entry,sizeof entry);
    const char* reason=nullptr; GeometryHostVertexRecipe recipe; GeometryHostVertexTail tail;
    if(!read_geometry_host_vertex_recipe(entry,e,input,recipe,&reason) ||
        !prepare_geometry_host_vertex_tail(entry,e,input,skin,workspace,recipe,tail,&reason)) {
        assert(reason&&!memcmp(before.get(),&entry,sizeof entry));++vertex_fallback[reason];return;
    }
    static uint8_t* storage=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x42000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    static bool guarded=false;
    if(!guarded){assert(storage);DWORD previous;assert(VirtualProtect(storage+0x41000,0x1000,PAGE_NOACCESS,&previous));guarded=true;}
    auto* destination=storage+0x41000-recipe.output.bytes;
    memset(destination-32,0xa5,recipe.output.bytes+32);
    assert(emit_geometry_host_vertices(input,data,recipe,tail,destination,recipe.output.bytes,&reason) && !reason);
    const unsigned source=geometry_job_be32(reference.ls+e.geometry_context_lsa+0x58);
    reference.gpr[0]=spu_link(0x1244);reference.gpr[3]=spu_il(e.geometry_context_lsa);
    g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_000054E8(&reference);drain(&reference,0x1244);
    assert(geometry_job_be32(reference.ls+e.geometry_context_lsa+0x54)==source);
    assert(geometry_job_be32(reference.ls+e.geometry_context_lsa+0x58)==source+recipe.output.bytes);
    for(unsigned at=0;at<recipe.output.bytes;++at) if(destination[at]!=reference.ls[source+at]) {
        fprintf(stderr,"%s vertex byte%u N=%u stride=%u actual=%02X expected=%02X\n",current_case.c_str(),at,e.vertices,recipe.output.stride,destination[at],reference.ls[source+at]);abort();
    }
    for(unsigned at=0;at<32;++at)assert(destination[-32+int(at)]==0xa5);
    assert(!emit_geometry_host_vertices(input,data,recipe,tail,destination,recipe.output.bytes-1,&reason)&&reason);
    assert(!memcmp(destination,reference.ls+source,recipe.output.bytes));
    assert(!memcmp(before.get(),&entry,sizeof entry));
    ++vertex_jobs;vertex_bytes+=recipe.output.bytes;++vertex_strides[recipe.output.stride];
    const unsigned padding=recipe.output.bytes-e.vertices*recipe.output.stride;
    if(padding&&!checked_vertex_rejects) {
        check_vertex_rejects(entry,e,input,skin,data,workspace,recipe,tail,destination,reference.ls+source);
        assert(!memcmp(before.get(),&entry,sizeof entry));checked_vertex_rejects=true;
    }
    vertex_padding+=padding;vertex_padded_jobs+=padding!=0;vertex_weighted+=skin.active;vertex_object+=skin.object;
    const unsigned kind=skin.object?1:0;
    if(padding && e.vertices%4==3 && !vertex_seed[kind]) {
        vertex_seed[kind]=std::make_unique<spu_context>();memcpy(vertex_seed[kind].get(),&entry,sizeof entry);
        vertex_seed[kind]->ls=vertex_seed[kind]->ls_store;
    }
}
static void report_vertex_output()
{
    // Counts revalidated against both AVX2 and NEON after the additional output codecs.
    fprintf(stderr,"vertex totals: jobs=%u bytes=%llu padding=%llu padded=%u weighted=%u object=%u rejects=%zu\n",vertex_jobs,(unsigned long long)vertex_bytes,(unsigned long long)vertex_padding,vertex_padded_jobs,vertex_weighted,vertex_object,vertex_fallback.size());
    for(const auto& item:vertex_fallback)fprintf(stderr,"reject %s=%u\n",item.first.c_str(),item.second);
    assert(checked_vertex_rejects && vertex_jobs==6535 && vertex_bytes==30927600 && vertex_padding==24168 &&
        vertex_padded_jobs==2416 && vertex_weighted==657 && vertex_object==5878 && vertex_fallback.size()==1 &&
        vertex_fallback["host vertex padding palette index"]==632);
    printf("PASS direct host vertices: %u real jobs/%llu bytes/%llu alignment bytes/%u padded jobs; %u weighted/%u object; guarded ends, prefix canaries and unchanged short-span rejects\n",
        vertex_jobs,static_cast<unsigned long long>(vertex_bytes),static_cast<unsigned long long>(vertex_padding),vertex_padded_jobs,vertex_weighted,vertex_object);
    for(const auto& [stride,count]:vertex_strides)printf("Observed final vertex stride%u:%u jobs\n",stride,count);
    for(const auto& [why,count]:vertex_fallback)printf("Host vertex admission reject: %s=%u\n",why.c_str(),count);
}

template<class Original> static void check_vertex_corners(Original& original,spu_context& entry,
    GeometryHostInputData& data,GeometryHostSkinWorkspace& workspace,
    GeometryHostClipData& clips,GeometryHostIndexData& indices)
{
    assert(vertex_seed[0] && vertex_seed[1]);
    constexpr uint32_t special[]={0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,0x7f7fffff,
        0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff,0x7fc12345,0x3f000000,0xbf000000};
    const unsigned previous_jobs=vertex_jobs;const uint64_t previous_bytes=vertex_bytes;
    vertex_numeric_fixture=true;
    for(unsigned kind=0;kind<2;++kind)for(unsigned trial=0;trial<48;++trial) {
        memcpy(&entry,vertex_seed[kind].get(),sizeof entry);entry.ls=entry.ls_store;
        current_case=std::string(kind?"object publication FP ":"weighted publication FP ")+std::to_string(trial);
        GeometryJobEntry e;const char* reason=nullptr;assert(read_geometry_job_entry(entry,e,&reason));
        for(unsigned axis=0;axis<3;++axis)
            geometry_job_put32(entry.ls+e.input[8].lsa+e.vertices*12+axis*4,special[(trial+axis*5)%16]);
        const unsigned palette=kind?e.input[15].lsa:e.input[4].lsa;
        const unsigned matrices=kind?1:(e.input[4].size_flags+e.input[5].size_flags)/48;
        if(trial>=16)for(unsigned matrix=0;matrix<matrices;++matrix)for(unsigned coefficient=0;coefficient<12;++coefficient)
            geometry_job_put32(entry.ls+palette+matrix*48+coefficient*4,special[(trial+matrix+coefficient)%16]);
        if(!kind)for(unsigned influence=0;influence<4;++influence) {
            auto* weight=entry.ls+e.input[6].lsa+e.vertices*8+influence*2;
            weight[0]=uint8_t(trial%4==0?0:trial%4==1?(influence==trial%4?255:0):trial%4==2?255:trial*29+influence*71);
            weight[1]=uint8_t((trial+influence)%matrices);
        }
        GeometryHostInputRecipe input;GeometryHostSkinRecipe skin;
        assert(prepare_geometry_host_inputs(entry,e,input,data,&reason));
        assert(read_geometry_host_skin_recipe(entry,e,input,skin,&reason));
        assert(skin_geometry_host_inputs(entry,e,input,data,workspace,&reason));
        assert(project_geometry_host_inputs(entry,e,input,data,clips,&reason));
        assert(decode_geometry_host_indices(entry,e,indices,&reason));
        assert(cull_geometry_host_triangles(entry,e,clips,indices,&reason));
        original(e,input,skin);
    }
    vertex_numeric_fixture=false;
    assert(vertex_jobs==previous_jobs+96);
    printf("PASS publication FP corners:96 modified observed-family fixtures/%llu complete vertex bytes; signed zeros/denormals/extended exponents, varied padding weights and matrix values\n",
        static_cast<unsigned long long>(vertex_bytes-previous_bytes));
}
