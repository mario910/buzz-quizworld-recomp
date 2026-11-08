#include "core.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace buzz_launcher;
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected rejection");}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--settings-oracle") {
            auto settings=settings_from_json(Json::parse(std::cin));
            std::cout<<Json{{"settings",settings_json(settings)},{"input_ini",input_ini(settings)}}.dump();return 0;
        }
        if(argc==2) {
            std::cout<<Json{{"argument",argv[1]},{"cwd",utf8(fs::current_path())},{"token",SDL_getenv("BUZZ_SYNTHETIC_TOKEN")?SDL_getenv("BUZZ_SYNTHETIC_TOKEN"):""}}.dump();
            return 0;
        }
        Settings s;check(key_code("4")==52&&key_code("0x34")==52&&key_code("F24")==135,"Key parser parity");
        rejects([]{key_code("INVALID");});
        check(settings_json(settings_from_json(settings_json(s)))==settings_json(s),"Settings roundtrip");
        auto bad=settings_json(s);bad["version"]=true;rejects([&]{settings_from_json(bad);});
        bad=settings_json(s);bad["players"][0]["keys"][1]="Space";rejects([&]{settings_from_json(bad);});
        bad=settings_json(s);bad["pads"][1]["player"]=0;rejects([&]{settings_from_json(bad);});
        s.players[0].name="Adam żółć";s.pads[0].player=-1;
        auto config=input_config(s);check(config.keys[0][1]==52&&config.pad_player[0]==-1,"INI/game parser parity");
        check(settings_from_json(settings_json(s)).players[0].name==s.players[0].name,"Unicode JSON roundtrip");
        check(sha256_text("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 vector");
        check(identify("BCES00647",elf100).family=="quizworld-eu100","EU profile");
        check(identify("BCUS98209",elfUS).family=="quizworld-us100","US profile");
        rejects([]{identify("BCUS98209",elf102);});rejects([]{title_id("synthetic bad SFO");});
        auto temp=fs::temp_directory_path()/path("Buzz native ą & ! "+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        check(fs::create_directory(temp),"Unique synthetic directory");
        // This directory is owned exclusively by this fixture; no user data.
        struct Cleanup {fs::path p;~Cleanup(){std::error_code e;auto target=fs::weakly_canonical(p,e);auto parent=fs::weakly_canonical(fs::temp_directory_path(),e);if(!e&&target.parent_path()==parent&&target.filename().u8string().rfind("Buzz native ",0)==0)fs::remove_all(target,e);}} cleanup{temp};
        const auto update_profile=identify("BCES00647",elf102);
        validate_prepared_content(identify("BCES00647",elf100),temp);
        rejects([&]{validate_prepared_content(update_profile,temp);});
        const auto patched=temp/"runtime/dev_hdd0/game/BCES00647/USRDIR/Patched";
        for(const char* language:{"GLOBAL","CRO","CZE","GRC","POL","RUS"})for(const char* suffix:{".EDAT","SCRIPTS.SDAT"})
            atomic_text(patched/(std::string(language)+suffix),"synthetic update content");
        validate_prepared_content(update_profile,temp);
        atomic_text(patched/"GLOBALSCRIPTS.SDAT","");rejects([&]{validate_prepared_content(update_profile,temp);});
        auto settings=temp/"runtime/launcher/settings.json";
        atomic_text(settings,settings_json(s).dump());atomic_text(settings,settings_json(s).dump());
        check(load_settings(temp).players[0].name==s.players[0].name,"Atomic replace + import");
        check(sha256(settings)==sha256_text(read_text(settings)),"Stream SHA256");
        Build b{temp,temp/"fake-native-game",{{"runtime_defaults",{{"BUZZ_NATIVE_COMPLETION","2"}}}}, {"quizworld-eu100","EU 1.00"}};
        s.paths={{"game_root",utf8(temp/"disc")},{"dev_flash",utf8(temp/"flash")},{"eboot_elf",utf8(temp/"EBOOT.elf")}};
        Environment inherited={{"SPURS_JOB_LIST_WORKERS","2"},{"RSX_PIXEL_WINDOW","0"},{"SPURS_JOB_LIST_EXTRA_IMAGES",""},{"BUZZ_SCRIPT","stale"},{"BUZZ_PULSE_MS","99"}};
        auto env=runtime_environment(s,b,temp/"snapshot",inherited);
        check(env.at("SPURS_JOB_LIST_WORKERS")=="2"&&env.at("RSX_PIXEL_WINDOW")=="0"&&env.at("SPURS_JOB_LIST_EXTRA_IMAGES").empty(),"Explicit performance overrides");
        check(env.at("BUZZ_NATIVE_COMPLETION")=="2"&&env.at("RSX_VERTEX_CAPACITY")=="1048576"&&env.at("SPURS_JTS_DMA_WAKE")=="1"&&!env.count("SPURS_POOL_SPIN_US")&&!env.count("SPURS_JOB_LS_ZERO"),"Runtime defaults");
        check(!env.count("BUZZ_SCRIPT")&&!env.count("BUZZ_PULSE_MS"),"Stale input removed");
        check(inherited.at("BUZZ_PULSE_MS")=="99","Parent environment untouched");
        auto geometry=b;
        geometry.manifest["family"]="quizworld-eu100";
        geometry.manifest["elf_sha256"]=elf100;
        geometry.manifest["renderer"]="vulkan";
        geometry.manifest["geometry_job_profile"]="host-groups-v1";
        geometry.manifest["runtime_defaults"]["BUZZ_GEOMETRY_JOB"]="1";
        check(runtime_environment(s,geometry,temp/"geometry",{}).at("BUZZ_GEOMETRY_JOB")=="1","Qualified geometry default");
        check(runtime_environment(s,geometry,temp/"geometry",{{"BUZZ_GEOMETRY_JOB","0"}}).at("BUZZ_GEOMETRY_JOB")=="0","Explicit geometry off overrides build default");
        check(!runtime_environment(s,b,temp/"legacy",{}).count("BUZZ_GEOMETRY_JOB"),"Legacy build keeps geometry opt-in");
        for(const auto& field:{"geometry_job_profile","family","elf_sha256","renderer"}) {
            auto bad=geometry;bad.manifest[field]="unqualified";
            rejects([&]{runtime_environment(s,bad,temp/"invalid",{});});
        }
        for(const auto& value:Json::array({"0","2",1,true,nullptr})) {
            auto bad=geometry;bad.manifest["runtime_defaults"]["BUZZ_GEOMETRY_JOB"]=value;
            rejects([&]{runtime_environment(s,bad,temp/"invalid",{});});
        }
        auto wrong_family=geometry;wrong_family.profile.family="quizworld-us100";
        rejects([&]{runtime_environment(s,wrong_family,temp/"invalid",{});});
        auto unknown_default=geometry;unknown_default.manifest["runtime_defaults"]["unexpected"]="1";
        rejects([&]{runtime_environment(s,unknown_default,temp/"invalid",{});});
        auto animation=geometry;
        animation.manifest["animation_job_profile"]="host-soa-v1";
        animation.manifest["runtime_defaults"]["BUZZ_ANIMATION_JOB"]="1";
        check(runtime_environment(s,animation,temp/"animation",{}).at("BUZZ_ANIMATION_JOB")=="1","Qualified animation default");
        for(const auto& value:{"0","1","2"})
            check(runtime_environment(s,animation,temp/"animation",{{"BUZZ_ANIMATION_JOB",value}}).at("BUZZ_ANIMATION_JOB")==value,"Explicit animation override preserved");
        check(!runtime_environment(s,b,temp/"legacy",{}).count("BUZZ_ANIMATION_JOB"),"Legacy build keeps animation opt-in");
        for(const auto& field:{"animation_job_profile","geometry_job_profile","family","elf_sha256","renderer"}) {
            auto bad=animation;bad.manifest[field]="unqualified";
            rejects([&]{runtime_environment(s,bad,temp/"invalid",{});});
        }
        for(const auto& value:Json::array({"0","2",1,true,nullptr})) {
            auto bad=animation;bad.manifest["runtime_defaults"]["BUZZ_ANIMATION_JOB"]=value;
            rejects([&]{runtime_environment(s,bad,temp/"invalid",{});});
        }
        auto embedded=animation;
        embedded.manifest["runtime_defaults"]=Json::object();
        embedded.manifest["embedded_runtime_profile"]="eu100-vulkan-native-v1";
        const auto bare=runtime_environment(s,embedded,temp/"embedded",{});
        auto embedded_us=embedded;
        embedded_us.profile=identify("BCUS98209",elfUS);
        embedded_us.manifest["family"]="quizworld-us100";
        embedded_us.manifest["elf_sha256"]=elfUS;
        embedded_us.manifest["embedded_runtime_profile"]="us100-vulkan-native-v1";
        check(!runtime_environment(s,embedded_us,temp/"embedded-us",{}).count("BUZZ_GEOMETRY_JOB"),"US EXE owns native defaults");
        check(runtime_environment(s,embedded_us,temp/"embedded-us",{{"BUZZ_ANIMATION_JOB","0"}}).at("BUZZ_ANIMATION_JOB")=="0","US explicit override");
        for(const auto& field:{"embedded_runtime_profile","animation_job_profile","geometry_job_profile","family","elf_sha256","renderer"}) {
            auto bad=embedded_us;bad.manifest[field]=embedded.manifest[field];
            if(bad.manifest[field]!=embedded_us.manifest[field])rejects([&]{runtime_environment(s,bad,temp/"invalid-us",{});});
        }
#define X(name, value) check(!bare.count(#name),"EXE owns " #name); check(runtime_environment(s,embedded,temp/"embedded",{{#name,"0"}}).at(#name)=="0","Explicit override " #name);
#include "../port/runtime_defaults.inc"
#undef X
        for(const auto& field:{"embedded_runtime_profile","animation_job_profile","geometry_job_profile","family","elf_sha256","renderer"}) {
            auto bad=embedded;bad.manifest[field]="unqualified";
            rejects([&]{runtime_environment(s,bad,temp/"invalid",{});});
        }
        auto one=plan_launch(s,b,{}),two=plan_launch(s,b,{});
        check(one.session!=two.session&&fs::is_regular_file(one.session/"input.ini"),"Immutable unique snapshots");
        // Spawn this native test executable, not a shell or a game. Literal
        // argument, UTF8 cwd, environment and redirected output must survive.
        auto executable=fs::absolute(path(argv[0]));
        check(executable_running(executable),"Find own executable without a shell");
        check(!executable_running(temp/"not-a-running-game"),"Do not confuse another executable with this build");
        auto probe=temp/path("literal ą & !.txt");
        const auto argument=path("literal argument ą & ! \"quoted\" end\\");
        LaunchPlan process{executable,argument,temp,probe,temp/"stderr.txt",{},{{"BUZZ_SYNTHETIC_TOKEN","native-only"}}};
        {Child child;child.start(process);auto deadline=SDL_GetTicks()+10000;while(child.running()&&SDL_GetTicks()<deadline)SDL_Delay(10);int code=0;if(child.running())child.stop_owned();check(!child.running(&code)&&code==0,"Native child process exit");}
        auto report=Json::parse(read_text(probe));
        check(report.at("argument")==utf8(argument),"Literal UTF8 child arguments");
        // macOS resolves /var to /private/var in getcwd. Compare directory
        // identity, not the textual spelling of an equivalent host path.
        check(fs::equivalent(path(report.at("cwd").get<std::string>()),temp),"Native child working directory");
        check(report.at("token")=="native-only","Native child environment and redirected logs");
        rejects([&]{validate_build(s,temp);});
        std::cout<<"PASS native settings, parser parity, hashes, profiles, defaults and snapshots\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
