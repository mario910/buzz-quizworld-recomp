#include "pc_runtime.h"
#include "pc_settings_file.h"
#include "pc_texture_upscaler.h"
#include "ps3emu/host_controls.h"
#include <chrono>
#include <atomic>
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
std::atomic<bool> performance_overlay{false};
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
    performance_overlay.store(p.performance_overlay,std::memory_order_relaxed);
    ps3_host_set_volume(p.volume_percent);
    ps3_host_set_frame_limit(p.fps_limit);
    ps3_host_set_fullscreen(p.fullscreen);
    ps3_host_set_window_size(p.window_width,p.window_height);
    ps3_host_set_upscaler(p.upscaler==buzz_pc::Upscaler::fsr1?PS3_HOST_UPSCALE_FSR1:PS3_HOST_UPSCALE_BILINEAR,p.fsr_sharpness);
}
}
bool buzz_pc_runtime_init() {
    try {
        std::lock_guard lock(apply_mutex);
#ifdef __ANDROID__
        // The SDK's Vulkan translator has a versioned disk cache, separate
        // from RSX_SHADER_CACHE_DIR (the D3D shader cache). Without this setup
        // Android repeated glslang/legalization work on every cold process.
        // An explicit override, including an empty string to disable, wins.
        if (!std::getenv("PS3RECOMP_SPIRV_CACHE")) {
            const auto directory = fs::current_path()/"spirv-cache";
            std::error_code error;
            fs::create_directories(directory, error);
            if (!error && fs::is_directory(directory, error)) {
                if (setenv("PS3RECOMP_SPIRV_CACHE", directory.c_str(), 0) == 0)
                    std::fprintf(stderr,"[spirv-cache] Android disk cache: %s\n",directory.c_str());
            } else {
                std::fprintf(stderr,"[spirv-cache] Cannot create cache directory; using the memory cache\n");
            }
        }
#endif
        buzz_pc::Preferences p;
        // Without a settings file every platform starts at the game's 30 FPS
        // target (Preferences' default); an explicit file still wins, even 0.
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
            bool display_reset=false;
            if(!buzz_pc::parse_with_display_fallback(text,p,error,display_reset))throw std::runtime_error(error);
            if(display_reset)fprintf(stderr,"[pc-settings] %s; using the default display and texture pack settings\n",error.c_str());
            if(!renderer_available(p.renderer))throw std::runtime_error("Requested renderer is not available in this build");
#ifdef _WIN32
            if(_putenv_s("BUZZ_OFFLINE",p.offline?"1":"0"))throw std::runtime_error("Cannot select offline policy");
#else
            if(setenv("BUZZ_OFFLINE",p.offline?"1":"0",1))throw std::runtime_error("Cannot select offline policy");
#endif
            apply_live(p);
            // Read once by the renderer when it creates its targets.
            ps3_host_set_render_scale(p.render_scale);
        }
        else {
#ifdef __ANDROID__
            apply_live(p);
#else
            ps3_host_set_frame_limit(p.fps_limit);
#endif
        }
        // Read once by the renderer's first texture upload. Defaults apply
        // without a settings file too: an empty or missing folder costs nothing.
        {
            // Automatic upscaling feeds on the dump and loads through replace/.
            const bool upscale=p.texture_auto_upscale&&buzz_pc::start_texture_upscaler(fs::current_path()/"textures");
            const unsigned flags=(p.texture_replace||upscale?PS3_HOST_TEXPACK_REPLACE:0u)|(p.texture_dump||upscale?PS3_HOST_TEXPACK_DUMP:0u);
            const auto dir=(fs::current_path()/"textures").u8string();
            if(!ps3_host_set_texture_pack(reinterpret_cast<const char*>(dir.c_str()),flags))
                fprintf(stderr,"[pc-settings] texture pack folder path is too long; texture packs disabled\n");
        }
        state=std::make_unique<buzz_pc::State>(p);
        performance_overlay.store(p.performance_overlay,std::memory_order_relaxed);
        fprintf(stderr,"[pc-settings] configured=%d volume=%u%% cap=%u fullscreen=%d offline=%d window=%ux%u render_scale=%u%% texture_replace=%d texture_dump=%d texture_auto_upscale=%d\n",configured,p.volume_percent,p.fps_limit,p.fullscreen,p.offline,p.window_width,p.window_height,p.render_scale,p.texture_replace,p.texture_dump,p.texture_auto_upscale);
        return true;
    }catch(const std::exception& e){fprintf(stderr,"[pc-settings] %s\n",e.what());return false;}
}
buzz_pc::Preferences buzz_pc_requested_preferences() {
    std::lock_guard lock(apply_mutex);
    return state?state->requested():buzz_pc::Preferences{};
}
bool buzz_pc_performance_overlay_enabled() {
    return performance_overlay.load(std::memory_order_relaxed);
}
unsigned buzz_pc_active_render_scale() {
    std::lock_guard lock(apply_mutex);
    return state?state->active().render_scale:100;
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
