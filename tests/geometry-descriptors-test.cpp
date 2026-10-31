#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_descriptors.h"
#include "../port/native_geometry_color.h"
#include "../port/native_geometry_unit_vectors.h"
#include "../port/native_geometry_half_stream.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
static unsigned current_trial=0;
static void drain(spu_context*,uint32_t);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* ctx,uint32_t pc){drain(ctx,pc);}
void spu_img_restore(spu_context* ctx,int32_t image){assert(ctx->image_id==image);}
void spu_stop(spu_context*){abort();}
void spu_wrch(spu_context*,uint32_t,u128){abort();}
u128 spu_rdch(spu_context*,uint32_t){abort();}
void buzz_geometry_job_native_fail(spu_context*,const char* reason){fprintf(stderr,"descriptor: %s\n",reason);abort();}
}
#include "../analysis/native-geometry/phase3/test-descriptors-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx){auto fn=descriptors_reference_lookup(ctx->pc);if(!fn){fprintf(stderr,"descriptor trial%u missing PC=%X r0=%X\n",current_trial,ctx->pc,ctx->gpr[0]._u32[0]);abort();}fn(ctx);}
static void drain(spu_context* ctx,uint32_t stop)
{
    unsigned steps=0;
    while(g_spu_trampoline_fn){
        assert(++steps<65536);auto fn=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;fn(ctx);
    }
    assert(ctx->pc==stop);
}
static uint32_t random_word(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint8_t constants[0xe680];
static void seed(spu_context& ctx,unsigned trial)
{
    memset(&ctx,0,sizeof ctx);ctx.ls=ctx.ls_store;ctx.image_id=41;ctx.pc=0x7df8;ctx.host_depth=1;
    uint32_t random=trial+1;
    for(unsigned at=0;at<SPU_LS_SIZE;++at)ctx.ls[at]=uint8_t(random_word(random));
    memcpy(ctx.ls,constants,sizeof constants);
    for(auto& reg:ctx.gpr)for(auto& value:reg._u32)value=random_word(random);
    ctx.gpr[0]._u32[0]=0x1380;ctx.gpr[1]._u32[0]=0x3c000;ctx.gpr[3]._u32[0]=0x3b000;
    ctx.gpr[4]._u32[0]=0x10000;ctx.gpr[5]._u32[0]=0x31000;ctx.gpr[6]._u32[0]=0x2c000;
    const unsigned counts[]={0,1,2,3,4,5,7,8,9,16,17,31,32,33,63,64,65,127,128,129};
    ctx.gpr[7]._u32[0]=counts[(trial/16)%20];
    ctx.gpr[8]._u32[0]=trial&1?0x3f000000:0;ctx.gpr[9]._u32[0]=0x32000;
    for(unsigned i=0;i<512;++i){ctx.ls[0x32000+i*2]=uint8_t((i*16)>>8);ctx.ls[0x32001+i*2]=uint8_t(i*16);}
    memset(ctx.ls+0x3b000,0,0x190);ctx.ls[0x3b000]=uint8_t(0x80|((trial/128)%16)|(trial&2?16:0));
    const unsigned destination_vertices=(ctx.gpr[7]._u32[0]+3)&~3u;
    ctx.ls[0x3b008]=uint8_t(destination_vertices>>8);ctx.ls[0x3b009]=uint8_t(destination_vertices);
    for(unsigned i=0;i<512;++i){const unsigned offset=(i%(destination_vertices?destination_vertices:1))*16;
        ctx.ls[0x32000+i*2]=uint8_t(offset>>8);ctx.ls[0x32001+i*2]=uint8_t(offset);}
    for(unsigned slot=0;slot<16;++slot)geometry_job_put32(ctx.ls+0x3b010+slot*4,0x18000+slot*0x1000);
    if(trial&1)for(unsigned slot=0;slot<8;++slot)ctx.ls[0x3b160+slot]=uint8_t(slot+1);
    const unsigned types[]={1,2,3,4,5,6,7,8,9};
    const unsigned type=types[(trial/4)%9];
    const unsigned widths[]={0,8,16,8,4,8,4,4,4,3};
    const unsigned count=(trial/32)%5;
    ctx.ls[0x2c000]=uint8_t(count);ctx.ls[0x2c001]=uint8_t(std::max(1u,count*widths[type])+(trial/320)%5);ctx.ls[0x2c002]=uint8_t(count);
    for(unsigned i=0;i<count;++i){
        auto* a=ctx.ls+0x2c008+i*8;
        a[0]=uint8_t(i*widths[type]);a[1]=uint8_t(type);a[2]=uint8_t(1+(trial+i)%4);a[3]=uint8_t(1+i);
        a[4]=uint8_t(widths[type]);a[5]=uint8_t(i);a[6]=uint8_t(type==8?count*8:0);a[7]=0;
    }
    for(unsigned i=0;i<4;++i){ctx.ls[0x2c008+count*8+i*2]=8;ctx.ls[0x2c009+count*8+i*2]=0;}
    // Reserved unit-vector selector 3 is not a valid input encoding.
    for(unsigned at=0x10000;at<0x14000;++at)ctx.ls[at]=uint8_t((ctx.ls[at]&0xf0)|((ctx.ls[at]&15)%12));
    ctx.int_enable=trial&1;ctx.resv_valid=trial&1;ctx.mfc_tag_status=trial*0x123;
}
static void native_color(spu_context* ctx)
{
    GeometryColorStream p;assert(read_geometry_color(*ctx,p));decode_geometry_color(*ctx,p);
    ctx->pc=0x83c8;g_spu_trampoline_fn=buzz_job_6eba_spu_func_000083C8;
}
static void native_unit_vectors(spu_context* ctx)
{
    GeometryUnitStream p;const char* reason=nullptr;if(!read_geometry_unit_vectors(*ctx,p,&reason)){fprintf(stderr,"descriptor unit preflight trial%u: %s\n",current_trial,reason);abort();}
    decode_geometry_unit_vectors(*ctx,p);ctx->pc=0x83c8;g_spu_trampoline_fn=buzz_job_6eba_spu_func_000083C8;
}
static void native_half_stream(spu_context* ctx)
{
    GeometryHalfStream p;const char* reason=nullptr;if(!read_geometry_half_stream(*ctx,p,&reason)){fprintf(stderr,"descriptor half preflight trial%u: %s\n",current_trial,reason);abort();}
    decode_geometry_half_stream(*ctx,p);ctx->pc=0x83c8;g_spu_trampoline_fn=buzz_job_6eba_spu_func_000083C8;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    current_trial=trial;
    memcpy(&reference,&native,sizeof native);reference.ls=reference.ls_store;
    GeometryDescriptorStream p;assert(read_geometry_descriptor_stream(native,p));
    buzz_job_6eba_spu_func_00007DF8(&reference);drain(&reference,native.gpr[0]._u32[0]);
    buzz_geometry_descriptor_functions functions{{buzz_job_6eba_spu_func_000085E8,buzz_job_6eba_spu_func_000087CC,
        trial&1024?native_half_stream:buzz_job_6eba_spu_func_00008378,trial&256?native_color:buzz_job_6eba_spu_func_000089B0,
        buzz_job_6eba_spu_func_00008B94,buzz_job_6eba_spu_func_00008D34,buzz_job_6eba_spu_func_00008EA8,
        buzz_job_6eba_spu_func_00009058,trial&512?native_unit_vectors:buzz_job_6eba_spu_func_00009114},buzz_job_6eba_spu_func_0000C750,buzz_job_6eba_spu_func_000083C8};
    decode_geometry_descriptor_stream(native,p,functions);native.pc=native.gpr[0]._u32[0];
    const auto diff=compare_function_state(native,reference);
    if(!diff.exact()){
        fprintf(stderr,"descriptor case%u type=%u count=%u blend=%u differs:r%d byte%d LS=%X/%u state=%s\n",trial,
            native.ls[0x2c009],p.count,trial&1,diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if(diff.first_register>=0)for(unsigned lane=0;lane<4;++lane)fprintf(stderr,"lane%u native=%08X reference=%08X\n",lane,native.gpr[diff.first_register]._u32[lane],reference.gpr[diff.first_register]._u32[lane]);
        abort();
    }
    assert(!g_spu_trampoline_fn && native.host_depth==reference.host_depth);
}
int main()
{
    auto* file=fopen("analysis/native-geometry/phase3/test-descriptors-constants.bin","rb");assert(file);
    assert(fread(constants,1,sizeof constants,file)==sizeof constants);fclose(file);
    const unsigned previous=_mm_getcsr();_mm_setcsr((previous&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for(unsigned trial=0;trial<2880;++trial){seed(*native,trial);compare(*native,*reference,trial);}
    const unsigned routes[]={0,1,2,3,4,5,6,9,14,15};
    for(unsigned trial=0;trial<1024;++trial){
        seed(*native,trial+2880);
        for(unsigned i=0;i<native->ls[0x2c000];++i)native->ls[0x2c00b+i*8]=uint8_t(routes[(trial+i)%10]);
        if(trial&4)for(unsigned slot=0;slot<16;++slot)native->ls[0x3b160+slot]=slot%3?0:uint8_t(1+slot);
        compare(*native,*reference,trial+2880);
    }
    for(unsigned rejection=0;rejection<20;++rejection){
        seed(*native,64);GeometryDescriptorStream p;
        switch(rejection){
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[3]._u32[0]=0xd200;break;
        case 2:native->gpr[1]._u32[0]=3;break;
        case 3:native->gpr[7]._u32[0]=8193;break;
        case 4:native->gpr[6]._u32[0]=0xd200;break;
        case 5:native->ls[0x2c000]=17;break;
        case 6:native->ls[0x2c001]=0;break;
        case 7:native->ls[0x8354]^=1;break;
        case 8:native->ls[0x842c]^=1;break;
        case 9:native->ls[0xcf50]^=1;break;
        case 10:memset(native->ls+0x3b160,1,16);break;
        case 11:geometry_job_put32(native->ls+0x3b010,0);break;
        case 12:geometry_job_put32(native->ls+0x3b010,0x3b000);break;
        case 13:geometry_job_put32(native->ls+0x3b010,0x3c000);break;
        case 14:geometry_job_put32(native->ls+0x3b010,0x2c000);break;
        case 15:native->ls[0x2c009]=0;break;
        case 16:native->ls[0x2c009]=10;break;
        case 17:native->ls[0x2c00a]=5;break;
        case 18:native->ls[0x2c00c]=0;break;
        case 19:native->ls[0x2c008]=255;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));
        assert(!read_geometry_descriptor_stream(*native,p));assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    const uint32_t factors[]={0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0x7f800000,0xffffffff};
    for(unsigned trial=0;trial<256;++trial){
        seed(*native,trial+3904);native->gpr[8]._u32[0]=factors[trial%8];compare(*native,*reference,trial+3904);
    }
    for(unsigned trial=0;trial<32;++trial){
        seed(*native,trial);native->gpr[7]._u32[0]=0;native->gpr[6]._u32[0]=3;compare(*native,*reference,trial+4160);
    }
    for(unsigned rejection=0;rejection<8;++rejection){
        seed(*native,65);GeometryDescriptorStream p;
        switch(rejection){
        case 0:native->ls[0x3b000]&=127;break;
        case 1:native->gpr[9]._u32[0]=3;break;
        case 2:native->ls[0x32001]=1;break;
        case 3:native->ls[0x32000]=255;break;
        case 4:native->gpr[9]._u32[0]=0x3bff0;break;
        case 5:native->ls[0x3b008]=native->ls[0x3b009]=0;break;
        case 6:geometry_job_put32(native->ls+0x3b010,0x32000);break;
        case 7:geometry_job_put32(native->ls+0x3b010+(native->ls[0x3b000]&15)*4,0x3b000);break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));assert(!read_geometry_descriptor_stream(*native,p));
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    _mm_setcsr(previous);puts("Descriptor control:4192 strict original/native-callee cases, all9storage families;28unchanged/noIO rejects PASS");
}
