#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_index_job.h"
#include "../port/native_geometry_job_setup.h"
#include "zlib.h"
#include <cassert>
#include <array>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
using namespace buzz;
static void drain(spu_context*,uint32_t);
static std::string current_case;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c,uint32_t stop){drain(c,stop);}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_stop(spu_context*){abort();}
void spu_wrch(spu_context*,uint32_t,u128){abort();}
u128 spu_rdch(spu_context*,uint32_t){abort();}
}
#include "../analysis/native-geometry/phase3/host-index-job-v1/reference.inc"
extern "C" void spu_indirect_branch(spu_context* c)
{
    auto next=host_index_reference_lookup(c->pc);
    if(!next){fprintf(stderr,"%s missing index PC%X\n",current_case.c_str(),c->pc);abort();}next(c);
}
static void drain(spu_context* c,uint32_t stop)
{
    unsigned steps=0;
    while(g_spu_trampoline_fn){assert(++steps<1048576);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}
    assert(c->pc==stop);
}
int main(int argc,char** argv)
{
    assert(argc==2);
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto entry=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    auto before=std::make_unique<uint8_t[]>(sizeof(spu_context));auto indices=std::make_unique<GeometryHostIndexData>();
    std::array<std::unique_ptr<spu_context>,12> samples;
    unsigned jobs=0,decoded=0,maximum=0;uint64_t compared=0;std::map<std::string,unsigned> fallback;std::map<unsigned,unsigned> widths;
    auto compare_original=[&](const GeometryJobEntry& e){
        memcpy(reference.get(),entry.get(),sizeof(*entry));reference->ls=reference->ls_store;
        const char* reason=nullptr;
        GeometryJobSetup setup;assert(read_geometry_job_setup(*reference,setup,&reason));commit_geometry_job_setup(*reference,setup);
        GeometryContextState state;assert(initialize_geometry_context(*reference,setup.initialization,state,&reason));
        reference->gpr[0]=spu_link(0x1234);reference->gpr[1]=spu_il(e.frame_lsa);
        reference->gpr[3]=spu_il(e.geometry_context_lsa);reference->gpr[4]=spu_il(e.input[2].lsa);
        g_spu_trampoline_fn=nullptr;buzz_job_6eba_spu_func_0000AEA0(reference.get());drain(reference.get(),0x1234);
        const unsigned source=geometry_job_be32(reference->ls+e.geometry_context_lsa+0x50);
        assert(indices->ready&&indices->triangles==e.index_count/3);
        for(unsigned triangle=0;triangle<indices->triangles;++triangle)for(unsigned corner=0;corner<3;++corner){
            const unsigned expected=geometry_job_be16(reference->ls+source+triangle*6+corner*2);
            if(indices->component[corner][triangle]!=expected){
                const auto* header=entry->ls+e.input[2].lsa;
                fprintf(stderr,"%s width%u D%u Q%u flags%u triangle%u corner%u actual%u expected%u\n",current_case.c_str(),header[6],
                    geometry_job_be16(header),geometry_job_be16(header+2),geometry_job_be16(header+4),triangle,corner,indices->component[corner][triangle],expected);abort();
            }
        }
    };
    for(const auto& path:std::filesystem::directory_iterator(argv[1])){
        if(path.path().extension()!=".gz")continue;current_case=path.path().filename().string();
        auto* file=gzopen(path.path().string().c_str(),"rb");assert(file);uint32_t header[6];
        assert(gzread(file,header,sizeof header)==sizeof header);
        assert(header[0]==0x434a5a42&&header[1]==1&&header[2]==41&&header[5]==SPU_LS_SIZE);
        memset(entry.get(),0,sizeof(*entry));entry->ls=entry->ls_store;entry->image_id=41;entry->host_depth=2;
        assert(gzread(file,entry->gpr,sizeof entry->gpr)==sizeof entry->gpr);
        assert(gzread(file,entry->ls,SPU_LS_SIZE)==SPU_LS_SIZE);gzclose(file);
        GeometryJobEntry e;const char* reason=nullptr;assert(read_geometry_job_entry(*entry,e,&reason));++jobs;
        memcpy(before.get(),entry.get(),sizeof(*entry));
        const bool supported=decode_geometry_host_indices(*entry,e,*indices,&reason);
        assert(!memcmp(before.get(),entry.get(),sizeof(*entry)));
        if(!supported){++fallback[reason];continue;}++decoded;++widths[entry->ls[e.input[2].lsa+6]];
        compare_original(e);compared+=e.index_count;assert(indices->maximum_index<e.vertices);
        const unsigned width=entry->ls[e.input[2].lsa+6];
        if(!samples[width]&&e.index_count>24){samples[width]=std::make_unique<spu_context>();memcpy(samples[width].get(),entry.get(),sizeof(*entry));samples[width]->ls=samples[width]->ls_store;}
        if(indices->maximum_index>maximum)maximum=indices->maximum_index;
        if(jobs%5000==0)printf("Host index corpus progress:%u entries\n",jobs);
    }
    assert(jobs==19508&&decoded);
    unsigned corners=0;uint64_t corner_indices=0;
    constexpr unsigned biases[]={0,1,32767,32768,65534,65535};unsigned random=0x812ba393;
    for(unsigned width=0;width<samples.size();++width)if(samples[width])for(unsigned bias:biases)for(unsigned shape=0;shape<4;++shape){
        memcpy(entry.get(),samples[width].get(),sizeof(*entry));entry->ls=entry->ls_store;
        GeometryJobEntry e;const char* reason=nullptr;assert(read_geometry_job_entry(*entry,e));auto* bytes=entry->ls+e.input[2].lsa;
        const unsigned deltas=geometry_job_be16(bytes),flags=geometry_job_be16(bytes+4),packed=8+flags+(e.index_count/3+3)/4;
        const unsigned end=packed+(deltas*width+7)/8;bytes[2]=uint8_t(bias>>8);bytes[3]=uint8_t(bias);
        for(unsigned byte=packed;byte<end;++byte){random^=random<<13;random^=random>>17;random^=random<<5;
            bytes[byte]=uint8_t(shape==0?0:shape==1?255:shape==2?random:byte*73u);}
        e.input[2].size=end; // Admit exactly the declared final payload byte.
        current_case="index bias/payload corner width"+std::to_string(width)+" bias"+std::to_string(bias)+" shape"+std::to_string(shape);
        memcpy(before.get(),entry.get(),sizeof(*entry));
        assert(decode_geometry_host_indices(*entry,e,*indices,&reason));assert(!memcmp(before.get(),entry.get(),sizeof(*entry)));
        compare_original(e);++corners;corner_indices+=e.index_count;
    }
    assert(samples[0]);
    for(unsigned fault=0;fault<18;++fault){
        memcpy(entry.get(),samples[0].get(),sizeof(*entry));entry->ls=entry->ls_store;
        GeometryJobEntry e;const char* reason=nullptr;assert(read_geometry_job_entry(*entry,e));auto* bytes=entry->ls+e.input[2].lsa;
        switch(fault){
        case 0:e.config[6]=uint8_t((e.config[6]&15)|0x20);break;
        case 1:e.index_count=65536;break;
        case 2:e.index_count=4;break;
        case 3:e.input[2].lsa|=1;break;
        case 4:e.input[2].size=7;break;
        case 5:e.io_bytes=UINT32_MAX;break;
        case 6:bytes[6]=1;break;
        case 7:bytes[6]=16;break;
        case 8:bytes[1]=1;break;
        case 9:bytes[7]=1;break;
        case 10:bytes[4]=bytes[5]=255;break;
        case 11:bytes[4]=bytes[5]=0;break;
        case 12:entry->ls[0xd690]^=1;break;
        case 13:entry->ls[0xd790]^=1;break;
        case 14:entry->ls[0xd110]^=1;break;
        case 15:{const unsigned flags=geometry_job_be16(bytes+4);bytes[8+flags]&=0x3f;break;}
        case 16:{
            const unsigned flags=geometry_job_be16(bytes+4),triangles=e.index_count/3;unsigned references=triangles;
            for(unsigned triangle=0;triangle<triangles;++triangle)references+=((bytes[8+flags+triangle/4]>>(6-triangle%4*2))&3)==3?2:0;
            assert(references&&references<=flags*8);const unsigned bit=references-1;
            bytes[8+bit/8]|=uint8_t(1u<<(7-bit%8));break;
        }
        case 17:e.input[2].lsa=SPU_LS_SIZE;break;
        }
        memcpy(before.get(),entry.get(),sizeof(*entry));indices->ready=true;
        if(decode_geometry_host_indices(*entry,e,*indices,&reason)||!reason){fprintf(stderr,"Index reject fault%u accepted\n",fault);abort();}
        assert(!indices->ready&&!memcmp(before.get(),entry.get(),sizeof(*entry)));
    }
    for(const auto& [reason,count]:fallback)printf("Host index unsupported:%s=%u\n",reason.c_str(),count);
    for(const auto& [width,count]:widths)printf("Observed packed width%u:%u jobs\n",width,count);
    printf("PASS host index logical decode:%u real entries/%u exact decodes/%llu indices;maximum%u;entry LS/GPR/control unchanged\n",jobs,decoded,static_cast<unsigned long long>(compared),maximum);
    printf("PASS host index bounds/wrap:%u modified hot-width cases/%llu indices;exact final payload boundary;all real indices within vertex count;18 unchanged-state rejects including late payload failure\n",corners,static_cast<unsigned long long>(corner_indices));
}
