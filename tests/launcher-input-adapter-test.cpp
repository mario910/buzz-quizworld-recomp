// Initialize the real Win32 input adapter, without a game or any window.
#include "../port/buzzer_input.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv){
    assert(argc==2);
    buzz_input_init();
    assert(buzz_input_players()==unsigned(std::atoi(argv[1])));
    // Internal scripted input only. This does not inject an OS key event.
    assert((buzz_input_poll(1)&1)==1);
    std::puts("PASS: native input adapter loaded Unicode config and internal scripted input; no window/game");
}
