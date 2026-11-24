#include "../port/phone_sessions.h"
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace buzz_phone;
void check(bool b){if(!b)throw std::runtime_error("Phone session regression");}
int main(){try {
    struct Event {unsigned player,held,pressed;bool release;};std::vector<Event> events;
    unsigned next=0;
    Sessions s(8,[&]{return "token"+std::to_string(++next);},[&](unsigned p,uint8_t h,uint8_t b,bool r){events.push_back({p,h,b,r});});
    // No pairing key: every join below uses only resume/slot/name.
    auto joined=s.join("",7,"Ada",10);check(joined.status==200&&joined.player==7);
    check(s.join("",7,"Bob",11).status==409);
    check(s.join("",8,"Bob",11).status==400);
    check(s.join("",0,"\n",11).status==400);
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
    s.locked=true;check(s.join("",0,"Bob",1500).status==423);
    const auto recovered=s.join(joined.resume,0,"ignored",1500);
    check(recovered.status==200&&recovered.player==7&&recovered.control!=joined.control&&events.back().release);
    check(s.update(joined.control,9,31,31,1501)==401); // old tab cannot drive
    check(s.update(recovered.control,1,1,1,1501)==200);
    check(s.leave(recovered.control)&&!s.leave(recovered.control));
    s.locked=false;
    for(unsigned i=0;i<8;++i)check(s.join("",i,"Player",1600).status==200);
    const auto room=s.room,credential=s.players[0].resume;s.reset();
    check(room!=s.room&&!s.locked);
    for(const auto& p:s.players)check(p.resume.empty()&&!p.online);
    check(s.join("",0,"Player",1700).status==200); // open joining after removing all phones
    check(s.join(credential,0,"Player",1700).status==401);
    Sessions four(4,[&]{return std::to_string(++next);},[](unsigned,uint8_t,uint8_t,bool){});
    check(four.join("",4,"Player",0).status==400);
    {   // Automatic joins wait for the game's player screen; resuming never waits.
        Sessions gated(8,[&]{return "g"+std::to_string(++next);},[](unsigned,uint8_t,uint8_t,bool){});
        bool open=false;gated.set_join_open([&]{return open;});
        check(gated.join("",auto_slot,"Ada",0).status==425&&gated.reserved_mask()==0);
        open=true;const auto ada=gated.join("",auto_slot,"Ada",1);check(ada.status==200&&ada.player==0);
        open=false;check(gated.join(ada.resume,auto_slot,"",2).status==200);
    }
    {
        // Automatic slots skip phones and controller-held slots; the pairing
        // survives a restart (room, slots, names, resume credentials).
        unsigned serial=0;
        auto hex=[&]{char text[33];snprintf(text,sizeof text,"%032x",++serial);return std::string(text);};
        uint32_t pads=0b101; // a gamepad already holds P1 and P3
        Sessions first(8,hex,[](unsigned,uint8_t,uint8_t,bool){});
        first.set_occupied([&]{return pads;});
        auto a=first.join("",auto_slot,"Ada",0);check(a.status==200&&a.player==1);
        auto b=first.join("",auto_slot,"Bob Ł",0);check(b.status==200&&b.player==3);
        check(first.reserved_mask()==0b1010);
        const auto saved=first.save();
        Sessions second(8,hex,[](unsigned,uint8_t,uint8_t,bool){});
        check(second.room!=first.room&&second.restore(saved)&&second.room==first.room);
        check(second.players[3].name=="Bob Ł"&&!second.players[3].online&&second.players[3].control.empty());
        auto again=second.join(b.resume,auto_slot,"",5);
        check(again.status==200&&again.player==3&&again.control!=b.control);
        second.set_occupied([&]{return pads;});
        for(unsigned i=0;i<4;++i)check(second.join("",auto_slot,"P",6).status==200);
        check(second.join("",auto_slot,"Late",6).status==409); // six phones + two pads
        Sessions four(4,hex,[](unsigned,uint8_t,uint8_t,bool){});
        const auto before=four.room;
        check(!four.restore(saved)&&four.room==before); // another slot count is ignored
        check(!second.restore("buzz-phone-pairing 1\nslots 8\nroom xyz\n"));
        check(!second.restore(saved.substr(0,saved.size()/2)+"player 9 0 00\n"));
    }
    std::cout<<"PASS phone sessions: slots, credentials, replay/order, expiry, neutral reconnect, resume/lock, tab takeover, leave/reset, automatic slots, saved pairing\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
