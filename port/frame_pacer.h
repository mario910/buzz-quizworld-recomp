#pragma once
#include <cstdint>

namespace buzz_pc {
// Preserve the 30 Hz phase through small scheduler/render overruns. Rebase
// after a whole missed period so loading and menu pauses never accrue debt.
struct FramePacer {
    static constexpr uint64_t period_ns=1000000000ull/30;
    uint64_t next_ns=0;
    uint64_t deadline(uint64_t now,bool enabled) {
        if(!enabled){next_ns=0;return now;}
        if(!next_ns || (now>=next_ns && now-next_ns>=period_ns)) {
            next_ns=now+period_ns;return now;
        }
        const auto due=next_ns;
        next_ns+=period_ns;
        return now>due?now:due;
    }
};
}
