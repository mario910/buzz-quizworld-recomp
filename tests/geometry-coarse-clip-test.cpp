#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_skin_job.h"
#include "../port/native_geometry_coarse_clip.h"
#include "../port/native_geometry_host_job.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>
using namespace buzz;
static void reciprocal_bound() {
    double maximum=0;uint64_t compared=0;
    for(unsigned sign=0;sign<2;++sign)for(unsigned first=0;first<0x800000;first+=8) {
        uint32_t input[8],output[8];
        for(unsigned lane=0;lane<8;++lane)input[lane]=(sign<<31)|0x3f800000|(first+lane);
        const auto w=_mm256_loadu_si256(reinterpret_cast<const v8*>(input));
        const auto estimate=v8_fi(w,v8_frest(w));
        const auto refined=v8_fma(v8_fnms(estimate,w,v8_set(0x3f800000)),estimate,estimate);
        _mm256_storeu_si256(reinterpret_cast<v8*>(output),refined);
        for(unsigned lane=0;lane<8;++lane) {
            float a,b;memcpy(&a,input+lane,4);memcpy(&b,output+lane,4);
            const double error=std::abs(double(a)*double(b)-1.0);
            assert(error<1.0/256.0);maximum=std::max(maximum,error);++compared;
        }
        // Normal exponent scaling preserves this sequence's mantissa: FREST
        // and FI scale the estimate; estimate*W and its residual are invariant;
        // the final FMA scales by the reciprocal power of two. No subnormal
        // occurs for the admitted exponent range. Check every exponent here
        // on a regularly spaced subset in addition to the full mantissa sweep.
        if((first&8191)==0)for(unsigned exponent=97;exponent<=157;++exponent) {
            uint32_t scaled[8],actual[8];
            for(unsigned lane=0;lane<8;++lane)scaled[lane]=(sign<<31)|(exponent<<23)|(first+lane);
            const auto sw=_mm256_loadu_si256(reinterpret_cast<const v8*>(scaled));
            const auto se=v8_fi(sw,v8_frest(sw));
            const auto sr=v8_fma(v8_fnms(se,sw,v8_set(0x3f800000)),se,se);
            _mm256_storeu_si256(reinterpret_cast<v8*>(actual),sr);
            for(unsigned lane=0;lane<8;++lane) {
                float original,value;memcpy(&original,output+lane,4);memcpy(&value,actual+lane,4);
                assert(double(value)==std::ldexp(double(original),127-int(exponent)));
                ++compared;
            }
        }
    }
    printf("PASS reciprocal bound: %llu lanes; max_relative=%.9g; all normal W exponents 97..157 checked\n",
        (unsigned long long)compared,maximum);fflush(stdout);
}
int main(int argc,char** argv) {
    assert(argc==2);buzz_test_enter_rtz();reciprocal_bound();
    auto c=std::make_unique<spu_context>();auto data=std::make_unique<GeometryHostInputData>();
    auto skin=std::make_unique<GeometryHostSkinWorkspace>();auto clips=std::make_unique<GeometryHostClipData>();
    auto indices=std::make_unique<GeometryHostIndexData>();
    auto old_job=std::make_unique<GeometryHostJobWorkspace>(),new_job=std::make_unique<GeometryHostJobWorkspace>();
    std::vector<uint8_t> old_bytes(SPU_LS_SIZE),new_bytes(SPU_LS_SIZE);
    unsigned jobs=0,admitted=0,rejected=0,fully_culled=0;uint64_t saved_vertices=0;
    unsigned producers=0;uint64_t published_bytes=0;
    bool checked_rejection=false;
    for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
        if(file.path().extension()!=".gz")continue;
        auto f=gzopen(file.path().string().c_str(),"rb");assert(f);uint32_t header[6];
        assert(gzread(f,header,sizeof header)==sizeof header);
        assert(header[0]==0x434a5a42 && header[2]==41 && header[5]==SPU_LS_SIZE);
        *c={};c->ls=c->ls_store;c->image_id=41;c->host_depth=2;
        assert(gzread(f,c->gpr,sizeof c->gpr)==sizeof c->gpr);
        assert(gzread(f,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(f);++jobs;
        GeometryJobEntry e;GeometryHostInputRecipe recipe;assert(read_geometry_job_entry(*c,e));
        const bool old_ready=prepare_geometry_host_job_impl<false>(*c,e,*old_job);
        const bool new_ready=prepare_geometry_host_job_impl<true>(*c,e,*new_job);
        assert(old_ready==new_ready);
        if(old_ready) {
            assert(old_job->index_bytes==new_job->index_bytes);
            assert(old_job->indices.triangles==new_job->indices.triangles);
            assert(old_job->indices.maximum_index==new_job->indices.maximum_index);
            assert(old_job->indices.final_group_kept==new_job->indices.final_group_kept);
            assert(emit_geometry_host_indices(old_job->indices,old_bytes.data(),old_job->index_bytes));
            assert(emit_geometry_host_indices(new_job->indices,new_bytes.data(),new_job->index_bytes));
            unsigned bytes=old_job->index_bytes;
            if(old_job->indices.triangles) {
                assert(old_job->vertices.output.bytes==new_job->vertices.output.bytes);
                assert(emit_geometry_host_vertices(old_job->input,old_job->data,old_job->vertices,old_job->tail,
                    old_bytes.data()+bytes,old_bytes.size()-bytes));
                assert(emit_geometry_host_vertices(new_job->input,new_job->data,new_job->vertices,new_job->tail,
                    new_bytes.data()+bytes,new_bytes.size()-bytes));
                bytes+=old_job->vertices.output.bytes;
            }
            if(memcmp(old_bytes.data(),new_bytes.data(),bytes)) {printf("PUBLISHED OUTPUT DIFF: %s\n",file.path().filename().string().c_str());return 1;}
            ++producers;published_bytes+=bytes;
        }
        if(!prepare_geometry_host_inputs(*c,e,recipe,*data) ||
           !skin_geometry_host_inputs(*c,e,recipe,*data,*skin))continue;
        const bool outside=geometry_host_trivially_outside(*c,e,recipe,*data);
        if(!project_geometry_host_inputs(*c,e,recipe,*data,*clips) ||
           !decode_geometry_host_indices(*c,e,*indices) ||
           !cull_geometry_host_triangles(*c,e,*clips,*indices))continue;
        ++admitted;fully_culled+=indices->triangles==0;
        if(outside) {
            if(indices->triangles) {printf("FALSE REJECT: %s triangles=%u\n",file.path().filename().string().c_str(),indices->triangles);return 1;}
            ++rejected;saved_vertices+=e.vertices;
            if(!checked_rejection) {
                unsigned field=16;
                for(unsigned n=0;n<recipe.fields;++n)if(recipe.field[n].semantic==1)field=n;
                assert(field<16);
                auto& component=data->at(recipe,field,0).component[0];const auto saved=component;
                component=v8_set(0x7f800000);assert(!geometry_host_trivially_outside(*c,e,recipe,*data));component=saved;
                auto invalid=recipe;invalid.blocks_per_field=0;
                assert(!geometry_host_trivially_outside(*c,e,invalid,*data));
                data->ready=false;assert(!geometry_host_trivially_outside(*c,e,recipe,*data));data->ready=true;
                checked_rejection=true;
            }
        }
    }
    assert(jobs==19508 && admitted>19000 && checked_rejection);
    assert(producers>18000 && published_bytes>0);
    printf("PASS coarse clip corpus: jobs=%u admitted=%u coarse_rejected=%u fully_culled=%u saved_vertices=%llu false_rejects=0; invalid/unbounded inputs retained\n",
        jobs,admitted,rejected,fully_culled,(unsigned long long)saved_vertices);
    printf("PASS whole host producer: %u matching admissions; %llu identical published index/vertex bytes\n",producers,(unsigned long long)published_bytes);
}
