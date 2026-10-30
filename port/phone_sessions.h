#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace buzz_phone {
// Pure session state. The server serializes calls; time is monotonic milliseconds.
// Only accepted, strictly increasing packets can produce an input edge.
constexpr uint64_t input_lease_ms=1200;
struct Player {
    std::string resume, control, name;
    uint64_t sequence=0,last_seen=0;
    uint8_t held=0;
    bool online=false;
    bool needs_neutral=false;
};
struct Join {int status=200;unsigned player=0;std::string resume,control,error;};
class Sessions {
    std::function<std::string()> token_;
    std::function<void(unsigned,uint8_t,uint8_t,bool)> input_;
public:
    unsigned count;
    bool locked=false;
    std::string room;
    std::array<Player,8> players{};
    Sessions(unsigned count,std::function<std::string()> token,
             std::function<void(unsigned,uint8_t,uint8_t,bool)> input);
    Join join(const std::string& room,const std::string& resume,unsigned slot,const std::string& name,uint64_t now);
    // 200 includes duplicates (idempotent acknowledgement), 409 is an old packet.
    int update(const std::string& control,uint64_t sequence,unsigned held,unsigned pressed,uint64_t now);
    bool leave(const std::string& control);
    void kick(unsigned slot);
    void reset();
    void expire(uint64_t now);
};
}
