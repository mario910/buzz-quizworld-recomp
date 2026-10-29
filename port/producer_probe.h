#pragma once
#include "ps3emu/ps3_ftrace.h"
/* Diagnostic only. No guest register/RAM writes and no host scheduling calls.
 * Keep the original incoming LR, even if the lifted function changes it. */
struct BuzzProducerProbeScope {
    uint64_t start;
    uint32_t tid,pc,lr;
    BuzzProducerProbeScope(ppu_context* ctx,uint32_t entry):start(0),tid(0),pc(entry),lr(0) {
        if(PS3_FT_ON()) {tid=(uint32_t)ctx->thread_id;lr=(uint32_t)ctx->lr;start=ps3_ft_now();}
    }
    ~BuzzProducerProbeScope() {
        if(start)ps3_ft_span(PS3_FT_PPU_SCOPE,tid,pc,lr,start,ps3_ft_now());
    }
};
