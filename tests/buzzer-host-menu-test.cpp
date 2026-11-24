// Exercise the real Windows adapter with synthetic OS reads. No desktop key
// events, foreground changes, real gamepad polling or game window are used.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
ULONGLONG test_time=100;
bool test_focused=true;
std::array<uint16_t,4> test_pads{};
std::array<bool,4> test_connected{{true,true,true,true}};
std::array<bool,256> test_keys{};
HWND test_foreground(){return reinterpret_cast<HWND>(uintptr_t(1));}
DWORD test_window_pid(HWND,DWORD* pid){*pid=test_focused?GetCurrentProcessId():0;return 1;}
SHORT test_key_state(int key){return key>=0&&key<256&&test_keys[unsigned(key)]?SHORT(0x8000):0;}
ULONGLONG test_ticks(){return test_time;}
DWORD WINAPI test_get_pad(DWORD slot,XINPUT_STATE* state){
    if(slot>=4||!test_connected[slot])return ERROR_DEVICE_NOT_CONNECTED;
    *state={};state->Gamepad.wButtons=test_pads[slot];return ERROR_SUCCESS;
}
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
#define GetForegroundWindow test_foreground
#define GetWindowThreadProcessId test_window_pid
#define GetAsyncKeyState test_key_state
#define GetTickCount64 test_ticks
#include "../port/buzzer_input_win32.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetAsyncKeyState
#undef GetTickCount64
uint32_t phones_released=0;
void record_phone_release(uint32_t mask){phones_released|=mask;}

