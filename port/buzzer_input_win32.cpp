#include "buzzer_input.h"
#include "pc_settings.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#else
#include "buzzer_sdl.h"
#include "buzzer_hid.h"
#include <SDL.h>
#include <chrono>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <mutex>

namespace {
std::mutex input_mutex;
buzz_input::Config config=buzz_input::defaults();
buzz_input::Router router;
uint64_t keyboard_held=0;
uint64_t touch_held=0;
#ifdef _WIN32
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
GetState get_state=nullptr;
bool controllers_available(){return get_state!=nullptr;}
bool read_pad(unsigned slot,uint16_t& buttons){
    XINPUT_STATE state{};
    if(!get_state||get_state(slot,&state)!=ERROR_SUCCESS)return false;
    buttons=state.Gamepad.wButtons;
    return true;
}
uint64_t ticks(){return GetTickCount64();}
#else
bool controllers_ready=false;
std::array<SDL_GameController*,4> pads{};
std::array<SDL_Joystick*,2> receivers{};
uint64_t next_scan=0;
bool controllers_available(){return controllers_ready;}
uint64_t ticks(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void scan_devices(){
    if(!controllers_ready)return;
    const auto now=ticks();
    if(now<next_scan)return;
    next_scan=now+1000;
    for(auto& pad:pads)if(pad&&!SDL_GameControllerGetAttached(pad)){
        SDL_GameControllerClose(pad);pad=nullptr;
    }
    for(auto& receiver:receivers)if(receiver&&!SDL_JoystickGetAttached(receiver)){
        SDL_JoystickClose(receiver);receiver=nullptr;
    }
    for(int index=0;index<SDL_NumJoysticks();++index){
        const auto id=SDL_JoystickGetDeviceInstanceID(index);
        if(id<0)continue;
        if(buzz_hid::supported(SDL_JoystickGetDeviceVendor(index),SDL_JoystickGetDeviceProduct(index))){
            const char* enabled=getenv("BUZZ_PHYSICAL_BUZZERS");
            if(enabled&&std::string(enabled)=="0")continue;
            auto free=std::find(receivers.begin(),receivers.end(),nullptr);
            if(free==receivers.end()||std::any_of(receivers.begin(),receivers.end(),[id](auto* r){
                return r&&SDL_JoystickInstanceID(r)==id;}))continue;
            if(auto* device=SDL_JoystickOpen(index)){
                if(SDL_JoystickNumButtons(device)>=20)*free=device;
                else SDL_JoystickClose(device);
            }
        }else if(SDL_IsGameController(index)){
            auto free=std::find(pads.begin(),pads.end(),nullptr);
            if(free!=pads.end()&&std::none_of(pads.begin(),pads.end(),[id](auto* pad){
                return pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==id;}))
                *free=SDL_GameControllerOpen(index);
        }
    }
}
bool read_pad(unsigned slot,uint16_t& buttons){
    auto* pad=pads[slot];
    if(!pad||!SDL_GameControllerGetAttached(pad))return false;
    constexpr std::pair<SDL_GameControllerButton,uint16_t> mapping[]={
        {SDL_CONTROLLER_BUTTON_DPAD_UP,1},{SDL_CONTROLLER_BUTTON_DPAD_DOWN,2},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,4},{SDL_CONTROLLER_BUTTON_DPAD_RIGHT,8},
        {SDL_CONTROLLER_BUTTON_START,0x10},{SDL_CONTROLLER_BUTTON_BACK,0x20},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK,0x40},{SDL_CONTROLLER_BUTTON_RIGHTSTICK,0x80},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,0x100},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,0x200},
        {SDL_CONTROLLER_BUTTON_A,0x1000},{SDL_CONTROLLER_BUTTON_B,0x2000},
        {SDL_CONTROLLER_BUTTON_X,0x4000},{SDL_CONTROLLER_BUTTON_Y,0x8000}};
    buttons=0;
    for(const auto& [key,bit]:mapping)if(SDL_GameControllerGetButton(pad,key))buttons|=bit;
    return true;
}
#endif
uint64_t next_pad_poll=0;
std::array<uint64_t,4> next_connect{};
std::array<bool,4> connected{};
bool menu_captured=false;
bool menu_requested=false;
bool menu_enabled=false;
std::array<uint16_t,4> previous_pad_buttons{};
std::array<uint32_t,2> physical_held{};
std::array<bool,2> physical_connected{};
std::array<uint8_t,8> remote_held{};
std::array<buzz_pc::MenuInputGate,buzz_input::provider_count> menu_gates{};
#ifdef _WIN32
bool foreground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
#else
bool focused=false; // Published by the SDL window thread under input_mutex.
bool foreground(){return focused;}
#endif
void capture_locked(bool open){
    if(menu_captured==open)return;
    menu_captured=open;
    // Key-up may be consumed by the overlay while it is open. Do not retain
    // the old event-driven down state; focused physical polling + the neutral
    // gate below will safely recover it after closing.
    keyboard_held=0;
    touch_held=0;
    for(unsigned i=0;i<menu_gates.size();++i){menu_gates[i].set_open(open);if(!open)menu_gates[i].require_neutral();router.release(i,true);}
}
void route_physical(unsigned receiver,bool active,uint64_t now) {
    const auto source=buzz_input::physical0+receiver;
    if(!physical_connected[receiver]){router.release(source,true);return;}
    const auto mask=uint64_t(physical_held[receiver])<<(receiver*20);
    const auto allowed=(uint64_t{1}<<(config.players*5))-1;
    if(!active){menu_gates[source].require_neutral();menu_gates[source].filter(mask);router.release(source,true);return;}
    router.state(source,menu_gates[source].filter(mask)&allowed,now);
}
}
void buzz_input_init(){
#ifdef _WIN32
    const auto* path=_wgetenv(L"BUZZ_INPUT_CONFIG");
#else
    const auto* path=getenv("BUZZ_INPUT_CONFIG");
#endif
    if(path){
        std::ifstream file(std::filesystem::path(path),std::ios::binary);
        if(!file){fprintf(stderr,"[input] cannot open BUZZ_INPUT_CONFIG\n");std::exit(2);}
        std::string text;char ch;
        while(text.size()<=65536&&file.get(ch))text.push_back(ch);
        std::string error;
        if(!buzz_input::parse_config(text,config,error)){fprintf(stderr,"[input] %s\n",error.c_str());std::exit(2);}
    }
    // Saved in-game preferences override the launcher's immutable snapshot.
    // Explicit legacy environment overrides below are still available to tools.
    const std::filesystem::path saved=std::filesystem::current_path()/"input-settings.ini";
    if(std::filesystem::exists(saved)&&!getenv("BUZZ_IGNORE_SAVED_INPUT")) {
        if(!std::filesystem::is_regular_file(saved)||std::filesystem::file_size(saved)>65536){fprintf(stderr,"[input] invalid saved input preferences\n");std::exit(2);}
        std::ifstream in(saved,std::ios::binary);std::string text{std::istreambuf_iterator<char>(in),{}},error;
        if(!in||!buzz_input::parse_config(text,config,error)){fprintf(stderr,"[input] saved preferences: %s\n",error.c_str());std::exit(2);}
        fprintf(stderr,"[input] loaded in-game controller preferences\n");
    }
    // Compatibility with existing scripts; explicit env overrides file values.
    for(unsigned p=0;p<4;++p){const std::string name=p?"BUZZ_KEYS_"+std::to_string(p+1):"BUZZ_KEYS";
        if(const char* value=getenv(name.c_str())){
            // Legacy BUZZ_KEYS uses numeric VK values, unlike human-readable file keys.
            unsigned values[5]{};char extra=0;
            int keys[5]{};
            if(sscanf(value,"%i,%i,%i,%i,%i %c",&keys[0],&keys[1],&keys[2],&keys[3],&keys[4],&extra)!=5){fprintf(stderr,"[input] invalid %s\n",name.c_str());std::exit(2);}
            for(unsigned b=0;b<5;++b){if(keys[b]<=0||keys[b]>255){fprintf(stderr,"[input] invalid %s key\n",name.c_str());std::exit(2);}values[b]=unsigned(keys[b]);}
            std::copy(values,values+5,config.keys[p].begin());
        }
    }
    if(const char* value=getenv("BUZZ_PULSE_MS")){
        char* end=nullptr;const auto pulse=strtoul(value,&end,10);
        if(end!=value&&!*end&&pulse>=10&&pulse<=5000)config.pulse_ms=unsigned(pulse);
    }
    router.set_pulse(config.pulse_ms);
#ifdef _WIN32
    {
        // Resolve the API even when mappings are disabled, so an in-game
        // settings change can enable controllers without a process restart.
        // Absolute System32 path, never search the working directory for a DLL.
        wchar_t path[MAX_PATH]{};const auto length=GetSystemDirectoryW(path,MAX_PATH);
        if(length&&length<MAX_PATH-18){
            wcscat_s(path,L"\\xinput1_4.dll");
            if(auto module=LoadLibraryW(path))get_state=reinterpret_cast<GetState>(GetProcAddress(module,"XInputGetState"));
        }
        if(!get_state){fprintf(stderr,"[input] XInput unavailable; keyboard remains active\n");config.gamepads=false;}
    }
#else
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    controllers_ready=SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0;
    if(!controllers_ready)config.gamepads=false;
#endif
    fprintf(stderr,"[input] logical-players=%u receivers=%u pulse=%ums gamepads=%s background-pads=%s\n",
        config.players,config.players/4,config.pulse_ms,config.gamepads?"on":"off",config.background_gamepads?"on":"off");
}
unsigned buzz_input_players(){std::lock_guard lock(input_mutex);return config.players;}
void buzz_input_capture(bool open){
    std::lock_guard lock(input_mutex);
    capture_locked(open);
}
buzz_input::Config buzz_input_current_config(){std::lock_guard lock(input_mutex);return config;}
bool buzz_input_take_menu_request(){std::lock_guard lock(input_mutex);const bool value=menu_requested;menu_requested=false;return value;}
void buzz_input_enable_menu(bool enabled){std::lock_guard lock(input_mutex);menu_enabled=enabled;if(!enabled){menu_requested=false;capture_locked(false);}}
uint16_t buzz_input_host_pad_buttons(unsigned slot){
    std::lock_guard lock(input_mutex);
    uint16_t buttons=0;
    return slot<4&&foreground()&&read_pad(slot,buttons)?buttons:0;
}
bool buzz_input_host_pad_connected(unsigned slot){
    std::lock_guard lock(input_mutex);
    uint16_t buttons=0;
    return slot<4&&read_pad(slot,buttons);
}
void buzz_input_physical_state(unsigned receiver,uint32_t buttons,bool is_connected){
    if(receiver>=2)return;
    std::lock_guard lock(input_mutex);
    physical_connected[receiver]=is_connected;physical_held[receiver]=is_connected?(buttons&0xfffff):0;
    if(!is_connected)menu_gates[buzz_input::physical0+receiver].require_neutral();
    route_physical(receiver,foreground(),ticks());
}
bool buzz_input_physical_connected(unsigned receiver){
    std::lock_guard lock(input_mutex);return receiver<2&&physical_connected[receiver];
}
uint8_t buzz_input_host_physical_buttons(){
    if(!foreground())return 0;
    std::lock_guard lock(input_mutex);uint8_t result=0;
    for(unsigned r=0;r<2;++r)if(physical_connected[r])for(unsigned p=0;p<4&&r*4+p<config.players;++p)result|=uint8_t((physical_held[r]>>(p*5))&31);
    return result;
}
bool buzz_input_reconfigure(const std::string& text,std::string& error){
    std::lock_guard lock(input_mutex);
    auto next=config;
    if(!buzz_input::parse_config(text,next,error))return false;
    if(next.players!=config.players){error="Changing the receiver count requires restarting the game";return false;}
    if(next.gamepads&&!controllers_available()){error="Gamepad input is unavailable on this system";return false;}
    // Re-map held physical buttons only after they have been released; do not
    // create answers from the button used to confirm an overlay setting.
    for(unsigned i=0;i<menu_gates.size();++i){menu_gates[i].require_neutral();router.release(i,true);}
    config=next;router.set_pulse(config.pulse_ms);keyboard_held=0;
    next_pad_poll=0;next_connect.fill(0);
    return true;
}
void buzz_input_key(uint32_t key,int down){
    std::lock_guard lock(input_mutex);
    if(!key){keyboard_held=0;router.release(buzz_input::keyboard,true);return;}
    for(unsigned p=0;p<config.players;++p)for(unsigned b=0;b<5;++b)
        if(config.keys[p][b]&&config.keys[p][b]==key){const auto bit=buzz_input::bits(p,uint8_t(1u<<b));
            if(down)keyboard_held|=bit;else keyboard_held&=~bit;}
    router.state(buzz_input::keyboard,menu_gates[buzz_input::keyboard].filter(keyboard_held),ticks());
}
uint64_t buzz_input_poll(uint32_t scripted_pulses){
    std::lock_guard lock(input_mutex);const auto now=ticks();const bool active=foreground();
    uint64_t polled=0;
#ifdef _WIN32
    if(active)for(unsigned p=0;p<config.players;++p)for(unsigned b=0;b<5;++b)
        if(config.keys[p][b]&&(GetAsyncKeyState(int(config.keys[p][b]))&0x8000))polled|=buzz_input::bits(p,uint8_t(1u<<b));
#endif
    if(!active){keyboard_held=0;router.release(buzz_input::keyboard,true);}
    router.state(buzz_input::keyboard,menu_gates[buzz_input::keyboard].filter(keyboard_held|polled),now);
    if(!active){touch_held=0;router.release(buzz_input::touchscreen,true);}
    router.state(buzz_input::touchscreen,menu_gates[buzz_input::touchscreen].filter(touch_held),now);
    for(unsigned receiver=0;receiver<2;++receiver)route_physical(receiver,active,now);
    // Phones intentionally work while the host window lacks focus. Menu gates
    // still suppress all gameplay and require a neutral sample on closing.
    for(unsigned p=0;p<config.players;++p){
        const auto source=buzz_input::remote0+p;
        router.state(source,menu_gates[source].filter(buzz_input::bits(p,remote_held[p])),now);
    }
    if(!menu_captured)router.pulse(buzz_input::script,scripted_pulses,now);
    if(controllers_available()&&now>=next_pad_poll){
        next_pad_poll=now+8;
        for(unsigned i=0;i<4;++i){
            if(!active&&!config.background_gamepads){router.release(buzz_input::gamepad0+i,true);continue;}
            if(!connected[i]&&now<next_connect[i])continue;
            uint16_t buttons=0;const bool found=read_pad(i,buttons);
            if(found!=connected[i])fprintf(stderr,"[input] Gamepad slot %u %s -> P%d\n",i,found?"connected":"disconnected",config.pad_player[i]+1);
            connected[i]=found;
            if(!found){next_connect[i]=now+1000;router.release(buzz_input::gamepad0+i,true);continue;}
            // Legacy custom START bindings remain gameplay bindings. Only the
            // new preset reserves START; never steal a configured buzzer key.
            const bool start_mapped=std::find(config.pad_buttons[i].begin(),config.pad_buttons[i].end(),uint16_t(0x10))!=config.pad_buttons[i].end();
            if(menu_enabled&&active&&!start_mapped&&(buttons&0x10)&&!(previous_pad_buttons[i]&0x10)) {
                menu_requested=true;capture_locked(true);
            }
            previous_pad_buttons[i]=buttons;
            if(!config.gamepads||config.pad_player[i]<0){router.release(buzz_input::gamepad0+i,true);continue;}
            const auto source=buzz_input::gamepad0+i;
            const auto raw=unsigned(config.pad_player[i])<config.players ? buzz_input::bits(unsigned(config.pad_player[i]),buzz_input::map_pad(buttons,config.pad_buttons[i])):0;
            router.state(source,menu_gates[source].filter(raw),now);
        }
    }
    return menu_captured?0:router.sample(now);
}
void buzz_input_remote(unsigned player,uint8_t held,uint8_t pressed,bool disconnected){
    std::lock_guard lock(input_mutex);
    if(player>=config.players)return;
    const auto source=buzz_input::remote0+player;
    remote_held[player]=disconnected?0:uint8_t(held&31);
    if(disconnected){router.release(source,true);menu_gates[source].require_neutral();return;}
    const auto now=ticks();
    // Include transient presses in the gate, so taps made inside the settings
    // menu (or before releasing an old hold) cannot escape as retained pulses.
    const auto allowed=menu_gates[source].filter(buzz_input::bits(player,uint8_t(held|pressed)));
    router.state(source,buzz_input::bits(player,held)&allowed,now);
    if(!menu_captured)router.pulse(source,buzz_input::bits(player,pressed)&allowed,now);
}


