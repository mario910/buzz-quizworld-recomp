#pragma once
#include <cstdint>

namespace buzz_pc {
// Preserve the 30/60 Hz phase through small scheduler/render overruns. Rebase
// after a whole missed period so loading and menu pauses never accrue debt.
struct FramePacer {
    static constexpr uint64_t period_ns=1000000000ull/30;
    uint64_t next_ns=0;
    unsigned fps=0;
    uint64_t deadline(uint64_t now,unsigned limit) {
        if(limit!=30&&limit!=60){next_ns=0;fps=0;return now;}
        const uint64_t period=1000000000ull/limit;
        if(fps!=limit||!next_ns || (now>=next_ns && now-next_ns>=period)) {
            fps=limit;next_ns=now+period;return now;
        }
        const auto due=next_ns;
        next_ns+=period;
        return now>due?now:due;
    }
};
}
