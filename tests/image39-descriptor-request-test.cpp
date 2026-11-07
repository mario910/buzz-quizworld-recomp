#define SPU_LS_DEBUG_HOOKS 0
#define BUZZ_IMAGE39_NATIVE_REQUEST 1
#define BUZZ_IMAGE39_REQUEST_PROFILE 100
#include "spu_helpers.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

static const spu_context* expected;
static unsigned commands;
static bool native_guard=true;
static bool synthetic_io=false, recording=false;
static uint32_t synthetic_transfer[5];
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = nullptr;
int buzz_spu_native_request_allowed(const spu_context*) { return native_guard; }
void spu_indirect_branch(spu_context*) { assert(false); }
void spu_halt(spu_context*) { assert(false); }
void spu_stop(spu_context*) { assert(false); }
void spu_img_restore(spu_context* c, int32_t image) { assert(c->image_id == image); }
void spu_drain_call(spu_context* c, uint32_t ret) {
    while (g_spu_trampoline_fn && c->pc != ret) {
        auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(c);
    }
    g_spu_trampoline_fn = nullptr;
}
void spu_wrch(spu_context* c, uint32_t channel, u128 value) {
    const auto v = value._u32[0];
    switch (channel) {
    case MFC_LSA: c->mfc_lsa=v; break;
    case MFC_EAH: c->mfc_eah=v; break;
    case MFC_EAL: c->mfc_eal=v; break;
    case MFC_Size: c->mfc_size=v; break;
    case MFC_TagID: c->mfc_tag=v&31; break;
    case MFC_Cmd:
        assert(v==64 && ++commands==1 && c->mfc_eah==0);
        if(synthetic_io) {
            const uint32_t fields[]={c->mfc_lsa,c->mfc_eal,c->mfc_size,c->mfc_tag,v};
            if(recording)memcpy(synthetic_transfer,fields,sizeof fields);
            else assert(!memcmp(synthetic_transfer,fields,sizeof fields));
            assert(c->mfc_size<=SPU_LS_SIZE-c->mfc_lsa);
            for(unsigned i=0;i<c->mfc_size;++i)c->ls[c->mfc_lsa+i]=(uint8_t)(c->mfc_eal^(i*31));
            break;
        }
        assert(c->mfc_lsa==expected->mfc_lsa && c->mfc_eal==expected->mfc_eal);
        assert(c->mfc_size==expected->mfc_size && c->mfc_tag==expected->mfc_tag);
        assert(c->mfc_size<=SPU_LS_SIZE-c->mfc_lsa);
        memcpy(c->ls+c->mfc_lsa,expected->ls+c->mfc_lsa,c->mfc_size);
        break;
    default: assert(false);
    }
}
}
#include "image39_request_oracle.inc"
#include "../port/native_image39_request.h"

