#pragma once
#ifdef _WIN32
#include <windows.h>

namespace buzz {
// One timer belongs to the RSX thread. The FIFO event remains first in the
// wait set, so a kick arriving before/during the timeout is never discarded.
class RsxIdleWait {
    HANDLE timer_ = nullptr;
    bool attempted_ = false;
public:
    enum class Result { fallback, kick, timeout };
    RsxIdleWait() = default;
    RsxIdleWait(const RsxIdleWait&) = delete;
    RsxIdleWait& operator=(const RsxIdleWait&) = delete;
    ~RsxIdleWait() { if(timer_) CloseHandle(timer_); }
    Result wait(HANDLE kick, unsigned long long ns, bool poll_first) {
        if(!ns || (poll_first && kick)) {
            if(kick && WaitForSingleObject(kick, 0)==WAIT_OBJECT_0) return Result::kick;
            if(!ns) return Result::timeout;
        }
        if(!attempted_) {
            attempted_=true;
            timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        }
        if(!timer_) return Result::fallback;
        // Round upward to 100 ns; callers admit at most 16 ms.
        LARGE_INTEGER due; due.QuadPart=-static_cast<LONGLONG>((ns+99)/100);
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)) return Result::fallback;
        HANDLE handles[2]={kick ? kick : timer_,timer_};
        const DWORD result=WaitForMultipleObjects(kick ? 2 : 1,handles,FALSE,INFINITE);
        if(result==WAIT_OBJECT_0) return kick ? Result::kick : Result::timeout;
        if(kick && result==WAIT_OBJECT_0+1) return Result::timeout;
        return Result::fallback;
    }
};
}
#endif
