#pragma once
// Platform-independent input contract. Providers never know about USB/guest RAM.
#include <array>
#include <cstdint>
#include <string>

namespace buzz_input {
constexpr unsigned max_players=8, button_count=5, provider_count=17;
constexpr uint64_t valid_mask=(uint64_t{1}<<40)-1;
enum Button : unsigned { red, yellow, green, orange, blue };
enum Provider : unsigned { keyboard=0, gamepad0=1, script=5, remote0=6, physical0=14, touchscreen=16 };
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

// The game's player-join screen, read from the handset lights it sends to one
// receiver: they blink between "all four on" and "only the joined players".
// Each return to all-on confirms the state before it as the joined set, so
// joining is open while those confirmations keep coming. A steady state, or the
// last change after the screen ends, is never mistaken for joined players.
class JoinWatch {
    uint8_t state_=0, joined_=0;
    uint64_t confirmed_at_=0;
    bool seen_=false;
public:
    static constexpr uint64_t window_ms=2000;
    void lights(uint8_t four,uint64_t now) {
        four&=15;
        if(seen_&&four==state_)return;
        if(seen_&&four==15&&state_!=15){joined_=state_;confirmed_at_=now?now:1;}
        state_=four;seen_=true;
    }
    bool open(uint64_t now) const {return confirmed_at_&&now>=confirmed_at_&&now-confirmed_at_<=window_ms;}
    uint8_t joined() const {return joined_;} // last confirmed joined handsets (bits 0-3)
    // At close: when the last free handset joins, the blinking stops with every
    // light steady on, so the lights the game leaves on also count as joined.
    uint8_t settled() const {return uint8_t(joined_|state_);}
};
struct Config {
    // Eight player slots by default; four remains available (restart).
    unsigned players=8, pulse_ms=250;
    // Gamepad support is always on. Older files may still carry gamepads=0/1;
    // the parser accepts and ignores it.
    bool background_gamepads=false;
    std::array<std::array<unsigned,5>,8> keys{};
    // One button layout shared by every gamepad (saved as "gamepad="). Pads
    // have no fixed player: each joins on the game's player screen.
    std::array<uint16_t,5> pad_buttons{};
};
Config defaults();
// Strict, transactional parser: unknown/duplicate entries and invalid mappings
// are errors; no partially applied configuration escapes on failure.
// Older files with per-slot "gamepad.N=player,buttons" lines still load: the
// lowest slot's buttons become the shared layout and the players are dropped.
// A layout equal to an earlier default becomes the current default.
bool parse_config(const std::string& text,Config& config,std::string& error);
bool parse_keys(const std::string& text,std::array<unsigned,5>& keys);
uint8_t map_pad(uint16_t buttons,const std::array<uint16_t,5>& mapping);
// Earlier defaults (START,Y,A,B,X; RB,LEFT,DOWN,RIGHT,UP; LB,A,B,X,Y). A saved
// mapping equal to one of them was never customised: parse_config replaces it
// with the current default. Any other mapping is kept as the user's choice.
bool is_previous_default_pad_mapping(const std::array<uint16_t,5>& mapping);
const char* pad_button_name(uint16_t button);
// Host slot for a non-XInput pad (DualShock 4 / DualSense via RawInput): the
// lowest of the four slots not occupied by an XInput pad or another such pad,
// like SDL's player index. -1 if all four are taken.
constexpr int pick_pad_slot(uint8_t occupied) {
    for(int slot=0;slot<4;++slot)if(!((occupied>>slot)&1))return slot;
    return -1;
}
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
// What is connected in a gamepad slot, for on-screen button symbols.
enum class PadKind : unsigned { none, xbox, playstation };
PadKind buzz_input_pad_kind(unsigned slot);
bool buzz_input_host_pad_connected(unsigned slot);
bool buzz_input_format_config(const buzz_input::Config&,std::string& ini,std::string& error);
// Automatic player slots. Keyboard key sets, AUTO pads and phones are equal
// devices: none has a player until it joins on the game's player-join screen
// (see JoinWatch); its press there takes the first free player and joins it.
// Outside that screen an unjoined keyboard set or pad drives P1 for the menus
// (unless a joined device holds P1); unjoined phones stay idle. When the
// screen closes, places that did not join in the game are released. A player
// the game has joined but no device holds any more (a phone that left) is
// taken over by the next device or phone that presses, even mid-game.
uint32_t buzz_input_pad_slots();               // slots held by local devices or joined in the game
void buzz_input_set_phone_slots(uint32_t mask); // slots reserved by phones
int buzz_input_pad_auto_player(unsigned slot); // auto pad's current player, or -1
void buzz_input_lights(unsigned receiver,uint8_t handsets); // guest light report, bit per handset
bool buzz_input_join_open();                   // player screen showing, or an orphaned player to take over
// Called (outside input locks) with phone slots to release: when the player
// screen closes, places that did not join; when it opens, every phone place
// with offline_only set (release only phones that are not connected).
void buzz_input_set_phone_release(void (*release)(uint32_t mask,bool offline_only));
// Host HID receivers are independent sources, not guest USB devices. Receiver
// slots map to P1-P4/P5-P8; the second requires the existing eight-player mode.
void buzz_input_physical_state(unsigned receiver,uint32_t buttons,bool connected);
bool buzz_input_physical_connected(unsigned receiver);
uint8_t buzz_input_host_physical_buttons(); // OR of handsets, focused menu navigation only
// Windows RawInput DualShock 4 / DualSense pads (adapter index 0..3), as
// XInput-style button bits. They share the four gamepad slots with XInput.
void buzz_input_hid_pad(unsigned index,uint16_t buttons,bool connected);
// Independent remote source per player; expiry/kick drops retained pulses too.
void buzz_input_remote(unsigned player,uint8_t held,uint8_t pressed,bool disconnected);
void buzz_input_touch(unsigned player,uint8_t held,bool cancel);