void buzz_input_touch(unsigned player,uint8_t held,bool cancel){
    std::lock_guard lock(input_mutex);
    constexpr auto source=buzz_input::touchscreen;
    if(cancel||player>=config.players||!foreground()){
        touch_held=0;router.release(source,true);menu_gates[source].require_neutral();return;
    }
    touch_held=buzz_input::bits(player,held);
    router.state(source,menu_gates[source].filter(touch_held),ticks());
}

#ifndef _WIN32
// SDL events and hardware polling run on the window thread. USB report
// callbacks consume the routed state independently on guest threads.
extern "C" void buzz_input_sdl_frame(int has_focus){
    std::lock_guard lock(input_mutex);
    focused=has_focus;
    scan_devices();
    for(unsigned slot=0;slot<receivers.size();++slot){
        auto* receiver=receivers[slot];
        const bool found=receiver&&SDL_JoystickGetAttached(receiver);
        uint32_t buttons=0;
        if(found)for(int button=0;button<20;++button)
            if(SDL_JoystickGetButton(receiver,button))buttons|=1u<<button;
        physical_held[slot]=buttons;
        physical_connected[slot]=found;
        if(!found)menu_gates[buzz_input::physical0+slot].require_neutral();
        route_physical(slot,has_focus,ticks());
    }
}
extern "C" uint32_t buzz_sdl_key_code(int key){
    if(key>=SDLK_a&&key<=SDLK_z)return unsigned(key-SDLK_a+'A');
    if(key>=SDLK_F1&&key<=SDLK_F12)return unsigned(key-SDLK_F1+112);
    if(key>=SDLK_F13&&key<=SDLK_F24)return unsigned(key-SDLK_F13+124);
    if(key>=SDLK_0&&key<=SDLK_9)return unsigned(key);
    switch(key){
    case SDLK_BACKSPACE:return 8; case SDLK_TAB:return 9; case SDLK_RETURN:return 13;
    case SDLK_ESCAPE:return 27; case SDLK_SPACE:return 32;
    case SDLK_LEFT:return 37; case SDLK_UP:return 38; case SDLK_RIGHT:return 39; case SDLK_DOWN:return 40;
    case SDLK_INSERT:return 45; case SDLK_DELETE:return 46; case SDLK_HOME:return 36; case SDLK_END:return 35;
    case SDLK_PAGEUP:return 33; case SDLK_PAGEDOWN:return 34;
    case SDLK_LSHIFT:return 160; case SDLK_RSHIFT:return 161; case SDLK_LCTRL:return 162; case SDLK_RCTRL:return 163;
    case SDLK_LALT:return 164; case SDLK_RALT:return 165;
    case SDLK_SEMICOLON:return 186; case SDLK_EQUALS:return 187; case SDLK_COMMA:return 188;
    case SDLK_MINUS:return 189; case SDLK_PERIOD:return 190; case SDLK_SLASH:return 191;
    case SDLK_BACKQUOTE:return 192; case SDLK_LEFTBRACKET:return 219; case SDLK_BACKSLASH:return 220;
    case SDLK_RIGHTBRACKET:return 221; case SDLK_QUOTE:return 222;
    default:return 0;
    }
}
#endif
