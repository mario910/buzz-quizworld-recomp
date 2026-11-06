#pragma once
#include <cstdint>
namespace buzz {
// A rational 60 Hz clock. Integer milliseconds would run at 62.5 Hz.
class VblankClock {
    uint64_t next_;
    unsigned remainder_ = 0;
public:
    explicit VblankClock(uint64_t now):next_(now){}
    bool due(uint64_t now) const { return now >= next_; }
    uint64_t deadline() const { return next_; }
    unsigned wait_ms(uint64_t now, unsigned maximum) const {
        if (due(now)) return 0;
        const uint64_t remaining = (next_ - now + 999999) / 1000000;
        return remaining < maximum ? unsigned(remaining) : maximum;
    }
    void reset(uint64_t now) { next_=now; remainder_=0; }
    void advance() { next_+=1000000000ull/60; remainder_+=1000000000u%60;
        if(remainder_>=60){++next_;remainder_-=60;} }
};
}
