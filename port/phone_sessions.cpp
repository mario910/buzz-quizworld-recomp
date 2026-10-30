#include "phone_sessions.h"
#include <stdexcept>
#include <utility>

namespace buzz_phone {
Sessions::Sessions(unsigned n,std::function<std::string()> token,
    std::function<void(unsigned,uint8_t,uint8_t,bool)> input)
    :token_(std::move(token)),input_(std::move(input)),count(n) {
    if(n!=4&&n!=8)throw std::invalid_argument("Phone controllers require four or eight slots");
    room=token_();
}
Join Sessions::join(const std::string& key,const std::string& resume,unsigned slot,const std::string& name,uint64_t now) {
    // A resume credential is sufficient after joining; the room may be locked.
    if(!resume.empty())for(unsigned i=0;i<count;++i)if(players[i].resume==resume) {
        auto& p=players[i];const auto control=token_();
        input_(i,0,0,true);p.held=0;p.control=control;p.sequence=0;p.last_seen=now;p.online=true;p.needs_neutral=false;
        return {200,i,p.resume,p.control,{}};
    }
    if(key!=room)return {403,0,{},{},"Scan the current QR code on the PC"};
    if(!resume.empty())return {401,0,{},{},"This session ended. Join again"};
    if(locked)return {423,0,{},{},"Joining is locked by the host"};
    if(slot>=count)return {400,0,{},{},"Choose a valid player"};
    if(!players[slot].resume.empty())return {409,0,{},{},"That player is already reserved"};
    if(name.empty()||name.size()>96)return {400,0,{},{},"Enter a shorter player name"};
    for(unsigned char c:name)if(c<32||c==127)return {400,0,{},{},"Name contains a control character"};
    const auto session=token_(),control=token_();
    players[slot]={session,control,name,0,now,0,true};
    return {200,slot,session,control,{}};
}
int Sessions::update(const std::string& control,uint64_t sequence,unsigned held,unsigned pressed,uint64_t now) {
    if(control.empty())return 401;
    if(held>31||pressed>31||!sequence)return 400;
    for(unsigned i=0;i<count;++i)if(players[i].control==control) {
        auto& p=players[i];
        if(sequence<p.sequence)return 409;
        if(sequence==p.sequence)return 200; // no refresh or second edge on replay
        p.sequence=sequence;p.last_seen=now;p.online=true;p.held=uint8_t(held);
        if(p.needs_neutral) {
            if(!held&&!pressed)p.needs_neutral=false;
            p.held=0;input_(i,0,0,true);return 200;
        }
        input_(i,uint8_t(held),uint8_t(pressed),false);return 200;
    }
    return 401;
}
bool Sessions::leave(const std::string& control) {
    if(control.empty())return false;
    for(unsigned i=0;i<count;++i)if(players[i].control==control){kick(i);return true;}
    return false;
}
void Sessions::kick(unsigned slot) {
    if(slot>=count)return;
    input_(slot,0,0,true);players[slot]={};
}
void Sessions::reset() {
    const auto key=token_();
    for(unsigned i=0;i<count;++i)kick(i);
    room=key;locked=false;
}
void Sessions::expire(uint64_t now) {
    for(unsigned i=0;i<count;++i) {
        auto& p=players[i];
        if(p.online&&now>=p.last_seen&&now-p.last_seen>=input_lease_ms) {
            input_(i,0,0,true);p.online=false;p.held=0;p.needs_neutral=true;
        }
    }
}
}
