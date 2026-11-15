#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_job.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>
#include <chrono>
using namespace buzz;
// The standalone fixture has no mutable blend request. Unexpected guest
// execution must fail instead of silently bypassing the oracle comparison.
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_drain_call(spu_context*,uint32_t){abort();}
void spu_img_restore(spu_context*,int32_t){abort();}
void buzz_geometry_job_wrch(spu_context*,uint32_t,u128){abort();}
u128 buzz_geometry_job_rdch(spu_context*,uint32_t){abort();}
}

template<bool Coarse> static void compare(const char* directory, unsigned expected_jobs, bool benchmark) {
    auto c=std::make_unique<spu_context>();
    auto before=std::make_unique<GeometryHostJobWorkspace>();
    auto after=std::make_unique<GeometryHostJobWorkspace>();
    std::vector<uint8_t> input(SPU_LS_SIZE),old_bytes(SPU_LS_SIZE),new_bytes(SPU_LS_SIZE);
    unsigned jobs=0,admitted=0,deferred=0,weighted=0,invalid=0,partial=0;
    uint64_t bytes=0,skipped_values=0;
    uint64_t timing[2][2][2]{}; unsigned samples[2][2]{};
    for(const auto& file:std::filesystem::directory_iterator(directory)) {
        if(file.path().extension()!=".gz")continue;
        auto f=gzopen(file.path().string().c_str(),"rb");assert(f);uint32_t header[6];
        assert(gzread(f,header,sizeof header)==sizeof header);
        assert(header[0]==0x434a5a42 && header[2]==41 && header[5]==SPU_LS_SIZE);
        *c={};c->ls=c->ls_store;c->image_id=41;c->host_depth=2;
        assert(gzread(f,c->gpr,sizeof c->gpr)==sizeof c->gpr);
        assert(gzread(f,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(f);++jobs;
        memcpy(input.data(),c->ls,SPU_LS_SIZE);
        u128 registers[128];memcpy(registers,c->gpr,sizeof registers);
        GeometryJobEntry e;assert(read_geometry_job_entry(*c,e));
        const bool a=prepare_geometry_host_job_impl<Coarse,false>(*c,e,*before);
        const bool b=prepare_geometry_host_job_impl<Coarse,true>(*c,e,*after);
        assert(a==b);
        assert(!memcmp(input.data(),c->ls,SPU_LS_SIZE));
        assert(!memcmp(registers,c->gpr,sizeof registers));
        if(!a)continue;
        ++admitted;weighted+=after->skin.active;
        const unsigned skin=after->skin.active, visible=!!after->indices.triangles;
        ++samples[skin][visible];
        // Alternate order within each job; exclude corpus loading and byte
        // comparisons from timings. This is a kernel benchmark, not FPS.
        if(benchmark)for(unsigned repeat=0;repeat<6;++repeat)for(unsigned j=0;j<2;++j) {
            const unsigned lazy=j^(repeat&1);
            const auto start=std::chrono::steady_clock::now();
            const bool ok=lazy?prepare_geometry_host_job_impl<Coarse,true>(*c,e,*after):
                prepare_geometry_host_job_impl<Coarse,false>(*c,e,*before);
            const auto stop=std::chrono::steady_clock::now();assert(ok);
            timing[lazy][skin][visible]+=std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();
        }
        assert(before->index_bytes==after->index_bytes);
        assert(before->indices.triangles==after->indices.triangles);
        assert(before->indices.maximum_index==after->indices.maximum_index);
        assert(before->indices.final_group_kept==after->indices.final_group_kept);
        assert(emit_geometry_host_indices(before->indices,old_bytes.data(),before->index_bytes));
        assert(emit_geometry_host_indices(after->indices,new_bytes.data(),after->index_bytes));
        unsigned n=before->index_bytes;
        if(before->indices.triangles) {
            assert(!after->data.pending_fields);
            assert(before->vertices.output.bytes==after->vertices.output.bytes);
            assert(emit_geometry_host_vertices(before->input,before->data,before->vertices,before->tail,
                old_bytes.data()+n,old_bytes.size()-n));
            assert(emit_geometry_host_vertices(after->input,after->data,after->vertices,after->tail,
                new_bytes.data()+n,new_bytes.size()-n));
            n+=before->vertices.output.bytes;
        } else if(after->data.pending_fields) {
            ++deferred;
            for(unsigned i=0;i<after->input.fields;++i)
                if(after->data.pending_fields&(1u<<i))skipped_values+=e.vertices;
            // A partial bank must never publish stale attributes, even if its
            // reusable tail still happens to describe a previous visible job.
            assert(!emit_geometry_host_vertices(after->input,after->data,after->vertices,after->tail,
                new_bytes.data(),new_bytes.size()));
            ++partial;
        }
        if(memcmp(old_bytes.data(),new_bytes.data(),n)) {
            printf("PUBLISHED OUTPUT DIFF: %s\n",file.path().filename().string().c_str());abort();
        }
        bytes+=n;
        // Deferred unit selectors still participate in admission. A culled
        // mesh cannot hide a malformed secondary stream from the fallback.
        if(invalid<32 && !after->skin.active && !after->indices.triangles)
            for(unsigned field=0;field<after->input.fields;++field) {
                const auto& attr=after->input.field[field];
                if(attr.storage!=9 || attr.semantic==1)continue;
                const unsigned at=attr.source+2;const uint8_t old=c->ls[at];
                c->ls[at]=uint8_t((old&0xf0)|15);
                assert((!prepare_geometry_host_job_impl<Coarse,false>(*c,e,*before)));
                assert((!prepare_geometry_host_job_impl<Coarse,true>(*c,e,*after)));
                assert(c->ls[at]==uint8_t((old&0xf0)|15));c->ls[at]=old;++invalid;break;
            }
    }
    assert(jobs==expected_jobs && admitted && weighted && deferred && invalid==32 && partial);
    if(expected_jobs==19508)assert(admitted>18000);
    if(benchmark)for(unsigned skin=0;skin<2;++skin)for(unsigned visible=0;visible<2;++visible)
        if(samples[skin][visible])printf("TIMING weighted=%u visible=%u jobs=%u eager=%.2f us lazy=%.2f us\n",
            skin,visible,samples[skin][visible],timing[0][skin][visible]/(6000.0*samples[skin][visible]),
            timing[1][skin][visible]/(6000.0*samples[skin][visible]));
    printf("PASS lazy attributes: coarse=%u jobs=%u admitted=%u weighted_single_pass=%u deferred_culled=%u "
           "skipped_attribute_values=%llu published_bytes=%llu invalid_selectors=%u partial_publish_rejected=%u\n",
        unsigned(Coarse),jobs,admitted,weighted,deferred,(unsigned long long)skipped_values,
        (unsigned long long)bytes,invalid,partial);fflush(stdout);
}
int main(int argc,char** argv) {
    assert(argc>=2);unsigned expected_jobs=19508;bool benchmark=false;
    for(int i=2;i<argc;++i) {
        if(!strcmp(argv[i],"--bench"))benchmark=true;
        else if(!strcmp(argv[i],"--expected-jobs")) {
            assert(++i<argc);char* end=nullptr;
            const unsigned long count=strtoul(argv[i],&end,10);
            assert(end!=argv[i] && !*end && count && count<=UINT32_MAX);
            expected_jobs=unsigned(count);
        } else {fprintf(stderr,"Unknown option: %s\n",argv[i]);return 2;}
    }
    buzz_test_enter_rtz();compare<false>(argv[1],expected_jobs,benchmark);
    compare<true>(argv[1],expected_jobs,benchmark);
}
