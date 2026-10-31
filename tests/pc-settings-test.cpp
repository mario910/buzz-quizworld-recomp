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
        menu.navigate(MenuAction::up);check(menu.selection()==row_quit&&menu.rows()==13);menu.navigate(MenuAction::down);check(menu.selection()==0);
        check(next_window_size({0,0},1).width==1280&&next_window_size({0,0},-1).width==3840);
        check(next_window_size({3840,2160},1).width==0);
        {auto custom=next_window_size({1000,700},1);check(custom.width==0);check(next_window_size({1000,700},-1).width==3840);}
        check(next_render_scale(100,1)==150&&next_render_scale(300,1)==300&&next_render_scale(100,-1)==100);
        check(next_render_scale(175,-1)==150&&next_render_scale(175,1)==200);
        menu.show(MenuPage::controllers);check(menu.rows()==10);menu.navigate(MenuAction::up);check(menu.selection()==9);
        menu.navigate(MenuAction::back);check(menu.open()&&menu.page()==MenuPage::settings);
        menu.show(MenuPage::quit_confirm);check(menu.rows()==2&&menu.selection()==0);
        menu.navigate(MenuAction::back);menu.navigate(MenuAction::back);check(!menu.open());
        menu.show(MenuPage::phones);check(menu.rows()==14);menu.select(99);check(menu.selection()==13);
        auto controls=buzz_input::defaults();std::string controls_text;
        check(buzz_input_format_config(controls,controls_text,error));
        auto loaded_controls=buzz_input::defaults();check(buzz_input::parse_config(controls_text,loaded_controls,error));
        check(loaded_controls.keys==controls.keys&&loaded_controls.pad_buttons==controls.pad_buttons);
        controls.pad_buttons[0][0]=0x1234;const auto old_text=controls_text;check(!buzz_input_format_config(controls,controls_text,error)&&controls_text==old_text);
        check(p.volume_percent==100&&p.fps_limit==30&&p.offline);
        Preferences decoded;check(parse(serialize(p),decoded,error)&&decoded==p);
        // Default display settings keep the exact version-1 file for older builds.
        check(serialize(p).find("version=1\n")!=std::string::npos&&serialize(p).find("window_width")==std::string::npos);
        check(parse("version=1\nvolume=7\nfullscreen=1",decoded,error)&&decoded.volume_percent==7&&decoded.fullscreen&&
              decoded.window_width==0&&decoded.window_height==0&&decoded.render_scale==100);
        {
            Preferences display;display.window_width=2560;display.window_height=1440;display.render_scale=200;
            const auto text=serialize(display);check(text.find("version=2\n")!=std::string::npos);
            check(parse(text,decoded,error)&&decoded==display);
            check(parse("version=2\nwindow_width=1000\nwindow_height=700\nrender_scale=150",decoded,error)&&decoded.window_width==1000&&decoded.render_scale==150);
            check(valid_window_size(0,0)&&valid_window_size(640,360)&&valid_window_size(7680,4320));
            check(!valid_window_size(1280,0)&&!valid_window_size(639,480)&&!valid_window_size(7681,4320));
            for(auto size:window_presets)check(valid_window_size(size.width,size.height));
            for(auto scale:render_scale_presets){display.render_scale=scale;check(validate(display,error));}
            display.upscaler=Upscaler::fsr1;display.fsr_sharpness=35;
            check(parse(serialize(display),decoded,error)&&decoded==display);
            check(parse("version=2\nupscale_filter=fsr1",decoded,error)&&decoded.upscaler==Upscaler::fsr1&&decoded.fsr_sharpness==80);
            Preferences packs;packs.texture_replace=false;packs.texture_dump=true;
            check(serialize(packs).find("version=2\n")!=std::string::npos&&parse(serialize(packs),decoded,error)&&decoded==packs);
            check(parse("version=2\ntexture_dump=1",decoded,error)&&decoded.texture_dump&&decoded.texture_replace);
            check(Preferences{}.texture_replace&&!Preferences{}.texture_dump&&!Preferences{}.texture_auto_upscale);
            Preferences upscale;upscale.texture_auto_upscale=true;
            check(serialize(upscale).find("texture_auto_upscale=1\n")!=std::string::npos&&parse(serialize(upscale),decoded,error)&&decoded==upscale);
            bool reset=false;
            check(parse_with_display_fallback("version=2\nvolume=9\ntexture_dump=yes",decoded,error,reset)&&reset&&
                  decoded.volume_percent==9&&!decoded.texture_dump);
        }
        p.volume_percent=42;p.fullscreen=true;p.fps_limit=0;p.offline=false;p.renderer=Renderer::vulkan;p.phone_buzzers=true;p.phone_port=65535;
        check(parse(serialize(p),decoded,error)&&decoded==p);
        const auto previous=decoded;
        for(const char* bad:{"version=0","version=1\nvolume=101","version=1\nvolume=-1","version=1\nfps_limit=60","version=1\nvolume=2\nvolume=3","version=1\nunknown=1","version=1\nrenderer=unimplemented","version=1\noffline=true","version=1\nphone_buzzers=yes","version=1\nphone_port=80","version=1\nphone_port=65536","volume=10",
            "version=3","version=1\nwindow_width=1920\nwindow_height=1080","version=2\nwindow_width=1920","version=2\nwindow_width=100\nwindow_height=100",
            "version=2\nwindow_width=99999\nwindow_height=1080","version=2\nrender_scale=50","version=2\nrender_scale=301","version=2\nrender_scale=1.5",
            "version=2\nupscale_filter=dlss","version=2\nfsr_sharpness=101","version=1\nupscale_filter=fsr1",
            "version=2\ntexture_replace=2","version=2\ntexture_dump=on","version=1\ntexture_dump=1",
            "version=2\ntexture_auto_upscale=yes","version=1\ntexture_auto_upscale=1"}) {
            check(!parse(bad,decoded,error)&&decoded==previous&&!error.empty());
        }
        check(!parse(std::string(4097,'x'),decoded,error));
        State state;auto result=state.apply(p);
        check(result.accepted&&result.restart==(network_restart|renderer_restart));
        check(state.active().volume_percent==42&&state.active().fullscreen&&state.active().fps_limit==0);
        check(state.active().phone_buzzers&&state.active().phone_port==65535);
        check(state.active().offline&&state.active().renderer==Renderer::automatic);
        check(state.requested()==p);
        const auto revision=result.revision;check(state.apply(p).revision==revision);
        auto invalid=p;invalid.volume_percent=101;check(!state.apply(invalid).accepted&&state.requested()==p);
        p.offline=true;p.renderer=Renderer::automatic;check(state.apply(p).restart==no_restart);
        p.window_width=1920;p.window_height=1080;check(state.apply(p).restart==no_restart&&state.active().window_width==1920);
        p.render_scale=200;result=state.apply(p);
        check(result.accepted&&result.restart==resolution_restart&&state.active().render_scale==100&&state.requested().render_scale==200);
        p.render_scale=100;p.window_width=0;p.window_height=0;check(state.apply(p).restart==no_restart);
        p.texture_dump=true;result=state.apply(p);
        check(result.restart==texture_restart&&!state.active().texture_dump&&state.requested().texture_dump);
        p.texture_dump=false;check(state.apply(p).restart==no_restart);
        p.texture_auto_upscale=true;check(state.apply(p).restart==texture_restart);
        p.texture_auto_upscale=false;check(state.apply(p).restart==no_restart);
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
