#include "phone_server.h"
#include "phone_sessions.h"
// Header-only HTTP transport, with bounded workers, queue and request sizes.
#include "../third_party/cpp-httplib/httplib.h"
#ifdef _WIN32
#include <iphlpapi.h>
#include <bcrypt.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <random>
#endif
#include <atomic>
#include <charconv>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

namespace buzz_phone {
namespace {
const char page[]=
#include "phone_web/page.inc"
;
const char style[]=
#include "phone_web/style.inc"
;
const char script[]=
#include "phone_web/script.inc"
;
uint64_t now_ms() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::string token() {
    unsigned char bytes[16];
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("Cannot generate phone pairing credentials");
#else
    std::random_device random;for(auto& b:bytes)b=static_cast<unsigned char>(random());
#endif
    const char* hex="0123456789abcdef";std::string result;
    for(auto b:bytes){result+=hex[b>>4];result+=hex[b&15];}return result;
}
std::string quote(const std::string& s) {
    std::string result="\"";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\'){result+='\\';result+=char(c);}
        else if(c<32){const char* hex="0123456789abcdef";result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=char(c);
    }
    return result+'"';
}
bool number(const std::string& text,uint64_t& n) {
    if(text.empty()||text.size()>16)return false;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&n<=9007199254740991ull;
}
// Private IPv4 interfaces only. The local URL remains available for diagnostics.
std::vector<std::string> addresses() {
    std::set<std::string> result;
    auto add=[&](const sockaddr* address) {
        if(!address||address->sa_family!=AF_INET)return;
        const auto* ipv4=reinterpret_cast<const sockaddr_in*>(address);
        const uint32_t ip=ntohl(ipv4->sin_addr.s_addr);
        if((ip>>24)!=10&&(ip>>20)!=0xac1&&(ip>>16)!=0xc0a8&&(ip>>16)!=0xa9fe)return;
        char text[INET_ADDRSTRLEN]{};
        if(inet_ntop(AF_INET,&ipv4->sin_addr,text,sizeof(text)))result.insert(text);
    };
#ifdef _WIN32
    ULONG size=16384;std::vector<unsigned char> buffer(size);ULONG code;
    for(int i=0;i<3;++i) {
        code=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);
        if(code!=ERROR_BUFFER_OVERFLOW)break;buffer.resize(size);
    }
    if(code==NO_ERROR)for(auto* a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a;a=a->Next)
        if(a->OperStatus==IfOperStatusUp&&a->IfType!=IF_TYPE_SOFTWARE_LOOPBACK)
            for(auto* u=a->FirstUnicastAddress;u;u=u->Next)add(u->Address.lpSockaddr);
#else
    ifaddrs* list=nullptr;
    if(getifaddrs(&list)==0){for(auto* a=list;a;a=a->ifa_next)if(a->ifa_flags&IFF_UP)add(a->ifa_addr);freeifaddrs(list);}
#endif
    std::vector<std::string> list(result.begin(),result.end());list.push_back("127.0.0.1");return list;
}
void json(httplib::Response& response,int status,const std::string& text) {
    response.status=status;response.set_content(text,"application/json; charset=utf-8");
}
void error(httplib::Response& response,int status,const std::string& message) {
    json(response,status,"{\"error\":"+quote(message)+"}");
}
bool api_request(const httplib::Request& request,httplib::Response& response) {
    // Check after httplib has read the bounded request body. Rejecting POST in
    // pre-routing leaves unread TCP bytes and can reset the error response.
    if(request.get_header_value("X-Buzz-Controller")!="1"||
       request.get_header_value("Content-Type").rfind("application/x-www-form-urlencoded",0)!=0) {
        error(response,403,"Use the controller page");return false;
    }
    const auto origin=request.get_header_value("Origin");
    if(!origin.empty()&&origin!="http://"+request.get_header_value("Host")) {
        error(response,403,"Origin is not allowed");return false;
    }
    return true;
}
}
struct Server::Impl {
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::unique_ptr<httplib::Server> http;
    std::unique_ptr<Sessions> sessions;
    std::thread listener,sweeper;
    std::atomic<bool> stopping{true},running{false};
    unsigned port=0;
    std::string failure;
    std::vector<std::string> ips;
    void stop() {
        stopping=true;
        if(http)http->stop();
        if(listener.joinable())listener.join();
        if(sweeper.joinable())sweeper.join();
        std::lock_guard lock(mutex);
        if(sessions)for(unsigned i=0;i<sessions->count;++i)sessions->kick(i);
        sessions.reset();http.reset();running=false;
    }
};
Server::Server():impl_(std::make_unique<Impl>()){}
Server::~Server(){stop();}
bool Server::start(unsigned port,unsigned players,Input input,std::string& message) {
    auto& p=*impl_;std::lock_guard lifecycle(p.lifecycle);
    if(p.running&&p.port==port){message.clear();return true;}
    p.stop();
    try {
        if(port<1024||port>65535)throw std::runtime_error("Phone controller port must be 1024-65535");
        {
            std::lock_guard lock(p.mutex);
            p.port=port;p.ips=addresses();p.sessions=std::make_unique<Sessions>(players,token,std::move(input));
            p.http=std::make_unique<httplib::Server>();
        }
        auto& http=*p.http;
        // httplib's default SO_REUSEADDR permits competing listeners on Windows
        // (and SO_REUSEPORT does on Unix). One game must own a controller port.
        http.set_socket_options([](socket_t socket) {
            int enabled=1;
#ifdef _WIN32
            setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&enabled),sizeof(enabled));
#else
            setsockopt(socket,SOL_SOCKET,SO_REUSEADDR,&enabled,sizeof(enabled));
#endif
        });
        http.new_task_queue=[] {return new httplib::ThreadPool(12,32);};
        http.set_payload_max_length(2048);http.set_read_timeout(2);http.set_write_timeout(2);
        http.set_keep_alive_timeout(1);http.set_keep_alive_max_count(100);
        http.set_default_headers({{"Cache-Control","no-store"},{"X-Content-Type-Options","nosniff"},
            {"Referrer-Policy","no-referrer"},{"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'"}});
        http.set_pre_routing_handler([](const httplib::Request& request,httplib::Response& response) {
            // No CORS, JSONP, query-string credentials or state-changing GETs.
            if(request.path.rfind("/api/",0)==0&&request.method!="POST") {
                error(response,403,"Use the controller page");return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        http.Get("/",[](const auto&,auto& r){r.set_content(page,"text/html; charset=utf-8");});
        http.Get("/style.css",[](const auto&,auto& r){r.set_content(style,"text/css; charset=utf-8");});
        http.Get("/controller.js",[](const auto&,auto& r){r.set_content(script,"text/javascript; charset=utf-8");});
        http.Get("/favicon.svg",[](const auto&,auto& r){r.set_content("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect width='64' height='64' rx='14' fill='#111026'/><circle cx='32' cy='32' r='23' fill='#ed325b'/><path d='M29 16h6v24h-6zm0 28h6v6h-6z' fill='white'/></svg>","image/svg+xml");});
        http.Post("/api/lobby",[&p](const auto& request,auto& r) {
            if(!api_request(request,r))return;
            std::lock_guard lock(p.mutex);const auto& s=*p.sessions;
            if(request.get_param_value("room")!=s.room){error(r,403,"Scan the current QR code on the PC");return;}
            std::string text="{\"locked\":"+std::string(s.locked?"true":"false")+",\"players\":[";
            for(unsigned i=0;i<s.count;++i) {
                if(i)text+=',';const auto& seat=s.players[i];
                text+="{\"name\":"+quote(seat.name)+",\"reserved\":"+(seat.resume.empty()?"false":"true")+",\"online\":"+(seat.online?"true":"false")+'}';
            }
            json(r,200,text+"]}");
        });
        http.Post("/api/join",[&p](const auto& request,auto& r) {
            if(!api_request(request,r))return;
            uint64_t slot=0;if(!number(request.get_param_value("player"),slot)||slot>7){error(r,400,"Choose a player");return;}
            std::lock_guard lock(p.mutex);
            const auto joined=p.sessions->join(request.get_param_value("room"),request.get_param_value("resume"),unsigned(slot),request.get_param_value("name"),now_ms());
            if(joined.status!=200){error(r,joined.status,joined.error);return;}
            json(r,200,"{\"player\":"+std::to_string(joined.player)+",\"resume\":"+quote(joined.resume)+",\"control\":"+quote(joined.control)+",\"name\":"+quote(p.sessions->players[joined.player].name)+"}");
        });
        http.Post("/api/input",[&p](const auto& request,auto& r) {
            if(!api_request(request,r))return;
            uint64_t sequence=0,held=0,pressed=0;
            if(!number(request.get_param_value("seq"),sequence)||!number(request.get_param_value("held"),held)||
               !number(request.get_param_value("pressed"),pressed)||held>31||pressed>31){error(r,400,"Invalid input");return;}
            std::lock_guard lock(p.mutex);
            const auto result=p.sessions->update(request.get_param_value("control"),sequence,unsigned(held),unsigned(pressed),now_ms());
            if(result!=200){error(r,result,"Session ended or superseded. Rejoin from this phone");return;}
            json(r,200,"{\"ok\":true}");
        });
        http.Post("/api/leave",[&p](const auto& request,auto& r) {
            if(!api_request(request,r))return;
            std::lock_guard lock(p.mutex);
            if(!p.sessions->leave(request.get_param_value("control"))){error(r,401,"Session ended");return;}
            json(r,200,"{\"ok\":true}");
        });
        if(!http.bind_to_port("0.0.0.0",int(port)))throw std::runtime_error("Cannot open phone controller port "+std::to_string(port)+". Choose another port or check Windows Firewall.");
        p.stopping=false;p.running=true;
        {std::lock_guard lock(p.mutex);p.failure.clear();}
        p.listener=std::thread([&p] {
            if(!p.http->listen_after_bind()&&!p.stopping){std::lock_guard lock(p.mutex);p.failure="Phone controller listener stopped unexpectedly";}
            p.running=false;
        });
        p.sweeper=std::thread([&p] {
            while(!p.stopping) {
                {std::lock_guard lock(p.mutex);if(p.sessions)p.sessions->expire(now_ms());}
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        message.clear();return true;
    }catch(const std::exception& e){p.stop();std::lock_guard lock(p.mutex);p.failure=e.what();message=p.failure;return false;}
}
void Server::stop(){std::lock_guard lifecycle(impl_->lifecycle);impl_->stop();std::lock_guard lock(impl_->mutex);impl_->failure.clear();}
Status Server::status() const {
    auto& p=*impl_;std::lock_guard lock(p.mutex);Status result;
    result.running=p.running;result.port=p.port;result.error=p.failure;
    if(p.sessions) {
        result.locked=p.sessions->locked;
        for(const auto& ip:p.ips)result.urls.push_back("http://"+ip+":"+std::to_string(p.port)+"/#"+p.sessions->room);
        for(unsigned i=0;i<p.sessions->count;++i){const auto& s=p.sessions->players[i];result.seats[i]={s.name,!s.resume.empty(),s.online};}
    }
    return result;
}
void Server::set_locked(bool value){std::lock_guard lock(impl_->mutex);if(impl_->sessions)impl_->sessions->locked=value;}
void Server::kick(unsigned player){std::lock_guard lock(impl_->mutex);if(impl_->sessions)impl_->sessions->kick(player);}
void Server::reset(){std::lock_guard lock(impl_->mutex);if(impl_->sessions)impl_->sessions->reset();}
Server& host(){static Server instance;return instance;}
}
