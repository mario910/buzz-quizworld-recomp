#include "core.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace buzz_launcher;
namespace {
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected settings rejection");}
}
int main(){try {
    const auto root=fs::temp_directory_path()/path("Buzz shared ą & "+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"Unique fixture directory");
    struct Cleanup {fs::path p;~Cleanup(){std::error_code e;auto q=fs::weakly_canonical(p,e);auto parent=fs::weakly_canonical(fs::temp_directory_path(),e);if(!e&&q.parent_path()==parent&&q.filename().u8string().rfind("Buzz shared ",0)==0)fs::remove_all(q,e);}} cleanup{root};
    const auto json=root/"runtime/launcher/settings.json",pc=root/"runtime/pc-settings.ini",input=root/"runtime/input-settings.ini";
    Settings legacy;legacy.players[0].name="Żółw Adam";legacy.background=true;
    legacy.paths["game_root"]="disc";
    atomic_text(json,settings_json(legacy).dump());const auto original_json=read_text(json);
    auto fresh=load_settings(root);
    check(!fresh.pc_configured&&fresh.pc.fps_limit==0,"Legacy JSON must not implicitly change the presentation mode");
    check(!fs::exists(pc)&&!fs::exists(input),"Loading must not create canonical files");

    buzz_pc::Preferences preferences;preferences.volume_percent=43;preferences.offline=false;preferences.fullscreen=true;
    auto controls=buzz_input::defaults();controls.players=8;controls.pulse_ms=310;
    controls.pad_player[0]=4;controls.pad_buttons[1][0]=0x10; // keep a legacy custom START binding
    controls.keys[4]={112,113,114,115,116};controls.background_gamepads=true;
    std::string text,error;check(buzz_input_format_config(controls,text,error),"Synthetic in-game controls");
    atomic_text(pc,buzz_pc::serialize(preferences));atomic_text(input,text);
    auto imported=load_settings(root);
    check(imported.pc==preferences&&!imported.offline&&imported.places==8&&imported.pulse_ms==310,"Import in-game audio/video and input");
    check(imported.players[0].name==legacy.players[0].name&&imported.paths==legacy.paths&&imported.background,"Keep launcher-only fields");
    check(imported.pads[0].player==4&&imported.pads[1].buttons[0]=="START"&&imported.players[4].keys[0]=="F1","Keep assignments and custom mappings");
    check(input_config(imported).background_gamepads,"Preserve focused/background controller policy");
    check(read_text(json)==original_json,"Read-only import preserves legacy JSON");

    imported.pc.volume_percent=72;imported.pc.fps_limit=0;imported.offline=true;imported.players[0].keys[0]="F8";
    save_shared_settings(root,imported);
    auto reloaded=load_settings(root);
    check(reloaded.pc.volume_percent==72&&reloaded.pc.fps_limit==0&&reloaded.pc.offline&&reloaded.offline,"Launcher changes are shared with the game");
    check(reloaded.players[0].keys[0]=="F8"&&reloaded.pads[1].buttons[0]=="START","Controller changes survive a new load");
    check(read_text(json)==original_json,"Canonical save leaves legacy labels and paths untouched");

    const auto old_input=read_text(input);auto stale=reloaded;
    preferences.volume_percent=11;atomic_text(pc,buzz_pc::serialize(preferences));const auto game_pc=read_text(pc);
    rejects([&]{save_shared_settings(root,stale);});
    check(read_text(pc)==game_pc&&read_text(input)==old_input,"Stale launcher cannot overwrite newer in-game settings");
    Build fake{root,root/"not-started",Json::object(),{"test","test"}};
    rejects([&]{plan_launch(stale,fake,{});});
    check(!fs::exists(root/"runtime/launcher/sessions"),"Stale launch rejected before creating snapshots");
    auto latest=load_settings(root);save_shared_settings(root,latest);
    check(load_settings(root).pc.volume_percent==11,"Reload resolves a revision conflict");
    const auto sibling=root/"another build";check(fs::create_directory(sibling),"Sibling fixture");
    rejects([&]{save_shared_settings(sibling,latest);});check(!fs::exists(sibling/"runtime"),"Wrong-build save must not create files");
    auto absent=load_settings(sibling);
    atomic_text(sibling/"runtime/input-settings.ini",old_input);
    rejects([&]{verify_shared_settings(sibling,absent);}); // another writer added a file
    auto appeared=load_settings(sibling);fs::remove(sibling/"runtime/input-settings.ini");
    rejects([&]{verify_shared_settings(sibling,appeared);}); // another writer removed it

    const auto healthy_pc=read_text(pc);atomic_text(input,"players=6\n");
    rejects([&]{load_settings(root);});
    check(read_text(input)=="players=6\n"&&read_text(pc)==healthy_pc,"Invalid controls are preserved, not silently reset");
    atomic_text(input,old_input);
    atomic_text(pc,std::string(4097,'x'));rejects([&]{load_settings(root);});
    check(fs::file_size(pc)==4097,"Oversized preferences are preserved");atomic_text(pc,healthy_pc);
    latest=load_settings(root);fs::remove(input);check(fs::create_directory(input),"Non-file fixture");
    rejects([&]{save_shared_settings(root,latest);});check(read_text(pc)==healthy_pc,"Validate both destinations before writing either");
    fs::remove(input);atomic_text(input,old_input);
#ifndef _WIN32
    const auto target=root/"linked-pc.ini";atomic_text(target,healthy_pc);fs::remove(pc);fs::create_symlink(target,pc);
    auto linked=load_settings(root);verify_shared_settings(root,linked);
    rejects([&]{save_shared_settings(root,linked);});rejects([&]{atomic_text(pc,"overwrite");});
    check(fs::is_symlink(pc)&&read_text(target)==healthy_pc,"Read-only linked settings are never replaced");
#endif
    std::cout<<"PASS shared per-build preferences: import, two-way save, stale revisions, cross-build isolation, strict parsing and path safety\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
