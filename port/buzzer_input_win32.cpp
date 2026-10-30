#include "buzzer_input.h"
#include "pc_settings.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
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
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
GetState get_state=nullptr;
uint64_t next_pad_poll=0;
std::array<uint64_t,4> next_connect{};
std::array<bool,4> connected{};
bool menu_captured=false;
bool menu_requested=false;
bool menu_enabled=false;
std::array<uint16_t,4> previous_pad_buttons{};
std::array<uint32_t,2> physical_held{};
std::array<bool,2> physical_connected{};
std::array<buzz_pc::MenuInputGate,buzz_input::provider_count> menu_gates{};
bool foreground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
void capture_locked(bool open){
    if(menu_captured==open)return;
    menu_captured=open;
    // Key-up may be consumed by the overlay while it is open. Do not retain
    // the old event-driven down state; focused physical polling + the neutral
    // gate below will safely recover it after closing.
    keyboard_held=0;
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
    if(const wchar_t* path=_wgetenv(L"BUZZ_INPUT_CONFIG")){
        std::ifstream file(std::filesystem::path(path),std::ios::binary);
        if(!file){fprintf(stderr,"[input] cannot open config: %ls\n",path);std::exit(2);}
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
    fprintf(stderr,"[input] logical-players=%u receivers=%u pulse=%ums XInput=%s background-pads=%s\n",
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
    if(slot>=4||!get_state||!foreground())return 0;
    XINPUT_STATE s{};return get_state(slot,&s)==ERROR_SUCCESS?s.Gamepad.wButtons:0;
}
bool buzz_input_host_pad_connected(unsigned slot){
    if(slot>=4||!get_state)return false;
    XINPUT_STATE s{};return get_state(slot,&s)==ERROR_SUCCESS;
}
void buzz_input_physical_state(unsigned receiver,uint32_t buttons,bool is_connected){
    if(receiver>=2)return;
    std::lock_guard lock(input_mutex);
    physical_connected[receiver]=is_connected;physical_held[receiver]=is_connected?(buttons&0xfffff):0;
    if(!is_connected)menu_gates[buzz_input::physical0+receiver].require_neutral();
    route_physical(receiver,foreground(),GetTickCount64());
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
    if(next.gamepads&&!get_state){error="XInput is unavailable on this system";return false;}
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
    router.state(buzz_input::keyboard,menu_gates[buzz_input::keyboard].filter(keyboard_held),GetTickCount64());
}
uint64_t buzz_input_poll(uint32_t scripted_pulses){
    std::lock_guard lock(input_mutex);const auto now=GetTickCount64();const bool active=foreground();
    uint64_t polled=0;
    if(active)for(unsigned p=0;p<config.players;++p)for(unsigned b=0;b<5;++b)
        if(config.keys[p][b]&&(GetAsyncKeyState(int(config.keys[p][b]))&0x8000))polled|=buzz_input::bits(p,uint8_t(1u<<b));
    if(!active){keyboard_held=0;router.release(buzz_input::keyboard,true);}
    router.state(buzz_input::keyboard,menu_gates[buzz_input::keyboard].filter(keyboard_held|polled),now);
    for(unsigned receiver=0;receiver<2;++receiver)route_physical(receiver,active,now);
    if(!menu_captured)router.pulse(buzz_input::script,scripted_pulses,now);
    if(get_state&&now>=next_pad_poll){
        next_pad_poll=now+8;
        for(unsigned i=0;i<4;++i){
            if(!active&&!config.background_gamepads){router.release(buzz_input::gamepad0+i,true);continue;}
            if(!connected[i]&&now<next_connect[i])continue;
            XINPUT_STATE state{};const bool found=get_state(i,&state)==ERROR_SUCCESS;
            if(found!=connected[i])fprintf(stderr,"[input] XInput slot %u %s -> P%d\n",i,found?"connected":"disconnected",config.pad_player[i]+1);
            connected[i]=found;
            if(!found){next_connect[i]=now+1000;router.release(buzz_input::gamepad0+i,true);continue;}
            // Legacy custom START bindings remain gameplay bindings. Only the
            // new preset reserves START; never steal a configured buzzer key.
            const bool start_mapped=std::find(config.pad_buttons[i].begin(),config.pad_buttons[i].end(),uint16_t(0x10))!=config.pad_buttons[i].end();
            if(menu_enabled&&active&&!start_mapped&&(state.Gamepad.wButtons&0x10)&&!(previous_pad_buttons[i]&0x10)) {
                menu_requested=true;capture_locked(true);
            }
            previous_pad_buttons[i]=state.Gamepad.wButtons;
            if(!config.gamepads||config.pad_player[i]<0){router.release(buzz_input::gamepad0+i,true);continue;}
            const auto source=buzz_input::gamepad0+i;
            const auto raw=unsigned(config.pad_player[i])<config.players ? buzz_input::bits(unsigned(config.pad_player[i]),buzz_input::map_pad(state.Gamepad.wButtons,config.pad_buttons[i])):0;
            router.state(source,menu_gates[source].filter(raw),now);
        }
    }
    return menu_captured?0:router.sample(now);
}
