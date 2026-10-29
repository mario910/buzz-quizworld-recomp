#pragma once
#include "ppu_recomp.h"
#include <cstdlib>
void buzz_video_probe(uint32_t pc,uint32_t object,uint32_t result,uint32_t arg4);
struct BuzzVideoProbeScope {
    ppu_context* context;
    uint32_t pc,object,arg4;
    BuzzVideoProbeScope(ppu_context* ctx,uint32_t entry):context(ctx),pc(entry),object((uint32_t)ctx->gpr[3]),arg4((uint32_t)ctx->gpr[4]) {
        // Both original entry points address the CRI singleton through the TOC.
        if(entry==0x30617c || entry==0x484f94) {
            uint32_t slot=(uint32_t)ctx->gpr[2]+0x78a8;
            object=slot>=0x10000 && slot<0xbffffffc?vm_read32(slot):0;
        }
    }
    ~BuzzVideoProbeScope() {
        static const bool enabled=[] {const char* e=std::getenv("BUZZ_VIDEO_TRACE");return e && *e && *e!='0';}();
        if(enabled)buzz_video_probe(pc,object,(uint32_t)context->gpr[3],arg4);
    }
};
