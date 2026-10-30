#include "../port/phone_sessions.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace buzz_phone;
void check(bool b){if(!b)throw std::runtime_error("Phone session regression");}
int main(){try {
    struct Event {unsigned player,held,pressed;bool release;};std::vector<Event> events;
    unsigned next=0;
    Sessions s(8,[&]{return "token"+std::to_string(++next);},[&](unsigned p,uint8_t h,uint8_t b,bool r){events.push_back({p,h,b,r});});
    check(s.join("wrong","",0,"Ada",10).status==403);
    auto joined=s.join(s.room,"",7,"Ada",10);check(joined.status==200&&joined.player==7);
    check(s.join(s.room,"",7,"Bob",11).status==409);
    check(s.join(s.room,"",8,"Bob",11).status==400);
    check(s.join(s.room,"",0,"\n",11).status==400);
    check(s.update("",1,1,1,12)==401);
    check(s.update(joined.control,0,1,1,12)==400);
    check(s.update(joined.control,1,32,0,12)==400);
    check(s.update(joined.control,1,31,31,12)==200);
    check(events.size()==1&&events.back().player==7&&events.back().held==31&&events.back().pressed==31);
    check(s.update(joined.control,1,31,31,200)==200&&events.size()==1);
    check(s.update(joined.control,2,0,0,201)==200);
    check(s.update(joined.control,1,31,31,202)==409&&events.size()==2);
    s.expire(1400);check(s.players[7].online);
    s.expire(1401);check(!s.players[7].online&&events.back().release);
    check(!s.players[7].resume.empty()); // reservation survives background/sleep
    check(s.update(joined.control,3,1,1,1402)==200&&events.back().release); // no delayed held edge after expiry
    check(s.update(joined.control,4,0,0,1403)==200);
    check(s.update(joined.control,5,1,1,1404)==200&&events.back().pressed==1);
    s.locked=true;check(s.join(s.room,"",0,"Bob",1500).status==423);
    const auto recovered=s.join("",joined.resume,0,"ignored",1500);
    check(recovered.status==200&&recovered.player==7&&recovered.control!=joined.control&&events.back().release);
    check(s.update(joined.control,9,31,31,1501)==401); // old tab cannot drive
    check(s.update(recovered.control,1,1,1,1501)==200);
    check(s.leave(recovered.control)&&!s.leave(recovered.control));
    s.locked=false;
    for(unsigned i=0;i<8;++i)check(s.join(s.room,"",i,"Player",1600).status==200);
    const auto room=s.room,credential=s.players[0].resume;s.reset();
    check(room!=s.room&&!s.locked);
    for(const auto& p:s.players)check(p.resume.empty()&&!p.online);
    check(s.join(room,"",0,"Player",1700).status==403);
    check(s.join(s.room,credential,0,"Player",1700).status==401);
    Sessions four(4,[&]{return std::to_string(++next);},[](unsigned,uint8_t,uint8_t,bool){});
    check(four.join(four.room,"",4,"Player",0).status==400);
    std::cout<<"PASS phone sessions: slots, credentials, replay/order, expiry, neutral reconnect, resume/lock, tab takeover, leave/reset\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
