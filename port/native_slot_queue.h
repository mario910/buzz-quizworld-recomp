#pragma once
#include "../ps3recomp/runtime/spu/spurs_completion_queue.h"
#include <chrono>
extern "C" void ppu_gcm_pump(void);
// Called only after the reference selector's free-slot scan failed, with its
// existing manager mutex held. Producers of EF completion do not need it.
static bool buzz_wait_completed_slot(ppu_context* c)
{
    static const bool enabled=[] {const char* e=getenv("BUZZ_COMPLETION_QUEUE");
        bool on=e&&e[0]&&e[0]!='0';fprintf(stderr,"[completion-queue] enabled=%d\n",on);return on;}();
    if (!enabled) return false;
    const uint32_t manager=(uint32_t)c->gpr[27], count=(uint32_t)c->gpr[28];
    const uint32_t base=vm_read32(manager);
    if (!base || !count || count>64 || (uint64_t)base+count*0x800ull>UINT32_MAX) return false;
    const uint64_t ticket=spurs_completion_begin(base+0x600,0x800,count);
    if (!ticket) return false;
    struct Release {uint64_t ticket;~Release(){spurs_completion_end(ticket);}} release{ticket};
    static thread_local uint64_t selections, probes, batches, timeouts;
    static thread_local auto last=std::chrono::steady_clock::now();
    for (;;) {
        uint64_t pending=spurs_completion_take(ticket,1);
        ++batches;
        if (!pending) {++timeouts;pending=count==64?UINT64_MAX:(1ull<<count)-1;}
        // Same callback delivery opportunity as the old HLE polling loop.
        ppu_gcm_pump();
        for (unsigned index=0;pending;++index,pending>>=1) {
            if (!(pending&1)) continue;
            const uint32_t slot=base+index*0x800;
            if (!vm_read8(slot+0x790)) continue; // not an in-flight job
            ++probes;
            c->gpr[30]=index;c->gpr[31]=index+1;c->gpr[29]=index*0x800;
            c->gpr[3]=slot;c->lr=0x26BF94;
            func_0026A8A0(c);DRAIN_TRAMPOLINE(c);
            if (!(c->gpr[3]&255)) continue; // stale hint, no completion consumed
            ++selections;
            if ((selections&1023)==0) {
                auto now=std::chrono::steady_clock::now();
                if (now-last>=std::chrono::seconds(5)) {
                    fprintf(stderr,"[completion-queue] slots=%u selected=%llu probes=%llu batches=%llu fallback=%llu\n",
                        count,(unsigned long long)selections,(unsigned long long)probes,
                        (unsigned long long)batches,(unsigned long long)timeouts);
                    last=now;
                }
            }
            // The original tail reloads base, clears 790/791, initializes the
            // chosen slot, publishes +780, unlocks, and restores all saved ABI.
            return true;
        }
    }
}
