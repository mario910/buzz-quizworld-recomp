// Captured-job CPU benchmark. Build unchanged before/after; run natively only.
#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include "../port/native_geometry_host_cull_job.h"
#include "../port/native_geometry_coarse_clip.h"
#include "zlib.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>
using namespace buzz;
int main(int argc,char** argv) {
    assert(argc==2||(argc==3&&!strcmp(argv[2],"--pipeline")));buzz_test_enter_rtz();
    const bool measure_pipeline=argc==3;
    std::vector<std::filesystem::path> paths;
    for(const auto& p:std::filesystem::directory_iterator(argv[1]))
        if(p.path().extension()==".gz")paths.push_back(p.path());
    std::sort(paths.begin(),paths.end());assert(paths.size()==19508);
    auto c=std::make_unique<spu_context>();auto data=std::make_unique<GeometryHostInputData>();
    auto workspace=std::make_unique<GeometryHostSkinWorkspace>();auto clips=std::make_unique<GeometryHostClipData>();
    auto original=std::make_unique<GeometryHostIndexData>();auto indices=std::make_unique<GeometryHostIndexData>();
    volatile uint64_t checksum=0;
    for(unsigned pass=0;pass<5;++pass){double projection=0,culling=0,decoding=0,pipeline=0;unsigned jobs=0;uint64_t vertices=0,triangles=0,empty=0;
        for(unsigned sample=0;sample<256;++sample){
            auto* file=gzopen(paths[sample*73].string().c_str(),"rb");assert(file);uint32_t header[6];
            assert(gzread(file,header,sizeof header)==sizeof header);
            memset(c.get(),0,sizeof(*c));c->ls=c->ls_store;c->image_id=41;c->host_depth=2;
            assert(gzread(file,c->gpr,sizeof c->gpr)==sizeof c->gpr);
            assert(gzread(file,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(file);
            GeometryJobEntry e;GeometryHostInputRecipe input;const char* reason=nullptr;
            assert(read_geometry_job_entry(*c,e));
            if(!prepare_geometry_host_inputs(*c,e,input,*data,&reason)||
               !skin_geometry_host_inputs(*c,e,input,*data,*workspace,&reason)||
               !project_geometry_host_inputs(*c,e,input,*data,*clips,&reason))continue;
            assert(decode_geometry_host_indices(*c,e,*original));
            constexpr unsigned repeat=200;
            auto start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<repeat;++n){assert(decode_geometry_host_indices(*c,e,*original));checksum+=original->maximum_index;}
            decoding+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<repeat;++n){assert(project_geometry_host_inputs(*c,e,input,*data,*clips));checksum+=clips->component[0][n%e.vertices];}
            projection+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<repeat;++n){
                for(unsigned corner=0;corner<3;++corner)memcpy(indices->component[corner],original->component[corner],((original->triangles+7)&~7u)*2);
                indices->triangles=original->triangles;indices->maximum_index=original->maximum_index;indices->ready=true;
                assert(cull_geometry_host_triangles(*c,e,*clips,*indices));checksum+=indices->triangles+indices->final_group_kept;
            }
            culling+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(measure_pipeline){
                start=std::chrono::steady_clock::now();
                for(unsigned n=0;n<repeat;++n){
                    assert(prepare_geometry_host_inputs(*c,e,input,*data));
                    assert(skin_geometry_host_inputs(*c,e,input,*data,*workspace));
#if defined(BUZZ_GEOMETRY_COARSE_CLIP) && BUZZ_GEOMETRY_COARSE_CLIP
                    assert(decode_geometry_host_indices(*c,e,*indices));
                    if(geometry_host_trivially_outside(*c,e,input,*data)) {
                        indices->triangles=indices->maximum_index=indices->final_group_kept=0;
                    } else {
                        assert(project_geometry_host_inputs(*c,e,input,*data,*clips));
                        assert(cull_geometry_host_triangles(*c,e,*clips,*indices));
                    }
#else
                    assert(project_geometry_host_inputs(*c,e,input,*data,*clips));
                    assert(decode_geometry_host_indices(*c,e,*indices));
                    assert(cull_geometry_host_triangles(*c,e,*clips,*indices));
#endif
                    checksum+=uint64_t(indices->maximum_index)+indices->triangles+indices->final_group_kept;
                }
                pipeline+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            }
            ++jobs;vertices+=e.vertices;triangles+=original->triangles;empty+=indices->triangles==0;
        }
        printf("pass=%u jobs=%u vertices=%llu triangles=%llu fully_culled=%llu projection_ms=%.3f cull_with_reset_ms=%.3f decode_ms=%.3f checksum=%llu",
            pass,jobs,(unsigned long long)vertices,(unsigned long long)triangles,(unsigned long long)empty,
            projection,culling,decoding,(unsigned long long)checksum);
        if(measure_pipeline)printf(" input_skin_clip_decode_cull_ms=%.3f",pipeline);
        putchar('\n');fflush(stdout);
    }
}
