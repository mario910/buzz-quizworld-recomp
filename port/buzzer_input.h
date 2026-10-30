#pragma once
// Platform-independent input contract. Providers never know about USB/guest RAM.
#include <array>
#include <cstdint>
#include <string>

namespace buzz_input {
constexpr unsigned max_players=8, button_count=5, provider_count=16;
constexpr uint64_t valid_mask=(uint64_t{1}<<40)-1;
enum Button : unsigned { red, yellow, green, orange, blue };
enum Provider : unsigned { keyboard=0, gamepad0=1, script=5, remote0=6, physical0=14 };
constexpr uint64_t bits(unsigned player, uint8_t buttons) {
    return player<max_players ? uint64_t(buttons&31u)<<(player*5) : 0;
}
constexpr uint32_t receiver_bits(uint64_t state,unsigned receiver) {
    return receiver<2 ? uint32_t((state>>(receiver*20))&0xfffffu) : 0;
}

// Serialized by the host adapter. Independent providers cannot release each
// other's buttons. Rising edges survive a short down/up between USB polls.
class Router {
    struct Source { uint64_t held=0; std::array<uint64_t,40> until{}; };
    std::array<Source,provider_count> sources{};
    unsigned pulse_ms;
public:
    explicit Router(unsigned pulse=80):pulse_ms(pulse){}
    void set_pulse(unsigned pulse){pulse_ms=pulse;}
    void pulse(unsigned source,uint64_t mask,uint64_t now) {
        if(source>=sources.size())return;
        auto& s=sources[source];
        const uint64_t end=now>UINT64_MAX-pulse_ms ? UINT64_MAX : now+pulse_ms;
        for(unsigned i=0;i<40;++i)if(mask&(uint64_t{1}<<i))
            if(s.until[i]<end)s.until[i]=end;
    }
    void state(unsigned source,uint64_t mask,uint64_t now) {
        if(source>=sources.size())return;
        mask&=valid_mask;
        pulse(source,mask&~sources[source].held,now);
        sources[source].held=mask;
    }
    void release(unsigned source,bool discard_pulses=false) {
        if(source>=sources.size())return;
        sources[source].held=0;
        if(discard_pulses)sources[source].until.fill(0);
    }
    uint64_t sample(uint64_t now) const {
        uint64_t result=0;
        for(const auto& s:sources){
            result|=s.held;
            for(unsigned i=0;i<40;++i)if(s.until[i]>now)result|=uint64_t{1}<<i;
        }
        return result&valid_mask;
    }
};

struct Config {
    unsigned players=4, pulse_ms=250;
    bool gamepads=true, background_gamepads=false;
    std::array<std::array<unsigned,5>,8> keys{};
    // XInput slots 0..3; each may be assigned to any one logical player.
    std::array<int,4> pad_player{{0,1,2,3}};
    std::array<std::array<uint16_t,5>,4> pad_buttons{};
};
Config defaults();
// Strict, transactional parser: unknown/duplicate entries and invalid mappings
// are errors; no partially applied configuration escapes on failure.
bool parse_config(const std::string& text,Config& config,std::string& error);
bool parse_keys(const std::string& text,std::array<unsigned,5>& keys);
uint8_t map_pad(uint16_t buttons,const std::array<uint16_t,5>& mapping);
const char* pad_button_name(uint16_t button);
}

void buzz_input_init();
void buzz_input_key(uint32_t key,int down);
uint64_t buzz_input_poll(uint32_t scripted_pulses);
unsigned buzz_input_players();
// Host PC-menu controls. Capture flushes retained taps and suppresses gameplay
// input until release. Reconfiguration is transactional; receiver count is fixed
// for this run (changing 4/8 players requires USB reinitialization/restart).
void buzz_input_capture(bool open);
bool buzz_input_reconfigure(const std::string& ini,std::string& error);
buzz_input::Config buzz_input_current_config();
bool buzz_input_take_menu_request();
void buzz_input_enable_menu(bool enabled);
uint16_t buzz_input_host_pad_buttons(unsigned slot); // focused host input, not guest input
bool buzz_input_host_pad_connected(unsigned slot);
bool buzz_input_format_config(const buzz_input::Config&,std::string& ini,std::string& error);
// Host HID receivers are independent sources, not guest USB devices. Receiver
// slots map to P1-P4/P5-P8; the second requires the existing eight-player mode.
void buzz_input_physical_state(unsigned receiver,uint32_t buttons,bool connected);
bool buzz_input_physical_connected(unsigned receiver);
uint8_t buzz_input_host_physical_buttons(); // OR of handsets, focused menu navigation only
