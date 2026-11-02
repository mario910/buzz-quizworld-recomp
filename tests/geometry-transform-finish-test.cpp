#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_transform_finish.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){if(g_spu_trampoline_fn==spu_indirect_branch && c->pc==link)g_spu_trampoline_fn=nullptr;assert(!g_spu_trampoline_fn && c->pc==link);}
}
#include "../analysis/native-geometry/phase3/test-transform-finish-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x6958;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    c.gpr[1]._u32[0]=0x39000-480+(trial&15);c.gpr[81]._u32[0]=0x3d000+((trial/16)&15);
    c.ls[0x3d000]=uint8_t((c.ls[0x3d000]&~16u)|(trial&1?16:0));
    c.gpr[50]._u32[0]=trial%65536;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryTransformFinish p;const char* why=nullptr;assert(read_geometry_transform_finish(native,p,&why));
    g_spu_trampoline_fn=spu_indirect_branch;transform_finish_oracle::buzz_job_6eba_spu_func_00006958(&reference);
    assert(g_spu_trampoline_fn==transform_finish_oracle::buzz_job_6eba_spu_func_000066C8);g_spu_trampoline_fn=nullptr;
    transform_finish_oracle::buzz_job_6eba_spu_func_000066C8(&reference);const auto expected=g_spu_trampoline_fn;
    g_spu_trampoline_fn=spu_indirect_branch;finish_geometry_transform(native,p);const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"finish trial%u publish%d r%d byte%d LS%X/%u state%s\n",trial,p.publish,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);}abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
static void scalar_store(unsigned char* ls,unsigned at,u128 v){at&=0x3fff0;for(unsigned lane=0;lane<4;++lane)for(unsigned b=0;b<4;++b)ls[at+lane*4+b]=uint8_t(v._u32[lane]>>(24-b*8));}
static u128 scalar_load(const unsigned char* ls,unsigned at){u128 v{};at&=0x3fff0;for(unsigned lane=0;lane<4;++lane)for(unsigned b=0;b<4;++b)v._u32[lane]=(v._u32[lane]<<8)|ls[at+lane*4+b];return v;}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);compare(*native,*reference,trial);++exact;}
    for(unsigned shape=0;shape<8;++shape)for(unsigned trial=0;trial<64;++trial){seed(*native,trial);
        const unsigned manager=native->gpr[81]._u32[0]&~15u;
        switch(shape){
        case 0:native->gpr[1]._u32[0]=manager+0x170-480+(trial&15);break;
        case 1:native->gpr[1]._u32[0]=manager+0x50-480-16+(trial&15);break;
        case 2:native->gpr[1]._u32[0]=manager+0x180+0x190-480+(trial&15);break;
        case 3:native->gpr[1]._u32[0]=0xfffffff0+(trial&15);break;
        case 4:native->gpr[1]._u32[0]=SPU_LS_SIZE-16+(trial&15);break;
        case 5:native->gpr[50]._u32[0]=0xffffffff;break;
        case 6:native->gpr[81]._u32[0]=0xe680+(trial&15);break;
        case 7:native->gpr[81]._u32[0]=SPU_LS_SIZE-0x190+(trial&15);break;
        }
        copy(*reference,*native);compare(*native,*reference,10000+shape*64+trial);++exact;
    }
    seed(*native,0);FILE* fixture=fopen("analysis/native-geometry/phase3/transform-audit-v1/pc_6958.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);compare(*native,*reference,20000);++exact;
    auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    // Independent byte publications: insert 3*K modulo u16, then optionally
    // insert align16(old_word+6*K modulo u32). Restore every frame quad only
    // AFTER these writes, including manager/return/nonvolatile aliases.
    for(unsigned shape=0;shape<5;++shape)for(unsigned trial=0;trial<64;++trial){seed(*native,trial);
        const unsigned manager=native->gpr[81]._u32[0]&~15u;
        if(shape==1)native->gpr[1]._u32[0]=manager+0x170-480+(trial&15);
        if(shape==2)native->gpr[1]._u32[0]=manager+0x50-480-16+(trial&15);
        if(shape==3)native->gpr[1]._u32[0]=manager+0x180+0x190-480+(trial&15);
        if(shape==4)native->gpr[1]._u32[0]=0xfffffff0+(trial&15);
        const unsigned sp=native->gpr[1]._u32[0],kept=native->gpr[50]._u32[0],count=kept*3;u128 counts{};
        for(unsigned lane=0;lane<4;++lane)counts._u32[lane]=native->gpr[50]._u32[lane]*3;
        memcpy(expected.get(),native->ls,SPU_LS_SIZE);unsigned at=manager+0x180+(sp&14);
        expected[at]=uint8_t(count>>8);expected[at+1]=uint8_t(count);
        if(!(native->ls[manager]&16)){
            const u128 packet=scalar_load(expected.get(),manager+0x50);u128 end{};
            for(unsigned lane=0;lane<4;++lane)end._u32[lane]=(packet._u32[lane]+counts._u32[lane]*2+15)&~15u;
            const unsigned dest=manager+0x50+((sp+8)&12);for(unsigned b=0;b<4;++b)expected[dest+b]=uint8_t(end._u32[0]>>(24-b*8));
        }
        const u128 return_link=scalar_load(expected.get(),sp+480+16);u128 restored[25];
        for(unsigned reg=80;reg<=103;++reg)restored[reg-80]=scalar_load(expected.get(),sp+480-(reg-79)*16);
        restored[24]=scalar_load(expected.get(),sp+480-0x190);
        copy(*reference,*native);compare(*native,*reference,30000+shape*64+trial);++exact;
        assert(!memcmp(native->ls,expected.get(),SPU_LS_SIZE) && native->gpr[3]._u32[0]==count && !memcmp(&native->gpr[0],&return_link,16));
        for(unsigned reg=80;reg<=103;++reg)assert(!memcmp(&native->gpr[reg],&restored[reg-80],16));assert(!memcmp(&native->gpr[126],&restored[24],16));
    }
    for(unsigned trial=0;trial<6;++trial){seed(*native,0);switch(trial){case 0:native->ls=reference->ls_store;break;
        case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;case 2:native->gpr[81]._u32[0]=0;break;
        case 3:native->gpr[81]._u32[0]=0xe670;break;case 4:native->gpr[81]._u32[0]=SPU_LS_SIZE;break;case 5:native->gpr[81]._u32[0]=SPU_LS_SIZE-0x180;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryTransformFinish p;p.publish=true;const char* why=nullptr;
        assert(!read_geometry_transform_finish(*native,p,&why) && why && p.publish && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS transform finish: %u strict full-state/LS/return;320 independent publication/frame models;6 unchanged rejects\n",exact);
}
