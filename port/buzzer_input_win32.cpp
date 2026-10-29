#include "buzzer_input.h"
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
bool foreground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
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
    if(config.gamepads){
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
unsigned buzz_input_players(){return config.players;}
void buzz_input_key(uint32_t key,int down){
    std::lock_guard lock(input_mutex);
    if(!key){keyboard_held=0;router.release(buzz_input::keyboard);return;}
    for(unsigned p=0;p<config.players;++p)for(unsigned b=0;b<5;++b)
        if(config.keys[p][b]&&config.keys[p][b]==key){const auto bit=buzz_input::bits(p,uint8_t(1u<<b));
            if(down)keyboard_held|=bit;else keyboard_held&=~bit;}
    router.state(buzz_input::keyboard,keyboard_held,GetTickCount64());
}
uint64_t buzz_input_poll(uint32_t scripted_pulses){
    std::lock_guard lock(input_mutex);const auto now=GetTickCount64();const bool active=foreground();
    uint64_t polled=0;
    if(active)for(unsigned p=0;p<config.players;++p)for(unsigned b=0;b<5;++b)
        if(config.keys[p][b]&&(GetAsyncKeyState(int(config.keys[p][b]))&0x8000))polled|=buzz_input::bits(p,uint8_t(1u<<b));
    if(!active)keyboard_held=0;
    router.state(buzz_input::keyboard,keyboard_held|polled,now);
    router.pulse(buzz_input::script,scripted_pulses,now);
    if(config.gamepads&&now>=next_pad_poll){
        next_pad_poll=now+8;
        for(unsigned i=0;i<4;++i){
            if(config.pad_player[i]<0){router.release(buzz_input::gamepad0+i,true);continue;}
            if(!active&&!config.background_gamepads){router.release(buzz_input::gamepad0+i,true);continue;}
            if(!connected[i]&&now<next_connect[i])continue;
            XINPUT_STATE state{};const bool found=get_state(i,&state)==ERROR_SUCCESS;
            if(found!=connected[i])fprintf(stderr,"[input] XInput slot %u %s -> P%d\n",i,found?"connected":"disconnected",config.pad_player[i]+1);
            connected[i]=found;
            if(!found){next_connect[i]=now+1000;router.release(buzz_input::gamepad0+i,true);continue;}
            router.state(buzz_input::gamepad0+i,
                unsigned(config.pad_player[i])<config.players ? buzz_input::bits(unsigned(config.pad_player[i]),buzz_input::map_pad(state.Gamepad.wButtons,config.pad_buttons[i])):0,now);
        }
    }
    return router.sample(now);
}
