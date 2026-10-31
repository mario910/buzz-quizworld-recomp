#include "pc_settings.h"
#include <set>
#include <sstream>
#include <stdexcept>

namespace buzz_pc {
bool operator==(const Preferences& a,const Preferences& b) {
    return a.volume_percent==b.volume_percent && a.fps_limit==b.fps_limit &&
        a.fullscreen==b.fullscreen && a.offline==b.offline && a.renderer==b.renderer &&
        a.phone_buzzers==b.phone_buzzers && a.phone_port==b.phone_port &&
        a.window_width==b.window_width && a.window_height==b.window_height && a.render_scale==b.render_scale;
}
bool valid_window_size(unsigned w,unsigned h) {
    if(!w&&!h)return true; // backend default
    return w>=min_window_width&&h>=min_window_height&&w<=max_window_extent&&h<=max_window_extent;
}
bool validate(const Preferences& p,std::string& error) {
    if(p.volume_percent>100)error="Volume must be 0-100 percent";
    else if(p.fps_limit!=0&&p.fps_limit!=30)error="Frame limit must be 0 or 30";
    else if(p.renderer!=Renderer::automatic&&p.renderer!=Renderer::d3d12&&p.renderer!=Renderer::vulkan)error="Invalid renderer preference";
    else if(p.phone_port<1024||p.phone_port>65535)error="Phone controller port must be 1024-65535";
    else if(!valid_window_size(p.window_width,p.window_height))error="Window size must be 640x360 to 7680x7680, or 0x0 for the default";
    else if(p.render_scale<min_render_scale||p.render_scale>max_render_scale)error="Internal resolution must be 100-300 percent";
    else {error.clear();return true;}
    return false;
}
std::string serialize(const Preferences& p) {
    std::string error;if(!validate(p,error))throw std::invalid_argument(error);
    std::ostringstream s;
    const char* renderer=p.renderer==Renderer::automatic?"auto":p.renderer==Renderer::d3d12?"d3d12":"vulkan";
    // Version 2 only adds display keys. Default display settings keep writing
    // the exact version-1 file, so older builds sharing it still load it.
    const bool display=p.window_width||p.window_height||p.render_scale!=100;
    s<<"# Buzz PC preferences v"<<(display?2:1)<<". Restart-only options are not live switches.\n"
      <<"version="<<(display?2:1)<<"\nvolume="<<p.volume_percent<<"\nfps_limit="<<p.fps_limit
      <<"\nfullscreen="<<p.fullscreen<<"\noffline="<<p.offline<<"\nrenderer="<<renderer
      <<"\nphone_buzzers="<<p.phone_buzzers<<"\nphone_port="<<p.phone_port<<'\n';
    if(display)s<<"window_width="<<p.window_width<<"\nwindow_height="<<p.window_height<<"\nrender_scale="<<p.render_scale<<'\n';
    return s.str();
}
bool parse(const std::string& text,Preferences& output,std::string& error) {
    if(text.size()>4096){error="PC preferences exceed 4 KiB";return false;}
    Preferences p;std::set<std::string> seen;std::istringstream input(text);std::string line;
    auto trim=[](std::string s){auto first=s.find_first_not_of(" \t\r");return first==std::string::npos?std::string{}:s.substr(first,s.find_last_not_of(" \t\r")-first+1);};
    unsigned line_number=0,version=0;
    while(std::getline(input,line)) {
        if(++line_number==1&&line.compare(0,3,"\xef\xbb\xbf")==0)line.erase(0,3);
        line=trim(line);if(line.empty()||line[0]=='#')continue;
        auto at=line.find('=');
        auto fail=[&]{error="Invalid PC preference at line "+std::to_string(line_number);return false;};
        if(at==std::string::npos)return fail();
        auto key=trim(line.substr(0,at)),value=trim(line.substr(at+1));
        if(!seen.insert(key).second)return fail();
        unsigned n=0;
        auto number=[&] {if(value.empty()||value.size()>5||value.find_first_not_of("0123456789")!=std::string::npos)return false;n=0;for(char c:value)n=n*10+unsigned(c-'0');return true;};
        if(key=="version"){if(value!="1"&&value!="2")return fail();version=value[0]-'0';}
        else if(key=="volume"){if(!number()||n>100)return fail();p.volume_percent=n;}
        else if(key=="fps_limit"){if(!number()||(n!=0&&n!=30))return fail();p.fps_limit=n;}
        else if(key=="fullscreen"||key=="offline") {if(value!="0"&&value!="1")return fail();(key=="fullscreen"?p.fullscreen:p.offline)=value=="1";}
        else if(key=="renderer") {if(value=="auto")p.renderer=Renderer::automatic;else if(value=="d3d12")p.renderer=Renderer::d3d12;else if(value=="vulkan")p.renderer=Renderer::vulkan;else return fail();}
        else if(key=="phone_buzzers") {if(value!="0"&&value!="1")return fail();p.phone_buzzers=value=="1";}
        else if(key=="phone_port") {if(!number()||n<1024||n>65535)return fail();p.phone_port=n;}
        else if(key=="window_width"||key=="window_height") {if(!number()||n>max_window_extent)return fail();(key=="window_width"?p.window_width:p.window_height)=n;}
        else if(key=="render_scale") {if(!number()||n<min_render_scale||n>max_render_scale)return fail();p.render_scale=n;}
        else return fail();
    }
    if(!seen.count("version")){error="PC preferences require version=1 or 2";return false;}
    if(version==1&&(seen.count("window_width")||seen.count("window_height")||seen.count("render_scale"))){error="Display size keys require version=2";return false;}
    if(!validate(p,error))return false;
    output=p;error.clear();return true;
}
bool parse_with_display_fallback(const std::string& text,Preferences& output,std::string& error,bool& display_reset) {
    display_reset=false;
    if(parse(text,output,error))return true;
    const std::string original=error;
    std::istringstream input(text);std::string line,kept;
    while(std::getline(input,line)) {
        auto key=line.substr(0,line.find('='));
        key.erase(0,key.find_first_not_of(" \t\r\xef\xbb\xbf"));
        key.erase(key.find_last_not_of(" \t\r")+1);
        if(key=="window_width"||key=="window_height"||key=="render_scale")continue;
        if(key=="version")line="version=1";
        kept+=line+'\n';
    }
    if(!parse(kept,output,error)){error=original;return false;}
    display_reset=true;error=original;return true;
}
State::State(const Preferences& p):active_(p),requested_(p) {
    std::string error;if(!validate(p,error))throw std::invalid_argument(error);
}
Preferences State::active() const {std::lock_guard lock(mutex_);return active_;}
Preferences State::requested() const {std::lock_guard lock(mutex_);return requested_;}
Change State::apply(const Preferences& p) {
    std::string error;if(!validate(p,error))return {false,no_restart,0,error};
    std::lock_guard lock(mutex_);
    if(requested_!=p)++revision_;
    requested_=p;
    active_.volume_percent=p.volume_percent;active_.fps_limit=p.fps_limit;active_.fullscreen=p.fullscreen;
    active_.phone_buzzers=p.phone_buzzers;active_.phone_port=p.phone_port;
    active_.window_width=p.window_width;active_.window_height=p.window_height;
    uint32_t restart=no_restart;
    if(active_.offline!=p.offline)restart|=network_restart;
    if(active_.renderer!=p.renderer)restart|=renderer_restart;
    if(active_.render_scale!=p.render_scale)restart|=resolution_restart;
    return {true,restart,revision_,{}};
}
void MenuInputGate::set_open(bool open) {
    if(open_&&!open)blocked_|=previous_;
    open_=open;
}
uint64_t MenuInputGate::filter(uint64_t held) {
    previous_=held;
    if(neutral_required_){if(!held){neutral_required_=false;blocked_=0;}return 0;}
    blocked_&=held;
    if(open_){blocked_|=held;return 0;}
    return held&~blocked_;
}
}