int main(){try {
    // This isolated process must not read a user's input file or legacy shell
    // mappings. Environment mutations do not escape this test executable.
    _wputenv_s(L"BUZZ_INPUT_CONFIG",L"");_putenv_s("BUZZ_IGNORE_SAVED_INPUT","1");
    for(const char* name:{"BUZZ_KEYS","BUZZ_KEYS_2","BUZZ_KEYS_3","BUZZ_KEYS_4","BUZZ_PULSE_MS"})_putenv_s(name,"");
    buzz_input_init();get_state=test_get_pad;buzz_input_enable_menu(true);
    // Fixture topology: four players (one receiver) until the eight-player part
    // below; eight is the shipped default. Pads use the default AUTO slots.
    config.players=4;
    auto poll=[](){test_time+=10;return buzz_input_poll(0);};
    // The game's player screen, as seen in the real game: lights blink all on <-> joined.
    auto open_join=[]{buzz_input_lights(0,15);test_time+=300;buzz_input_lights(0,0);test_time+=300;buzz_input_lights(0,15);};
    auto close_join=[&]{test_time+=buzz_input::JoinWatch::window_ms+100;poll();};
    buzz_input_set_phone_release(record_phone_release);
    check(poll()==0,"Initial neutral input");
    test_pads[0]=0x100;auto state=poll();
    check(buzz_input_pad_auto_player(0)==-1&&(state&1),"Outside the player screen an unjoined pad drives P1 for menus without taking a place");
    test_pads[0]=0;poll();
    open_join();check(buzz_input_join_open(),"Blinking lights open the player screen");
    test_pads[0]=0x100;test_pads[1]=0x8000;
    state=poll();check((state&buzz_input::bits(0,1))&&(state&buzz_input::bits(1,16)),"LB red and Triangle/Y blue join as separate players");
    test_pads[0]|=0x10;
    check(poll()==0&&buzz_input_take_menu_request(),"START captures immediately before a guest answer can escape");
    check(poll()==0&&!buzz_input_take_menu_request(),"Holding START does not repeatedly toggle the menu");
    buzz_input_capture(false);check(poll()==0,"Held answers cannot escape after closing");
    test_pads.fill(0);check(poll()==0,"Releasing after close is neutral");
    test_pads[0]=0x100;check((poll()&1)==1,"A fresh LB press works after release");
    test_pads.fill(0);close_join();
    check(!buzz_input_join_open()&&buzz_input_pad_auto_player(0)==-1&&buzz_input_pad_auto_player(1)==-1,
          "Places the game did not show as joined are released when the screen closes");

    {   // Keyboard key sets and pads are equal devices: press order on the player screen decides.
        open_join();
        test_keys[32]=true;buzz_input_key(32,1);poll();check(key_player[0]==0,"Keyboard joins first -> P1");
        test_keys[32]=false;buzz_input_key(32,0);poll();
        test_pads[0]=0x100;auto joined=poll();
        check(buzz_input_pad_auto_player(0)==1&&(joined&buzz_input::bits(1,1)),"Pad joins next -> P2, not the keyboard's P1");
        test_pads[0]=0;poll();
        test_pads[1]=0x8000;poll();check(buzz_input_pad_auto_player(1)==2,"Second pad -> P3");test_pads[1]=0;poll();
        buzz_input_set_phone_slots(1u<<3); // a phone took P4
        check((buzz_input_pad_slots()&0b111)==0b111,"Phones see the local places as taken");
        buzz_input_lights(0,3);test_time+=300;buzz_input_lights(0,15); // the game shows P1+P2 joined
        close_join();
        check(key_player[0]==0&&buzz_input_pad_auto_player(0)==1&&buzz_input_pad_auto_player(1)==-1,"Joined places stay, P3 is released");
        check(phones_released==(1u<<3),"The unjoined phone place is released");
        buzz_input_set_phone_slots(0);
        test_pads[1]=0x100;check(poll()==0&&buzz_input_pad_auto_player(1)==-1,"An unjoined pad is idle while P1 belongs to a joined device");
        test_pads[1]=0;poll();
        // Pressed before the blinking was recognised: the press already joined P1 in the game.
        std::string reset_error;check(buzz_input_reconfigure("pulse_ms=250\n",reset_error),"Reset places");
        test_pads[2]=0x100;poll();test_pads[2]=0;poll();
        buzz_input_lights(0,15);test_time+=300;buzz_input_lights(0,1);test_time+=300;buzz_input_lights(0,15);
        test_pads[3]=0x100;poll();test_pads[3]=0;poll();
        test_pads[2]=0x100;poll();test_pads[2]=0;poll();
        check(buzz_input_pad_auto_player(2)==0&&buzz_input_pad_auto_player(3)==1,"The device that joined P1 early owns P1");
        close_join();check(buzz_input_reconfigure("pulse_ms=250\n",reset_error),"Reset places");poll();
    }

    buzz_input_capture(true);test_pads.fill(0);test_keys[32]=true;buzz_input_key(32,1);
    check(poll()==0,"Menu captures keyboard input");
    test_keys[32]=false; // window consumes key-up: deliberately no adapter event
    buzz_input_capture(false);check(poll()==0,"Closing clears stale event-driven keyboard state");
    test_keys[32]=true;buzz_input_key(32,1);check((poll()&1)==1,"Keyboard remains usable after captured key-up");
    test_keys[32]=false;buzz_input_key(32,0);

    std::string error;
    check(buzz_input_reconfigure("gamepad.1=1,START,LEFT,DOWN,RIGHT,UP\n",error),"Legacy mapping applied");
    test_pads.fill(0);poll();test_pads[0]=0x10;
    check((poll()&1)==1&&!buzz_input_take_menu_request(),"Custom START remains a gameplay binding");
    // Legacy gamepads=0 from old settings files is accepted but ignored.
    check(buzz_input_reconfigure("gamepads=0\ngamepad.1=1,RB,LEFT,DOWN,RIGHT,UP\n",error),"Legacy gamepads=0 still parses");
    test_pads.fill(0);poll();test_pads[0]=0x10;
    check(poll()==0&&buzz_input_take_menu_request(),"START opens the menu with the default preset");
    test_pads.fill(0);buzz_input_capture(false);poll();
    test_pads[0]=0x1;check(poll()==0,"An unchanged old RB+D-pad default migrates: the D-pad is free");
    test_pads.fill(0);poll();
    test_pads[0]=0x200;check((poll()&1)==1,"Migrated default: RB is red");
    test_pads.fill(0);poll();
    test_pads[0]=0x100;check((poll()&1)==1,"Pads stay on despite legacy gamepads=0; LB is red");
    test_pads.fill(0);poll();
    check(buzz_input_reconfigure("gamepad.1=NONE\n",error),"Unassign slot 1");poll();
    test_pads[0]=0x100;check(poll()==0,"An unassigned slot answers nothing");
    test_pads.fill(0);poll();
    check(buzz_input_reconfigure("gamepad.1=1,LB,A,B,X,Y\n",error),"Reassign slot 1");poll();
    test_pads[0]=0x100;check((poll()&1)==1,"Reassigned LB");
    test_focused=false;check(poll()==0,"Unfocused XInput cannot retain answers");
    test_focused=true;test_connected[0]=false;check(poll()==0,"Disconnect clears the controller");
    test_time+=1001;test_connected[0]=true;test_pads.fill(0);check(poll()==0,"Neutral hotplug");
    test_pads[0]=0x8000;check((poll()&16)==16,"Reconnected controller works");
    test_pads.fill(0);buzz_input_capture(true);buzz_input_capture(false);poll();
    test_keys[32]=true;buzz_input_key(32,1);check((poll()&1)==1,"Fresh keyboard answer");
    test_focused=false;buzz_input_key(0,0);check(poll()==0,"Focus loss must discard retained keyboard taps");
    test_keys[32]=false;test_focused=true;check(poll()==0,"No stale keyboard tap on refocus");
    test_pads.fill(0);buzz_input_capture(true);buzz_input_capture(false);poll();
    buzz_input_physical_state(0,0,true);buzz_input_physical_state(1,0,true);
    buzz_input_physical_state(0,1,true);buzz_input_physical_state(1,1u<<19,true);
    check((poll()&1)==1&&(poll()&buzz_input::bits(7,16))==0,"Four-player topology ignores receiver two");
    check(buzz_input_physical_connected(0)&&buzz_input_physical_connected(1),"Both physical receivers remain detectable");
    buzz_input_capture(true);check(poll()==0,"Menu suppresses physical receivers");
    check((buzz_input_host_physical_buttons()&1)==1,"Menu can read focused physical colours separately");
    buzz_input_capture(false);check(poll()==0,"Held physical red cannot answer on close");
    buzz_input_physical_state(0,0,true);poll();buzz_input_physical_state(0,1,true);
    check((poll()&1)==1,"Physical red works after one release sample");
    test_focused=false;check(poll()==0&&buzz_input_host_physical_buttons()==0,"Unfocused physical input stays out of gameplay/menu");
    test_focused=true;check(poll()==0,"Refocusing cannot re-submit a held physical button");
    buzz_input_physical_state(0,0,true);poll();
    // Fixture-only startup topology. Public live reconfiguration still rejects
    // count changes; virtual-buzzer tests independently cover two USB devices.
    config.players=8;buzz_input_physical_state(1,0,true);poll();
    buzz_input_physical_state(1,1u<<19,true);check((poll()&buzz_input::bits(7,16))!=0,"Receiver two reaches player eight");
    buzz_input_physical_state(1,0,false);check((poll()&buzz_input::bits(7,16))==0,"Disconnect discards physical pulses immediately");
    test_keys[32]=true;buzz_input_key(32,1);buzz_input_physical_state(0,1u<<9,true);
    check((poll()&1)&&(poll()&(1u<<9)),"Keyboard and HID are independent sources");
    buzz_input_physical_state(0,0,false);check((poll()&1)&&!(poll()&(1u<<9)),"HID disconnect cannot release keyboard answers");
    test_keys[32]=false;buzz_input_key(32,0);
    buzz_input_capture(true);buzz_input_capture(false);poll();
    test_focused=false;
    buzz_input_remote(7,16,16,false);check((poll()&buzz_input::bits(7,16))!=0,"Phone P8 works without host focus");
    buzz_input_remote(7,0,0,false);check((poll()&buzz_input::bits(7,16))!=0,"Short phone taps are retained for USB polls");
    buzz_input_remote(7,0,0,true);check((poll()&buzz_input::bits(7,16))==0,"Phone disconnect discards retained taps");
    buzz_input_remote(7,0,0,false);buzz_input_remote(7,1,1,false);
    check((poll()&buzz_input::bits(7,1))!=0,"Phone reconnect after neutral");
    buzz_input_capture(true);buzz_input_remote(7,31,31,false);check(poll()==0,"Menu suppresses all phone buttons");
    buzz_input_capture(false);check(poll()==0,"Phone held during menu cannot answer after close");
    buzz_input_remote(7,0,0,false);poll();buzz_input_remote(7,0,2,false);
    check((poll()&buzz_input::bits(7,2))!=0,"A fast phone pulse after menu release is routed");
    buzz_input_remote(7,0,0,true);test_focused=true;
    buzz_input_key(32,1);buzz_input_remote(7,8,8,false);buzz_input_remote(7,0,0,true);
    check((poll()&1)&&!(poll()&buzz_input::bits(7,31)),"Removing a phone cannot release keyboard input");
    buzz_input_enable_menu(false);
    std::cout<<"PASS real Win32 input adapter: synthetic START capture, RB/D-pad, held-key release, legacy START, live remap, focus, hotplug and two HID receivers\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
