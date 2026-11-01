#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_alternate_cull.h"
#include "../port/native_function_check.h"
#include "../port/native_geometry_transform_control.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_drain_call(spu_context* c,uint32_t link){for(unsigned steps=0;g_spu_trampoline_fn && g_spu_trampoline_fn!=spu_indirect_branch;++steps){assert(steps<32);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);}if(g_spu_trampoline_fn==spu_indirect_branch && c->pc==link)g_spu_trampoline_fn=nullptr;assert(!g_spu_trampoline_fn && c->pc==link);}
}
#include "../analysis/native-geometry/phase3/test-alternate-cull-reference.inc"
static void transform_control_test_bridge(spu_context*,buzz_geometry_job_fn,buzz_geometry_job_fn,buzz_geometry_job_fn);
#include "../analysis/native-geometry/phase3/test-transform-control-reference.inc"

static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial,unsigned n)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x69e8;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    c.gpr[15]._u32[0]=n-2;c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;
    c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryAlternateCull p;assert(read_geometry_alternate_cull(native,p));
    g_spu_trampoline_fn=spu_indirect_branch;alternate_cull_oracle::buzz_job_6eba_spu_func_000069E8(&reference);
    for(unsigned steps=0;g_spu_trampoline_fn!=alternate_cull_oracle::buzz_job_6eba_spu_func_00006958;++steps){
        assert(steps<2*p.iterations+1);auto next=g_spu_trampoline_fn;
        assert(next==alternate_cull_oracle::buzz_job_6eba_spu_func_00006B18 || next==alternate_cull_oracle::buzz_job_6eba_spu_func_00006B10);
        g_spu_trampoline_fn=nullptr;next(&reference);
    }
    g_spu_trampoline_fn=spu_indirect_branch;cull_geometry_alternate(native,p,alternate_cull_oracle::buzz_job_6eba_spu_func_00006958);
    const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"alternate trial%u n%u r%d byte%d LS%X/%u state%s\n",trial,p.iterations,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);}abort();
    }
    assert(g_spu_trampoline_fn==alternate_cull_oracle::buzz_job_6eba_spu_func_00006958 && native.pc==0x6958 && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
static void geometry(spu_context& c,unsigned triangles,unsigned mode,unsigned shape,unsigned bias,bool prepare=true)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.pc=0x69e8;c.image_id=41;alternate_cull_oracle::seed_tables(c);
    c.gpr[1]=spu_ila(0x3d000-480+bias);c.gpr[81]=spu_ila(0x30000+bias);c.gpr[80]=spu_il(mode);
    u128 config{};config._u32[0]=0x10000000;spu_ls_write128(&c,0x30000,config);
    u128 indices{};indices._u32[0]=0x18000;indices._u32[2]=0x18000;indices._u32[3]=0x20000;spu_ls_write128(&c,0x30050,indices);
    u128 count{};count._u32[0]=triangles*3u<<16;spu_ls_write128(&c,0x30180,count);c.ls[0x30140]=5;
    for(unsigned v=0;v<3;++v){u128 point{};point._f32[0]=v==1?16.0f:shape==2&&v==2?32.0f:0.0f;point._f32[1]=v==2&&shape!=2?16.0f:0.0f;
        if(shape==7){point._f32[0]*=1.0f/128;point._f32[1]*=1.0f/128;}
        point._u32[2]=shape==5?1:shape==6&&v==0?1:0;point._u32[3]=shape==3?0x80010001:0x80000000u|((v+1)*0x10001);
        spu_ls_write128(&c,0x20000+v*16,point);
    }
    for(unsigned triangle=0;triangle<triangles;++triangle){const bool reverse=shape==1 || (shape==4 && (triangle&1));const unsigned ids[]={0,reverse?2u:1u,reverse?1u:2u};
        for(unsigned v=0;v<3;++v){c.ls[0x18000+triangle*6+v*2]=0;c.ls[0x18001+triangle*6+v*2]=uint8_t(ids[v]);}
    }
    if(!prepare){c.gpr[1]=spu_ai(c.gpr[1],480);c.gpr[3]=c.gpr[81];c.gpr[4]=spu_il(mode);c.gpr[0]=spu_ila(0x66c8);c.pc=0x6608;return;}
    prepare_geometry_cull_control(c);c.gpr[11]=mode==2?spu_il(0):spu_ilhu(0x8000);c.gpr[82]=spu_il(mode==1?0:-1);c.pc=0x69e8;
}
static void vertex_model(spu_context* c){SPU_RET(c);}
static void ordinary_cull(spu_context*){abort();}
static void original_footer(spu_context* c){transform_control_oracle::buzz_job_6eba_spu_func_00006958(c);}
static void native_footer(spu_context* c){GeometryTransformFinish p;assert(read_geometry_transform_finish(*c,p));finish_geometry_transform(*c,p);}
static void original_alternate(spu_context* c){
    g_spu_trampoline_fn=nullptr;alternate_cull_oracle::buzz_job_6eba_spu_func_000069E8(c);
    for(unsigned steps=0;g_spu_trampoline_fn!=alternate_cull_oracle::buzz_job_6eba_spu_func_00006958;++steps){assert(steps<16386);auto next=g_spu_trampoline_fn;assert(next);g_spu_trampoline_fn=nullptr;next(c);}
    g_spu_trampoline_fn=original_footer;
}
static void native_alternate(spu_context* c){GeometryAlternateCull p;assert(read_geometry_alternate_cull(*c,p));cull_geometry_alternate(*c,p,native_footer);}
static const buzz_geometry_transform_functions original_library={vertex_model,ordinary_cull,original_alternate,original_footer,transform_control_oracle::buzz_job_6eba_spu_func_000066C8};
static const buzz_geometry_transform_functions native_library={vertex_model,ordinary_cull,native_alternate,native_footer,transform_control_oracle::buzz_job_6eba_spu_func_000066C8};
static void transform_control_test_bridge(spu_context* c,buzz_geometry_job_fn root,buzz_geometry_job_fn boundary,buzz_geometry_job_fn replacement){g_spu_trampoline_fn=nullptr;root(c);assert(c->pc==0x6958 && g_spu_trampoline_fn==boundary);g_spu_trampoline_fn=replacement;}
static void compare_controller(spu_context& native,spu_context& reference,unsigned trial){
    GeometryTransformControl p;assert(read_geometry_transform_control(native,p));const unsigned stack=native.gpr[1]._u32[0];
    transform_control_oracle::children=&original_library;g_spu_trampoline_fn=nullptr;transform_control_oracle::buzz_job_6eba_spu_func_00006608(&reference);
    for(unsigned steps=0;!(reference.gpr[1]._u32[0]==stack && reference.pc==reference.gpr[0]._u32[0] && g_spu_trampoline_fn==(reference.host_depth?nullptr:spu_indirect_branch));++steps){assert(steps<32 && g_spu_trampoline_fn);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);}
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=spu_indirect_branch;const auto path=execute_geometry_transform_control(native,p,native_library);assert(path==TransformControlPath::alternate);
    const auto d=compare_function_state(native,reference);if(!d.exact()){fprintf(stderr,"composed alternate trial%u r%d byte%d LS%X/%u state%s\n",trial,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");abort();}
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0;
    const unsigned counts[]={1,2,3,4,5,8,16,31,64,128,256};
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial,counts[trial%11]);copy(*reference,*native);compare(*native,*reference,trial);++exact;}
    for(unsigned shape=0;shape<8;++shape)for(unsigned trial=0;trial<64;++trial){seed(*native,trial,counts[trial%11]);
        switch(shape){
        case 0:native->gpr[37]._u32[0]=native->gpr[13]._u32[0];native->gpr[43]=spu_il(0);break;
        case 1:native->gpr[37]._u32[0]=native->gpr[10]._u32[0];break;
        case 2:native->gpr[37]._u32[0]=0xd980+(trial&15);break;
        case 3:native->gpr[37]._u32[0]=0;native->gpr[64]=native->gpr[91]=spu_il(-1);native->gpr[57]=spu_il(0);break;
        case 4:native->gpr[37]._u32[0]=0xffffffff;native->gpr[43]._u32[0]=0x10;break;
        case 5:native->gpr[3]._u32[0]=native->gpr[7]._u32[0]=native->gpr[8]._u32[0]=native->gpr[37]._u32[0];break;
        case 6:native->gpr[83]=spu_il(0);native->gpr[43]=spu_il(0);break;
        case 7:native->gpr[48]._u32[0]=0xfffffff0;native->gpr[83]=spu_il(24);break;
        }
        copy(*reference,*native);compare(*native,*reference,10000+shape*64+trial);++exact;
    }
    for(unsigned trial=0;trial<8;++trial){seed(*native,trial,8192);copy(*reference,*native);compare(*native,*reference,20000+trial);++exact;}
    const unsigned triangles[]={1,2,3,4,5,7,8,9,12,13,16,17};unsigned numeric=0;
    for(unsigned count:triangles)for(unsigned mode=1;mode<=3;++mode)for(unsigned shape=0;shape<8;++shape){
        geometry(*native,count,mode,shape,numeric&15);copy(*reference,*native);compare(*native,*reference,30000+numeric);++exact;++numeric;
        unsigned kept=0;unsigned char expected[17*6]{};
        for(unsigned triangle=0;triangle<count;++triangle){const bool reverse=shape==1 || (shape==4 && (triangle&1));
            const bool rejected=shape==2 || shape==3 || shape==5 || shape==7 || (mode==2 && reverse) || (mode==3 && !reverse);
            if(!rejected){expected[kept*6+1]=0;expected[kept*6+3]=reverse?2:1;expected[kept*6+5]=reverse?1:2;++kept;}
        }
        if(native->gpr[50]._u32[0]!=kept || memcmp(native->ls+0x18000,expected,kept*6)){
            fprintf(stderr,"numeric count%u mode%u shape%u got%u expected%u\n",count,mode,shape,native->gpr[50]._u32[0],kept);abort();
        }
    }
    unsigned composed=0;
    for(unsigned count:triangles)for(unsigned mode=1;mode<=3;++mode)for(unsigned shape=0;shape<8;++shape){geometry(*native,count,mode,shape,composed&15,false);native->host_depth=composed%3;copy(*reference,*native);compare_controller(*native,*reference,composed);++composed;++exact;}
    const uint32_t special[]={0,0x80000000,1,0x807fffff,0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00001,0xffc00001,0x7fffffff,0xffffffff};
    for(unsigned trial=0;trial<256;++trial){geometry(*native,4,trial%3+1,0,trial&15);
        for(unsigned v=0;v<3;++v){u128 point{};for(unsigned lane=0;lane<4;++lane)point._u32[lane]=special[(trial+v*3+lane*5)&15];spu_ls_write128(native.get(),0x20000+16*v,point);}
        copy(*reference,*native);compare(*native,*reference,40000+trial);++exact;
    }
    for(unsigned failure=0;failure<4;++failure){seed(*native,0,2);switch(failure){case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;case 2:native->gpr[15]._u32[0]=0xfffffffe;break;case 3:native->gpr[15]._u32[0]=8191;break;}
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryAlternateCull p;p.iterations=77;const char* why=nullptr;
        assert(!read_geometry_alternate_cull(*native,p,&why) && why && p.iterations==77 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS alternate cull: %u strict full-state/LS/handoff;288 independent geometric cases;288 original-control/data composition cases;256 special FP;4 unchanged rejects\n",exact);
}
