#include "spu_context.h"
#include "../port/spu_scoped_rtz.h"
#include <cassert>
#include <cstdio>
#include <cstring>
extern "C" {
void buzz_video_idct_native(spu_context*);
void buzz_video_idct_oracle(spu_context*);
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_0007_at_0080A080_spu_func_00020E14(spu_context*) {}
}
static spu_context reference, native;
static uint8_t external_reference[SPU_LS_SIZE], external_native[SPU_LS_SIZE];
static unsigned rng=0x202b020e;
static unsigned word(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(){
    for(unsigned n=0;n<6000;++n){
        std::memset(&reference,0,sizeof reference);reference.ls=reference.ls_store;
        if((n&3)==0)reference.ls=external_reference;
        for(auto& r:reference.gpr)for(auto& x:r._u32)x=word();
        for(unsigned o=0;o<SPU_LS_SIZE;o+=4){unsigned x=word();std::memcpy(reference.ls+o,&x,4);}
        // Exercise both arbitrary SPU encodings and realistic finite inputs.
        if(n&1)for(auto& r:reference.gpr)for(auto& x:r._u32)x=(x&0x807fffff)|((120+word()%15)<<23);
        std::memcpy(&native,&reference,sizeof native);native.ls=native.ls_store;
        if((n&3)==0){native.ls=external_native;std::memcpy(native.ls,reference.ls,SPU_LS_SIZE);}
        g_spu_trampoline_fn=nullptr;buzz_video_idct_oracle(&reference);auto next=g_spu_trampoline_fn;
        g_spu_trampoline_fn=nullptr;buzz_spu_scoped_rtz(&native,buzz_video_idct_native);
        assert(next==g_spu_trampoline_fn&&native.pc==0x20e14);
        assert(!std::memcmp(reference.ls,native.ls,SPU_LS_SIZE));
        native.ls=reference.ls;
        if(std::memcmp(&native,&reference,sizeof native)){
            for(unsigned r=0;r<128;++r)for(unsigned l=0;l<4;++l)if(native.gpr[r]._u32[l]!=reference.gpr[r]._u32[l]){
                std::printf("FAIL case=%u r%u[%u] native=%08x exact=%08x\n",n,r,l,native.gpr[r]._u32[l],reference.gpr[r]._u32[l]);return 1;
            }
            return 2;
        }
    }
    std::puts("PASS video transform: 6000 complete SPU states, random and finite operands");
}
