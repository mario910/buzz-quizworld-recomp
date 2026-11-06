#include "../port/rsx_idle_wait.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <initializer_list>
#include <thread>

int main(int argc, char**) {
#ifdef _WIN32
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); assert(event);
    buzz::RsxIdleWait timer;
    using R=buzz::RsxIdleWait::Result;
    for(bool poll : {false,true}) {
        // Auto-reset kicks must be consumed once, including zero-budget polls.
        SetEvent(event); assert(timer.wait(event,0,poll)==R::kick);
        assert(timer.wait(event,0,poll)==R::timeout);
        SetEvent(event); assert(timer.wait(event,1000000000,poll)==R::kick);
        assert(timer.wait(event,0,poll)==R::timeout);
        assert(timer.wait(event,1000000,poll)==R::timeout);
        // Wake while blocked, rather than waiting for the one-second timer.
        std::thread producer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10));SetEvent(event);});
        assert(timer.wait(event,1000000000,poll)==R::kick); producer.join();
        // Rearm after a kick; the previous deadline cannot leak into this wait.
        assert(timer.wait(nullptr,1000000,poll)==R::timeout);
    }
    puts("PASS RSX idle: prequeued/during-wait kicks, timeout, zero poll, rearm, disabled kick");
    if(argc>1) {
        for(unsigned round=0;round<3;++round) for(bool poll : {false,true,true,false}) {
            const auto start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<20000;++n){SetEvent(event);assert(timer.wait(event,4000000,poll)==R::kick);}
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            printf("queued-kick poll=%d calls=20000 ms=%.3f\n",poll,ms);
        }
    }
    CloseHandle(event);
#endif
}
