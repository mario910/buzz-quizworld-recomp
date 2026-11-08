#pragma once
#include <cstdint>

namespace buzz_pc {
// Counts game frames, not host redraws (e.g. the paused settings menu).
// Call only while visible; no clocks, readbacks or profiling in the off path.
class PerformanceSample {
    uint64_t start_ms_=0,first_frame_=0,last_frame_=0;
    bool started_=false;
public:
    double fps=0;
    bool ready=false;
    void reset(){*this=PerformanceSample{};}
    void update(uint64_t now_ms,uint64_t frame,bool paused) {
        if(paused){reset();return;}
        if(!started_||now_ms<start_ms_||frame<last_frame_) {
            start_ms_=now_ms;first_frame_=last_frame_=frame;started_=true;ready=false;return;
        }
        last_frame_=frame;
        if(now_ms-start_ms_>=500) {
            fps=double(frame-first_frame_)*1000.0/double(now_ms-start_ms_);
            ready=true;start_ms_=now_ms;first_frame_=frame;
        }
    }
};
}
