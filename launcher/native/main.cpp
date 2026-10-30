#include "core.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    using namespace buzz_launcher;
    try {
        fs::path root=path(SDL_GetBasePath()?SDL_GetBasePath():".");
        bool smoke=false,check=false,dry=false,launch=false,skip_intro=false;
        int seconds=0,tab=0;fs::path capture;
        // Developer builds live in build-launcher; installed binaries use their
        // own directory. No current-working-directory dependency for players.
        if(root.filename().empty())root=root.parent_path();
        if(root.filename()=="build-launcher")root=root.parent_path();
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            auto value=[&] {if(i+1>=argc)throw std::runtime_error("Missing value for "+a);return std::string(argv[++i]);};
            if(a=="--project")root=path(value());
            else if(a=="--smoke")smoke=true;
            else if(a=="--capture")capture=path(value());
            else if(a=="--smoke-tab")tab=std::stoi(value());
            else if(a=="--check")check=true;
            else if(a=="--dry-run")dry=true;
            else if(a=="--launch")launch=true;
            else if(a=="--test-seconds")seconds=std::stoi(value());
            else if(a=="--test-skip-intro")skip_intro=true;
            else if(a=="--help") {
                std::cout<<"BuzzLauncher [--project DIR] [--check | --dry-run | --launch]\n"
                    "  Default: SDL3/ImGui launcher. No Python or PowerShell at runtime.\n"
                    "  --smoke [--smoke-tab 0..2] [--capture FILE.bmp]: hidden UI test, no writes/input/game.\n"
                    "  --test-seconds N: bounded integration; stops only the process it creates.\n"
                    "  --test-skip-intro: startup-video skip only with --launch --test-seconds.\n";return 0;
            } else throw std::runtime_error("Unknown argument: "+a);
        }
        if(seconds<0||seconds>300||tab<0||tab>2)throw std::runtime_error("Invalid test option");
        if(skip_intro&&(!launch||!seconds||check||dry||smoke))throw std::runtime_error("--test-skip-intro requires only --launch with bounded --test-seconds");
        root=fs::canonical(root);
        if(smoke||(!check&&!dry&&!launch))return run_ui(root,smoke,capture,tab);
        auto s=load_settings(root);auto b=validate_build(s,root);
        s=load_shared_settings(b.root,s);
        std::cout<<"Validated "<<b.profile.version<<" at "<<utf8(b.executable)<<'\n';
        if(check)return 0;
        // Normalize paths once against the configuration's root, not the
        // selected sibling build's root, before creating the immutable snapshot.
        for(auto& [k,v]:s.paths) { (void)k;v=utf8(resolve(root,v)); }
        if(dry) {
            auto env=runtime_environment(s,b,b.root/"runtime/launcher/dry-run",current_environment());
            Json diagnostic=Json::object();
            // Never dump the inherited environment (which may contain tokens).
            for(const auto* key:{"SPURS_JOB_LIST_WORKERS","SPURS_JOB_LIST_IMAGE","SPURS_JOB_LIST_EXTRA_IMAGES","SPURS_JOB_LS_EXCHANGE","SPURS_JOB_LS_PUBLISH","SPURS_JOB_CACHE_ENV","RSX_CACHE_ENV","SPURS_MERGE_READY_LISTS","SPURS_NATIVE_EXTRA_FP","RSX_GUEST_SAMPLERS","RSX_PIXEL_WINDOW","SPURS_JTS_WAKE","SPURS_JTS_DMA_WAKE","SPURS_GUARD_POLL_ONLY","BUZZ_STAGEF","BUZZ_COMPLETION_QUEUE","BUZZ_NATIVE_COMPLETION","GCM_SECOND_V","RSX_TRACK_ZETA","RSX_VERTEX_CAPACITY","RSX_DRAW_CAPACITY","RSX_TEXTURE_CAPACITY","PS3_USLEEP_TIMER_TAIL_US","BUZZ_NO_FMV"})if(env.count(key))diagnostic[key]=env.at(key);
            Json preferences={{"configured",s.pc_configured},{"volume",s.pc.volume_percent},{"fps_limit",s.pc.fps_limit},{"fullscreen",s.pc.fullscreen},{"offline",s.offline}};
            std::cout<<Json{{"executable",utf8(b.executable)},{"elf",s.paths.at("eboot_elf")},{"cwd",utf8(b.root/"runtime")},{"runtime_options",diagnostic},{"input_ini",input_ini(s)},{"preferences",preferences}}.dump(2)<<'\n';return 0;
        }
        if(executable_running(b.executable))throw std::runtime_error("This game build is already running");
        auto plan=plan_launch(s,b,current_environment());
        if(skip_intro)plan.environment["BUZZ_NO_FMV"]="1";
        Child child;child.start(plan);
        std::cout<<"Game started directly. Logs: "<<utf8(plan.session)<<'\n';
        if(seconds) { auto end=SDL_GetTicks()+uint64_t(seconds)*1000;while(child.running()&&SDL_GetTicks()<end)SDL_Delay(50);child.stop_owned(); }
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Buzz launcher: "<<e.what()<<'\n';return 1;
    }
}
