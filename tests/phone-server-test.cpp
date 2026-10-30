#include "../port/phone_server.h"
#include "../third_party/cpp-httplib/httplib.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace buzz_phone;
#define check(b) do {if(!(b))throw std::runtime_error("Phone HTTP regression at line "+std::to_string(__LINE__));} while(false)
std::string field(const std::string& json,const std::string& name) {
    auto start=json.find('"'+name+"\":\"");check(start!=std::string::npos);start+=name.size()+4;
    return json.substr(start,json.find('"',start)-start);
}
int main(int argc,char** argv){try {
    if(argc==2&&std::string(argv[1])=="--serve") {
        Server demo;std::string message;
        check(demo.start(18730,8,[](unsigned p,uint8_t held,uint8_t pressed,bool release){if(pressed||release)std::cout<<"INPUT P"<<p+1<<" held="<<unsigned(held)<<" pressed="<<unsigned(pressed)<<" release="<<release<<std::endl;},message));
        for(const auto& url:demo.status().urls)std::cout<<url<<std::endl;
        std::cout<<"Press Enter to stop this test host."<<std::endl;
        std::string line;std::getline(std::cin,line);return 0;
    }
    std::atomic<unsigned> held{0},edges{0},releases{0};Server server;std::string error;unsigned port=0;
    for(unsigned i=18730;i<18750;++i)if(server.start(i,8,[&](unsigned p,uint8_t h,uint8_t b,bool r){if(p==7){held=h;if(b)++edges;if(r)++releases;}},error)){port=i;break;}
    check(port!=0);const auto status=server.status();check(status.running&&!status.urls.empty());
    Server competing;std::string conflict;
    check(!competing.start(port,4,[](unsigned,uint8_t,uint8_t,bool){},conflict)&&!conflict.empty());
    const auto room=status.urls[0].substr(status.urls[0].find('#')+1);check(room.size()==32);
    httplib::Client client("127.0.0.1",int(port));client.set_connection_timeout(2);client.set_read_timeout(3);
    auto page=client.Get("/");check(page&&page->status==200&&page->body.find("Your phone. Your buzzer.")!=std::string::npos);
    check(page->get_header_value("Content-Security-Policy").find("frame-ancestors 'none'")!=std::string::npos);
    for(const char* path:{"/controller.js","/style.css"}){auto r=client.Get(path);check(r&&r->status==200&&!r->body.empty());}
    auto forbidden=client.Get("/api/input");check(forbidden&&forbidden->status==403);
    auto no_header=client.Post("/api/join","room=x","application/x-www-form-urlencoded");check(no_header&&no_header->status==403);
    const httplib::Headers headers{{"X-Buzz-Controller","1"}};
    auto post=[&](const char* path,const std::string& data){auto r=client.Post(path,headers,data,"application/x-www-form-urlencoded");check(bool(r));return r;};
    check(post("/api/lobby","room=bad")->status==403);
    check(post("/api/lobby","room="+room)->status==200);
    auto joined=post("/api/join","room="+room+"&player=7&name=Ada");check(joined->status==200);
    const auto control=field(joined->body,"control"),resume=field(joined->body,"resume");
    check(control.size()==32&&resume.size()==32&&control!=resume);
    check(server.status().seats[7].reserved&&server.status().seats[7].online);
    check(post("/api/join","room="+room+"&player=7&name=Bob")->status==409);
    const auto input="control="+control+"&seq=1&held=31&pressed=31";
    check(post("/api/input",input)->status==200&&held==31&&edges==1);
    check(post("/api/input",input)->status==200&&edges==1);
    check(post("/api/input","control="+control+"&seq=2&held=0&pressed=1")->status==200&&held==0&&edges==2);
    check(post("/api/input",input)->status==409&&held==0&&edges==2);
    httplib::Headers foreign=headers;foreign.emplace("Origin","https://example.com");
    auto rejected=client.Post("/api/input",foreign,input,"application/x-www-form-urlencoded");
    if(!rejected)std::cerr<<"Origin request transport error: "<<httplib::to_string(rejected.error())<<'\n';
    else if(rejected->status!=403)std::cerr<<"Origin response: "<<rejected->status<<' '<<rejected->body<<'\n';
    check(rejected&&rejected->status==403);
    check(post("/api/input",std::string(4096,'x'))->status==413);
    server.set_locked(true);check(post("/api/join","room="+room+"&player=0&name=Bob")->status==423);
    auto recovered=post("/api/join","resume="+resume+"&player=0");check(recovered->status==200);
    const auto current=field(recovered->body,"control");check(current!=control&&held==0);
    check(post("/api/input",input)->status==401);
    check(post("/api/input","control="+current+"&seq=1&held=1&pressed=1")->status==200&&held==1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1450));check(held==0&&!server.status().seats[7].online&&server.status().seats[7].reserved);
    check(post("/api/input","control="+current+"&seq=2&held=0&pressed=0")->status==200);
    server.kick(7);check(!server.status().seats[7].reserved);
    check(post("/api/input","control="+current+"&seq=3&held=1&pressed=1")->status==401);
    server.reset();check(server.status().urls[0]!=status.urls[0]);check(post("/api/lobby","room="+room)->status==403);
    const auto next_url=server.status().urls[0],next_room=next_url.substr(next_url.find('#')+1);
    std::atomic<unsigned> completed{0},failed{0};std::vector<std::thread> phones;
    for(unsigned player=0;player<8;++player)phones.emplace_back([&,player] {
        httplib::Client phone("127.0.0.1",int(port));phone.set_read_timeout(3);
        auto joined=phone.Post("/api/join",headers,"room="+next_room+"&player="+std::to_string(player)+"&name=Phone"+std::to_string(player+1),"application/x-www-form-urlencoded");
        if(!joined||joined->status!=200){++failed;return;}
        const auto control=field(joined->body,"control");
        for(unsigned sequence=1;sequence<=3;++sequence) {
            auto input=phone.Post("/api/input",headers,"control="+control+"&seq="+std::to_string(sequence)+"&held=0&pressed="+std::to_string(sequence==1?1u<<(player%5):0),"application/x-www-form-urlencoded");
            if(!input||input->status!=200){++failed;return;}
        }
        ++completed;
    });
    for(auto& phone:phones)phone.join();check(completed==8&&failed==0);
    for(const auto& seat:server.status().seats)check(seat.reserved&&seat.online);
    client.stop();server.stop();check(!server.status().running&&releases>0);
    check(!server.start(80,4,[](unsigned,uint8_t,uint8_t,bool){},error)&&!error.empty());
    std::cout<<"PASS embedded HTTP: assets, real pairing/input, auth/origin/size limits, sequence protection, resume, expiry, host kick/reset/stop\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
