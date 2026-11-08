#pragma once
// Shared player preferences. No renderer, SDL, guest memory or platform API.
// Live consumers poll State::active(); restart-only changes remain requested.
#include <cstdint>
#include <mutex>
#include <string>

namespace buzz_pc {
enum class Renderer { automatic, d3d12, vulkan };
// How the frame is scaled into the window; FSR 1 only acts when upscaling.
enum class Upscaler { bilinear, fsr1 };
struct Preferences {
    unsigned volume_percent = 100;
    unsigned fps_limit = 30; // 0 (uncapped), 30 or 60; not a simulation-speed knob.
    bool performance_overlay = false; // Live, non-interactive FPS/draw/resolution HUD.
    bool fullscreen = false; // Borderless at the desktop resolution of the window's monitor.
    // Windowed client size in physical pixels. 0x0 keeps the backend's own
    // default size; the backend shrinks it to fit the monitor's work area.
    unsigned window_width = 0, window_height = 0;
    // Internal rendering resolution, in percent of the guest's own surfaces
    // (1280x720 at 100). Independent of the window/swapchain size above.
    unsigned render_scale = 100;
    Upscaler upscaler = Upscaler::bilinear;
    unsigned fsr_sharpness = 80; // 0-100; RCAS strength
    // Texture packs in <game folder>/textures: replace/ is loaded in place of
    // matching game textures, dump/ receives every new texture. Restart-only.
    bool texture_replace = true;
    bool texture_dump = false;
    // Dump, upscale with Real-ESRGAN (textures/upscaler) into replace/auto and
    // load the results while playing. Implies dumping. Restart-only.
    bool texture_auto_upscale = false;
    bool offline = true;
    Renderer renderer = Renderer::automatic;
    bool phone_buzzers = false;
    unsigned phone_port = 8073;
};
bool operator==(const Preferences&, const Preferences&);
inline constexpr unsigned frame_limit_presets[]={30,60,0};
inline unsigned next_frame_limit(unsigned current,int direction){
    int index=0;for(int i=0;i<3;++i)if(frame_limit_presets[i]==current)index=i;
    return frame_limit_presets[(index+(direction<0?-1:1)+3)%3];
}
inline bool operator!=(const Preferences& a,const Preferences& b){return !(a==b);}
bool validate(const Preferences&,std::string& error);
std::string serialize(const Preferences&);
bool parse(const std::string&,Preferences&,std::string& error);
// The game's startup read. A file whose only problem is its version-2 keys
// (display and texture pack) still loads, with those keys at their defaults;
// display_reset reports it. Anything else fails as parse() does.
bool parse_with_display_fallback(const std::string&,Preferences&,std::string& error,bool& display_reset);
enum Restart : uint32_t { no_restart = 0, network_restart = 1, renderer_restart = 2, resolution_restart = 4, texture_restart = 8 };
struct Size {unsigned width,height;};
// Window size presets offered by the UIs; any other valid size is "custom".
inline constexpr Size window_presets[] = {{1280,720},{1920,1080},{2560,1440},{3840,2160}};
inline constexpr unsigned min_window_width = 640, min_window_height = 360, max_window_extent = 7680;
// Internal resolution presets, as render_scale percentages of 1280x720.
inline constexpr unsigned render_scale_presets[] = {100,150,200,300};
inline constexpr unsigned min_render_scale = 100, max_render_scale = 300;
bool valid_window_size(unsigned width,unsigned height);
struct Change {
    bool accepted = false;
    uint32_t restart = no_restart;
    uint64_t revision = 0;
    std::string error;
};
class State {
    mutable std::mutex mutex_;
    Preferences active_, requested_;
    uint64_t revision_ = 0;
public:
    explicit State(const Preferences& initial = {});
    Preferences active() const;
    Preferences requested() const;
    Change apply(const Preferences&);
};
// Opening a PC menu must discard old pulses, and closing it must not turn
// a still-held confirm button into an answer. One gate per input provider.
class MenuInputGate {
    bool open_ = false;
    bool neutral_required_ = false;
    uint64_t blocked_ = 0, previous_ = 0;
public:
    void set_open(bool);
    void require_neutral() {neutral_required_=true;}
    bool open() const {return open_;}
    uint64_t filter(uint64_t held);
};
}
