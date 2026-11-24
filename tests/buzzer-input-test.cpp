#include "../port/buzzer_input.h"
#include "../port/buzzer_protocol.h"
#include <cassert>
#include <cstdio>

int main(){
    using namespace buzz_input;
    Router router(100);
    router.state(keyboard,bits(0,1),10);
    router.state(keyboard,0,11);
    assert(router.sample(109)==1&&router.sample(110)==0);
    router.state(keyboard,bits(7,31),200);
    router.state(gamepad0,bits(7,31),200);
    router.release(keyboard,true);
    assert(router.sample(1000)==bits(7,31)); // another provider still holds it
    router.release(gamepad0,true);assert(router.sample(1000)==0);
    router.state(touchscreen,bits(2,31),1001);
    router.state(remote0,bits(2,1),1001);
    router.release(touchscreen,true);
    assert(router.sample(2000)==bits(2,1));
    router.release(remote0,true);assert(!router.sample(2000));
    for(unsigned player=0;player<8;++player)for(unsigned button=0;button<5;++button){
        Router r(10);const auto mask=bits(player,uint8_t(1u<<button));
        r.state(remote0,mask,0);assert(r.sample(0)==mask);
        for(unsigned receiver=0;receiver<2;++receiver){
            const auto packed=buzz_usb::report_mask(receiver_bits(r.sample(0),receiver));
            const auto decoded=uint32_t(packed[2])|(uint32_t(packed[3])<<8)|(uint32_t(packed[4]&15)<<16);
            assert(decoded==(receiver==player/4?(1u<<((player%4)*5+button)):0));
        }
    }
    assert(bits(8,31)==0&&receiver_bits(valid_mask,2)==0);
    Router invalid;invalid.state(999,UINT64_MAX,0);invalid.release(999,true);assert(!invalid.sample(0));
    auto config=defaults();std::string error;
    const char* answer_rows[] = {"1234", "WERT", "SDFG", "XCVB"};
    const unsigned buzz_keys[] = {' ', 'Q', 'A', 'Z'};
    for (unsigned player = 0; player < 4; ++player) {
        assert(config.keys[player][red] == buzz_keys[player]);
        for (unsigned answer = 0; answer < 4; ++answer)
            assert(config.keys[player][blue - answer] == unsigned(answer_rows[player][answer]));
    }
    {   // Pads are always on: legacy gamepads=0/1 parses, is ignored and is saved as 1.
        auto legacy=defaults();std::string text;
        assert(parse_config("gamepads=0\n",legacy,error)&&legacy.pad_player==defaults().pad_player);
        assert(buzz_input_format_config(legacy,text,error)&&text.find("\ngamepads=1\n")!=std::string::npos);
    }
    for(auto& mapping:config.pad_buttons){
        // RPCS3/PCSX2 layout: LB or RB red; Y/X/B/A = handset top to bottom.
        assert(map_pad(0x100,mapping)==(1u<<red)&&map_pad(0x200,mapping)==(1u<<red)&&map_pad(0x300,mapping)==(1u<<red));
        assert(map_pad(0x1000,mapping)==(1u<<yellow)); // A / Cross
        assert(map_pad(0x2000,mapping)==(1u<<green));  // B / Circle
        assert(map_pad(0x4000,mapping)==(1u<<orange)); // X / Square
        assert(map_pad(0x8000,mapping)==(1u<<blue));   // Y / Triangle
        assert(map_pad(0x10,mapping)==0); // START must not answer a question.
        assert(map_pad(0xf,mapping)==0); // the D-pad is free
    }
    {   // LB+RB: one entry for both bumpers; it round-trips and cannot share a bumper.
        auto both=defaults();std::string text;
        assert(parse_config("gamepad.1=1,LB+RB,A,B,X,Y\n",both,error)&&both.pad_buttons[0][0]==0x300);
        assert(buzz_input_format_config(both,text,error)&&text.find("gamepad.1=1,LB+RB,A,B,X,Y\n")!=std::string::npos);
        for(const char* overlap:{"gamepad.1=1,LB+RB,LB,B,X,Y","gamepad.1=1,LB+RB,A,RB,X,Y"})
            assert(!parse_config(overlap,both,error));
        assert(parse_config("gamepad.1=1,LB,RB,B,X,Y\n",both,error)&&both.pad_buttons[0][1]==0x200); // separate bumpers stay valid
    }
    {   // Never-customised earlier defaults migrate; real customisations stay.
        auto migrated=defaults();
        assert(parse_config("gamepad.1=AUTO,START,Y,A,B,X\ngamepad.2=2,RB,LEFT,DOWN,RIGHT,UP\ngamepad.3=3,RB,UP,DOWN,RIGHT,LEFT\n",migrated,error));
        assert(migrated.pad_buttons[0]==defaults().pad_buttons[0]&&migrated.pad_player[0]==Config::pad_auto);
        assert(migrated.pad_buttons[1]==defaults().pad_buttons[1]&&migrated.pad_player[1]==1);
        assert(migrated.pad_buttons[2][1]==0x1&&migrated.pad_buttons[2][4]==0x4); // custom order kept
        assert(is_previous_default_pad_mapping({0x10,0x8000,0x1000,0x2000,0x4000})&&is_previous_default_pad_mapping({0x200,0x4,0x2,0x8,0x1}));
        assert(is_previous_default_pad_mapping({0x100,0x1000,0x2000,0x4000,0x8000}));
        assert(parse_config("gamepad.4=4,LB,A,B,X,Y\n",migrated,error)&&migrated.pad_buttons[3][0]==0x300);
        assert(!is_previous_default_pad_mapping(defaults().pad_buttons[0]));
    }
    assert(parse_config("players=8\nkeyboard.8=F1,F2,F3,F4,F5\ngamepads=1\ngamepad.2=8,START,Y,A,B,RB\npulse_ms=100\n",config,error));
    assert(config.players==8&&config.keys[7][0]==112&&config.pad_player[1]==7&&config.pulse_ms==100);
    assert(map_pad(0xffff,config.pad_buttons[1])==31);
    assert(map_pad(0x1000,config.pad_buttons[1])==(1u<<green));
    const auto previous=config;
    for(const char* bad:{"players=6","players=8\nplayers=4","players=4\nunknown=1", "keyboard.0=A,B,C,D,E", "keyboard.9=A,B,C,D,E", "keyboard.1=A,A,C,D,E", "keyboard.1=A,B,C,D", "keyboard.1=0x100,B,C,D,E", "gamepad.1=9,START,Y,A,B,X", "gamepad.1=1,A,A,B,X,Y", "gamepads=2", "pulse_ms=0"}){
        assert(!parse_config(bad,config,error));assert(!error.empty());
        assert(config.players==previous.players&&config.keys==previous.keys&&config.pad_player==previous.pad_player);
    }
    assert(!parse_config(std::string(65537,'a'),config,error));
    assert(parse_config("# comment\nkeyboard.1=Space,4,3,2,1\nkeyboard.5=NONE,NONE,NONE,NONE,NONE\n",config,error));
    assert(config.keys[0][1]=='4'&&config.keys[4][0]==0);
    assert(parse_config("\xef\xbb\xbfplayers=4\r\npulse_ms=80\r\n",config,error));
    assert(config.players==4&&config.pulse_ms==80);
    assert(parse_config("gamepad.1=NONE\n",config,error)&&config.pad_player[0]==-1);
    Router focus(250);focus.state(keyboard,1,100);focus.release(keyboard);
    assert(focus.sample(101)==1&&focus.sample(350)==0);
    Router edges(10);edges.state(keyboard,1,0);edges.state(keyboard,1,5);edges.state(keyboard,0,6);
    assert(edges.sample(9)==1&&edges.sample(10)==0); // repeats don't prolong a tap
    puts("PASS: input routing, 8 players/2 reports, provider isolation, short taps/focus/disconnect, config/mapping");
}
