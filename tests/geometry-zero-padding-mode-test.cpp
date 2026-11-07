#define BUZZ_GEOMETRY_ZERO_VERTEX_PADDING 1
#define BUZZ_GEOMETRY_COARSE_CLIP 1
#define BUZZ_GEOMETRY_LAZY_ATTRIBUTES 1
#define SPU_LS_DEBUG_HOOKS 0
#include "geometry-test-fp.h"
#include "../port/native_geometry_host_job.h"
#include "zlib.h"
#include <cassert>
#include <filesystem>
#include <memory>
#include <vector>
static int mode=1;
extern "C" int buzz_geometry_job_mode(void){return mode;}
using namespace buzz;
int main(int argc,char** argv){
    assert(argc==2);buzz_test_enter_rtz();
    auto c=std::make_unique<spu_context>();
    auto old=std::make_unique<GeometryHostJobWorkspace>(),candidate=std::make_unique<GeometryHostJobWorkspace>();
    bool checked_admission=false,checked_exact=false;
    unsigned visited=0;
    for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
        if(file.path().extension()!=".gz")continue;
        auto f=gzopen(file.path().string().c_str(),"rb");assert(f);uint32_t header[6];assert(gzread(f,header,sizeof header)==sizeof header);
        *c={};c->ls=c->ls_store;c->image_id=41;c->host_depth=2;
        assert(gzread(f,c->gpr,sizeof c->gpr)==sizeof c->gpr);assert(gzread(f,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(f);++visited;
        GeometryJobEntry e;assert(read_geometry_job_entry(*c,e));
        std::vector<uint8_t> unchanged(c->ls,c->ls+SPU_LS_SIZE);u128 regs[128];memcpy(regs,c->gpr,sizeof regs);
        const char* why=nullptr;
        const bool prior=prepare_geometry_host_job_impl<true,true,true,false>(*c,e,*old,&why);
        mode=1;if(!prepare_geometry_host_job(*c,e,*candidate))continue;
        if(!prior && !checked_admission){
            assert(why && !strcmp(why,"host vertex padding palette index"));
            assert(candidate->tail.ready && !candidate->tail.position[0] && !candidate->tail.position[1] && !candidate->tail.position[2]);
            for(int strict:{0,2}){mode=strict;assert(!prepare_geometry_host_job(*c,e,*candidate,&why));assert(why&&!strcmp(why,"host vertex padding palette index"));}
            mode=1;assert(prepare_geometry_host_job(*c,e,*candidate));
            // An ignored alignment vertex is distinct from a logical zero-
            // weight influence: every logical palette index remains checked.
            assert(candidate->skin.active && candidate->skin.matrices<256);
            auto* influence=c->ls+candidate->skin.weights+6;
            const auto weight=influence[0],index=influence[1];
            influence[0]=0;influence[1]=uint8_t(candidate->skin.matrices);
            assert(!prepare_geometry_host_job(*c,e,*candidate,&why));
            assert(why&&!strcmp(why,"host skin palette index"));
            influence[0]=weight;influence[1]=index;
            assert(prepare_geometry_host_job(*c,e,*candidate));checked_admission=true;
        }
        if(prior && old->indices.triangles && !checked_exact){
            mode=2;assert(prepare_geometry_host_job(*c,e,*candidate));
            assert(candidate->tail.ready && !memcmp(candidate->tail.position,old->tail.position,sizeof old->tail.position));
            assert(candidate->indices.triangles==old->indices.triangles && candidate->index_bytes==old->index_bytes);
            std::vector<uint8_t> a(old->vertices.output.bytes),b(a.size());
            assert(emit_geometry_host_vertices(old->input,old->data,old->vertices,old->tail,a.data(),a.size()));
            assert(emit_geometry_host_vertices(candidate->input,candidate->data,candidate->vertices,candidate->tail,b.data(),b.size()));
            assert(a==b);checked_exact=true;
        }
        assert(!memcmp(c->ls,unchanged.data(),SPU_LS_SIZE));assert(!memcmp(c->gpr,regs,sizeof regs));
        if(checked_admission&&checked_exact)break;
    }
    assert(checked_admission&&checked_exact);
    printf("PASS zero-padding mode guard: native1 admits padding-only case; bad logical palette still rejected; mode0/paired2 retain rejection; paired2 retains complete strict bytes; %u entries inspected; no architectural writes\n",visited);
}
