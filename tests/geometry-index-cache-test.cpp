#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_index_job.h"
#include "zlib.h"
#include <cassert>
#include <filesystem>
#include <memory>
#include <cstdio>
using namespace buzz;
int main(int argc,char** argv) {
    assert(argc==2);
    auto c=std::make_unique<spu_context>();
    auto expected=std::make_unique<GeometryHostIndexData>();
    auto actual=std::make_unique<GeometryHostIndexData>();
    GeometryHostIndexCache cache;
    unsigned jobs=0,admitted=0,mutations=0,collision_sets=0,collisions=0;
    auto compare=[&](const GeometryJobEntry& e) {
        const char *a=nullptr,*b=nullptr;
        const bool ok=decode_geometry_host_indices_uncached(*c,e,*expected,&a);
        memset(actual.get(),0xa5,sizeof(*actual));
        assert(cache.decode(*c,e,*actual,&b)==ok);
        assert((a==nullptr)==(b==nullptr));
        if(a)assert(!strcmp(a,b));
        assert(actual->ready==expected->ready);
        if(ok) {
            assert(actual->triangles==expected->triangles && actual->maximum_index==expected->maximum_index &&
                   actual->final_group_kept==expected->final_group_kept);
            const unsigned n=(expected->triangles+7)&~7u;
            for(unsigned corner=0;corner<3;++corner)
                assert(!memcmp(actual->component[corner],expected->component[corner],n*sizeof(uint16_t)));
        }
        return ok;
    };
    for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
        if(file.path().extension()!=".gz")continue;
        auto f=gzopen(file.path().string().c_str(),"rb");assert(f);uint32_t header[6];
        assert(gzread(f,header,sizeof header)==sizeof header && header[0]==0x434a5a42 && header[2]==41);
        *c={};c->ls=c->ls_store;c->image_id=41;
        assert(gzread(f,c->gpr,sizeof c->gpr)==sizeof c->gpr);
        assert(gzread(f,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(f);
        GeometryJobEntry e;assert(read_geometry_job_entry(*c,e));++jobs;
        const bool ok=compare(e);assert(compare(e)==ok);admitted+=ok;
        if(!ok)continue;
        // A warmed cache must revalidate source bytes, codec and entry spans.
        const unsigned source=e.input[GeometryJobEntry::indices].lsa,size=e.input[GeometryJobEntry::indices].size;
        if(collision_sets<32 && size>64 && size<=8192) {
            // Change consumed packed deltas outside all three fingerprint
            // samples. These valid, different payloads have the SAME full
            // fingerprint; exercise all ways and force repeated evictions.
            const auto* bytes=c->ls+source;
            const unsigned deltas=geometry_job_be16(bytes),width=bytes[6];
            const unsigned packed=8+geometry_job_be16(bytes+4)+(e.index_count/3+3)/4;
            const unsigned end=packed+(deltas*width+7)/8;
            for(unsigned at=packed;at<end && end<=size;++at) {
                const unsigned middle=(size-8)/2;
                if(at<8 || (at>=middle && at<middle+8) || at>=size-8)continue;
                const uint8_t original=c->ls[source+at];
                for(unsigned variant:{1u,2u,3u,4u,1u,3u,2u,4u,5u,6u,7u,4u,1u,0u}) {
                    c->ls[source+at]=original^variant;
                    assert(compare(e));assert(compare(e));++collisions;
                }
                assert(c->ls[source+at]==original);++collision_sets;break;
            }
        }
        if(size>64 && size<=8192) {
            // Offset 16 avoids all three fingerprint samples for large inputs:
            // full memcmp, rather than the sampled key, must reject a stale hit.
            c->ls[source+16]^=1;compare(e);c->ls[source+16]^=1;compare(e);++mutations;
        }
        c->ls[0xd690]^=1;assert(!compare(e));c->ls[0xd690]^=1;assert(compare(e));++mutations;
        auto malformed=e;malformed.io_bytes=UINT32_MAX;assert(!compare(malformed));assert(compare(e));++mutations;
        malformed=e;malformed.index_count=4;assert(!compare(malformed));assert(compare(e));++mutations;
    }
    assert(jobs==19508 && admitted);
    assert(collision_sets==32 && collisions==448);
    printf("Geometry index cache: %u corpus jobs, %u admitted, %u mutations; cold/warm/poisoned outputs PASS\n",jobs,admitted,mutations);
    printf("Geometry index cache: %u same-fingerprint sets, %u valid payload variants/evictions PASS\n",collision_sets,collisions);
}
