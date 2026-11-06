#include "geometry-test-fp.h"
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_cull_job.h"
#include "zlib.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>
using namespace buzz;
int main(int argc,char** argv) {
    assert(argc==2);buzz_test_enter_rtz();
    auto c=std::make_unique<spu_context>();GeometryJobEntry entry;bool found=false;
    for(const auto& p:std::filesystem::directory_iterator(argv[1])){
        if(p.path().extension()!=".gz")continue;
        auto* file=gzopen(p.path().string().c_str(),"rb");assert(file);uint32_t header[6];
        assert(gzread(file,header,sizeof header)==sizeof header);
        memset(c.get(),0,sizeof(*c));c->ls=c->ls_store;c->image_id=41;
        assert(gzread(file,c->gpr,sizeof c->gpr)==sizeof c->gpr);
        assert(gzread(file,c->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(file);
        GeometryHostCullParameters params;GeometryHostClipParameters clip_params;
        if(read_geometry_job_entry(*c,entry)&&geometry_host_index_codec_matches(*c)&&read_geometry_host_cull_parameters(*c,entry,params)&&
           read_geometry_host_clip_parameters(*c,entry,clip_params)){found=true;break;}
    }
    assert(found);
    unsigned mutations=0;
    // Independently enumerate every codec quad consumed by these admissions.
    // Corrupt every bit, restore it, then require the same LS pointer to pass
    // again. This catches byte-order, partial-load and stale-cache mistakes.
    const std::vector<unsigned> quads[] = {
        {0xd110,0xd120,0xd130,0xd140,0xd150,0xd160,0xd170,0xd180,0xd8a0,0xd8e0,0xd8f0,0xd950,0xd980,0xd9d0,0xd9e0,0xda30},
        {0xd020,0xd030,0xd040,0xd050,0xd060,0xd070,0xd980,0xcff0,0xcfe0,0xdaa0},
        {0xd930,0xd940,0xda40,0xdaa0,0xd8d0,0xda90,0xd960,0xcfc0,0xcfd0,0xd990,0xdae0}
    };
    auto admitted = [&](unsigned stage) {
        if(stage==0)return geometry_host_index_codec_matches(*c);
        if(stage==1){GeometryHostCullParameters p;return read_geometry_host_cull_parameters(*c,entry,p);}
        GeometryHostClipParameters p;return read_geometry_host_clip_parameters(*c,entry,p);
    };
    for(unsigned stage=0;stage<3;++stage)for(unsigned base:quads[stage])
    for(unsigned byte=0;byte<16;++byte)for(unsigned bit=0;bit<8;++bit){
        c->ls[base+byte]^=1u<<bit;assert(!admitted(stage));
        c->ls[base+byte]^=1u<<bit;assert(admitted(stage));++mutations;
    }
    for(unsigned region=0;region<2;++region)for(unsigned byte=0;byte<(region?768u:512u);++byte)for(unsigned bit=0;bit<8;++bit){
        const unsigned at=(region?0xdaf0:0xd690)+byte;c->ls[at]^=1u<<bit;
        if(region){GeometryHostCullParameters params{77,0x12345678};const char* reason=nullptr;
            assert(!read_geometry_host_cull_parameters(*c,entry,params,&reason)&&reason);
            assert(params.mode==77&&params.winding==0x12345678);
        }else assert(!geometry_host_index_codec_matches(*c));
        c->ls[at]^=1u<<bit;++mutations;
    }
    auto clips=std::make_unique<GeometryHostClipData>();auto indices=std::make_unique<GeometryHostIndexData>();
    unsigned cases=0;
    for(unsigned active=1;active<=17;++active)for(unsigned mask=0;mask<(active<8?1u<<active:256u);++mask)
    for(unsigned mode=1;mode<=3;++mode)for(unsigned orientation=0;orientation<2;++orientation)for(unsigned kind=0;kind<3;++kind){
        auto e=entry;e.vertices=active*3;e.index_count=active*3;e.config[0]&=~16u;e.config[6]=orientation<<4;
        e.flags=8|(mode==1?0:32)|(mode==3?0x100:0);
        GeometryHostCullParameters params;assert(read_geometry_host_cull_parameters(*c,e,params));assert(params.mode==mode);
        const bool back=mode!=1&&params.winding==0;
        std::vector<unsigned> keep;
        for(unsigned t=0;t<active;++t){
            const bool drop=(mask>>(t%8))&1;
            const bool flag_drop=drop&&(kind==0||(kind==2&&!(t&1)));
            const bool packed_drop=drop&&!flag_drop;
            for(unsigned corner=0;corner<3;++corner){
                const unsigned vertex=t*3+corner;indices->component[corner][t]=uint16_t(vertex);
                clips->component[0][vertex]=corner==1?0x3f800000:0;
                clips->component[1][vertex]=corner==2?0x3f800000:0;
                clips->component[2][vertex]=flag_drop?0x80000000:0;
                clips->component[3][vertex]=packed_drop?0x80010001:0x80000000|(corner<<16)|corner;
            }
            if(!drop&&!back)keep.push_back(t);
        }
        for(unsigned t=active;t<((active+7)&~7u);++t)for(auto& component:indices->component)component[t]=0;
        clips->vertices=e.vertices;clips->ready=true;
        indices->triangles=active;indices->maximum_index=e.vertices-1;indices->ready=true;indices->final_group_kept=5;
        assert(cull_geometry_host_triangles(*c,e,*clips,*indices));assert(indices->ready&&indices->triangles==keep.size());
        unsigned final_kept=0,maximum=0;
        for(unsigned n=0;n<keep.size();++n){
            for(unsigned corner=0;corner<3;++corner)assert(indices->component[corner][n]==keep[n]*3+corner);
            maximum=keep[n]*3+2;final_kept+=keep[n]>=((active-1)&~3u);
        }
        assert(indices->maximum_index==maximum&&indices->final_group_kept==final_kept);
        for(unsigned t=keep.size();t<((keep.size()+7)&~7u);++t)for(auto& component:indices->component)assert(component[t]==0);
        ++cases;
    }
    // Exercise the end of each owned component, with the maximum legal vertex
    // index and both dense and sparse survivors. In-place compaction must
    // preserve every later unread batch, including at the capacity boundary.
    for(unsigned count : {21840u,21841u,21845u}) for(unsigned mask : {0u,1u,0x55u,0xfeu,0xffu}) {
        auto e=entry;e.vertices=4096;e.index_count=count*3;e.config[0]&=~16u;e.flags=8;
        for(unsigned vertex=0;vertex<4096;++vertex) {
            clips->component[0][vertex]=(vertex%3==1)?0x3f800000:0;
            clips->component[1][vertex]=(vertex%3==2)?0x3f800000:0;
            clips->component[2][vertex]=0;
            clips->component[3][vertex]=0x80000000|(vertex%3<<16)|(vertex%3);
        }
        // These three vertices form a valid triangle and include index 4095.
        clips->component[0][4093]=0;clips->component[1][4093]=0;
        clips->component[0][4094]=0x3f800000;clips->component[1][4094]=0;
        clips->component[0][4095]=0;clips->component[1][4095]=0x3f800000;
        for(unsigned t=0;t<count;++t)for(unsigned corner=0;corner<3;++corner)
            indices->component[corner][t]=uint16_t((mask&(1u<<(t%8)))?0:4093+corner);
        for(unsigned t=count;t<((count+7)&~7u);++t)for(auto& component:indices->component)component[t]=0;
        clips->vertices=4096;clips->ready=true;indices->triangles=count;indices->maximum_index=4095;indices->ready=true;
        unsigned expected=0,last=0;
        for(unsigned t=0;t<count;++t)if(!(mask&(1u<<(t%8)))){++expected;last+=t>=((count-1)&~3u);}
        assert(cull_geometry_host_triangles(*c,e,*clips,*indices));
        assert(indices->triangles==expected&&indices->maximum_index==(expected?4095u:0u)&&indices->final_group_kept==last);
        for(unsigned t=0;t<expected;++t)for(unsigned corner=0;corner<3;++corner)assert(indices->component[corner][t]==4093+corner);
        for(unsigned t=expected;t<((expected+7)&~7u);++t)for(auto& component:indices->component)assert(component[t]==0);
        ++cases;
    }
    printf("PASS cull early rejection: %u stable compaction/mode/tail/capacity cases; %u single-bit codec mutations rejected\n",cases,mutations);
}
