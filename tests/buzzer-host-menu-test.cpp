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

int main(){try {
    // This isolated process must not read a user's input file or legacy shell
    // mappings. Environment mutations do not escape this test executable.
    _wputenv_s(L"BUZZ_INPUT_CONFIG",L"");_putenv_s("BUZZ_IGNORE_SAVED_INPUT","1");
    for(const char* name:{"BUZZ_KEYS","BUZZ_KEYS_2","BUZZ_KEYS_3","BUZZ_KEYS_4","BUZZ_PULSE_MS"})_putenv_s(name,"");
    buzz_input_init();get_state=test_get_pad;buzz_input_enable_menu(true);
    auto poll=[](){test_time+=10;return buzz_input_poll(0);};
    check(poll()==0,"Initial neutral input");
    test_pads[0]=0x200;test_pads[1]=1;
    auto state=poll();check((state&buzz_input::bits(0,1))&&(state&buzz_input::bits(1,16)),"RB red and D-pad UP blue on separate players");
    test_pads[0]|=0x10;
    check(poll()==0&&buzz_input_take_menu_request(),"START captures immediately before a guest answer can escape");
    check(poll()==0&&!buzz_input_take_menu_request(),"Holding START does not repeatedly toggle the menu");
    buzz_input_capture(false);check(poll()==0,"Held answers cannot escape after closing");
    test_pads.fill(0);check(poll()==0,"Releasing after close is neutral");
    test_pads[0]=0x200;check((poll()&1)==1,"A fresh RB press works after release");

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
    check(buzz_input_reconfigure("gamepads=0\ngamepad.1=1,RB,LEFT,DOWN,RIGHT,UP\n",error),"Disable gameplay pads but retain menu access");
    test_pads.fill(0);poll();test_pads[0]=0x10;
    check(poll()==0&&buzz_input_take_menu_request(),"START menu still works with gameplay mappings disabled");
    test_pads.fill(0);buzz_input_capture(false);poll();
    check(buzz_input_reconfigure("gamepads=1\n",error),"Re-enable pads live");poll();
    test_pads[0]=0x200;check((poll()&1)==1,"Re-enabled RB");
    test_focused=false;check(poll()==0,"Unfocused XInput cannot retain answers");
    test_focused=true;test_connected[0]=false;check(poll()==0,"Disconnect clears the controller");
    test_time+=1001;test_connected[0]=true;test_pads.fill(0);check(poll()==0,"Neutral hotplug");
    test_pads[0]=1;check((poll()&16)==16,"Reconnected controller works");
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
