#include "buzzer_input.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>
#include <vector>

namespace buzz_input {
namespace {
std::string trim(std::string s){
    const auto first=s.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
std::string upper(std::string s){for(auto& c:s)c=char(std::toupper((unsigned char)c));return s;}
bool number(const std::string& s,unsigned& value,unsigned maximum){
    if(s.empty()||s[0]=='-'||s[0]=='+')return false;
    char* end=nullptr;const auto n=std::strtoul(s.c_str(),&end,0);
    if(end==s.c_str()||*end||n>maximum)return false;
    value=unsigned(n);return true;
}
std::vector<std::string> split(const std::string& text){
    std::vector<std::string> v;size_t start=0;
    for(;;){const auto end=text.find(',',start);v.push_back(trim(text.substr(start,end-start)));
        if(end==std::string::npos)return v;start=end+1;}
}
bool key(std::string s,unsigned& value){
    s=upper(s);
    if(s=="NONE"){value=0;return true;}
    if(s.size()==1&&((s[0]>='A'&&s[0]<='Z')||(s[0]>='0'&&s[0]<='9'))){value=(unsigned)s[0];return true;}
    const std::pair<const char*,unsigned> names[]={{"SPACE",32},{"ENTER",13},{"TAB",9},
        {"ESC",27},{"UP",38},{"DOWN",40},{"LEFT",37},{"RIGHT",39},
        {"LSHIFT",160},{"RSHIFT",161},{"LCTRL",162},{"RCTRL",163},{"BACKSPACE",8}};
    for(const auto& n:names)if(s==n.first){value=n.second;return true;}
    unsigned n=0;
    if(s.size()>1&&s[0]=='F'&&number(s.substr(1),n,24)&&n){value=111+n;return true;}
    return number(s,value,255)&&value>0;
}
uint16_t pad_button(std::string s){
    s=upper(s);
    const std::pair<const char*,uint16_t> names[]={{"START",0x10},{"BACK",0x20},
        {"A",0x1000},{"B",0x2000},{"X",0x4000},{"Y",0x8000},
        {"LB",0x100},{"RB",0x200},{"UP",1},{"DOWN",2},{"LEFT",4},{"RIGHT",8},
        {"LS",0x40},{"RS",0x80}};
    for(const auto& n:names)if(s==n.first)return n.second;
    return 0;
}
}
Config defaults(){
    Config c;
    c.keys[0]={32,'4','3','2','1'};c.keys[1]={'Q','W','E','R','T'};
    c.keys[2]={'A','S','D','F','G'};c.keys[3]={'Z','X','C','V','B'};
    for(auto& mapping:c.pad_buttons)mapping={0x10,0x8000,0x1000,0x2000,0x4000};
    return c;
}
bool parse_keys(const std::string& text,std::array<unsigned,5>& keys){
    auto tokens=split(text);if(tokens.size()!=5)return false;
    std::array<unsigned,5> candidate{};std::set<unsigned> used;
    for(unsigned i=0;i<5;++i){
        if(!key(tokens[i],candidate[i]))return false;
        if(candidate[i]&&!used.insert(candidate[i]).second)return false;
    }
    keys=candidate;return true;
}
bool parse_config(const std::string& text,Config& config,std::string& error){
    if(text.size()>65536){error="input config exceeds 64 KiB";return false;}
    Config candidate=config;std::set<std::string> seen;std::istringstream input(text);
    std::string line;unsigned line_no=0;
    while(std::getline(input,line)){
        ++line_no;
        if(line_no==1&&line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        line=trim(line);if(line.empty()||line[0]=='#')continue;
        const auto equal=line.find('=');
        auto fail=[&](){error="invalid input config at line "+std::to_string(line_no)+": "+line;return false;};
        if(equal==std::string::npos)return fail();
        const auto name=trim(line.substr(0,equal)), value=trim(line.substr(equal+1));
        if(!seen.insert(name).second)return fail();
        unsigned n=0;
        if(name=="players"){
            if(!number(value,n,8)||(n!=4&&n!=8))return fail();candidate.players=n;
        }else if(name=="pulse_ms"){
            if(!number(value,n,5000)||n<10)return fail();candidate.pulse_ms=n;
        }else if(name=="gamepads"||name=="background_gamepads"){
            if(value!="0"&&value!="1")return fail();
            (name=="gamepads"?candidate.gamepads:candidate.background_gamepads)=value=="1";
        }else if(name.rfind("keyboard.",0)==0){
            if(!number(name.substr(9),n,8)||!n||!parse_keys(value,candidate.keys[n-1]))return fail();
        }else if(name.rfind("gamepad.",0)==0){
            if(!number(name.substr(8),n,4)||!n)return fail();
            if(value=="NONE"){candidate.pad_player[n-1]=-1;continue;}
            auto tokens=split(value);unsigned player=0;
            if(tokens.size()!=6||!number(tokens[0],player,8)||!player)return fail();
            std::array<uint16_t,5> mapping{};std::set<uint16_t> used;
            for(unsigned i=0;i<5;++i){mapping[i]=pad_button(tokens[i+1]);
                if(!mapping[i]||!used.insert(mapping[i]).second)return fail();}
            candidate.pad_player[n-1]=int(player-1);candidate.pad_buttons[n-1]=mapping;
        }else return fail();
    }
    config=candidate;error.clear();return true;
}
uint8_t map_pad(uint16_t buttons,const std::array<uint16_t,5>& mapping){
    uint8_t result=0;for(unsigned i=0;i<5;++i)if(buttons&mapping[i])result|=uint8_t(1u<<i);return result;
}
}
