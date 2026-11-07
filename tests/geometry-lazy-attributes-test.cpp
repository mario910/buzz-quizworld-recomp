#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_job.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>
using namespace buzz;

template<bool Coarse> static void compare(const char* directory) {
    auto c=std::make_unique<spu_context>();
    auto before=std::make_unique<GeometryHostJobWorkspace>();
    auto after=std::make_unique<GeometryHostJobWorkspace>();
    std::vector<uint8_t> input(SPU_LS_SIZE),old_bytes(SPU_LS_SIZE),new_bytes(SPU_LS_SIZE);
    unsigned jobs=0,admitted=0,deferred=0,weighted=0,invalid=0,partial=0;
    uint64_t bytes=0,skipped_values=0;
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
    assert(jobs==19508 && admitted>18000 && weighted && deferred && invalid==32 && partial);
    printf("PASS lazy attributes: coarse=%u jobs=%u admitted=%u weighted_single_pass=%u deferred_culled=%u "
           "skipped_attribute_values=%llu published_bytes=%llu invalid_selectors=%u partial_publish_rejected=%u\n",
        unsigned(Coarse),jobs,admitted,weighted,deferred,(unsigned long long)skipped_values,
        (unsigned long long)bytes,invalid,partial);fflush(stdout);
}
int main(int argc,char** argv) {
    assert(argc==2);buzz_test_enter_rtz();compare<false>(argv[1]);compare<true>(argv[1]);
}
