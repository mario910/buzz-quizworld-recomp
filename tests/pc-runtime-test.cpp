#include "pc_runtime.h"
#include "ps3emu/host_controls.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void check(bool b){if(!b)throw std::runtime_error("PC runtime regression");}
std::string read(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){try {
    const auto root=fs::temp_directory_path()/fs::u8path("Buzz runtime ą & "+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root));
    struct Cleanup{fs::path p;~Cleanup(){std::error_code e;auto q=fs::weakly_canonical(p,e);auto base=fs::weakly_canonical(fs::temp_directory_path(),e);if(!e&&q.parent_path()==base&&q.filename().u8string().rfind("Buzz runtime ",0)==0)fs::remove_all(q,e);}} cleanup{root};
    const auto file=root/"settings.ini";
    struct RestoreDirectory {fs::path previous=fs::current_path();~RestoreDirectory(){fs::current_path(previous);}} restore_directory;
    fs::current_path(root);
    buzz_pc::Preferences p;p.volume_percent=37;p.fullscreen=true;p.offline=false;
    {std::ofstream out(file,std::ios::binary);out<<buzz_pc::serialize(p);}
#ifdef _WIN32
    check(_wputenv_s(L"BUZZ_PC_SETTINGS",file.c_str())==0);
#else
    check(setenv("BUZZ_PC_SETTINGS",file.c_str(),1)==0);
#endif
    check(buzz_pc_runtime_init());
    check(ps3_host_get_volume()==37&&ps3_host_get_frame_limit()==30&&ps3_host_get_fullscreen()==1);
    check(buzz_pc_requested_preferences()==p&&std::string(getenv("BUZZ_OFFLINE"))=="0");
    p.volume_percent=20;p.fps_limit=0;p.fullscreen=false;p.offline=true;
    auto changed=buzz_pc_apply_preferences(p,true);
    check(changed.accepted&&changed.restart==buzz_pc::network_restart);
    check(ps3_host_get_volume()==20&&ps3_host_get_frame_limit()==0&&ps3_host_get_fullscreen()==0);
    check(std::string(getenv("BUZZ_OFFLINE"))=="0"); // no fake live PSN switch
    check(read(file)==buzz_pc::serialize(p));
    check(buzz_pc_runtime_init());check(std::string(getenv("BUZZ_OFFLINE"))=="1");
    const auto before=read(file);auto bad=p;bad.volume_percent=101;
    check(!buzz_pc_apply_preferences(bad,true).accepted&&read(file)==before&&ps3_host_get_volume()==20);
#ifdef _WIN32
    bad=p;bad.renderer=buzz_pc::Renderer::vulkan;
#else
    bad=p;bad.renderer=buzz_pc::Renderer::d3d12;
#endif
    check(!buzz_pc_apply_preferences(bad,true).accepted&&read(file)==before);
    fs::remove(file);check(fs::create_directory(file));
    auto input=buzz_input::defaults();std::string input_error;
    input.keys[0][0]='F';input.pulse_ms=300;
    check(buzz_pc_save_input_preferences(input,input_error));
    const auto saved_input=read(root/"input-settings.ini");
    auto parsed_input=buzz_input::defaults();check(buzz_input::parse_config(saved_input,parsed_input,input_error));
    check(parsed_input.keys==input.keys&&parsed_input.pad_buttons==input.pad_buttons&&parsed_input.pulse_ms==300);
    input.pad_buttons[0][0]=0x1234;
    check(!buzz_pc_save_input_preferences(input,input_error)&&read(root/"input-settings.ini")==saved_input);
    p.volume_percent=10;check(!buzz_pc_apply_preferences(p,true).accepted&&ps3_host_get_volume()==20);
    std::cout<<"PASS runtime settings: Unicode persistence, live host controls, restart-only offline policy, unsupported renderer and failed-save transaction\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
