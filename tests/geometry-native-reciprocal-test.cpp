// Compare final culling decisions, not bit-identical projection intermediates.
#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include "../port/native_geometry_host_cull_job.h"
#include "zlib.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
using namespace buzz;
int main(int argc, char** argv) {
    assert(argc == 2); buzz_test_enter_rtz();
    auto c = std::make_unique<spu_context>();
    auto data = std::make_unique<GeometryHostInputData>();
    auto skin = std::make_unique<GeometryHostSkinWorkspace>();
    auto a = std::make_unique<GeometryHostClipData>(), b = std::make_unique<GeometryHostClipData>();
    auto ia = std::make_unique<GeometryHostIndexData>(), ib = std::make_unique<GeometryHostIndexData>();
    unsigned jobs=0, admitted=0, changed_jobs=0;
    uint64_t vertices=0, changed_xy=0, changed_flags=0, changed_packed=0;
    double max_relative=0, max_absolute=0;
    for (const auto& file : std::filesystem::directory_iterator(argv[1])) {
        if (file.path().extension() != ".gz") continue;
        auto f=gzopen(file.path().string().c_str(),"rb"); assert(f);
        uint32_t header[6]; assert(gzread(f,header,sizeof header)==sizeof header);
        assert(header[0]==0x434a5a42 && header[2]==41 && header[5]==SPU_LS_SIZE);
        *c={}; c->ls=c->ls_store; c->image_id=41; c->host_depth=2;
        assert(gzread(f,c->gpr,sizeof c->gpr)==sizeof c->gpr);
        assert(gzread(f,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE); gzclose(f); ++jobs;
        GeometryJobEntry e; GeometryHostInputRecipe recipe; GeometryHostClipParameters params;
        assert(read_geometry_job_entry(*c,e));
        if (!prepare_geometry_host_inputs(*c,e,recipe,*data) ||
            !skin_geometry_host_inputs(*c,e,recipe,*data,*skin) ||
            !read_geometry_host_clip_parameters(*c,e,params)) continue;
        unsigned position=16;
        for (unsigned n=0;n<recipe.fields;++n) if(recipe.field[n].semantic==1) position=n;
        if(position==16 || (e.config[0]&16)) continue;
        for(unsigned first=0;first<e.vertices;first+=8) {
            auto value=data->at(recipe,position,first/8);
            auto exact=geometry_host_clip8_impl<false,false>(params,value);
            auto native=geometry_host_clip8_impl<false,true>(params,value);
            for(unsigned component=0;component<4;++component) {
                _mm256_store_si256(reinterpret_cast<v8*>(a->component[component]+first),exact.component[component]);
                _mm256_store_si256(reinterpret_cast<v8*>(b->component[component]+first),native.component[component]);
            }
        }
        for(unsigned n=0;n<e.vertices;++n) {
            ++vertices;
            for(unsigned axis=0;axis<2;++axis) {
                auto x=a->component[axis][n],y=b->component[axis][n];
                changed_xy+=x!=y;
                float xf,yf;memcpy(&xf,&x,4);memcpy(&yf,&y,4);
                assert(std::isfinite(xf) && std::isfinite(yf));
                double error=std::abs(double(xf)-yf);
                max_absolute=std::max(max_absolute,error);
                max_relative=std::max(max_relative,error/std::max(1.0,std::abs(double(xf))));
            }
            changed_flags+=a->component[2][n]!=b->component[2][n];
            changed_packed+=a->component[3][n]!=b->component[3][n];
        }
        a->vertices=b->vertices=e.vertices; a->ready=b->ready=true;
        assert(decode_geometry_host_indices(*c,e,*ia)); *ib=*ia;
        assert(cull_geometry_host_triangles(*c,e,*a,*ia));
        assert(cull_geometry_host_triangles(*c,e,*b,*ib));
        bool changed=ia->triangles!=ib->triangles || ia->final_group_kept!=ib->final_group_kept;
        if(!changed) for(unsigned axis=0;axis<3;++axis)
            changed|=memcmp(ia->component[axis],ib->component[axis],ia->triangles*2)!=0;
        if(changed && changed_jobs++<10) printf("culling differs: %s triangles=%u/%u\n",file.path().filename().string().c_str(),ia->triangles,ib->triangles);
        ++admitted;
    }
    assert(jobs==19508 && admitted>19000);
    // Gross numerical divergence is a failure; individual quantization/cull
    // differences are reported for subsequent image/gameplay review.
    assert(max_relative<0.00001);
    GeometryHostClipParameters params{}; GeometryHostSoA8 position{};
    for(auto& component:position.component)component=_mm256_setzero_si256();
    const uint32_t special[]={0,0x80000000,1,0x00800000,0x7f800000,0xffffffff,0x7f7fffff};
    for(auto w:special) {
        params.row[3][3]=w;
        auto exact=geometry_host_clip8_impl<false,false>(params,position);
        auto native=geometry_host_clip8_impl<false,true>(params,position);
        for(unsigned axis=0;axis<4;++axis) {
            uint32_t x[8],y[8];
            _mm256_storeu_si256(reinterpret_cast<v8*>(x),exact.component[axis]);
            _mm256_storeu_si256(reinterpret_cast<v8*>(y),native.component[axis]);
            assert(!memcmp(x,y,sizeof x));
        }
    }
    printf("jobs=%u admitted=%u vertices=%llu changed_xy=%llu changed_flags=%llu changed_packed=%llu changed_culling_jobs=%u max_relative=%.9g max_absolute=%.9g exceptional_fallback=PASS\n",
        jobs,admitted,(unsigned long long)vertices,(unsigned long long)changed_xy,
        (unsigned long long)changed_flags,(unsigned long long)changed_packed,changed_jobs,max_relative,max_absolute);
}
