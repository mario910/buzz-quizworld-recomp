#pragma once
#include <algorithm>
#include <cstdint>
#include "pc_settings.h"
#include "buzzer_input.h"
namespace buzz_pc {
enum class MenuAction {toggle,up,down,left,right,accept,back};
enum class MenuPage {settings,controllers,quit_confirm,phones,bindings,pad_assignments,controller_options,phone_seats};
enum class MenuTab {general,display,textures,controllers,phones,count};
inline constexpr const char* menu_tab_names[]={"General","Display","Textures","Controls","Phones"};
// Same five colours for physical handsets and configured keyboard buzzers.
inline MenuAction buzzer_menu_action(unsigned button){
    return button==1?MenuAction::accept:button==2?MenuAction::left:
        button==4?MenuAction::right:button==8?MenuAction::down:MenuAction::up;
}
inline unsigned buzzer_menu_key(const buzz_input::Config& config,unsigned key){
    if(key)for(unsigned player=0;player<config.players;++player)for(unsigned color=0;color<5;++color)
        if(config.keys[player][color]==key)return 1u<<color;
    return 0;
}
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
enum SettingsRow {row_volume,row_frame_lock,row_performance_overlay,row_display_mode,row_window_size,row_render_scale,row_upscaler,row_sharpness,row_texture_replace,row_texture_dump,row_texture_auto,row_offline,
    row_save,row_return,row_quit,row_settings_count};
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
    MenuTab tab_=MenuTab::general;
    int selection_=0;
    bool tabs_focused_=false;
    inline static constexpr SettingsRow general_rows_[]={row_volume,row_frame_lock,row_performance_overlay,row_offline};
    inline static constexpr SettingsRow display_rows_[]={row_display_mode,row_window_size,row_render_scale,row_upscaler,row_sharpness};
    inline static constexpr SettingsRow texture_rows_[]={row_texture_replace,row_texture_dump,row_texture_auto};
public:
    bool open()const{return opened_;}
    MenuPage page()const{return page_;}
    MenuTab tab()const{return tab_;}
    bool root()const{return page_==MenuPage::settings||page_==MenuPage::controllers||page_==MenuPage::phones;}
    bool tabs_focused()const{return tabs_focused_;}
    void focus_tabs(){tabs_focused_=true;}
    void enter_options(){tabs_focused_=false;selection_=0;}
    void set_tab(MenuTab tab){
        tab_=tab;page_=tab==MenuTab::controllers?MenuPage::controllers:tab==MenuTab::phones?MenuPage::phones:MenuPage::settings;
        selection_=0;tabs_focused_=true;
    }
    void cycle_tab(int direction){
        set_tab(MenuTab((int(tab_)+direction+int(MenuTab::count))%int(MenuTab::count)));
    }
    int selection()const{return selection_;}
    int option_rows()const{
        if(page_==MenuPage::settings)return tab_==MenuTab::display?5:tab_==MenuTab::textures?3:4;
        return page_==MenuPage::controllers||page_==MenuPage::phones?5:
            page_==MenuPage::bindings?8:page_==MenuPage::pad_assignments?5:
            page_==MenuPage::controller_options?4:page_==MenuPage::phone_seats?9:2;
    }
    int rows()const{return option_rows()+(root()?3:0);}
    // Stable setting/action IDs are separate from the short visible row index.
    int setting_row(int row)const{
        if(row<0||row>=rows())return -1;
        if(root()&&row>=option_rows())return row_save+(row-option_rows());
        if(page_!=MenuPage::settings)return -1;
        if(row<0||row>=option_rows())return -1;
        return tab_==MenuTab::display?display_rows_[row]:tab_==MenuTab::textures?texture_rows_[row]:general_rows_[row];
    }
    void select(int row){selection_=std::clamp(row,0,rows()-1);tabs_focused_=false;}
    void select_setting(SettingsRow setting){
        for(int tab=0;tab<int(MenuTab::count);++tab){
            set_tab(MenuTab(tab));
            for(int row=0;row<rows();++row)if(setting_row(row)==setting){select(row);return;}
        }
        set_tab(MenuTab::general);
    }
    void show(MenuPage page){
        if(page==MenuPage::settings){set_tab(MenuTab::general);enter_options();return;}
        if(page==MenuPage::controllers)tab_=MenuTab::controllers;
        if(page==MenuPage::phones)tab_=MenuTab::phones;
        page_=page;selection_=0;tabs_focused_=false;
    }
    void back(){
        if(root()){focus_tabs();return;}
        if(page_==MenuPage::bindings||page_==MenuPage::pad_assignments||page_==MenuPage::controller_options)set_tab(MenuTab::controllers);
        else if(page_==MenuPage::phone_seats)set_tab(MenuTab::phones);
        else set_tab(tab_);
        enter_options();
    }
    void close(){opened_=false;set_tab(MenuTab::general);}
    void navigate(MenuAction a){
        if(a==MenuAction::toggle){if(opened_)close();else {opened_=true;set_tab(MenuTab::general);}return;}
        if(!opened_)return;
        if(a==MenuAction::back){if(root()&&tabs_focused_)close();else back();return;}
        if(tabs_focused_){
            if(a==MenuAction::left)cycle_tab(-1);
            if(a==MenuAction::right)cycle_tab(1);
            if(a==MenuAction::accept||a==MenuAction::down){set_tab(tab_);enter_options();}
            if(a==MenuAction::up){tabs_focused_=false;selection_=rows()-1;}
            return;
        }
        if(a==MenuAction::up){if(selection_==0)focus_tabs();else --selection_;}
        if(a==MenuAction::down){if(selection_==rows()-1)focus_tabs();else ++selection_;}
    }
};
}
