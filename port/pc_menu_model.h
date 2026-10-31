#pragma once
#include <algorithm>
#include <cstdint>
#include "pc_settings.h"
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
// Settings page rows. Display (window/swapchain) size and the internal
// render resolution are separate rows: one is live, one needs a restart.
enum SettingsRow {row_volume,row_frame_lock,row_display_mode,row_window_size,row_render_scale,row_upscaler,row_sharpness,row_offline,
    row_controllers,row_phones,row_save,row_return,row_quit};
// Cycle a window size through Default (0x0), the presets and, when the
// current size is none of those, that custom size kept in its place.
inline Size next_window_size(Size current,int direction){
    constexpr int presets=int(sizeof(window_presets)/sizeof(window_presets[0]));
    int index=-1;bool custom=current.width!=0;
    if(!current.width)index=0;
    for(int i=0;i<presets;++i)if(window_presets[i].width==current.width&&window_presets[i].height==current.height){index=i+1;custom=false;}
    const int count=presets+1+(custom?1:0);
    if(index<0)index=presets+1; // the custom entry sits last
    index=((index+direction)%count+count)%count;
    if(index==0)return {0,0};
    if(index<=presets)return window_presets[index-1];
    return current;
}
inline unsigned next_render_scale(unsigned current,int direction){
    constexpr int presets=int(sizeof(render_scale_presets)/sizeof(render_scale_presets[0]));
    int index=0;
    for(int i=0;i<presets;++i)if(render_scale_presets[i]<=current)index=i;
    if(render_scale_presets[index]!=current&&direction<0)direction=0; // snap down to the preset below
    return render_scale_presets[std::clamp(index+direction,0,presets-1)];
}
class MenuModel {
    bool opened_=false;
    MenuPage page_=MenuPage::settings;
    int selection_=0;
public:
    bool open()const{return opened_;}
    MenuPage page()const{return page_;}
    int selection()const{return selection_;}
    int rows()const{return page_==MenuPage::settings?13:page_==MenuPage::controllers?10:page_==MenuPage::phones?14:2;}
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
