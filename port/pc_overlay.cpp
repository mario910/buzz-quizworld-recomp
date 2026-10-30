#include "pc_overlay.h"
#include "pc_menu_model.h"
#include "pc_runtime.h"
#include "buzzer_hid_win32.h"
#include "phone_server.h"
#include "../third_party/qrcodegen/qrcodegen.hpp"
#include "ps3emu/host_overlay.h"
#include "ps3emu/host_controls.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#ifdef BUZZ_EXPERIMENTAL_VULKAN
#include <imgui_impl_vulkan.h>
#endif
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
// The in-game XMB sends MENU_OPEN, DRAWING_BEGIN on opening and DRAWING_END,
// MENU_CLOSE on closing, to every registered sysutil callback. Buzz handles
// only DRAWING_BEGIN/END (it pauses); the menu pair is sent as hardware would.
extern "C" int cellSysutilBroadcastEvent(unsigned status,unsigned param);
constexpr unsigned SYSUTIL_DRAWING_BEGIN=0x0121,SYSUTIL_DRAWING_END=0x0122,
    SYSUTIL_SYSTEM_MENU_OPEN=0x0131,SYSUTIL_SYSTEM_MENU_CLOSE=0x0132;

namespace {
buzz_pc::MenuModel menu;
buzz_pc::Preferences preferences;
buzz_input::Config input;
buzz_phone::Status phone_status;
std::string status;
ImGuiContext* context=nullptr;
ID3D12DescriptorHeap* descriptors=nullptr;
std::array<bool,64> used{};
UINT descriptor_size=0;
HWND window=nullptr;
bool initialized=false,failed=false,win32_initialized=false,dx12_initialized=false,vulkan_initialized=false;
unsigned overlay_api=PS3_HOST_OVERLAY_D3D12;
bool pause_guest=true;
#ifdef BUZZ_EXPERIMENTAL_VULKAN
// The Vulkan frame clock can hold the guest and redraw the last frame.
constexpr bool host_pause=true;
#else
constexpr bool host_pause=false;
#endif
buzz_pc::MenuPadNavigation pad_navigation;
bool test_mode=false,pause_test=false,frame_limit_test=false;
ULONGLONG test_delay_ms=0;
bool menu_ui_enabled=true;
unsigned test_stage=0;
ULONGLONG first_frame=0;

void capture(){buzz_input_capture(menu.open());}
void release_resources() {
    if(dx12_initialized){ImGui_ImplDX12_Shutdown();dx12_initialized=false;}
#ifdef BUZZ_EXPERIMENTAL_VULKAN
    if(vulkan_initialized){ImGui_ImplVulkan_Shutdown();vulkan_initialized=false;}
#endif
    if(win32_initialized){ImGui_ImplWin32_Shutdown();win32_initialized=false;}
    if(context){ImGui::DestroyContext(context);context=nullptr;}
    if(descriptors){descriptors->Release();descriptors=nullptr;}
    used.fill(false);initialized=false;
}
void pause_events(bool open) {
    if(!pause_guest)return;
    if(host_pause)ps3_host_set_paused(open);
    const int slots=open?cellSysutilBroadcastEvent(SYSUTIL_SYSTEM_MENU_OPEN,0)+cellSysutilBroadcastEvent(SYSUTIL_DRAWING_BEGIN,0)
                        :cellSysutilBroadcastEvent(SYSUTIL_DRAWING_END,0)+cellSysutilBroadcastEvent(SYSUTIL_SYSTEM_MENU_CLOSE,0);
    fprintf(stderr,"[pc-menu] sysutil %s queued (%d callback event(s))\n",open?"SYSTEM_MENU_OPEN+DRAWING_BEGIN":"DRAWING_END+SYSTEM_MENU_CLOSE",slots);
}
void opened() {
    preferences=buzz_pc_requested_preferences();input=buzz_input_current_config();status.clear();
    fprintf(stderr,"[pc-menu] opened\n");
    pause_events(true);
}
void navigate(buzz_pc::MenuAction action) {
    const bool before=menu.open();menu.navigate(action);
    if(menu.open()&&!before)opened();
    if(before&&!menu.open()) {
        if(context){ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();ImGui::GetIO().ClearEventsQueue();}
        fprintf(stderr,"[pc-menu] closed\n");
        pause_events(false);
    }
    capture();
}
void apply_live() {
    auto change=buzz_pc_apply_preferences(preferences,false);
    if(!change.accepted)status=change.error;
    else if(change.restart)status="Online/renderer changes take effect after restarting the game.";
    else status="Applied. Choose Save settings to keep changes after restart.";
}
bool apply_input() {
    if(input.players!=buzz_input_players()) {status="Changing the number of buzzers takes effect after restarting.";return true;}
    std::string text,error;
    if(!buzz_input_format_config(input,text,error)||!buzz_input_reconfigure(text,error)){status=error;return false;}
    else status="Controller mapping applied. Choose Save to keep it.";
    return true;
}
void save_all() {
    auto change=buzz_pc_apply_preferences(preferences,true);
    if(!change.accepted){status=change.error;return;}
    std::string error;
    if(!buzz_pc_save_input_preferences(input,error)){status="Audio/video saved; controller settings were NOT saved: "+error;return;}
    if(!apply_input()){status="Settings saved, but controller changes could not be applied: "+status;return;}
    status=change.restart||input.players!=buzz_input_players()?"Settings saved. Some changes require a game restart.":"Settings saved.";
    fprintf(stderr,"[pc-menu] settings saved: volume=%u cap=%u fullscreen=%d offline=%d\n",preferences.volume_percent,preferences.fps_limit,preferences.fullscreen,preferences.offline);
}
void act(buzz_pc::MenuAction action) {
    using buzz_pc::MenuAction;using buzz_pc::MenuPage;
    if(action==MenuAction::toggle||action==MenuAction::back||action==MenuAction::up||action==MenuAction::down){navigate(action);return;}
    if(!menu.open())return;
    const int row=menu.selection(),direction=action==MenuAction::left?-1:1;
    const bool accept=action==MenuAction::accept;
    if(menu.page()==MenuPage::quit_confirm) {
        if(!accept)return;
        if(row==0)menu.show(MenuPage::settings);else PostMessageW(window,WM_CLOSE,0,0);
        return;
    }
    if(menu.page()==MenuPage::phones) {
        if(row==0){preferences.phone_buzzers=!preferences.phone_buzzers;apply_live();}
        else if(row==1&&!accept){preferences.phone_port=unsigned(std::clamp(int(preferences.phone_port)+direction,1024,65535));apply_live();}
        else if(row==2&&accept)buzz_phone::host().set_locked(!phone_status.locked);
        else if(row==3&&accept){buzz_phone::host().reset();status="New pairing code created. All phone sessions released.";}
        else if(row>=4&&row<=11&&accept)buzz_phone::host().kick(unsigned(row-4));
        else if(row==12&&accept)save_all();
        else if(row==13&&accept)menu.show(MenuPage::settings);
        return;
    }
    if(menu.page()==MenuPage::settings) {
        if(row==0&&!accept){preferences.volume_percent=unsigned(std::clamp(int(preferences.volume_percent)+direction*5,0,100));apply_live();}
        else if(row==1){preferences.fps_limit=preferences.fps_limit?0:30;apply_live();}
        else if(row==2){preferences.fullscreen=!preferences.fullscreen;apply_live();}
        else if(row==3){preferences.offline=!preferences.offline;apply_live();}
        else if(row==4&&accept)menu.show(MenuPage::controllers);
        else if(row==5&&accept)menu.show(MenuPage::phones);
        else if(row==6&&accept)save_all();
        else if(row==7&&accept)navigate(MenuAction::toggle);
        else if(row==8&&accept)menu.show(MenuPage::quit_confirm);
    }else {
        if(row==0){input.gamepads=!input.gamepads;apply_input();}
        else if(row==1&&accept){input.pad_buttons=buzz_input::defaults().pad_buttons;input.gamepads=true;apply_input();}
        else if(row>=2&&row<=5) {
            int slot=row-2,old=input.pad_player[slot];
            int next=(old+1+direction+int(input.players)+1)%(int(input.players)+1)-1;
            for(int i=0;i<4;++i)if(i!=slot&&next>=0&&input.pad_player[i]==next)input.pad_player[i]=old;
            input.pad_player[slot]=next;apply_input();
        }else if(row==6&&!accept){input.pulse_ms=unsigned(std::clamp(int(input.pulse_ms)+direction*10,10,5000));apply_input();}
        else if(row==7){input.players=input.players==4?8:4;status="Buzzer count is saved for the next game start.";}
        else if(row==8&&accept)save_all();
        else if(row==9&&accept)menu.show(MenuPage::settings);
    }
}
int message(void* handle,unsigned msg,uintptr_t wp,intptr_t lp) {
    buzz_hid_host_message(handle,msg,wp,lp);
    if(!menu_ui_enabled||failed)return 0;
    using buzz_pc::MenuAction;
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE) {if(!(lp&(1ll<<30)))navigate(MenuAction::toggle);return 1;}
    if(!menu.open())return 0;
    if(msg==WM_KILLFOCUS){pad_navigation.reset();if(context)ImGui::GetIO().ClearInputKeys();}
    if(context)ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(handle),msg,WPARAM(wp),LPARAM(lp));
    if(msg==WM_KEYDOWN) {
        if(wp==VK_UP)act(MenuAction::up);else if(wp==VK_DOWN)act(MenuAction::down);
        else if(wp==VK_LEFT)act(MenuAction::left);else if(wp==VK_RIGHT)act(MenuAction::right);
        else if((wp==VK_RETURN||wp==VK_SPACE)&&!(lp&(1ll<<30)))act(MenuAction::accept);else if(wp==VK_BACK&&!(lp&(1ll<<30)))act(MenuAction::back);
    }
    return msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_CHAR||msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_RBUTTONDOWN||msg==WM_RBUTTONUP||msg==WM_MOUSEWHEEL||msg==WM_MOUSEMOVE;
}
void alloc_descriptor(ImGui_ImplDX12_InitInfo*,D3D12_CPU_DESCRIPTOR_HANDLE* cpu,D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    for(unsigned i=0;i<used.size();++i)if(!used[i]) {
        used[i]=true;*cpu=descriptors->GetCPUDescriptorHandleForHeapStart();cpu->ptr+=SIZE_T(i)*descriptor_size;
        *gpu=descriptors->GetGPUDescriptorHandleForHeapStart();gpu->ptr+=UINT64(i)*descriptor_size;return;
    }
    IM_ASSERT(false&&"PC menu descriptor pool exhausted");cpu->ptr=0;gpu->ptr=0;
}
void free_descriptor(ImGui_ImplDX12_InitInfo*,D3D12_CPU_DESCRIPTOR_HANDLE cpu,D3D12_GPU_DESCRIPTOR_HANDLE) {
    const auto first=descriptors->GetCPUDescriptorHandleForHeapStart();const auto slot=(cpu.ptr-first.ptr)/descriptor_size;
    if(slot<used.size())used[slot]=false;
}
#ifdef BUZZ_EXPERIMENTAL_VULKAN
PFN_vkVoidFunction vulkan_function(const char* name,void* user) {
    auto vulkan=static_cast<const ps3_host_overlay_vulkan*>(user);
    return reinterpret_cast<PFN_vkVoidFunction>(vulkan->get_instance_proc_addr(vulkan->instance,name));
}
bool init_vulkan(const ps3_host_overlay_frame& frame) {
    const auto& vulkan=*frame.vulkan;
    if(!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0,vulkan_function,const_cast<ps3_host_overlay_vulkan*>(&vulkan)))return false;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion=VK_API_VERSION_1_0;info.Instance=static_cast<VkInstance>(vulkan.instance);
    info.PhysicalDevice=static_cast<VkPhysicalDevice>(vulkan.physical_device);info.Device=static_cast<VkDevice>(frame.device);
    info.QueueFamily=vulkan.queue_family;info.Queue=static_cast<VkQueue>(frame.queue);
    info.DescriptorPoolSize=IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    info.MinImageCount=std::max(2u,vulkan.min_image_count);info.ImageCount=std::max(info.MinImageCount,vulkan.image_count);
    info.PipelineInfoMain.RenderPass=static_cast<VkRenderPass>(vulkan.render_pass);info.PipelineInfoMain.Subpass=0;
    info.PipelineInfoMain.MSAASamples=VK_SAMPLE_COUNT_1_BIT;
    if(!ImGui_ImplVulkan_Init(&info))return false;
    vulkan_initialized=true;
    return true;
}
#endif
bool init(const ps3_host_overlay_frame& frame) {
    window=static_cast<HWND>(frame.window);overlay_api=frame.api;
    if(frame.api==PS3_HOST_OVERLAY_D3D12) {
        auto device=static_cast<ID3D12Device*>(frame.device);
        D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=UINT(used.size());desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&descriptors))))return false;
        descriptor_size=device->GetDescriptorHandleIncrementSize(desc.Type);
    }
