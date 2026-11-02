#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_index_control.h"
#include "../port/native_geometry_index_unpack.h"
#include "../port/native_geometry_index_deltas.h"
#include "../port/native_geometry_index_expand.h"
#include "../port/native_geometry_index_relocate.h"
#include "../port/native_geometry_index_triangles.h"
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
void spu_drain_call(spu_context* c,uint32_t link){
    for(unsigned steps=0;g_spu_trampoline_fn;++steps){assert(steps<64);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;
        if(next==spu_indirect_branch){assert((c->pc&0x3ffff)==(link&0x3ffff));return;}next(c);}
    assert((c->pc&0x3ffff)==(link&0x3ffff));
}
}
#include "../analysis/native-geometry/phase3/test-index-control-reference.inc"
namespace original=index_control_oracle;
static unsigned child_mask=31,native_children[5]{},original_children[5]{};
static void child_unpack(spu_context*);static void child_deltas(spu_context*);static void child_expand(spu_context*);
static void child_relocate(spu_context*);static void child_triangles(spu_context*);
static void child_unpack(spu_context* c){
    const auto d=child_deltas,e=original::buzz_job_6eba_spu_func_000077F8;
    if(child_mask&1){GeometryIndexUnpack p;assert(read_geometry_index_unpack(*c,p));unpack_geometry_indices(*c,p,d,e);++native_children[0];}
    else{auto boundary=run_geometry_index_region(*c,original::library.unpack,original::library.deltas,e);g_spu_trampoline_fn=boundary==e?e:d;++original_children[0];}
}
static void child_deltas(spu_context* c){
    const auto e=original::library.expansion_setup;
    if(child_mask&2){GeometryIndexDeltas p;assert(read_geometry_index_deltas(*c,p));reconstruct_geometry_index_deltas(*c,p,e);++native_children[1];}
    else{run_geometry_index_region(*c,original::library.deltas,e);g_spu_trampoline_fn=e;++original_children[1];}
}
static void child_expand(spu_context* c){
    if(child_mask&4){GeometryIndexExpand p;assert(read_geometry_index_expand(*c,p));expand_geometry_indices(*c,p,child_relocate,original::buzz_job_6eba_spu_func_00007DCC);++native_children[2];}
    else{run_geometry_index_region(*c,original::library.expand,original::library.relocate);g_spu_trampoline_fn=child_relocate;++original_children[2];}
}
static void child_relocate(spu_context* c){
    if(child_mask&8){GeometryIndexRelocate p;assert(read_geometry_index_relocate(*c,p));relocate_geometry_index_commands(*c,p,child_triangles);++native_children[3];}
    else{run_geometry_index_region(*c,original::library.relocate,original::library.triangles);g_spu_trampoline_fn=child_triangles;++original_children[3];}
}
static void child_triangles(spu_context* c){
    if(child_mask&16){GeometryIndexTriangles p;assert(read_geometry_index_triangles(*c,p));stitch_geometry_index_triangles(*c,p,original::library.publish);++native_children[4];}
    else{run_geometry_index_region(*c,original::library.triangles,original::library.publish);g_spu_trampoline_fn=original::library.publish;++original_children[4];}
}
static const buzz_geometry_index_functions library={child_unpack,child_deltas,original::buzz_job_6eba_spu_func_000077F8,child_expand,child_relocate,child_triangles,original::library.publish};
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void word(spu_context& c,unsigned at,unsigned value){for(unsigned b=0;b<4;++b)c.ls[at+b]=uint8_t(value>>(24-b*8));}
static void half(spu_context& c,unsigned at,unsigned value){c.ls[at]=uint8_t(value>>8);c.ls[at+1]=uint8_t(value);}
static void scalar_store(unsigned char* ls,unsigned at,u128 value){at&=0x3fff0;for(unsigned lane=0;lane<4;++lane)for(unsigned b=0;b<4;++b)ls[at+lane*4+b]=uint8_t(value._u32[lane]>>(24-b*8));}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0xaea0;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    original::seed_tables(c);const unsigned ts[]={1,2,3,4,8,9,16,31,64,255,1024,2048},ds[]={0,1,31,32,33,64,129,511};
    const unsigned t=ts[trial%12],d=ds[(trial/12)%8],m=(t*3+7)/8+(trial%3),w=d?1+(trial%16):0;
    c.gpr[1]._u32[0]=0x3d000+(trial&15);c.gpr[3]._u32[0]=0x39000+(trial&15);c.gpr[4]._u32[0]=0x10000+16*(trial%4);
    const unsigned links[]={0xaea0,0xaee8,0xaf10,0x7390,0x2738,0x1234};c.gpr[0]._u32[0]=links[trial%6];
    c.ls[0x39006]=uint8_t((trial&1?2:3)<<4);half(c,0x39180,t*3);word(c,0x3917c,0x38000);
    const unsigned base=c.gpr[4]._u32[0];half(c,base,d);half(c,base+2,trial%65536);half(c,base+4,m);c.ls[base+6]=uint8_t(w);
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static bool compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryIndexControl p;const char* why=nullptr;
    if(!read_geometry_index_control(native,p,&why)){fprintf(stderr,"control reject%u: %s\n",trial,why);return false;}
    const unsigned link=reference.gpr[0]._u32[0],stack=reference.gpr[1]._u32[0],depth=reference.host_depth;
    g_spu_trampoline_fn=original::buzz_job_6eba_spu_func_0000AEE8;original::buzz_job_6eba_spu_func_0000AEA0(&reference);
    for(unsigned steps=0;!(reference.pc==link && reference.gpr[1]._u32[0]==stack && g_spu_trampoline_fn==(depth?nullptr:spu_indirect_branch));++steps){
        assert(steps<64 && g_spu_trampoline_fn && g_spu_trampoline_fn!=spu_indirect_branch);const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&reference);
    }
    const auto expected=g_spu_trampoline_fn;g_spu_trampoline_fn=original::buzz_job_6eba_spu_func_0000AEE8;execute_geometry_index_control(native,p,library);const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"control trial%u path%u r%d byte%d LS%X/%u state%s\n",trial,unsigned(p.path),d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned r=0;r<128;++r)if(memcmp(&native.gpr[r],&reference.gpr[r],16)){fprintf(stderr,"r%u native",r);for(auto w:native.gpr[r]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[r]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);}abort();
    }
    assert(g_spu_trampoline_fn==expected && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);return true;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0,rejected=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);if(compare(*native,*reference,trial))++exact;else ++rejected;}
    for(unsigned shape=0;shape<8;++shape)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial+24);const unsigned base=native->gpr[4]._u32[0];
        switch(shape){
        case 0:half(*native,base,0);native->ls[base+6]=16;memcpy(native->ls+0x20000,native->ls+base,16);native->gpr[4]._u32[0]=0x20000;break;
        case 1:native->ls[base+6]=0;half(*native,base,65);break;
        case 2:spu_ls_write128(native.get(),0xda30,spu_splat_u32(0x80808080));break;
        case 3:half(*native,base,0);native->ls[base+6]=0;memcpy(native->ls+0xe680,native->ls+base,16);native->gpr[4]._u32[0]=0xe680;break;
        case 4:half(*native,0x39180,0);native->gpr[3]._u32[0]=(native->gpr[1]._u32[0]&~15u)-16;half(*native,0x3cff0+0x180,0);break;
        case 5:half(*native,0x39180,0);native->gpr[4]._u32[0]=0xffffffff;break;
        case 6:native->ls[0x39006]=0xf0;native->gpr[4]._u32[0]=0xffffffff;break;
        case 7:half(*native,base,511);native->ls[base+6]=15;break;
        }
        copy(*reference,*native);assert(compare(*native,*reference,50000+shape*64+trial));++exact;
    }
    for(unsigned mask=0;mask<32;++mask)for(unsigned trial=0;trial<24;++trial){child_mask=mask;seed(*native,trial+36);copy(*reference,*native);if(compare(*native,*reference,10000+mask*24+trial))++exact;else ++rejected;}
    child_mask=31;
    auto expected_ls=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    for(unsigned trial=0;trial<256;++trial){seed(*native,trial);if(trial&1)half(*native,0x39180,0);else native->ls[0x39006]=uint8_t((trial%4?1:15)<<4);
        const u128 stack=native->gpr[1],return_link=native->gpr[0],saved80=native->gpr[80],saved81=native->gpr[81];
        memcpy(expected_ls.get(),native->ls,SPU_LS_SIZE);const unsigned sp=stack._u32[0];
        scalar_store(expected_ls.get(),sp-16,saved80);scalar_store(expected_ls.get(),sp-32,saved81);
        scalar_store(expected_ls.get(),sp+16,return_link);scalar_store(expected_ls.get(),sp-64,stack);
        if(!(trial&1)){
            const unsigned at=((native->gpr[3]._u32[0]+0x50)&0x3fff0)+((sp-64)&12),pointer=native->gpr[4]._u32[0];
            for(unsigned b=0;b<4;++b)expected_ls[at+b]=uint8_t(pointer>>(24-b*8));
        }
        copy(*reference,*native);assert(compare(*native,*reference,20000+trial));++exact;
        assert(!memcmp(native->ls,expected_ls.get(),SPU_LS_SIZE) && !memcmp(&native->gpr[1],&stack,16) &&
            !memcmp(&native->gpr[0],&return_link,16) && !memcmp(&native->gpr[80],&saved80,16) && !memcmp(&native->gpr[81],&saved81,16));
    }
    seed(*native,0);FILE* fixture=fopen("analysis/native-geometry/phase3/index-audit-v1/pc_AEA0.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);assert(compare(*native,*reference,30000));++exact;
    for(unsigned trial=0;trial<20;++trial){seed(*native,36);const unsigned base=native->gpr[4]._u32[0];
        switch(trial){
        case 0:native->ls=reference->ls_store;break;case 1:native->int_enable=1;native->event_status=native->event_mask=1;break;
        case 2:native->gpr[3]._u32[0]=0;break;case 3:native->gpr[3]._u32[0]=SPU_LS_SIZE-16;break;
        case 4:native->gpr[1]._u32[0]=0xe680;break;case 5:native->gpr[1]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[3]._u32[0]=0x3cff0;half(*native,0x3d170,3);break;
        case 7:native->gpr[1]._u32[0]=0xe680+80;break;case 8:native->gpr[4]._u32[0]=base+1;break;
        case 9:native->gpr[4]._u32[0]=0xd000;break;case 10:native->gpr[4]._u32[0]=SPU_LS_SIZE;break;
        case 11:native->gpr[4]._u32[0]=0x3cf80;break;case 12:half(*native,0x39180,1);break;
        case 13:native->ls[base+6]=17;break;case 14:word(*native,0x3917c,0xffffffff);break;
        case 15:half(*native,base,511);native->ls[base+6]=8;word(*native,0x3917c,base+64);break;
        case 16:half(*native,base,0);native->ls[base+6]=0;half(*native,base+4,2000);word(*native,0x3917c,base+32000);break;
        case 17:half(*native,base,0);native->ls[base+6]=0;half(*native,base+4,0);word(*native,0x3917c,base+16);break;
        case 18:half(*native,base+4,65535);break;
        case 19:native->gpr[4]._u32[0]=0x38f00;memcpy(native->ls+0x38f00,native->ls+base,16);half(*native,0x39180,12000);break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryIndexControl p;p.width=123;p.path=IndexControlPath::high;const char* why=nullptr;
        if(read_geometry_index_control(*native,p,&why)){fprintf(stderr,"reject trial%u unexpectedly eligible\n",trial);abort();}
        assert(why && p.width==123 && p.path==IndexControlPath::high && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    for(unsigned i=0;i<5;++i)assert(native_children[i] && original_children[i]);
    printf("PASS index control: %u strict full-state/LS/return; %u unsupported seeds;32 child switch combinations;256 independent early/publication models;20 unchanged rejects\n",exact,rejected);
}
