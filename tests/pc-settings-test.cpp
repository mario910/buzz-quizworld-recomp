#include "../port/pc_settings.h"
#include "../port/buzzer_input.h"
#include "../port/pc_menu_model.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace buzz_pc;
void check(bool value){if(!value)throw std::runtime_error("PC settings regression");}
int main() {
    try {
        Preferences p;std::string error;
        MenuPadNavigation pad;
        check(pad.sample(1,100)==1);check(pad.sample(1,449)==0);
        check(pad.sample(1,450)==1);check(pad.sample(1,559)==0);
        check(pad.sample(1,560)==1);check(pad.sample(0,561)==0);
        check(pad.sample(8,562)==8);check(pad.sample(8|0x200,563)==0x200);
        check(pad.sample(8|0x200,564)==0); // holding RB never repeatedly selects
        check(pad.sample(0,565)==0);check(pad.sample(0x1000|0x2000,566)==(0x1000|0x2000));
        pad.reset();check(pad.sample(0x10,567)==0); // START is not an answer/select
        MenuModel menu;check(!menu.open());menu.navigate(MenuAction::toggle);check(menu.open());
        menu.navigate(MenuAction::up);check(menu.selection()==7);menu.navigate(MenuAction::down);check(menu.selection()==0);
        menu.show(MenuPage::controllers);check(menu.rows()==10);menu.navigate(MenuAction::up);check(menu.selection()==9);
        menu.navigate(MenuAction::back);check(menu.open()&&menu.page()==MenuPage::settings);
        menu.show(MenuPage::quit_confirm);check(menu.rows()==2&&menu.selection()==0);
        menu.navigate(MenuAction::back);menu.navigate(MenuAction::back);check(!menu.open());
        auto controls=buzz_input::defaults();std::string controls_text;
        check(buzz_input_format_config(controls,controls_text,error));
        auto loaded_controls=buzz_input::defaults();check(buzz_input::parse_config(controls_text,loaded_controls,error));
        check(loaded_controls.keys==controls.keys&&loaded_controls.pad_buttons==controls.pad_buttons);
        controls.pad_buttons[0][0]=0x1234;const auto old_text=controls_text;check(!buzz_input_format_config(controls,controls_text,error)&&controls_text==old_text);
        check(p.volume_percent==100&&p.fps_limit==30&&p.offline);
        Preferences decoded;check(parse(serialize(p),decoded,error)&&decoded==p);
        p.volume_percent=42;p.fullscreen=true;p.fps_limit=0;p.offline=false;p.renderer=Renderer::vulkan;
        check(parse(serialize(p),decoded,error)&&decoded==p);
        const auto previous=decoded;
        for(const char* bad:{"version=2","version=1\nvolume=101","version=1\nvolume=-1","version=1\nfps_limit=60","version=1\nvolume=2\nvolume=3","version=1\nunknown=1","version=1\nrenderer=unimplemented","version=1\noffline=true","volume=10"}) {
            check(!parse(bad,decoded,error)&&decoded==previous&&!error.empty());
        }
        check(!parse(std::string(4097,'x'),decoded,error));
        State state;auto result=state.apply(p);
        check(result.accepted&&result.restart==(network_restart|renderer_restart));
        check(state.active().volume_percent==42&&state.active().fullscreen&&state.active().fps_limit==0);
        check(state.active().offline&&state.active().renderer==Renderer::automatic);
        check(state.requested()==p);
        const auto revision=result.revision;check(state.apply(p).revision==revision);
        auto invalid=p;invalid.volume_percent=101;check(!state.apply(invalid).accepted&&state.requested()==p);
        p.offline=true;p.renderer=Renderer::automatic;check(state.apply(p).restart==no_restart);
        std::thread writer([&]{for(unsigned i=0;i<10000;++i){Preferences q;q.volume_percent=i%101;state.apply(q);}});
        for(unsigned i=0;i<10000;++i)check(state.active().volume_percent<=100);
        writer.join();
        MenuInputGate gate;
        const auto button=buzz_input::bits(7,31);
        check(gate.filter(button)==button);gate.set_open(true);check(gate.filter(button)==0);
        gate.set_open(false);check(gate.filter(button)==0);check(gate.filter(0)==0);check(gate.filter(button)==button);
        gate.set_open(true);check(gate.filter(0)==0);gate.set_open(false);check(gate.filter(button)==button);
        gate.require_neutral();check(gate.filter(buzz_input::bits(0,1))==0);
        check(gate.filter(buzz_input::bits(3,16))==0); // remapped held key is still blocked
        check(gate.filter(0)==0);check(gate.filter(buzz_input::bits(3,16))==buzz_input::bits(3,16));
        gate.set_open(true);check(gate.filter(button)==0);gate.set_open(false);gate.require_neutral();
        check(gate.filter(button)==0);check(gate.filter(0)==0);
        check(gate.filter(button)==button); // exactly one neutral sample clears the old blocked mask
        // Discard every provider's retained taps on open; the gate protects
        // held physical input separately until release after closing.
        buzz_input::Router router(250);router.state(buzz_input::gamepad0,button,0);
        for(unsigned provider=0;provider<buzz_input::provider_count;++provider)router.release(provider,true);
        check(router.sample(1)==0);
        std::cout<<"PASS PC preferences: transactional parsing, live/restart distinction, concurrent snapshots and menu release gate\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