#ifndef BUZZ_EXPERIMENTAL_VULKAN
    else return false;
#else
    else if(frame.api!=PS3_HOST_OVERLAY_VULKAN||!frame.vulkan)return false;
#endif
    IMGUI_CHECKVERSION();context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange; // never change the RDP user's cursor
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.FrameRounding=9;style.WindowRounding=14;
    style.FramePadding=ImVec2(16,9);style.ItemSpacing=ImVec2(12,9);style.WindowPadding=ImVec2(26,22);style.FontSizeBase=22;
    const char* font="C:/Windows/Fonts/segoeui.ttf";
    if(std::filesystem::is_regular_file(font))io.Fonts->AddFontFromFileTTF(font,22);
    if(!ImGui_ImplWin32_Init(window))return false;
    win32_initialized=true;
#ifdef BUZZ_EXPERIMENTAL_VULKAN
    if(frame.api==PS3_HOST_OVERLAY_VULKAN) {
        if(!init_vulkan(frame))return false;
        initialized=true;fprintf(stderr,"[pc-menu] Vulkan overlay initialized\n");return true;
    }
#endif
    ImGui_ImplDX12_InitInfo info{};info.Device=static_cast<ID3D12Device*>(frame.device);info.CommandQueue=static_cast<ID3D12CommandQueue*>(frame.queue);
    info.NumFramesInFlight=int(frame.frames_in_flight);info.RTVFormat=DXGI_FORMAT(frame.format);info.DSVFormat=DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap=descriptors;info.SrvDescriptorAllocFn=alloc_descriptor;info.SrvDescriptorFreeFn=free_descriptor;
    if(!ImGui_ImplDX12_Init(&info))return false;
    dx12_initialized=true;
    initialized=true;fprintf(stderr,"[pc-menu] D3D12 overlay initialized\n");return true;
}
void controller() {
    using buzz_pc::MenuAction;
    if(buzz_input_take_menu_request())navigate(MenuAction::toggle);
    if(!menu.open()){pad_navigation.reset();return;}
    uint16_t buttons=0;for(unsigned i=0;i<4;++i)buttons|=buzz_input_host_pad_buttons(i);
    const auto physical=buzz_input_host_physical_buttons();
    // Physical handsets have no START. Open with Escape, then use the familiar
    // blue/orange up/down, yellow/green adjust and red confirm; Back/Return rows
    // remain reachable without inventing a delayed gameplay chord.
    if(physical&1)buttons|=0x200;
    if(physical&16)buttons|=1;if(physical&8)buttons|=2;
    if(physical&2)buttons|=4;if(physical&4)buttons|=8;
    const auto pressed=pad_navigation.sample(buttons,GetTickCount64());
    if(pressed&1)act(MenuAction::up);if(pressed&2)act(MenuAction::down);if(pressed&4)act(MenuAction::left);if(pressed&8)act(MenuAction::right);
    if(pressed&(0x1000|0x200))act(MenuAction::accept); // A or R1/RB
    if(pressed&0x2000)act(MenuAction::back);
}
std::string row_label(int row) {
    using buzz_pc::MenuPage;
    if(menu.page()==MenuPage::quit_confirm)return row==0?"KEEP PLAYING":"QUIT GAME";
    if(menu.page()==MenuPage::phones) {
        if(row==0)return std::string("HOST PHONE BUZZERS: ")+(phone_status.running?"ON":preferences.phone_buzzers?"ON [unavailable]":"OFF");
        if(row==1)return "HTTP PORT: "+std::to_string(preferences.phone_port);
        if(row==2)return std::string("JOINING: ")+(phone_status.locked?"LOCKED":"OPEN");
        if(row==3)return "NEW CODE / REMOVE ALL PHONES";
        if(row==12)return "SAVE SETTINGS";
        if(row==13)return "BACK";
        const auto player=unsigned(row-4);const auto& seat=phone_status.seats[player];
        if(player>=buzz_input_players())return "P"+std::to_string(player+1)+": unavailable (4-slot mode)";
        return "P"+std::to_string(player+1)+": "+(seat.reserved?seat.name+(seat.online?" [online]":" [away]")+" - REMOVE":"available");
    }
    if(menu.page()==MenuPage::settings) {
        switch(row) {
            case 0:return "VOLUME                                      "+std::to_string(preferences.volume_percent)+"%";
            case 1:return std::string("30 FPS FRAME LOCK                   ")+(preferences.fps_limit?"ON":"OFF");
            case 2:return std::string("BORDERLESS FULLSCREEN          ")+(preferences.fullscreen?"ON":"OFF");
            case 3:return std::string("OFFLINE MODE [restart]              ")+(preferences.offline?"ON":"OFF");
            case 4:return "CONTROLLERS  >";case 5:return "PHONE BUZZERS  >";case 6:return "SAVE SETTINGS";case 7:return "RETURN TO GAME";default:return "QUIT GAME...";
        }
    }
    switch(row) {
        case 0:return std::string("XINPUT CONTROLLERS                   ")+(input.gamepads?"ON":"OFF");
        case 1:return "USE R1 + D-PAD PRESET";
        case 6:return "TAP RETENTION                             "+std::to_string(input.pulse_ms)+" ms";
        case 7:return "BUZZER SLOTS [restart]                  "+std::to_string(input.players);
        case 8:return "SAVE CONTROLLER SETTINGS";case 9:return "BACK";
        default:{unsigned slot=unsigned(row-2);return "PAD "+std::to_string(slot+1)+(buzz_input_host_pad_connected(slot)?" [connected]":" [not connected]")+"  >  "+(input.pad_player[slot]<0?"UNASSIGNED":"PLAYER "+std::to_string(input.pad_player[slot]+1));}
    }
}
void draw_phone_pairing() {
    if(!phone_status.running){ImGui::TextWrapped("Enable Host phone buzzers to display the QR code.");return;}
    static int address=0;
    address=std::clamp(address,0,std::max(0,int(phone_status.urls.size())-1));
    if(phone_status.urls.empty())return;
    const auto& url=phone_status.urls[address];const auto endpoint=url.substr(0,url.find("/#"));
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##phone-address",endpoint.c_str())) {
        for(unsigned i=0;i<phone_status.urls.size();++i){const auto& u=phone_status.urls[i];if(ImGui::Selectable(u.substr(0,u.find("/#")).c_str(),int(i)==address))address=int(i);}
        ImGui::EndCombo();
    }
    static std::string encoded;
    static std::unique_ptr<qrcodegen::QrCode> qr;
    if(encoded!=url) {
        try {qr=std::make_unique<qrcodegen::QrCode>(qrcodegen::QrCode::encodeText(url.c_str(),qrcodegen::QrCode::Ecc::MEDIUM));encoded=url;}
        catch(const std::exception& e){qr.reset();status=e.what();}
    }
    if(qr) {
        const int modules=qr->getSize(),border=4;
        const float scale=std::max(1.0f,std::floor(std::min(ImGui::GetContentRegionAvail().x,260.0f)/(modules+border*2)));
        const float extent=scale*(modules+border*2);const auto p=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(p,ImVec2(p.x+extent,p.y+extent),IM_COL32_WHITE);
        for(int y=0;y<modules;++y)for(int x=0;x<modules;++x)if(qr->getModule(x,y)) {
            const ImVec2 at(p.x+(x+border)*scale,p.y+(y+border)*scale);
            draw->AddRectFilled(at,ImVec2(at.x+scale,at.y+scale),IM_COL32_BLACK);
        }
        ImGui::Dummy(ImVec2(extent,extent));
    }
    ImGui::TextWrapped("Scan with your phone camera. Use the same Wi-Fi as this PC.");
    if(ImGui::Button("Copy pairing link"))ImGui::SetClipboardText(url.c_str());
    ImGui::TextWrapped("%s",url.c_str());
    if(endpoint.find("127.0.0.1")!=std::string::npos)ImGui::TextWrapped("Loopback is for this PC only. Select a LAN address above. If none is listed, connect the PC to Wi-Fi or Ethernet and restart hosting.");
    ImGui::TextDisabled("Local HTTP / trusted networks only");
    ImGui::TextWrapped("Allow this game's port on private networks in Windows Firewall if phones cannot connect. Guest Wi-Fi may block devices from reaching the PC.");
}
void draw_menu() {
    auto& io=ImGui::GetIO();auto size=io.DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0),size,IM_COL32(4,5,20,200));
    const float width=std::min(size.x-30,920.0f),height=std::min(size.y-30,760.0f);
    ImGui::SetNextWindowPos(ImVec2((size.x-width)*.5f,(size.y-height)*.5f));ImGui::SetNextWindowSize(ImVec2(width,height));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.045f,.035f,.12f,.98f));
    ImGui::Begin("Buzz PC settings",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    auto p=ImGui::GetCursorScreenPos();const auto available=ImGui::GetContentRegionAvail();
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(p,ImVec2(p.x+available.x,p.y+64),IM_COL32(145,20,255,255),IM_COL32(75,35,180,255),IM_COL32(45,30,120,255),IM_COL32(100,30,210,255));
    const bool phones=menu.page()==buzz_pc::MenuPage::phones;
    if(phones)phone_status=buzz_phone::host().status();
    ImGui::SetCursorScreenPos(ImVec2(p.x+18,p.y+14));ImGui::TextUnformatted(phones?"BUZZ!  /  PHONE BUZZERS":menu.page()==buzz_pc::MenuPage::controllers?"BUZZ!  /  CONTROLLERS":"BUZZ!  /  PC SETTINGS");
    ImGui::SetCursorScreenPos(ImVec2(p.x,p.y+78));
    if(menu.page()==buzz_pc::MenuPage::controllers) {
        const unsigned receivers=unsigned(buzz_input_physical_connected(0))+unsigned(buzz_input_physical_connected(1));
        ImGui::TextDisabled("Buzz USB: %u receiver(s) | hardware-unverified%s",receivers,buzz_hid_host_registered()?"":" | unavailable");
        if(receivers>1&&input.players==4)ImGui::TextWrapped("Receiver 2 needs eight buzzer slots (restart).");
    }
    if(phones) {
        ImGui::BeginChild("pairing",ImVec2(std::max(160.0f,available.x*.36f),-100));draw_phone_pairing();ImGui::EndChild();ImGui::SameLine();
    }
    ImGui::BeginChild("options",ImVec2(0,-100));
    static int last_selection=-1;static buzz_pc::MenuPage last_page=buzz_pc::MenuPage::settings;
    const bool scroll_selection=menu.selection()!=last_selection||menu.page()!=last_page;
    const int rows=menu.rows();
    const float row_height=std::clamp((ImGui::GetContentRegionAvail().y-rows*ImGui::GetStyle().ItemSpacing.y)/rows,27.0f,41.0f);
    // Fixed-height buttons must still leave enough room for the font. The
    // default 9px vertical padding clips 22px text in compact 27px rows.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(16,std::max(0.0f,(row_height-ImGui::GetFontSize())*.5f)));
    for(int row=0;row<rows;++row) {
        ImGui::PushID(row);const bool selected=menu.selection()==row;
        ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4(.48f,.12f,.86f,1):ImVec4(.12f,.13f,.24f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.48f,.22f,.78f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.67f,.28f,.96f,1));
        auto label=row_label(row);
        const bool adjustable=(menu.page()==buzz_pc::MenuPage::settings&&row==0)||(menu.page()==buzz_pc::MenuPage::controllers&&row==6)||(phones&&row==1);
        const float adjust_width=44,space=ImGui::GetStyle().ItemSpacing.x;
        if(adjustable){
            if(ImGui::Button("-",ImVec2(adjust_width,row_height))){menu.select(row);act(buzz_pc::MenuAction::left);}
            ImGui::SameLine();
        }
        const float button_width=adjustable?ImGui::GetContentRegionAvail().x-adjust_width-space:-1;
        if(ImGui::Button(label.c_str(),ImVec2(button_width,row_height))){menu.select(row);act(buzz_pc::MenuAction::accept);}
        if(selected&&scroll_selection)ImGui::SetScrollHereY(.5f);
        if(adjustable){ImGui::SameLine();if(ImGui::Button("+",ImVec2(adjust_width,row_height))){menu.select(row);act(buzz_pc::MenuAction::right);}}
        ImGui::PopStyleColor(3);ImGui::PopID();
    }
    last_selection=menu.selection();last_page=menu.page();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    if(phones&&!phone_status.error.empty())ImGui::TextWrapped("%s",phone_status.error.c_str());
    if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    else ImGui::TextDisabled(pause_guest&&host_pause?"The game is paused while this menu is open.":"Game continues running while this menu is open.");
    if(buzz_input_physical_connected(0)||buzz_input_physical_connected(1))ImGui::TextDisabled("Buzz: blue/orange up/down, yellow/green adjust, red select");
    else ImGui::TextDisabled("Arrows / D-pad: navigate & adjust   A / R1: select   B: back");
    ImGui::TextDisabled("ESC / START: return to game    Renderer: %s",overlay_api==PS3_HOST_OVERLAY_VULKAN?"Vulkan":"D3D12");
    ImGui::End();ImGui::PopStyleColor();
}
void test_actions() {
    // Explicit test mode only; this is internal menu input, never desktop input.
    if(!test_mode)return;
    const auto now=GetTickCount64();if(!first_frame)first_frame=now;
    if(now-first_frame<test_delay_ms)return;
    const auto elapsed=now-first_frame-test_delay_ms;
    if(frame_limit_test) {
        // Repeated live changes through the real menu, with no settings save.
        const unsigned phase=unsigned(elapsed/15000);
        if(test_stage<=phase) {
            const unsigned limit=(phase&1)?30:0;
            navigate(buzz_pc::MenuAction::toggle);
            if(preferences.fps_limit!=limit){menu.select(1);act(buzz_pc::MenuAction::accept);}
            navigate(buzz_pc::MenuAction::toggle);
            fprintf(stderr,"[pc-menu] test: frame limit=%u elapsed=%llums\n",limit,elapsed);
            test_stage=phase+1;
        }
        return;
    }
    if(pause_test) {
        // Hold the menu over gameplay: the game should pause behind it.
        if(test_stage==0){navigate(buzz_pc::MenuAction::toggle);++test_stage;}
        if(test_stage==1&&elapsed>=8000){message(window,WM_KEYDOWN,VK_ESCAPE,0);++test_stage;fprintf(stderr,"[pc-menu] test: pause hold completed\n");}
        return;
    }
    if(test_stage==0&&elapsed>=2500){navigate(buzz_pc::MenuAction::toggle);++test_stage;}
    if(test_stage==1&&elapsed>=3500){menu.select(0);act(buzz_pc::MenuAction::left);++test_stage;}
    if(test_stage==2&&elapsed>=4500){menu.select(1);act(buzz_pc::MenuAction::accept);++test_stage;}
    if(test_stage==3&&elapsed>=5500){input.pulse_ms=260;save_all();++test_stage;}
    if(test_stage==4&&elapsed>=9000){message(window,WM_KEYDOWN,VK_ESCAPE,0);++test_stage;}
    if(test_stage==5&&elapsed>=9500){message(window,WM_KEYDOWN,VK_ESCAPE,0);++test_stage;}
    if(test_stage==6&&elapsed>=10500){menu.select(4);act(buzz_pc::MenuAction::accept);++test_stage;}
    if(test_stage==7&&elapsed>=11500){menu.select(9);++test_stage;fprintf(stderr,"[pc-menu] test: controllers page, last row selected\n");}
    if(test_stage==8&&elapsed>=13000){message(window,WM_KEYDOWN,VK_BACK,0);message(window,WM_KEYDOWN,VK_ESCAPE,0);++test_stage;fprintf(stderr,"[pc-menu] test: keyboard close/reopen and controller page completed\n");}
}
void draw(const ps3_host_overlay_frame* frame) {
    if(failed)return;window=static_cast<HWND>(frame->window);
    test_actions();controller();if(!menu.open())return;
    if(!initialized&&!init(*frame)){release_resources();failed=true;menu.close();pause_events(false);capture();buzz_input_enable_menu(false);fprintf(stderr,"[pc-menu] overlay init FAILED\n");return;}
#ifdef BUZZ_EXPERIMENTAL_VULKAN
    if(frame->api==PS3_HOST_OVERLAY_VULKAN) {
        ImGui_ImplVulkan_NewFrame();ImGui_ImplWin32_NewFrame();
        auto& io=ImGui::GetIO();io.DisplayFramebufferScale=ImVec2(frame->width/io.DisplaySize.x,frame->height/io.DisplaySize.y);
        ImGui::NewFrame();draw_menu();ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),static_cast<VkCommandBuffer>(frame->commands));
        return;
    }