static void load(FILE* f,spu_context* c) {
    *c={};c->ls=c->ls_store;uint32_t x[22];
    assert(fread(x,sizeof x,1,f)==1);
    c->image_id=x[0];c->host_depth=x[1];c->policy_mode=x[2];c->resident_ovl=x[3];
    c->irq_saved=x[4];c->irq_frame=reinterpret_cast<void*>(uintptr_t(x[5]));
    c->event_mask=x[6];c->list_stall_mask=x[7];c->int_enable=x[8];c->pc=x[9];
    c->status=x[10];c->stop_code=x[11];c->mfc_lsa=x[12];c->mfc_eah=x[13];c->mfc_eal=x[14];
    c->mfc_size=x[15];c->mfc_tag=x[16];c->mfc_tag_mask=x[17];c->mfc_tag_status=x[18];
    c->resv_valid=x[19];c->atomic_stat=x[20];c->event_status=x[21];
    assert(fread(c->gpr,sizeof c->gpr,1,f)==1 && fread(c->ls,SPU_LS_SIZE,1,f)==1);
}
static void reference(spu_context* c) {
    const auto ret=c->gpr[0]._u32[0];g_spu_trampoline_fn=nullptr;
    buzz_job_c0bc_spu_func_00003A10(c);spu_drain_call(c,ret);c->pc=ret;
}
static void controls(const spu_context* a,const spu_context* b) {
    assert(a->pc==b->pc && a->status==b->status && a->stop_code==b->stop_code);
    assert(a->mfc_lsa==b->mfc_lsa && a->mfc_eah==b->mfc_eah && a->mfc_eal==b->mfc_eal);
    assert(a->mfc_size==b->mfc_size && a->mfc_tag==b->mfc_tag);
    assert(a->mfc_tag_mask==b->mfc_tag_mask && a->mfc_tag_status==b->mfc_tag_status);
    assert(a->event_mask==b->event_mask && a->event_status==b->event_status);
    assert(a->resv_valid==b->resv_valid && a->atomic_stat==b->atomic_stat);
}
int main(int argc,char** argv) {
    assert(argc==2);buzz_image39_request_mode=1;auto a=std::make_unique<spu_context>(),b=std::make_unique<spu_context>(),gold=std::make_unique<spu_context>();
    unsigned cases=0,native=0,hits=0,misses=0,fallback=0;std::filesystem::path seed_path;
    for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
        if(file.path().extension()!=".bin")continue;
        FILE* f=fopen(file.path().string().c_str(),"rb");assert(f);char magic[8];
        assert(fread(magic,8,1,f)==1 && !memcmp(magic,"B39REQ01",8));
        load(f,a.get());load(f,gold.get());assert(fgetc(f)==EOF);fclose(f);
        *b=*a;b->ls=b->ls_store;expected=gold.get();commands=0;reference(a.get());
        assert(!memcmp(a->gpr,gold->gpr,sizeof a->gpr) && !memcmp(a->ls,gold->ls,SPU_LS_SIZE));controls(a.get(),gold.get());
        const unsigned reference_commands=commands;commands=0;
        if(!buzz_image39_native_request(b.get())){++fallback;continue;}
        if(seed_path.empty())seed_path=file.path();
        ++native;hits+=commands==0;misses+=commands!=0;assert(commands==reference_commands);
        assert(!memcmp(&b->gpr[3],&gold->gpr[3],16));controls(b.get(),gold.get());
        for(unsigned r=0;r<128;++r)if(r<2||r>=80)assert(!memcmp(&b->gpr[r],&gold->gpr[r],16));
        const auto sp=b->gpr[1]._u32[0];
        assert(!memcmp(b->ls+sp-112,gold->ls+sp-112,16));
        for(unsigned i=0;i<SPU_LS_SIZE;++i)
            if(i<sp-144||i>=sp+32)assert(b->ls[i]==gold->ls[i]);
        ++cases;
    }
    assert(native>600 && hits && misses && fallback);
    printf("PASS captured requests=%u native=%u hits=%u misses=%u fallback=%u (oracle LS/registers, native live state/ordered GET)\n",cases+fallback,native,hits,misses,fallback);
    FILE* seed_file=fopen(seed_path.string().c_str(),"rb");assert(seed_file);fseek(seed_file,8,SEEK_SET);load(seed_file,a.get());fclose(seed_file);
    unsigned rejected=0;
    auto reject=[&](auto mutate){
        *b=*a;b->ls=b->ls_store;mutate(b.get());
        std::vector<unsigned char> before(sizeof *b);memcpy(before.data(),b.get(),sizeof *b);
        commands=0;assert(!buzz_image39_native_request(b.get()));assert(!commands);
        assert(!memcmp(before.data(),b.get(),sizeof *b));++rejected;
    };
    reject([](spu_context* c){c->gpr[0]._u32[0]=0x1234;});
    reject([](spu_context* c){c->image_id=40;});
    reject([](spu_context* c){c->host_depth=0;});
    reject([](spu_context* c){c->policy_mode=1;});
    reject([](spu_context* c){c->resident_ovl=1;});
    reject([](spu_context* c){c->irq_saved=1;});
    reject([](spu_context* c){c->irq_frame=reinterpret_cast<void*>(1);});
    reject([](spu_context* c){c->event_mask=1;});
    reject([](spu_context* c){c->list_stall_mask=1;});
    reject([](spu_context* c){c->int_enable=1;});
    reject([](spu_context* c){c->gpr[3]._u32[0]|=1;});
    reject([](spu_context* c){c->gpr[6]._u32[0]=c->gpr[3]._u32[0];});
    reject([](spu_context* c){c->gpr[1]._u32[0]=32;});
    reject([](spu_context* c){c->gpr[1]._u32[0]=0x5DA0;});
    reject([](spu_context* c){spu_ls_write128(c,c->gpr[3]._u32[0]+64,spu_il(0));});
    reject([](spu_context* c){spu_ls_write128(c,c->gpr[3]._u32[0]+48,spu_il(4097));});
    reject([](spu_context* c){spu_ls_write128(c,c->gpr[3]._u32[0]+16,c->gpr[3]);});
    auto miss=[](spu_context* c){
        const auto d=c->gpr[3]._u32[0];c->gpr[4]=spu_il(0x10000);
        spu_ls_write128(c,d+48,spu_il(1));spu_ls_write128(c,d+64,spu_il(1));
        spu_ls_write128(c,spu_ls_read128(c,d+16)._u32[0],spu_il(0));
    };
    reject([&](spu_context* c){miss(c);c->gpr[5]=spu_il(32);});
    reject([&](spu_context* c){miss(c);spu_ls_write128(c,c->gpr[3]._u32[0]+32,spu_il(17));});
    native_guard=false;reject([](spu_context*){});native_guard=true;
    std::vector<unsigned char> external(SPU_LS_SIZE,0x5a);
    reject([&](spu_context* c){c->ls=external.data();});
    for(auto byte:external)assert(byte==0x5a);
    printf("PASS %u unsupported mutations fall back before LS/control/channel mutation\n",rejected);
    // Independently run the retained algorithm over duplicate keys, age ties,
    // signed tick wrap, replacement/full/available slots and packed tail lanes.
    synthetic_io=true;auto seed=std::make_unique<spu_context>(*a);seed->ls=seed->ls_store;
    unsigned random=0x39c0bc,admitted=0,unsupported=0,synthetic_hits=0,synthetic_misses=0;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned trial=0;trial<1024;++trial) {
        *a=*seed;a->ls=a->ls_store;const auto d=a->gpr[3]._u32[0];
        const unsigned used=1+next()%8,capacity=used+(trial&1);
        a->gpr[4]=spu_il(0x200000);a->gpr[5]=spu_il(5);
        spu_ls_write128(a.get(),d,spu_il(0x22000));spu_ls_write128(a.get(),d+16,spu_il(0x30000));
        spu_ls_write128(a.get(),d+32,spu_il(64));spu_ls_write128(a.get(),d+48,spu_il((int)capacity));
        spu_ls_write128(a.get(),d+64,spu_il((int)used));
        const uint32_t tick=trial%4==0?0x7fffffff:trial%4==1?0xffffffff:next();
        spu_ls_write128(a.get(),d+80,spu_il((int)tick));
        const u128 mask=spu_ls_read128(a.get(),0x5D90);
        for(unsigned i=0;i<used;++i) {
            u128 key=a->gpr[4];if(trial%3==0 || i%2)key._u32[0]^=16*(i+1);
            const uint32_t age=trial%7==0?tick:next();
            spu_ls_write128(a.get(),0x30000+16*i,spu_shufb(key,spu_il((int)age),mask));
        }
        *b=*a;b->ls=b->ls_store;buzz_image39_request_plan plan;
        if(!buzz_image39_prepare_request(b.get(),&plan)) {
            commands=0;assert(!buzz_image39_native_request(b.get()) && !commands);
            assert(!memcmp(a->ls,b->ls,SPU_LS_SIZE)&&!memcmp(a->gpr,b->gpr,sizeof a->gpr));++unsupported;continue;
        }
        commands=0;recording=true;reference(a.get());const unsigned count=commands;
        commands=0;recording=false;assert(buzz_image39_native_request(b.get()));
        assert(commands==count);synthetic_hits+=!count;synthetic_misses+=!!count;
        assert(!memcmp(&a->gpr[3],&b->gpr[3],16));controls(a.get(),b.get());
        for(unsigned r=0;r<128;++r)if(r<2||r>=80)assert(!memcmp(&a->gpr[r],&b->gpr[r],16));
        const auto sp=a->gpr[1]._u32[0];
        assert(!memcmp(a->ls+sp-112,b->ls+sp-112,16));
        for(unsigned i=0;i<SPU_LS_SIZE;++i)if(i<sp-144||i>=sp+32)assert(a->ls[i]==b->ls[i]);
        ++admitted;
    }
    assert(admitted>700 && unsupported && synthetic_hits && synthetic_misses);
    printf("PASS synthetic requests=%u hits=%u misses=%u unsupported=%u\n",admitted,synthetic_hits,synthetic_misses,unsupported);
    unsigned limits=0;
    for(unsigned used:{1u,7u,8u,31u,32u,255u,256u,4095u,4096u})for(unsigned kind=0;kind<6;++kind) {
        *a=*seed;a->ls=a->ls_store;
        const unsigned d=0x23000,table=0x10000;
        a->gpr[3]=spu_il(d);a->gpr[4]=spu_il(0x400000);a->gpr[5]=spu_il(5);
        a->gpr[6]=spu_il(0x23100);a->gpr[1]=spu_il(0x3f000);
        if(kind==1 || kind==2)a->gpr[4]._u32[0]|=(used%4)*4;
        u128 stride=spu_il(16);
        for(unsigned lane=1;lane<4;++lane) {
            a->gpr[4]._u32[lane]=next();a->gpr[5]._u32[lane]=next();
            stride._u32[lane]=next()&0xffff0000u;
        }
        if(kind==5)stride._u32[1]|=1;
        spu_ls_write128(a.get(),d,spu_il(kind==4?0x3f000-112:0x24000));spu_ls_write128(a.get(),d+16,spu_il(table));
        spu_ls_write128(a.get(),d+32,stride);spu_ls_write128(a.get(),d+48,spu_il(4096));
        spu_ls_write128(a.get(),d+64,spu_il(used));
        spu_ls_write128(a.get(),d+80,spu_il(kind==3?-1:1000));
        const auto mask=spu_ls_read128(a.get(),0x5d90);
        for(unsigned i=0;i<used;++i) {
            u128 key=a->gpr[4];
            const bool hit=kind==1?i==0:kind==2?(i==0||i==used-1):kind>=4?i==0:false;
            if(!hit)key._u32[0]^=16*(i+1);
            for(unsigned lane=1;lane<4;++lane)key._u32[lane]=next();
            const uint32_t age=(kind==3?0x80000000u:0u)+i%3;
            spu_ls_write128(a.get(),table+16*i,spu_shufb(key,spu_il((int)age),mask));
        }
        // Unused records deliberately match. A rounded-up SIMD tail would
        // select one and change the published result, even inside capacity.
        for(unsigned i=used;i<4096 && i<used+4;++i)
            spu_ls_write128(a.get(),table+16*i,a->gpr[4]);
        *b=*a;b->ls=b->ls_store;buzz_image39_request_plan plan;
        assert(buzz_image39_prepare_request(b.get(),&plan));
        commands=0;recording=true;reference(a.get());const unsigned count=commands;
        commands=0;recording=false;assert(buzz_image39_native_request(b.get()) && commands==count);
        assert(!memcmp(&a->gpr[3],&b->gpr[3],16));controls(a.get(),b.get());
        for(unsigned r=0;r<128;++r)if(r<2||r>=80)assert(!memcmp(&a->gpr[r],&b->gpr[r],16));
        const unsigned sp=a->gpr[1]._u32[0];
        assert(!memcmp(a->ls+sp-112,b->ls+sp-112,16));
        for(unsigned i=0;i<SPU_LS_SIZE;++i)if(i<sp-144||i>=sp+32)assert(a->ls[i]==b->ls[i]);
        ++limits;
    }
    printf("PASS %u cache capacity/search boundaries: 4096 entries, packed fields, result slots and generic guards\n",limits);
}
