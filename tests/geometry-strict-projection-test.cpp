#define BUZZ_GEOMETRY_NATIVE_RECIPROCAL 1
#define SPU_LS_DEBUG_HOOKS 0
#include "geometry-test-fp.h"
#include "../port/native_geometry_host_clip_job.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;

static uint32_t area(const GeometryHostClipData& clips)
{
    auto x=[&](unsigned i){return spu_pref_u32(clips.component[0][i]);};
    auto y=[&](unsigned i){return spu_pref_u32(clips.component[1][i]);};
    return spu_fs(spu_fm(spu_fs(x(1),x(0)),spu_fs(y(2),y(0))),
                  spu_fm(spu_fs(y(1),y(0)),spu_fs(x(2),x(0))))._u32[0];
}
int main()
{
    buzz_test_enter_rtz();
    // Reduced numeric case from a live strict-DMA mismatch. The original
    // projection retains a very thin triangle; native division collapses it.
    GeometryHostClipParameters params={
        {{0xc510e22d,0xc41ffff3,0xb9c5d5c8,0x496dccde},
         {0x3993a565,0xc3b00003,0xc50da9f2,0x49237ee5},
         {0xb4d50c9d,0xbf9be371,0x3364a4ae,0x441a0d4e},
         {0xb4aeef4c,0xbf800000,0x333bbd2e,0x4457f12c}},
        {0,0,0},{0x44a00000,0x44300000,0x3f800000},0xfffffffe};
    constexpr uint32_t positions[3][3]={
        {0x41ba4777,0xc136bcdf,0x429a8e80},
        {0x41be3a06,0xc13c3f41,0x42882058},
        {0x41be3a05,0xc13c3f3f,0x42882058}};
    // Independently observed original-SPU clip output, not the native helper.
    constexpr uint32_t expected[4][3]={
        {0x4484739b,0x44844483,0x44844483},
        {0x440e6896,0x441459ed,0x441459ee},
        {0,0,0},{0xa11c11cc,0xa110128a,0xa110128a}};
    auto context=std::make_unique<spu_context>();context->ls=context->ls_store;
    auto before=std::make_unique<spu_context>();*before=*context;
    GeometryJobEntry entry;entry.vertices=3;
    GeometryHostInputRecipe input;input.vertices=3;input.fields=1;input.blocks_per_field=1;input.field[0].semantic=1;
    auto data=std::make_unique<GeometryHostInputData>();data->ready=true;
    for(unsigned component=0;component<3;++component){
        uint32_t values[8];for(unsigned i=0;i<8;++i)values[i]=positions[i<3?i:2][component];
        data->block[0].component[component]=_mm256_loadu_si256(reinterpret_cast<const v8*>(values));
    }
    auto strict=std::make_unique<GeometryHostClipData>(),relaxed=std::make_unique<GeometryHostClipData>();
    for(bool bounded:{false,true}){
        assert(project_geometry_host_inputs<true>(*context,entry,input,*data,*strict,nullptr,&params,bounded));
        assert(project_geometry_host_inputs(*context,entry,input,*data,*relaxed,nullptr,&params,bounded));
        assert(strict->ready && relaxed->ready && strict->vertices==3 && relaxed->vertices==3);
        for(unsigned k=0;k<4;++k){
            for(unsigned i=0;i<3;++i)assert(strict->component[k][i]==expected[k][i]);
            for(unsigned i=3;i<8;++i)assert(strict->component[k][i]==0 && relaxed->component[k][i]==0);
        }
        assert(area(*strict)==0xb8c00000 && area(*relaxed)==0);
        assert(!std::memcmp(context.get(),before.get(),sizeof(spu_context)));
    }
    std::puts("PASS strict projection: original thin-triangle coordinates/area; bounded and checked paths; relaxed division remains explicit; padded lanes zero; no context writes");
}