#endif
    ImGui_ImplDX12_NewFrame();ImGui_ImplWin32_NewFrame();
    auto& io=ImGui::GetIO();io.DisplayFramebufferScale=ImVec2(frame->width/io.DisplaySize.x,frame->height/io.DisplaySize.y);
    ImGui::NewFrame();draw_menu();ImGui::Render();
    auto commands=static_cast<ID3D12GraphicsCommandList*>(frame->commands);
    commands->SetDescriptorHeaps(1,&descriptors);D3D12_CPU_DESCRIPTOR_HANDLE rtv{frame->rtv};commands->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),commands);
}
void shutdown() {
    buzz_phone::host().stop();
    ps3_host_set_paused(0);
    buzz_hid_host_shutdown();
    buzz_input_enable_menu(false);
    release_resources();
}
}
void buzz_pc_overlay_register(){
    const char* enabled=getenv("BUZZ_PC_OVERLAY");menu_ui_enabled=!(enabled&&std::string(enabled)=="0");
    // BUZZ_PC_MENU_PAUSE=0: leave the game running behind the menu.
    const char* pause=getenv("BUZZ_PC_MENU_PAUSE");pause_guest=!(pause&&std::string(pause)=="0");
    const char* test=getenv("BUZZ_PC_MENU_TEST");
    frame_limit_test=test&&std::string(test)=="frame-limit";
    test_mode=test&&(std::string(test)=="1"||std::string(test)=="pause"||frame_limit_test);pause_test=test_mode&&std::string(test)=="pause";
    if(const char* delay=getenv("BUZZ_PC_MENU_TEST_DELAY_MS"))test_delay_ms=strtoull(delay,nullptr,10);
    const ps3_host_overlay_hooks hooks{message,menu_ui_enabled?draw:nullptr,shutdown};ps3_host_set_overlay(&hooks);
    buzz_input_enable_menu(menu_ui_enabled);
}
#else
void buzz_pc_overlay_register(){}
#endif
