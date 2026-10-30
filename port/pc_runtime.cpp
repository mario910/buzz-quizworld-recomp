#include "pc_runtime.h"
#include "pc_settings_file.h"
#include "ps3emu/host_controls.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
namespace fs=std::filesystem;
std::unique_ptr<buzz_pc::State> state;
std::mutex apply_mutex;
fs::path preferences_file;
unsigned phone_players=4;
buzz_phone::Server::Input phone_input;
bool configure_phone(const buzz_pc::Preferences& p,std::string& error) {
    if(!phone_input)return true;
    if(!p.phone_buzzers){buzz_phone::host().stop();return true;}
    return buzz_phone::host().start(p.phone_port,phone_players,phone_input,error);
}
bool renderer_available(buzz_pc::Renderer r) {
    if(r==buzz_pc::Renderer::automatic)return true;
#if defined(BUZZ_EXPERIMENTAL_VULKAN)
    return r==buzz_pc::Renderer::vulkan;
#elif defined(_WIN32)
    return r==buzz_pc::Renderer::d3d12;
#elif defined(PS3RECOMP_RSX_VULKAN)
    return r==buzz_pc::Renderer::vulkan;
#else
    return false;
#endif
}
void atomic_save(const buzz_pc::Preferences& p) {
    buzz_pc_write_file_atomic(preferences_file,buzz_pc::serialize(p));
}
void apply_live(const buzz_pc::Preferences& p) {
    ps3_host_set_volume(p.volume_percent);
    ps3_host_set_frame_limit(p.fps_limit);
    ps3_host_set_fullscreen(p.fullscreen);
}
}
bool buzz_pc_runtime_init() {
    try {
        std::lock_guard lock(apply_mutex);
        buzz_pc::Preferences p;
        // No settings file: leave the SDK's existing presentation/vsync path
        // untouched. Its absence is not a new implicit frame limiter.
        p.fps_limit=0;
        const char* offline=getenv("BUZZ_OFFLINE");p.offline=!offline||std::string(offline)!="0";
        preferences_file=fs::current_path()/"pc-settings.ini";
#ifdef _WIN32
        if(const wchar_t* custom=_wgetenv(L"BUZZ_PC_SETTINGS"))preferences_file=fs::absolute(fs::path(custom));
#else
        if(const char* custom=getenv("BUZZ_PC_SETTINGS"))preferences_file=fs::absolute(fs::u8path(custom));
#endif
        const bool configured=fs::exists(preferences_file);
        if(configured) {
            if(!fs::is_regular_file(preferences_file)||fs::file_size(preferences_file)>4096)throw std::runtime_error("Invalid/oversized pc-settings.ini");
            std::ifstream in(preferences_file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read pc-settings.ini");
            std::string text{std::istreambuf_iterator<char>(in),{}},error;
            if(!buzz_pc::parse(text,p,error))throw std::runtime_error(error);
            if(!renderer_available(p.renderer))throw std::runtime_error("Requested renderer is not available in this build");
#ifdef _WIN32
            if(_putenv_s("BUZZ_OFFLINE",p.offline?"1":"0"))throw std::runtime_error("Cannot select offline policy");
#else
            if(setenv("BUZZ_OFFLINE",p.offline?"1":"0",1))throw std::runtime_error("Cannot select offline policy");
#endif
            apply_live(p);
        }
        state=std::make_unique<buzz_pc::State>(p);
        fprintf(stderr,"[pc-settings] configured=%d volume=%u%% cap=%u fullscreen=%d offline=%d\n",configured,p.volume_percent,p.fps_limit,p.fullscreen,p.offline);
        return true;
    }catch(const std::exception& e){fprintf(stderr,"[pc-settings] %s\n",e.what());return false;}
}
buzz_pc::Preferences buzz_pc_requested_preferences() {
    std::lock_guard lock(apply_mutex);
    return state?state->requested():buzz_pc::Preferences{};
}
void buzz_pc_phone_initialize(unsigned players,buzz_phone::Server::Input input) {
    std::lock_guard lock(apply_mutex);phone_players=players;phone_input=std::move(input);
    std::string error;
    if(state&&!configure_phone(state->active(),error))fprintf(stderr,"[phone] %s\n",error.c_str());
}
buzz_pc::Change buzz_pc_apply_preferences(const buzz_pc::Preferences& p,bool persist) {
    std::lock_guard lock(apply_mutex);
    if(!state)return {false,0,0,"PC settings are not initialized"};
    std::string error;
    if(!buzz_pc::validate(p,error))return {false,0,0,error};
    if(!renderer_available(p.renderer))return {false,0,0,"Requested renderer is not available in this build"};
    const auto previous=state->active();
    try {
        if(!configure_phone(p,error)){
            std::string ignored;configure_phone(previous,ignored);
            return {false,0,0,error};
        }
        if(persist)atomic_save(p);
        auto result=state->apply(p);
        if(result.accepted)apply_live(state->active());
        return result;
    }catch(const std::exception& e){std::string ignored;configure_phone(previous,ignored);return {false,0,0,e.what()};}
}
bool buzz_pc_save_input_preferences(const buzz_input::Config& input,std::string& error) {
    std::string text;
    if(!buzz_input_format_config(input,text,error))return false;
    try {buzz_pc_write_file_atomic(fs::current_path()/"input-settings.ini",text);return true;}
    catch(const std::exception& e){error=e.what();return false;}
}
