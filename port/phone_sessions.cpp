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
    if(slot==auto_slot) {
        const uint32_t taken=occupied_?occupied_():0;
        for(unsigned i=0;i<count;++i)if(players[i].resume.empty()&&!(taken>>i&1)){slot=i;break;}
        if(slot==auto_slot)return {409,0,{},{},"All player slots are taken"};
    }
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
uint32_t Sessions::reserved_mask() const {
    uint32_t mask=0;
    for(unsigned i=0;i<count;++i)if(!players[i].resume.empty())mask|=1u<<i;
    return mask;
}
namespace {
std::string hex(const std::string& text){
    static const char* digits="0123456789abcdef";std::string out;
    for(unsigned char c:text){out+=digits[c>>4];out+=digits[c&15];}
    return out;
}
bool unhex(const std::string& text,std::string& out){
    if(text.size()%2)return false;out.clear();
    auto value=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<text.size();i+=2){const int hi=value(text[i]),lo=value(text[i+1]);if(hi<0||lo<0)return false;out+=char(hi*16+lo);}
    return true;
}
bool token_ok(const std::string& t){
    if(t.size()!=32)return false;
    for(char c:t)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
}
std::string Sessions::save() const {
    std::string out="buzz-phone-pairing 1\nslots "+std::to_string(count)+"\nroom "+room+"\nlocked "+(locked?"1":"0")+"\n";
    for(unsigned i=0;i<count;++i)if(!players[i].resume.empty())
        out+="player "+std::to_string(i)+" "+players[i].resume+" "+hex(players[i].name)+"\n";
    return out;
}
bool Sessions::restore(const std::string& text) {
    std::string room_value;bool locked_value=false,header=false;unsigned slots=0;
    std::array<Player,8> loaded{};
    size_t start=0;
    while(start<text.size()) {
        const auto end=text.find('\n',start);
        const auto line=text.substr(start,end==std::string::npos?std::string::npos:end-start);
        start=end==std::string::npos?text.size():end+1;
        if(line.empty())continue;
        const auto space=line.find(' ');
        const auto key=line.substr(0,space),value=space==std::string::npos?std::string():line.substr(space+1);
        if(key=="buzz-phone-pairing"){if(value!="1")return false;header=true;}
        else if(key=="slots"){if(value!="4"&&value!="8")return false;slots=unsigned(value[0]-'0');}
        else if(key=="room"){if(!token_ok(value))return false;room_value=value;}
        else if(key=="locked"){if(value!="0"&&value!="1")return false;locked_value=value=="1";}
        else if(key=="player") {
            const auto a=value.find(' '),b=a==std::string::npos?a:value.find(' ',a+1);
            if(b==std::string::npos||a!=1||value[0]<'0'||value[0]>'7')return false;
            const unsigned slot=unsigned(value[0]-'0');
            const auto resume=value.substr(a+1,b-a-1);std::string name;
            if(!token_ok(resume)||!unhex(value.substr(b+1),name)||name.empty()||name.size()>96)return false;
            for(unsigned char c:name)if(c<32||c==127)return false;
            loaded[slot]=Player{resume,{},name,0,0,0,false,true};
        } else return false;
    }
    // A pairing for the other player count, or a damaged file, is ignored.
    if(!header||slots!=count||room_value.empty())return false;
    for(unsigned i=count;i<8;++i)if(!loaded[i].resume.empty())return false;
    room=room_value;locked=locked_value;
    for(unsigned i=0;i<count;++i)players[i]=loaded[i];
    return true;
}
}
