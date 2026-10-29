#define PPU_NO_INLINE_LOADS
#include "../port/recompiled/ppu_recomp.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../port/completion_profile.h"
static unsigned char ram[8192], expected[8192];
extern "C" {
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
uint64_t vm_read64(uint64_t at) { assert(at+8<=sizeof ram); uint64_t v=0; for(int n=0;n<8;++n)v=(v<<8)|ram[at+n]; return v; }
void vm_write64(uint64_t at,uint64_t v) { assert(at+8<=sizeof ram); for(int n=7;n>=0;--n){ram[at+n]=(unsigned char)v;v>>=8;} }
void vm_write16(uint64_t at,uint16_t v) { assert(at+2<=sizeof ram); ram[at]=v>>8;ram[at+1]=(unsigned char)v; }
}
#define DRAIN_TRAMPOLINE(c) do { while(g_trampoline_fn){auto fn=g_trampoline_fn;g_trampoline_fn=nullptr;fn(c);} } while(0)
static uint64_t ppc_rldicl(uint64_t v,int sh,int mb){ assert(sh==0&&mb==32); return (uint32_t)v; }
static uint32_t result;
static unsigned polls,retired,callbacks;
static int probe_result;
static unsigned probe_calls;
static int probe(uint32_t flag) { assert(flag==0x1600);++probe_calls;return probe_result; }
static void callback(void* opaque) { auto c=(ppu_context*)opaque; ++callbacks;c->gpr[12]^=0x1234; }
void BUZZ_COMPLETION_POLL_FN(ppu_context* c) {
    ++polls; assert(c->lr==buzz_completion_poll_lr && c->gpr[5]==1);
    assert(ram[c->gpr[4]]==0 && ram[c->gpr[4]+1]==1);
    vm_write64(c->gpr[1]+0x28,c->gpr[2]);
    c->gpr[3]=result; if(!result)vm_write16(c->gpr[4],7);
    g_trampoline_fn=callback;
}
void BUZZ_COMPLETION_RETIRE_FN(ppu_context* c) {
    ++retired; assert(c->lr==buzz_completion_retire_lr);
    c->gpr[0]=0x55;c->gpr[11]=c->gpr[3]+9;c->cr^=0x76543210;
    g_trampoline_fn=callback;
}
#include "../analysis/native-completion/reference.inc"
#include "../port/native_completion.h"
int main() {
    for(unsigned n=0;n<12000;++n) {
        ppu_context a{},b{};
        for(unsigned r=0;r<32;++r)a.gpr[r]=0x1234567800000000ull+r*17+n;
        a.gpr[1]=0x800;a.lr=0x26bf94+n;a.xer=n<<28;a.cr=0x98765432+n;b=a;
        result=n%3==0?0:n%3==1?0x8041090a:3;
        memset(ram,0xaa,sizeof ram);polls=retired=callbacks=0;
        buzz_completion_reference(&a);
        assert(polls==1&&retired==(!result)&&callbacks==1+(!result));
        memcpy(expected,ram,sizeof ram);
        memset(ram,0xaa,sizeof ram);polls=retired=callbacks=0;
        buzz_native_completion(&b);
        assert(polls==1&&retired==(!result)&&callbacks==1+(!result));
        assert(!memcmp(&a,&b,sizeof a)&&!memcmp(ram,expected,sizeof ram));
    }
    puts("PASS: 12000 completion states, BUSY/error/success, full context/stack/TOC, exactly-once retirement, callback drain");
    for (uint64_t lr : {uint64_t(buzz_completion_busy_lr1),uint64_t(buzz_completion_busy_lr2),uint64_t(0x123456)})
        for(int p=-1;p<=1;++p) {
            ppu_context a{},b{};for(unsigned r=0;r<32;++r)a.gpr[r]=r*19+77;
            a.gpr[3]=0x1000;a.lr=lr;a.cr=0xabcdef12;b=a;
            probe_result=p;probe_calls=0;
            bool done=buzz_native_completion_busy(&a,probe);
            bool allowed=lr!=0x123456;
            assert(done==(allowed&&p==1));assert(probe_calls==unsigned(allowed));
            if(done)b.gpr[3]=0;
            assert(!memcmp(&a,&b,sizeof a));
        }
    puts("PASS: native BUSY ABI, caller gating, ready/unknown fallback, only r3 changed");
}
