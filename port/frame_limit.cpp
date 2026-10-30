// Buzz resets flip status once after its frame wait, before starting the next
// frame. Pace that guest PPU thread; the RSX worker must keep draining fences.
#include "ppu_recomp.h"
#include "ps3emu/host_controls.h"
#include "frame_pacer.h"
#include <chrono>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
extern "C" void cellGcmResetFlipStatus(void);

namespace {
using Clock = std::chrono::steady_clock;
void wait_until(Clock::time_point deadline) {
#ifdef _WIN32
    struct Timer {
        HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        ~Timer(){if(handle)CloseHandle(handle);}
    };
    static thread_local Timer timer;
    if(timer.handle) {
        // Block for most of the interval; only the final 200 us may spin.
        constexpr auto tail=std::chrono::microseconds(200);
        const auto left=deadline-Clock::now();
        if(left>tail) {
            LARGE_INTEGER due;
            due.QuadPart=-std::chrono::duration_cast<std::chrono::nanoseconds>(left-tail).count()/100;
            if(!SetWaitableTimer(timer.handle,&due,0,nullptr,nullptr,FALSE) ||
               WaitForSingleObject(timer.handle,INFINITE)!=WAIT_OBJECT_0) {
                std::this_thread::sleep_until(deadline);return;
            }
        }
        while(Clock::now()<deadline)YieldProcessor();
        return;
    }
#endif
    std::this_thread::sleep_until(deadline);
}
void reset_flip(ppu_context*) {
    static thread_local buzz_pc::FramePacer pacer;
    const auto now = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    const auto due = pacer.deadline(uint64_t(ns), ps3_host_get_frame_limit()==30);
    if (due > uint64_t(ns))
        wait_until(now + std::chrono::nanoseconds(due - uint64_t(ns)));
    cellGcmResetFlipStatus();
}
}

extern "C" void buzz_register_frame_limit(void) {
    ps3_hle_register_ctx(0xB2E761D4u, "Buzz: paced flip reset", reset_flip);
}
