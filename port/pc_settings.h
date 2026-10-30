#pragma once
// Shared player preferences. No renderer, SDL, guest memory or platform API.
// Live consumers poll State::active(); restart-only changes remain requested.
#include <cstdint>
#include <mutex>
#include <string>

namespace buzz_pc {
enum class Renderer { automatic, d3d12, vulkan };
struct Preferences {
    unsigned volume_percent = 100;
    unsigned fps_limit = 30; // Only 0 (uncapped) or 30, not a simulation-speed knob.
    bool fullscreen = false;
    bool offline = true;
    Renderer renderer = Renderer::automatic;
};
bool operator==(const Preferences&, const Preferences&);
inline bool operator!=(const Preferences& a,const Preferences& b){return !(a==b);}
bool validate(const Preferences&,std::string& error);
std::string serialize(const Preferences&);
bool parse(const std::string&,Preferences&,std::string& error);
enum Restart : uint32_t { no_restart = 0, network_restart = 1, renderer_restart = 2 };
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
