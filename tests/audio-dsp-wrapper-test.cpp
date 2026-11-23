#include "spu_context.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void buzz_audio_compressor_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*), unsigned);
}
static unsigned original_calls, native_calls;
static spu_context context;
static void operation(spu_context* ctx) { ++ctx->gpr[3]._u32[0]; ++ctx->ls[4]; ctx->pc = 0x1000; }
static void reference(spu_context* ctx) { ++original_calls; operation(ctx); }
static void native(spu_context* ctx) { ++native_calls; operation(ctx); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const int mode = std::atoi(argv[1]);
    setenv("BUZZ_AUDIO_COMPRESSOR", argv[1], 1);
    context.ls = context.ls_store;
    const unsigned entries[]={0x2ff18u,0x36538u,0x36710u,0x4a4cu,0x47b4u,0xac1cu,0x8400u,0x5524u,
        0x55DC, 0x57EC, 0x36370, 0x368B8, 0x3614C, 0x37AE8, 0x36EB0, 0x37750, 0x37148, 0x37590};
    for(const unsigned entry:entries) {
        std::memset(&context,0,sizeof context);context.ls=context.ls_store;
        original_calls=native_calls=0;
        for (unsigned call = 0; call < 20; ++call)
            buzz_audio_compressor_run(&context, reference, native, entry);
        if (context.gpr[3]._u32[0] != 20 || context.ls[4] != 20 || context.pc != 0x1000) return 1;
        if (original_calls != (mode == 1 ? 0u : 20u) || native_calls != (mode == 0 ? 0u : mode == 1 ? 20u : 16u)) return 1;
    }
    std::printf("PASS audio DSP wrapper mode=%d: original=%u native=%u\n", mode, original_calls, native_calls);
}
