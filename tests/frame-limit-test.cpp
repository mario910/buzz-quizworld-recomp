#include "ppu_recomp.h"
#include "ps3emu/host_controls.h"
#include "../port/frame_pacer.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void (*reset_hook)(ppu_context*);
unsigned resets;
void check(bool ok) { if(!ok)throw std::runtime_error("Frame limiter regression"); }
}
extern "C" void buzz_register_frame_limit(void);
extern "C" void ps3_hle_register_ctx(uint32_t nid,const char*,void (*hook)(ppu_context*)) {
    check(nid==0xB2E761D4u);reset_hook=hook;
}
extern "C" void cellGcmResetFlipStatus(void) { ++resets; }

int main() { try {
    using Clock=std::chrono::steady_clock;
    buzz_register_frame_limit();check(reset_hook!=nullptr);
    ppu_context ctx{};ctx.gpr[3]=123;ctx.lr=0x123456;ctx.thread_id=1;
    const auto original=ctx;
    ps3_host_set_frame_limit(30);
    const auto start=Clock::now();
    for(unsigned i=0;i<7;++i)reset_hook(&ctx);
    check(Clock::now()-start>=std::chrono::milliseconds(199));
    check(resets==7&&std::memcmp(&ctx,&original,sizeof(ctx))==0);
    ps3_host_set_frame_limit(0);reset_hook(&ctx);
    // A disable must clear the old future deadline, even without a frame gap.
    buzz_pc::FramePacer pacer;
    check(pacer.deadline(100,true)==100);
    check(pacer.deadline(101,false)==101&&pacer.next_ns==0);
    check(pacer.deadline(102,true)==102);
    const auto period=buzz_pc::FramePacer::period_ns;
    // A one-ms overrun must not move every subsequent deadline one ms later.
    check(pacer.deadline(102+period+1000000,true)==102+period+1000000);
    check(pacer.deadline(102+2*period-1000000,true)==102+2*period);
    // Long frames and menu pauses discard debt instead of running a burst.
    check(pacer.deadline(1000000000,true)==1000000000);
    check(pacer.deadline(1000000001,true)==1033333333);
    // A varying render workload averaging 32 ms can sustain 30 FPS. The
    // SDK pacer's per-overrun rebase stretched this to ~28.8 FPS when capped.
    buzz_pc::FramePacer varying;
    ps3_host_pacer legacy{};
    uint64_t paced=1,old=1;
    varying.deadline(paced,true);ps3_host_frame_deadline(&legacy,old,30);
    for(unsigned i=0;i<600;++i) {
        const uint64_t work=(i&1)?36000000:28000000;
        paced=varying.deadline(paced+work,true);
        old=ps3_host_frame_deadline(&legacy,old+work,30);
    }
    check(paced>=20000000000ull&&paced<20004000000ull);
    check(old>20700000000ull);
    // Each guest thread owns its own schedule (threaded display/loading).
    ps3_host_set_frame_limit(30);
    std::thread other([&]{ppu_context c{};reset_hook(&c);});other.join();
    check(resets==9);
    std::cout<<"PASS guest frame hook, real 30 FPS pacing, register preservation, disable/resume and stall deadlines\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
