// Actual queue publication for CRI's YUV converter, not conversion completion.
#include "ppu_recomp.h"
#include "video_queue_layout.h"
#include <cstdio>
#include <thread>
#include <chrono>
extern "C" {
void ps3_hle_register_ctx(uint32_t,const char*,void (*)(ppu_context*));
int32_t _cellSpursLFQueueInitialize(uint64_t,uint64_t,uint64_t,uint32_t,uint32_t,uint32_t);
void spu_lockline_lock(void);
void spu_lockline_unlock(void);
int spu_coh_is_reserved(uint32_t);
void spu_coh_notify_write(uint32_t);
void spu_taskset_signal_task(uint32_t,uint32_t);
}
static constexpr uint64_t guest_size=0x100000000ull;
static void initialize(ppu_context* c) {
    if((uint32_t)c->lr!=0x490d84 || c->gpr[6]!=16 || c->gpr[8]!=2) {
        c->gpr[3]=(uint64_t)(int64_t)_cellSpursLFQueueInitialize(c->gpr[3],c->gpr[4],c->gpr[5],(uint32_t)c->gpr[6],(uint32_t)c->gpr[7],(uint32_t)c->gpr[8]);return;
    }
    uint32_t owner=(uint32_t)c->gpr[3],q=(uint32_t)c->gpr[4],buffer=(uint32_t)c->gpr[5],depth=(uint32_t)c->gpr[7];
    int rc=buzz_vq_init(vm_base,guest_size,owner,q,buffer,depth);
    std::fprintf(stderr,"[video-queue] initialize owner=%08X queue=%08X buffer=%08X depth=%u rc=%d\n",owner,q,buffer,depth,rc);
    c->gpr[3]=rc==BUZZ_VQ_OK?0:(uint64_t)(int64_t)(int32_t)0x80410902u;
}
bool buzz_video_queue_push(ppu_context* c) {
    uint32_t pc=(uint32_t)c->lr,q=(uint32_t)c->gpr[3],data=(uint32_t)c->gpr[4];
    if(pc!=0x4906d4 && pc!=0x490a30)return false;
    for(;;) {
        spu_lockline_lock();
        int task=-1,rc=buzz_vq_push_locked(vm_base,guest_size,q,data,&task);
        uint32_t owner=rc==BUZZ_VQ_OK?buzz_vq_r32(vm_base+q+0x74):0;
        if(rc==BUZZ_VQ_OK && spu_coh_is_reserved(q))spu_coh_notify_write(q);
        spu_lockline_unlock();
        if(rc==BUZZ_VQ_AGAIN && c->gpr[5]) {std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        if(rc==BUZZ_VQ_OK && task>=0)spu_taskset_signal_task(owner,(uint32_t)task);
        static unsigned printed=0;
        if(printed++<12)std::fprintf(stderr,"[video-queue] publish q=%08X rc=%d task=%d\n",q,rc,task);
        c->gpr[3]=rc==BUZZ_VQ_OK?0:(uint64_t)(int64_t)(int32_t)(rc==BUZZ_VQ_AGAIN?0x80410901u:0x8041090fu);
        return true;
    }
}
void buzz_register_video_queue() {ps3_hle_register_ctx(0x011ee38b,"Buzz: converter queue initialize",initialize);}
