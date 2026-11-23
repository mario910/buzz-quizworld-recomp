#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace buzz_phone {
struct Seat {std::string name;bool reserved=false,online=false;};
struct Status {
    bool running=false,locked=false;
    unsigned port=0;
    std::string error;
    std::vector<std::string> urls;
    std::array<Seat,8> seats{};
};
class Server {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    using Input=std::function<void(unsigned,uint8_t,uint8_t,bool)>;
    Server();
    ~Server();
    Server(const Server&)=delete;
    Server& operator=(const Server&)=delete;
    // Optional, set before start(): the file that keeps the pairing (QR code,
    // joined phones) across restarts, and the slot masks shared with local
    // controllers for automatic player slots.
    void set_persistence(std::filesystem::path path);
    void set_occupancy(std::function<uint32_t()> occupied,std::function<void(uint32_t)> publish);
    bool start(unsigned port,unsigned players,Input input,std::string& error);
    void stop();
    Status status() const;
    void set_locked(bool);
    void kick(unsigned);
    void reset();
};
// One opt-in host instance, shared by runtime settings and the PC overlay.
Server& host();
}
