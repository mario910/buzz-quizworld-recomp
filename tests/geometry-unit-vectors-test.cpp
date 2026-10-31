#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_unit_vectors.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_drain_call(spu_context*,uint32_t){abort();}
void spu_img_restore(spu_context*,int32_t){abort();}
}
#include "../analysis/native-geometry/phase3/test-unit-vectors-reference.inc"
static uint8_t fixture[264192];
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& ctx,unsigned trial)
{
    memset(&ctx,0,sizeof ctx);ctx.ls=ctx.ls_store;ctx.image_id=41;ctx.pc=0x9114;ctx.host_depth=2;
    uint32_t random=trial+1;
    for(auto& r:ctx.gpr)for(auto& w:r._u32)w=random_word(random);
    for(unsigned i=0;i<SPU_LS_SIZE;++i)ctx.ls[i]=uint8_t(random_word(random));
    memcpy(ctx.ls,fixture+2048,0xe680);
    const unsigned constants[]={96,87,85,86,89,88,97,109,112,108,102,101,110};
    for(unsigned r:constants)memcpy(&ctx.gpr[r],fixture+r*16,16);
    ctx.gpr[1]._u32[0]=0x3d000;ctx.gpr[121]._u32[0]=1;
    ctx.gpr[107]._u32[0]=1+(trial/16)%16;ctx.gpr[106]._u32[0]=3+(trial/256)%8;
    ctx.gpr[42]._u32[0]=trial%16;
    const unsigned counts[]={0,1,2,3,4,7,15,31};
    const unsigned groups=counts[(trial/4)%8],input_step=256,output_step=1024;
    for(unsigned lane=0;lane<4;++lane){
        ctx.gpr[41]._u32[lane]=0x24000+lane*256+trial%16;
        ctx.gpr[80]._u32[lane]=0x24000+groups*output_step+240;
        ctx.gpr[81]._u32[lane]=output_step;ctx.gpr[83]._u32[lane]=input_step;
        ctx.gpr[82]._u32[lane]=0x3c000+(trial&1?lane*16:0);
    }
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        ctx.gpr[bases[row]]._u32[0]=0x10000+row*0x4000;
        u128 pointer=ctx.gpr[row+4];pointer._u32[0]=ctx.gpr[bases[row]]._u32[0]+16;
        spu_ls_write128(&ctx,0x3d070-row*16,pointer);
    }
    u128 table=ctx.gpr[25];table._u32[0]=0xddf0;spu_ls_write128(&ctx,0x3d0c0,table);
    // Every source and speculative byte has a valid selector. Independent
    // exhaustive packed coordinates below also cover all twelve row mappings.
    for(unsigned at=0x10000;at<0x22000;++at)ctx.ls[at]=uint8_t((ctx.ls[at]&0xf0)|((ctx.ls[at]&15)%12));
    ctx.int_enable=trial&1;ctx.resv_valid=trial&1;ctx.mfc_tag_status=trial*0x123;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof native);reference.ls=reference.ls_store;
    GeometryUnitStream p;if(!read_geometry_unit_vectors(native,p)){fprintf(stderr,"unit preflight rejected trial%u\n",trial);abort();}
    buzz_job_6eba_spu_func_00009114(&reference);unsigned steps=0;
    while(g_spu_trampoline_fn && reference.pc!=0x83c8){assert(++steps<65536);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(&reference);}
    assert(reference.pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);g_spu_trampoline_fn=nullptr;
    decode_geometry_unit_vectors(native,p);native.pc=0x83c8;
    auto diff=compare_function_state(native,reference);
    if(!diff.exact()){
        fprintf(stderr,"unit trial%u cols%u differs r%d byte%d LS=%X/%u control=%s\n",trial,p.columns,diff.first_register,
            diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned i=0;i<4;++i)fprintf(stderr,"lane%u %08X / %08X\n",i,native.gpr[diff.first_register]._u32[i],reference.gpr[diff.first_register]._u32[i]);
        abort();
    }
    assert(native.host_depth==reference.host_depth && native.image_id==reference.image_id);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto* file=fopen("analysis/native-geometry/phase3/test-unit-vectors-fixture.bin","rb");assert(file);
    assert(fread(fixture,1,sizeof fixture,file)==sizeof fixture);fclose(file);
    const auto previous=_mm_getcsr();_mm_setcsr((previous&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    for(unsigned trial=0;trial<64;++trial){seed(*native,trial);native->gpr[121]._u32[0]=0;native->gpr[1]._u32[0]=3;compare(*native,*reference,4096+trial);}
    const unsigned coordinates[]={0,1,2,3,63,127,255,383,511,512,639,767,895,1021,1022,1023};
    const unsigned bases[]={118,117,116,124};
    for(unsigned trial=0;trial<3072;++trial){
        seed(*native,0);for(auto& word:native->gpr[80]._u32)word+=1024;const unsigned packed=(coordinates[trial%16]<<14)|(coordinates[(trial/16)%16]<<4)|(trial/256);
        for(unsigned row=0;row<4;++row){auto* at=native->ls+native->gpr[bases[row]]._u32[0];
            at[0]=uint8_t(packed>>16);at[1]=uint8_t(packed>>8);at[2]=uint8_t(packed);}
        compare(*native,*reference,4160+trial);
    }
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial);native->gpr[107]._u32[0]=1;
        GeometryUnitStream p;assert(read_geometry_unit_vectors(*native,p));
        const auto& c=p.column[0];const unsigned prefix=c.groups>2?c.groups-2:0;
        for(unsigned row=0;row<4;++row)for(unsigned group=prefix;group<c.groups+2;++group){
            auto& byte=native->ls[native->gpr[bases[row]]._u32[0]+c.offset+group*p.input_step+2];
            byte=uint8_t((byte&0xf0)|12|((trial+row+group)&3));
        }
        compare(*native,*reference,7232+trial);
    }
    for(unsigned rejection=0;rejection<23;++rejection){
        seed(*native,16);GeometryUnitStream p;
        switch(rejection){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[107]._u32[0]=0;break;
        case 2:native->gpr[107]._u32[0]=17;break;
        case 3:native->gpr[106]._u32[0]=0;break;
        case 4:native->gpr[42]._u32[0]=256;break;
        case 5:native->gpr[83]._u32[0]=1;break;
        case 6:native->gpr[81]._u32[0]=0;break;
        case 7:native->gpr[80]._u32[0]=SPU_LS_SIZE;break;
        case 8:native->gpr[1]._u32[0]=3;break;
        case 9:native->ls[0xd8b0]^=1;break;
        case 10:native->gpr[96]._u32[0]^=1;break;
        case 11:native->ls[0xddf0]^=1;break;
        case 12:native->gpr[118]._u32[0]|=1;break;
        case 13:native->ls[0x3d070]^=1;break;
        case 14:native->gpr[82]._u32[0]=SPU_LS_SIZE;break;
        case 15:native->gpr[41]._u32[0]=0x10000;break;
        case 16:native->gpr[41]._u32[0]=0xd200;break;
        case 17:native->gpr[82]._u32[0]=0x3d000;break;
        case 18:native->gpr[83]._u32[1]^=1;break;
        case 19:native->gpr[81]._u32[1]^=1;break;
        case 20:native->gpr[80]._u32[1]^=1;break;
        case 21:native->ls[0x3d0c2]^=1;break;
        case 22:native->ls[0x10002]=15;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));if(read_geometry_unit_vectors(*native,p)){fprintf(stderr,"accepted reject%u\n",rejection);abort();}
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    _mm_setcsr(previous);puts("Complete unit-vector codec:8256 strict GPR/LS/control/continuation cases;23unchanged/noIO rejects PASS");
}
