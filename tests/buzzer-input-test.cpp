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
        assert(parse_config("gamepads=0\n",legacy,error)&&legacy.pad_buttons==defaults().pad_buttons);
        assert(buzz_input_format_config(legacy,text,error)&&text.find("\ngamepads=1\n")!=std::string::npos);
        // One shared layout, saved as "gamepad=": no per-slot lines, no players.
        assert(text.find("\ngamepad=LB+RB,A,B,X,Y\n")!=std::string::npos&&text.find("gamepad.")==std::string::npos);
    }
    {   // RPCS3/PCSX2 layout: LB or RB red; Y/X/B/A = handset top to bottom.
        const auto& mapping=config.pad_buttons;
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
        assert(parse_config("gamepad=LB+RB,A,B,X,Y\n",both,error)&&both.pad_buttons[0]==0x300);
        assert(buzz_input_format_config(both,text,error)&&text.find("gamepad=LB+RB,A,B,X,Y\n")!=std::string::npos);
        for(const char* overlap:{"gamepad=LB+RB,LB,B,X,Y","gamepad=LB+RB,A,RB,X,Y","gamepad.1=1,LB+RB,LB,B,X,Y"})
            assert(!parse_config(overlap,both,error));
        assert(parse_config("gamepad=LB,RB,B,X,Y\n",both,error)&&both.pad_buttons[1]==0x200); // separate bumpers stay valid
        assert(!parse_config("gamepad=LB,A,B,X\n",both,error)&&!parse_config("gamepad=AUTO,LB,A,B,X,Y\n",both,error));
    }
    {   // Older per-slot files: the lowest slot's buttons become the shared layout,
        // players (AUTO, fixed or NONE) are dropped; "gamepad=" wins when present.
        auto old=defaults();
        assert(parse_config("gamepad.1=NONE\ngamepad.2=3,RB,UP,DOWN,RIGHT,LEFT\ngamepad.3=AUTO,START,Y,A,B,X\n",old,error));
        assert(old.pad_buttons[0]==0x200&&old.pad_buttons[1]==0x1&&old.pad_buttons[4]==0x4); // slot 2's custom layout
        assert(parse_config("gamepad.1=1,LB,RB,B,X,Y\ngamepad=Y,A,B,X,LB\n",old,error)&&old.pad_buttons[0]==0x8000);
        assert(!parse_config("gamepad.1=9,START,Y,A,B,X",old,error)&&!parse_config("gamepad.5=AUTO,LB,A,B,X,Y",old,error));
        // Never-customised earlier defaults migrate; real customisations stay.
        for(const char* earlier:{"gamepad.1=AUTO,START,Y,A,B,X\n","gamepad.1=1,RB,LEFT,DOWN,RIGHT,UP\n","gamepad.4=4,LB,A,B,X,Y\n","gamepad=START,Y,A,B,X\n"}){
            auto migrated=defaults();migrated.pad_buttons={0x1,0x2,0x4,0x8,0x10};
            assert(parse_config(earlier,migrated,error)&&migrated.pad_buttons==defaults().pad_buttons);
        }
        assert(is_previous_default_pad_mapping({0x10,0x8000,0x1000,0x2000,0x4000})&&is_previous_default_pad_mapping({0x200,0x4,0x2,0x8,0x1}));
        assert(is_previous_default_pad_mapping({0x100,0x1000,0x2000,0x4000,0x8000})&&!is_previous_default_pad_mapping(defaults().pad_buttons));
    }
    assert(parse_config("players=8\nkeyboard.8=F1,F2,F3,F4,F5\ngamepads=1\ngamepad=START,Y,A,B,RB\npulse_ms=100\n",config,error));
    assert(config.players==8&&config.keys[7][0]==112&&config.pulse_ms==100);
    assert(map_pad(0xffff,config.pad_buttons)==31);
    assert(map_pad(0x1000,config.pad_buttons)==(1u<<green));
    const auto previous=config;
    for(const char* bad:{"players=6","players=8\nplayers=4","players=4\nunknown=1", "keyboard.0=A,B,C,D,E", "keyboard.9=A,B,C,D,E", "keyboard.1=A,A,C,D,E", "keyboard.1=A,B,C,D", "keyboard.1=0x100,B,C,D,E", "gamepad.1=9,START,Y,A,B,X", "gamepad=A,A,B,X,Y", "gamepads=2", "pulse_ms=0", "gamepad=LB,A,B,X,Y\ngamepad=LB,A,B,X,Y"}){
        assert(!parse_config(bad,config,error));assert(!error.empty());
        assert(config.players==previous.players&&config.keys==previous.keys&&config.pad_buttons==previous.pad_buttons);
    }
    assert(!parse_config(std::string(65537,'a'),config,error));
    assert(parse_config("# comment\nkeyboard.1=Space,4,3,2,1\nkeyboard.5=NONE,NONE,NONE,NONE,NONE\n",config,error));
    assert(config.keys[0][1]=='4'&&config.keys[4][0]==0);
    assert(parse_config("\xef\xbb\xbfplayers=4\r\npulse_ms=80\r\n",config,error));
    assert(config.players==4&&config.pulse_ms==80);    {   // Player-join screen read from the light blinking seen in the real game:
        // all on <-> joined only. Steady or one-off changes never open it.
        JoinWatch w;
        w.lights(15,100);w.lights(0,600);assert(!w.open(700)&&w.joined()==0); // title: on, then off
        w.lights(15,1100);assert(w.open(1100)&&w.joined()==0);                 // blinking, nobody yet
        w.lights(1,1600);w.lights(15,2100);assert(w.open(2100)&&w.joined()==1); // P1 joined
        w.lights(3,2600);w.lights(15,3100);assert(w.joined()==3);               // P2 joined
        w.lights(0,3600);                                                       // screen ends: lights off
        assert(w.joined()==3&&w.open(3100+JoinWatch::window_ms)&&!w.open(3101+JoinWatch::window_ms));
        assert(w.settled()==3); // the screen ended with the lights off: P1+P2
        w.lights(0,9000);w.lights(15,9000);assert(w.joined()==0); // steady repeats are ignored; new blink restarts
        // The last free handset joins: blinking stops with all four steady on.
        JoinWatch full;
        full.lights(15,0);full.lights(7,500);full.lights(15,1000);assert(full.open(1000)&&full.joined()==7);
        assert(!full.open(1001+JoinWatch::window_ms)&&full.settled()==15);
    }
    Router focus(250);focus.state(keyboard,1,100);focus.release(keyboard);
    assert(focus.sample(101)==1&&focus.sample(350)==0);
    Router edges(10);edges.state(keyboard,1,0);edges.state(keyboard,1,5);edges.state(keyboard,0,6);
    assert(edges.sample(9)==1&&edges.sample(10)==0); // repeats don't prolong a tap
    puts("PASS: input routing, 8 players/2 reports, provider isolation, short taps/focus/disconnect, config/mapping");
}
