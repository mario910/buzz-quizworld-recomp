#define PPU_NO_INLINE_LOADS
#include "../port/recompiled/ppu_recomp.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
static unsigned takes,probes,ended,pumps,scenario;
extern "C" {
uint32_t vm_read32(uint64_t ea){assert(ea==0x100);return 0x1000;}
uint8_t vm_read8(uint64_t ea){assert(ea>=0x1790 && ea<=0x2790);return ea!=0x1f90;}
uint64_t spurs_completion_begin(uint32_t first,uint32_t stride,unsigned count){assert(first==0x1600&&stride==0x800&&count==3);return 77;}
uint64_t spurs_completion_take(uint64_t t,unsigned ms){assert(t==77&&ms==1);++takes;assert(takes<=2);return takes==1?7:scenario?0:4;}
void spurs_completion_end(uint64_t t){assert(t==77);++ended;}
void ppu_gcm_pump(){++pumps;}
}
#define DRAIN_TRAMPOLINE(c) ((void)0)
void func_0026A8A0(ppu_context* c){++probes;assert(c->lr==0x26bf94);c->gpr[3]=takes==2 && c->gpr[3]==0x2000;}
#include "../port/native_slot_queue.h"
int main(){
#ifdef _WIN32
    _putenv_s("BUZZ_COMPLETION_QUEUE","1");
#else
    setenv("BUZZ_COMPLETION_QUEUE","1",1);
#endif
    for(scenario=0;scenario<2;++scenario){
        takes=probes=ended=pumps=0;ppu_context c{};c.gpr[27]=0x100;c.gpr[28]=3;
        assert(buzz_wait_completed_slot(&c));
        assert(c.gpr[30]==2&&c.gpr[29]==0x1000&&c.gpr[31]==3);
        assert(takes==2&&ended==1&&pumps==2&&probes==(scenario?4u:3u));
    }
    ppu_context c{};c.gpr[27]=0x100;c.gpr[28]=65;
    assert(!buzz_wait_completed_slot(&c));
    puts("PASS: initial scan, inactive slot exclusion, stale hints, candidate selection, timeout rescan, pump, release, bounded fallback");
}
