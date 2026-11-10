#include "../port/android_pause_state.h"
#include <cstdio>
#include <cstdlib>
static void check(bool value) { if(!value)std::abort(); }
int main() {
    buzz_android::PauseState state;
    check(state.paused()); // Not visible before surface/lifecycle readiness.
    state.visible=true; check(!state.paused());
    state.menu=true; check(state.paused());
    state.visible=false; check(state.paused());
    state.visible=true; check(state.paused()); // Resume cannot close the menu.
    state.menu=false; check(!state.paused());
    state.visible=false; state.menu=true; state.menu=false;
    check(state.paused()); // Closing the menu cannot resume a hidden game.
    state.visible=true; check(!state.paused());
    for(unsigned mask=0;mask<4;++mask) {
        state.visible=mask&1;state.menu=mask&2;
        check(state.paused()==(!(mask&1)||bool(mask&2)));
    }
    std::puts("PASS Android pause owners: visibility and menu, initial/background/resume/close order");
}
