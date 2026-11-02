#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_animation_host_matrices.h"
#include "zlib.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_drain_call(spu_context*, uint32_t) { abort(); }
void spu_img_restore(spu_context*, int32_t) { abort(); }
}
#include "../analysis/native-geometry/phase4/matrices-v1/reference.inc"

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
static void put32(uint8_t* p,uint32_t v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static uint64_t matrices=0, bytes=0, jobs=0, numeric=0, numeric_matrices=0;

// Inactive lanes read no following record, and publication ends at the guard.
static void compare(const uint8_t* world,const uint8_t* reference,const uint8_t* expected,unsigned count)
{
    assert(count && count<=8);
#ifdef _WIN32
    static uint8_t* pages[3]{};
    if(!pages[0]) for(auto& page:pages) {
        page=static_cast<uint8_t*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        assert(page);DWORD previous;assert(VirtualProtect(page+4096,4096,PAGE_NOACCESS,&previous));
    }
    auto* a=pages[0]+4096-count*64;auto* b=pages[1]+4096-count*48;auto* out=pages[2]+4096-count*48;
    memcpy(a,world,count*64);memcpy(b,reference,count*48);memset(out-32,0xa5,count*48+32);
#else
    std::array<uint8_t,512> aa{},bb{};std::array<uint8_t,416> oo{};
    auto* a=aa.data();auto* b=bb.data();auto* out=oo.data()+32;
    memcpy(a,world,count*64);memcpy(b,reference,count*48);memset(out-32,0xa5,count*48+32);
#endif
    auto x=buzz::decode_animation_host_affines8(a,64,count);
    auto y=buzz::decode_animation_host_affines8(b,48,count);
    auto z=buzz::compose_animation_host_affines8(x,y);
    buzz::emit_animation_host_affines8(z,out,count);
    if(memcmp(out,expected,count*48)) {
        for(unsigned at=0;at<count*48;at+=4) if(be32(out+at)!=be32(expected+at)) {
            fprintf(stderr,"matrix mismatch joint=%u coefficient=%u expected=%08X actual=%08X jobs=%llu numeric=%llu\n",at/48,(at%48)/4,be32(expected+at),be32(out+at),(unsigned long long)jobs,(unsigned long long)numeric);abort();
        }
    }
    for(unsigned at=0;at<32;++at) assert(out[-32+int(at)]==0xa5);
    assert(!memcmp(a,world,count*64)&&!memcmp(b,reference,count*48));
    matrices+=count;bytes+=count*48;
}

static void numeric_cases(spu_context& oracle)
{
    const uint64_t before=matrices;
    constexpr unsigned dst=0x10000,world=0x11000,reference=0x12000;
    uint32_t random=0x91743a21;
    constexpr uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x00800000,
        0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    std::array<uint8_t,512> a{};std::array<uint8_t,384> b{};
    for(unsigned test=0;test<4096;++test) {
        const unsigned count=1+test%8;
        for(unsigned joint=0;joint<count;++joint) {
            for(unsigned row=0;row<4;++row) for(unsigned column=0;column<4;++column) {
                random^=random<<13;random^=random>>17;random^=random<<5;
                const unsigned word=row==3?(column==3?0x3f800000:0):
                    test<512?special[(test+joint*3+row*4+column)%std::size(special)]:random;
                put32(a.data()+joint*64+row*16+column*4,word);
                put32(oracle.ls+world+joint*64+column*16+row*4,word);
                if(row<3) {
                    random^=random<<13;random^=random>>17;random^=random<<5;
                    const unsigned value=test<512?special[(test*3+joint+row*4+column)%std::size(special)]:random;
                    put32(b.data()+joint*48+row*16+column*4,value);
                }
            }
        }
        memcpy(oracle.ls+reference,b.data(),count*48);
        oracle.gpr[0]=spu_link(0x1234);oracle.gpr[3]=spu_il(dst);oracle.gpr[4]=spu_il(world);
        oracle.gpr[5]=spu_il(reference);oracle.gpr[6]=spu_il(count);oracle.host_depth=1;
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00000020(&oracle);
        unsigned steps=0;while(g_spu_trampoline_fn){assert(++steps<100);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&oracle);}
        assert(oracle.pc==0x1234);++numeric;compare(a.data(),b.data(),oracle.ls+dst,count);
    }
    numeric_matrices=matrices-before;
}

int main(int argc,char** argv)
{
    assert(argc==2);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto entry=std::make_unique<spu_context>();bool numeric_done=false;
    for(const auto& path:std::filesystem::directory_iterator(argv[1])) {
        if(path.path().extension()!=".gz")continue;
        auto* file=gzopen(path.path().string().c_str(),"rb");assert(file);
        auto read=[&](void* data,unsigned n){assert(gzread(file,data,n)==int(n));};
        uint32_t header[6];read(header,sizeof(header));assert(header[0]==0x434a5a42&&header[1]==1&&header[2]==40&&header[5]==SPU_LS_SIZE);
        memset(entry.get(),0,sizeof(*entry));entry->ls=entry->ls_store;entry->image_id=40;
        read(entry->gpr,sizeof(entry->gpr));read(entry->ls,SPU_LS_SIZE);
        uint32_t transfers;read(&transfers,4);std::vector<std::vector<uint8_t>> puts;
        for(unsigned n=0;n<transfers;++n){uint32_t h[4];read(h,16);std::vector<uint8_t> data(h[3]);read(data.data(),h[3]);if(h[0]==2)puts.push_back(std::move(data));}
        gzclose(file);assert(puts.size()>=2);
        const unsigned context=entry->gpr[3]._u32[0],descriptor=entry->gpr[4]._u32[0];
        const unsigned io=be32(entry->ls+context),input_count=(unsigned(entry->ls[descriptor+10])<<8|entry->ls[descriptor+11])/8;
        assert(input_count==4);unsigned input2=io;
        for(unsigned n=0;n<2;++n){const unsigned size=be32(entry->ls+descriptor+0x30+8*n)&0x7fff;input2+=(size+15)&~15u;}
        const unsigned input_bytes=be32(entry->ls+descriptor+0x40)&0x7fff;
        assert(input_bytes%48==0&&puts[0].size()==input_bytes&&puts[1].size()==input_bytes/48*64);
        ++jobs;
        for(unsigned first=0;first<input_bytes/48;first+=8)compare(puts[1].data()+first*64,entry->ls+input2+first*48,puts[0].data()+first*48,std::min(8u,input_bytes/48-first));
        if(!numeric_done){numeric_cases(*entry);numeric_done=true;}
    }
    assert(jobs==17714&&numeric==4096);
    printf("PASS %llu real jobs / %llu captured matrix records / %llu published bytes; %llu original-producer numeric batches / %llu numeric matrices; guarded tails 1..8; inputs unchanged\n",(unsigned long long)jobs,(unsigned long long)(matrices-numeric_matrices),(unsigned long long)(bytes-numeric_matrices*48),(unsigned long long)numeric,(unsigned long long)numeric_matrices);
}
