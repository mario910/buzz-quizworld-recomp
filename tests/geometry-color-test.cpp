#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_color.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_drain_call(spu_context*,uint32_t) { abort(); }
void spu_img_restore(spu_context*,int32_t) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-color-reference.inc"
static uint32_t random_word(uint32_t& state) { state ^= state<<13; state ^= state>>17; state ^= state<<5; return state; }
static void seed(spu_context& ctx,unsigned trial)
{
    memset(&ctx,0,sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0x89b0;
    uint32_t random = trial+1;
    for (unsigned at=0; at<SPU_LS_SIZE; ++at) ctx.ls[at] = uint8_t(random_word(random));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(random);
    ctx.gpr[1]._u32[0] = 0x3d000;
    ctx.gpr[121]._u32[0] = 1;
    ctx.gpr[107]._u32[0] = 1+(trial/16)%16;
    ctx.gpr[106]._u32[0] = 3+(trial/256)%7;
    ctx.gpr[42]._u32[0] = trial%16;
    ctx.gpr[25]._u32[0] = 1+(trial/64)%4;
    const unsigned counts[] = {0,1,2,3,4,7,15,31};
    const unsigned groups = counts[(trial/4)%8], output_step=1024,input_step=128;
    for (unsigned lane=0; lane<4; ++lane) {
        ctx.gpr[41]._u32[lane] = 0x20000+lane*256+trial%16;
        ctx.gpr[80]._u32[lane] = 0x20000+groups*output_step+240;
        ctx.gpr[81]._u32[lane] = output_step;
        ctx.gpr[82]._u32[lane] = 0x3c000+(trial&1 ? lane*16 : 0);
        ctx.gpr[91]._u32[lane] = 0x3b808080;
        ctx.gpr[94]._u32[lane] = 0;
    }
    ctx.gpr[83]._u32[0] = input_step;
    const unsigned bases[] = {118,117,116,124};
    for (unsigned row=0; row<4; ++row) {
        ctx.gpr[bases[row]]._u32[0] = 0x10000+row*0x2000;
        u128 pointer = ctx.gpr[row+4]; pointer._u32[0] = ctx.gpr[bases[row]]._u32[0]+16;
        spu_ls_write128(&ctx,0x3d070-row*16,pointer);
    }
    for (unsigned byte=0; byte<16; ++byte) {
        ctx.gpr[112]._u8[byte^3] = uint8_t(byte); ctx.gpr[108]._u8[byte] = 3;
    }
    const uint8_t selector[] = {255,0,255,255,255,1,255,255,255,2,255,255,255,3,255,255};
    memcpy(ctx.ls+0xd8c0,selector,16);
    ctx.int_enable=trial&1; ctx.resv_valid=trial&1; ctx.mfc_tag_status=trial*0x123;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    memcpy(&reference,&native,sizeof native); reference.ls=reference.ls_store;
    GeometryColorStream p; assert(read_geometry_color(native,p));
    buzz_job_6eba_spu_func_000089B0(&reference);
    unsigned steps=0;
    while (g_spu_trampoline_fn && reference.pc != 0x83c8) {
        assert(++steps<10000); auto fn=g_spu_trampoline_fn; g_spu_trampoline_fn=nullptr; fn(&reference);
    }
    assert(reference.pc==0x83c8 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_000083C8);
    g_spu_trampoline_fn=nullptr;
    decode_geometry_color(native,p); native.pc=0x83c8;
    const auto diff=compare_function_state(native,reference);
    if (!diff.exact()) {
        fprintf(stderr,"color case%u cols=%u differs:r%d byte%d LS=%X/%u controls=%s\n",trial,p.columns,
            diff.first_register,diff.first_register_byte,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");
        if (diff.first_register>=0) for (unsigned lane=0;lane<4;++lane)
            fprintf(stderr,"lane%u native=%08X reference=%08X\n",lane,native.gpr[diff.first_register]._u32[lane],reference.gpr[diff.first_register]._u32[lane]);
        if (diff.ls_bytes) {
            for (unsigned at=diff.first_ls&~15u;at<(diff.first_ls&~15u)+16;at+=4)
                fprintf(stderr,"LS%X native=%08X reference=%08X\n",at,geometry_job_be32(native.ls+at),geometry_job_be32(reference.ls+at));
            fprintf(stderr,"input at10000=%02X 1000D=%02X 10010=%02X mask=%08X,%08X,%08X,%08X\n",native.ls[0x10000],native.ls[0x1000d],native.ls[0x10010],native.gpr[47]._u32[0],native.gpr[47]._u32[1],native.gpr[47]._u32[2],native.gpr[47]._u32[3]);
        }
        abort();
    }
}
int main()
{
    const unsigned previous=_mm_getcsr(); _mm_setcsr((previous&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    for (unsigned trial=0;trial<4096;++trial) { seed(*native,trial); compare(*native,*reference,trial); }
    for (unsigned trial=0;trial<256;++trial) {
        seed(*native,trial); native->gpr[107]._u32[0]=16;
        for (unsigned lane=0;lane<4;++lane) native->gpr[80]._u32[lane]=0x20000+trial%16;
        compare(*native,*reference,4096+trial);
    }
    for (unsigned trial=0;trial<64;++trial) {
        seed(*native,trial); native->gpr[121]._u32[0]=0; native->gpr[1]._u32[0]=3;
        compare(*native,*reference,4352+trial);
    }
    const uint32_t edges[]={0,0x80000000,1,0x007fffff,0x00800000,0x3b808080,0x7f800000,0xffffffff};
    for (unsigned trial=0;trial<128;++trial) {
        seed(*native,trial);
        for (unsigned lane=0;lane<4;++lane) {
            native->gpr[91]._u32[lane]=edges[(trial+lane)%8];
            native->gpr[94]._u32[lane]=edges[(trial/8+lane)%8];
        }
        compare(*native,*reference,4416+trial);
    }
    for (unsigned rejection=0;rejection<18;++rejection) {
        seed(*native,16); GeometryColorStream p;
        switch (rejection) {
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[107]._u32[0]=0;break;
        case 2:native->gpr[107]._u32[0]=17;break;
        case 3:native->gpr[106]._u32[0]=0;break;
        case 4:native->gpr[25]._u32[0]=5;break;
        case 5:native->gpr[83]._u32[0]=1;break;
        case 6:native->gpr[81]._u32[0]=0;break;
        case 7:native->gpr[80]._u32[0]=SPU_LS_SIZE;break;
        case 8:native->ls[0xd8c0]^=1;break;
        case 9:native->gpr[112]._u8[0]^=1;break;
        case 10:native->gpr[108]._u8[0]^=1;break;
        case 11:native->gpr[1]._u32[0]=3;break;
        case 12:native->gpr[118]._u32[0]|=1;break;
        case 13:native->ls[0x3d070]^=1;break;
        case 14:native->gpr[82]._u32[0]=SPU_LS_SIZE;break;
        case 15:native->gpr[41]._u32[0]=0x10000;break;
        case 16:native->gpr[41]._u32[0]=0xd200;break;
        case 17:native->gpr[42]._u32[0]=256;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));
        assert(!read_geometry_color(*native,p)); assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    _mm_setcsr(previous);
    puts("Complete color codec:4544 strict GPR/LS/control/continuation cases;18unchanged/noIO rejects PASS");
}
