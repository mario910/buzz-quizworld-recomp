#pragma once
#include <algorithm>
#include <cstdint>
namespace buzz_pc {
enum class MenuAction {toggle,up,down,left,right,accept,back};
enum class MenuPage {settings,controllers,quit_confirm,phones};
// Raw XInput-style bits only, with no OS polling: shared by the host menu and
// synthetic tests. START is intercepted before gameplay by the input adapter.
class MenuPadNavigation {
    uint16_t previous_=0;
    uint64_t repeat_due_=0;
public:
    void reset(){previous_=0;repeat_due_=0;}
    uint16_t sample(uint16_t buttons,uint64_t now){
        const auto pressed=uint16_t(buttons&~previous_);
        uint16_t arrows=uint16_t(pressed&15);
        if(pressed&15)repeat_due_=now+350;
        else if((buttons&15)&&now>=repeat_due_){arrows=uint16_t(buttons&15);repeat_due_=now+110;}
        if(!(buttons&15))repeat_due_=0;
        previous_=buttons;
        return uint16_t(arrows|(pressed&(0x1000|0x200|0x2000)));
    }
};
class MenuModel {
    bool opened_=false;
    MenuPage page_=MenuPage::settings;
    int selection_=0;
public:
    bool open()const{return opened_;}
    MenuPage page()const{return page_;}
    int selection()const{return selection_;}
    int rows()const{return page_==MenuPage::settings?9:page_==MenuPage::controllers?10:page_==MenuPage::phones?14:2;}
    void select(int row){selection_=std::clamp(row,0,rows()-1);}
    void show(MenuPage page){page_=page;selection_=0;}
    void close(){opened_=false;show(MenuPage::settings);}
    void navigate(MenuAction a){
        if(a==MenuAction::toggle){if(opened_)close();else {opened_=true;show(MenuPage::settings);}return;}
        if(!opened_)return;
        if(a==MenuAction::up)selection_=(selection_+rows()-1)%rows();
        if(a==MenuAction::down)selection_=(selection_+1)%rows();
        if(a==MenuAction::back){if(page_==MenuPage::settings)close();else show(MenuPage::settings);}
    }
};
}
