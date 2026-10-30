// Initialize the real Win32 input adapter, without a game or any window.
#include "../port/buzzer_input.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv){
    assert(argc==2);
    buzz_input_init();
    buzz_input_enable_menu(true);
    assert(!buzz_input_take_menu_request());
    const auto initial_config=buzz_input_current_config();
    assert(initial_config.pad_buttons[0][0]==0x200); // RB = red; START reserved
    assert(buzz_input_players()==unsigned(std::atoi(argv[1])));
    // Internal scripted input only. This does not inject an OS key event.
    assert((buzz_input_poll(1)&1)==1);
    buzz_input_enable_menu(false);
    buzz_input_capture(true);
    assert(buzz_input_poll(1)==0); // retained and new taps cannot leak into the game
    buzz_input_capture(false);
    assert(buzz_input_poll(0)==0);
    std::string error;
    assert(buzz_input_reconfigure("gamepad.1=1,RB,LEFT,DOWN,RIGHT,UP\npulse_ms=300\n",error));
    assert(!buzz_input_reconfigure("players=6\n",error));
    const auto count=buzz_input_players();
    assert(!buzz_input_reconfigure(count==4?"players=8\n":"players=4\n",error));
    assert(buzz_input_players()==count);
    assert(buzz_input_reconfigure("gamepads=0\n",error));
    assert(buzz_input_reconfigure("gamepads=1\n",error));
    assert((buzz_input_poll(1)&1)==1);
    std::puts("PASS: native input adapter loaded Unicode config and internal scripted input; no window/game");
}
