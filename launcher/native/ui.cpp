#include "core.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace buzz_launcher {
namespace {
constexpr ImVec4 accent{0.28f,0.76f,0.88f,1};
const ImVec4 button_colors[]={{0.9f,.19f,.25f,1},{.95f,.8f,.13f,1},{.24f,.78f,.42f,1},{1,.5f,.12f,1},{.2f,.48f,.95f,1}};
unsigned virtual_key(SDL_Keycode k) {
    if(k>=0x41&&k<=0x5a)return unsigned(k);
    if(k>=SDLK_A&&k<=SDLK_Z)return unsigned(k-SDLK_A+'A');
    if(k>=SDLK_0&&k<=SDLK_9)return unsigned(k);
    if(k>=SDLK_F1&&k<=SDLK_F12)return unsigned(k-SDLK_F1+112);
    if(k>=SDLK_F13&&k<=SDLK_F24)return unsigned(k-SDLK_F13+124);
    if(k>=SDLK_KP_1&&k<=SDLK_KP_9)return unsigned(k-SDLK_KP_1+0x61);
    switch(k) {
        case SDLK_SPACE:return 32;case SDLK_RETURN:return 13;case SDLK_TAB:return 9;
        case SDLK_ESCAPE:return 27;case SDLK_BACKSPACE:return 8;case SDLK_UP:return 38;
        case SDLK_DOWN:return 40;case SDLK_LEFT:return 37;case SDLK_RIGHT:return 39;
        case SDLK_LSHIFT:return 160;case SDLK_RSHIFT:return 161;case SDLK_LCTRL:return 162;
        case SDLK_RCTRL:return 163;case SDLK_KP_0:return 0x60;
        case SDLK_KP_ENTER:return 13;case SDLK_DELETE:return 46;case SDLK_INSERT:return 45;
        case SDLK_HOME:return 36;case SDLK_END:return 35;case SDLK_PAGEUP:return 33;case SDLK_PAGEDOWN:return 34;
        case SDLK_PAUSE:return 19;case SDLK_CAPSLOCK:return 20;case SDLK_NUMLOCKCLEAR:return 144;
        case SDLK_SCROLLLOCK:return 145;case SDLK_PRINTSCREEN:return 44;case SDLK_LALT:return 164;
        case SDLK_RALT:return 165;case SDLK_LGUI:return 91;case SDLK_RGUI:return 92;case SDLK_APPLICATION:return 93;
        case SDLK_KP_MULTIPLY:return 106;case SDLK_KP_PLUS:return 107;case SDLK_KP_MINUS:return 109;
        case SDLK_KP_DIVIDE:return 111;case SDLK_KP_DECIMAL:return 110;case SDLK_KP_PERIOD:return 110;
        case SDLK_SEMICOLON:return 186;case SDLK_EQUALS:return 187;case SDLK_COMMA:return 188;
        case SDLK_MINUS:return 189;case SDLK_PERIOD:return 190;case SDLK_SLASH:return 191;case SDLK_GRAVE:return 192;
        case SDLK_LEFTBRACKET:return 219;case SDLK_BACKSLASH:return 220;case SDLK_RIGHTBRACKET:return 221;
        case SDLK_APOSTROPHE:return 222;default:return 0;
    }
}
struct DialogMailbox {
    std::mutex mutex;
    std::vector<std::pair<std::string,std::string>> selected;
    bool pending=false;
};
struct DialogRequest {std::shared_ptr<DialogMailbox> mailbox;std::string field;};
void SDLCALL dialog_result(void* data,const char* const* files,int) {
    std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(data));
    std::lock_guard<std::mutex> lock(request->mailbox->mutex);
    if(!files)request->mailbox->selected.emplace_back("error",SDL_GetError());
    else if(files[0])request->mailbox->selected.emplace_back(request->field,files[0]);
    request->mailbox->pending=false;
}
struct Pads {
    std::array<SDL_Gamepad*,4> opened{};
    std::array<SDL_JoystickID,4> ids{};
    ~Pads(){for(auto p:opened)if(p)SDL_CloseGamepad(p);}
    void refresh() {
        for(int i=0;i<4;++i)if(opened[i]&&!SDL_GamepadConnected(opened[i])) {SDL_CloseGamepad(opened[i]);opened[i]=nullptr;ids[i]=0;}
        int n=0;auto list=SDL_GetGamepads(&n);
        if(!list)return;
        for(int j=0;j<n;++j) {
            if(std::find(ids.begin(),ids.end(),list[j])!=ids.end())continue;
            auto p=SDL_OpenGamepad(list[j]);if(!p)continue;
#ifdef _WIN32
            // The game still consumes XInput slots. Do not promise that an
            // unrelated SDL HID controller will work in the Windows game.
            int slot=SDL_GetGamepadPlayerIndex(p);
#else
            int slot=0;while(slot<4&&opened[slot])++slot;
#endif
            if(slot<0||slot>=4||opened[slot]) {SDL_CloseGamepad(p);continue;}
            opened[slot]=p;ids[slot]=list[j];
        }
        SDL_free(list);
    }
    std::array<uint16_t,4> sample(bool focused) {
        std::array<uint16_t,4> state{};if(!focused)return state;
        const std::pair<SDL_GamepadButton,uint16_t> bits[]={
            {SDL_GAMEPAD_BUTTON_START,0x10},{SDL_GAMEPAD_BUTTON_BACK,0x20},
            {SDL_GAMEPAD_BUTTON_SOUTH,0x1000},{SDL_GAMEPAD_BUTTON_EAST,0x2000},
            {SDL_GAMEPAD_BUTTON_WEST,0x4000},{SDL_GAMEPAD_BUTTON_NORTH,0x8000},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,0x100},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0x200},
            {SDL_GAMEPAD_BUTTON_DPAD_UP,1},{SDL_GAMEPAD_BUTTON_DPAD_DOWN,2},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT,4},{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,8},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK,0x40},{SDL_GAMEPAD_BUTTON_RIGHT_STICK,0x80}};
        for(int i=0;i<4;++i)if(opened[i])for(auto [button,mask]:bits)if(SDL_GetGamepadButton(opened[i],button))state[i]|=mask;
        return state;
    }
};
struct CheckResult {Settings settings;Build build;bool play=false,save=false;};
struct App {
    fs::path root;
    Settings settings;
    std::string status="Select your prepared game files, then check or play.",load_error;
    std::future<CheckResult> pending;
    std::optional<Build> ready;
    std::unique_ptr<Child> child;
    std::shared_ptr<DialogMailbox> dialogs=std::make_shared<DialogMailbox>();
    Pads pads;
    std::set<unsigned> held;
    std::array<uint16_t,4> previous{};
    int recording=-1,record_count=0,assign_player=-1,assign_choice=0,current_tab=0;
    std::array<std::string,5> recorded;
    bool smoke=false;
    explicit App(fs::path p,bool s):root(std::move(p)),smoke(s) {
        try {settings=load_settings(root);if(!smoke)check_files(false);} catch(const std::exception& e) {load_error=e.what();status="Settings were not loaded. Existing files will not be overwritten.";}
    }
    bool busy() const{return pending.valid();}
    bool game_running()const{return child&&child->running();}
    void save() {
        if(!load_error.empty())throw std::runtime_error("Repair the existing settings file before saving; it has been preserved.");
        if(!ready)throw std::runtime_error("Check the selected build before saving gameplay settings.");
        if(executable_running(ready->executable))throw std::runtime_error("Use the in-game PC menu while the game is running.");
        save_shared_settings(ready->root,settings);
        atomic_text(root/"runtime/launcher/settings.json",settings_json(settings).dump(2)+"\n");
        status="Settings saved. The game menu and native launcher share this build's preferences.";
    }
    void reload() {
        auto next=load_settings(root);
        if(!settings.shared.root.empty()&&settings.shared.root!=fs::weakly_canonical(root)) {
            next.paths=settings.paths;next=load_shared_settings(settings.shared.root,next);
        }
        settings=std::move(next);load_error.clear();ready.reset();recording=-1;assign_player=-1;
        status="Saved settings reloaded. Unsaved launcher edits were discarded.";
    }
    void check_files(bool play,bool save_requested=false) {
        if(busy()||smoke||!load_error.empty())return;
        if(game_running()){status="Use the in-game PC menu while the game is running.";return;}
        auto snapshot=settings;auto catalog=root;
        validate_settings(snapshot);
        status="Checking game identity and build hashes...";
        pending=std::async(std::launch::async,[snapshot,catalog,play,save_requested]() mutable {
            for(auto& [k,v]:snapshot.paths){(void)k;v=utf8(resolve(catalog,v));}
            auto build=validate_build(snapshot,catalog);
            if(snapshot.shared.root!=fs::weakly_canonical(build.root))snapshot=load_shared_settings(build.root,snapshot);
            return CheckResult{snapshot,build,play,save_requested};
        });
    }
    void update() {
        {std::lock_guard<std::mutex> lock(dialogs->mutex);
            for(auto& [field,value]:dialogs->selected) {if(field=="error")status="File dialog: "+value;else {settings.paths.at(field)=value;ready.reset();}}
            dialogs->selected.clear();
        }
        if(pending.valid()&&pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            try {
                auto result=pending.get();ready=result.build;
                status="Verified "+ready->profile.version+". Ready to play.";
                if(settings.shared.root!=result.settings.shared.root) {
                    settings=std::move(result.settings);
                    status="Loaded this build's saved preferences. Review them, then press Play or Save again.";
                    return;
                }
                settings=std::move(result.settings);
                if(result.save)save();
                if(result.play) {
                    if(child&&child->running())throw std::runtime_error("The game started by this launcher is already running.");
                    if(executable_running(result.build.executable))throw std::runtime_error("This game build is already running. Close it before starting another instance.");
                    save();auto plan=plan_launch(settings,result.build,current_environment());
                    auto next=std::make_unique<Child>();next->start(plan);child=std::move(next);
                    recording=-1;assign_player=-1;held.clear();
                    status="Game started directly. Logs: "+utf8(plan.session);
                }
            } catch(const std::exception& e) {ready.reset();status=e.what();}
        }
        if(child) {int code=0;if(!child->running(&code)){
            status="Game exited with code "+std::to_string(code)+". Its logs are in the launch session.";child.reset();
            try {settings=load_shared_settings(settings.shared.root,settings);status+=" In-game preferences reloaded.";}
            catch(const std::exception& e){status+=" Could not reload in-game settings: ";status+=e.what();}
        }}
    }
    void browse(SDL_Window* window,const char* field,bool folder) {
        if(smoke)return;
        {std::lock_guard<std::mutex> lock(dialogs->mutex);if(dialogs->pending)return;dialogs->pending=true;}
        auto request=new DialogRequest{dialogs,field};auto& initial=settings.paths.at(field);
        if(folder)SDL_ShowOpenFolderDialog(dialog_result,request,window,initial.empty()?nullptr:initial.c_str(),false);
        else {static const SDL_DialogFileFilter filters[]={{"Decrypted game executable","elf"}};SDL_ShowOpenFileDialog(dialog_result,request,window,filters,1,initial.empty()?nullptr:initial.c_str(),false);}
    }
    void event(const SDL_Event& e) {
        if(busy()||game_running()){held.clear();return;}
        if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) {held.clear();recording=-1;assign_player=-1;previous.fill(0);}
        if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat) {
            auto key=virtual_key(e.key.key);if(key)held.insert(key);
            if(e.key.key==SDLK_ESCAPE){recording=-1;assign_player=-1;return;}
            if(recording>=0&&key) {
                auto label=key_label(key);
                if(std::find(recorded.begin(),recorded.begin()+record_count,label)!=recorded.begin()+record_count) {status="Choose a different key for each button.";return;}
                recorded[record_count++]=label;
                if(record_count==5) {settings.players[recording].keys=recorded;recording=-1;status="Five-key mapping recorded.";}
            }
        }
        if(e.type==SDL_EVENT_KEY_UP)held.erase(virtual_key(e.key.key));
    }
};
void heading(const char* title,const char* description) {
    ImGui::TextColored(accent,"%s",title);ImGui::Spacing();ImGui::TextWrapped("%s",description);ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
}
void synthetic_input_check(App& app) {
    auto saved=app.settings;auto saved_status=app.status;app.settings=Settings{};
    const auto before=app.settings.players[0].keys;
    auto key=[&](SDL_Keycode code){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=code;app.event(e);};
    auto check=[](bool good){if(!good)throw std::runtime_error("Native UI synthetic input regression");};
    app.recording=0;app.record_count=0;
    key(SDLK_F1);key(SDLK_F1);check(app.record_count==1); // duplicate is ignored
    key(SDLK_F2);key(SDLK_F3);key(SDLK_F4);
    check(app.settings.players[0].keys==before); // no partial record escapes
    key(SDLK_F5);check(app.recording<0&&app.settings.players[0].keys[4]=="F5");
    const auto complete=app.settings.players[0].keys;
    app.recording=0;app.record_count=0;key(SDLK_A);key(SDLK_ESCAPE);
    check(app.recording<0&&app.settings.players[0].keys==complete);
    app.recording=0;app.assign_player=1;key(SDLK_SPACE);
    SDL_Event blur{};blur.type=SDL_EVENT_WINDOW_FOCUS_LOST;app.event(blur);
    check(app.recording<0&&app.assign_player<0&&app.held.empty());
    check(virtual_key(SDLK_F24)==135&&virtual_key(SDLK_RSHIFT)==161&&virtual_key(SDLK_KP_1)==0x61);
    app.settings=saved;app.status=saved_status;app.record_count=0;
}
void game_tab(App& app,SDL_Window* window) {
    heading("YOUR PREPARED GAME","Run a locally built copy directly. This launcher never downloads or builds game content.");
    const std::pair<const char*,const char*> fields[]={{"game_root","Game folder (parent of PS3_GAME)"},{"dev_flash","PS3 firmware (dev_flash)"},{"eboot_elf","Decrypted game executable (EBOOT.elf)"}};
    for(auto [key,label]:fields) {
        ImGui::TextUnformatted(label);ImGui::PushID(key);ImGui::SetNextItemWidth(std::max(100.0f,ImGui::GetContentRegionAvail().x-105));
        if(ImGui::InputText("##path",&app.settings.paths.at(key)))app.ready.reset();
        ImGui::SameLine();if(ImGui::Button("Browse...",ImVec2(95,0)))app.browse(window,key,std::string(key)!="eboot_elf");ImGui::PopID();ImGui::Spacing();
    }
    ImGui::Separator();ImGui::Spacing();
    ImGui::Checkbox("Offline mode (skip account-service errors)",&app.settings.offline);
    ImGui::Checkbox("Hide PlayStation Store entry",&app.settings.hide_store);
    ImGui::Checkbox("Start game without taking focus",&app.settings.background);
    ImGui::Separator();ImGui::TextUnformatted("Shared game preferences");
    int volume=int(app.settings.pc.volume_percent);ImGui::SetNextItemWidth(240);
    if(ImGui::SliderInt("Master volume",&volume,0,100,"%d%%")){app.settings.pc.volume_percent=unsigned(volume);app.settings.pc_configured=true;}
    bool cap=app.settings.pc.fps_limit!=0;
    if(ImGui::Checkbox("30 FPS frame lock",&cap)){app.settings.pc.fps_limit=cap?30:0;app.settings.pc_configured=true;}
    if(ImGui::Checkbox("Borderless fullscreen",&app.settings.pc.fullscreen))app.settings.pc_configured=true;
    ImGui::TextDisabled("Offline mode requires restarting. Fullscreen does not change rendering resolution.");
    ImGui::Spacing();ImGui::TextDisabled("Supported identities: EU 1.00, US 1.00, EU 1.02. ELF and EXE hashes are checked.");
    ImGui::TextWrapped("Configuration root: %s",utf8(app.root).c_str());
    if(app.ready) {ImGui::Spacing();ImGui::TextColored(accent,"VERIFIED: %s",app.ready->profile.version.c_str());ImGui::TextWrapped("%s",utf8(app.ready->executable).c_str());}
#ifndef _WIN32
    ImGui::Spacing();ImGui::TextWrapped("This is a native Linux launcher, not a Linux game port. It only starts a matching Linux game binary; it will not run a Windows EXE through Wine automatically.");
#endif
    ImGui::Spacing();ImGui::TextWrapped("Need to import an ISO/PUP or compile? Use the separate developer preparation tools. Python and a compiler are not needed to play a prepared copy.");
}
void controls_tab(App& app) {
    heading("VIRTUAL BUZZERS","Five buttons per handset: red, yellow, green, orange, blue. Player labels here do not rename in-game profiles.");
    int slots=app.settings.places==8?1:0;ImGui::SetNextItemWidth(145);
    if(ImGui::Combo("Available handsets",&slots,"4 players\0" "8 players\0"))app.settings.places=slots?8:4;
    ImGui::SetNextItemWidth(145);ImGui::InputInt("Tap retention (ms)",&app.settings.pulse_ms,10,50);
    for(int p=0;p<app.settings.places;++p) {
        ImGui::PushID(p);ImGui::Separator();ImGui::TextColored(accent,"PLAYER %d",p+1);ImGui::SameLine();ImGui::SetNextItemWidth(190);ImGui::InputText("##name",&app.settings.players[p].name);
        ImGui::SameLine();if(ImGui::Button(app.recording==p?"Cancel recording":"Record 5 keys")) {if(app.recording==p)app.recording=-1;else {app.recording=p;app.record_count=0;app.status="Press red, yellow, green, orange, blue keys. Esc cancels; changes apply only after all five.";}}
        if(ImGui::BeginTable("keys",5,ImGuiTableFlags_SizingStretchSame)) {
            for(int b=0;b<5;++b) {ImGui::TableNextColumn();ImGui::TextColored(button_colors[b],"%s",button_names[b]);}
            ImGui::TableNextRow();
            for(int b=0;b<5;++b) {ImGui::TableNextColumn();ImGui::PushID(b);ImGui::SetNextItemWidth(-1);ImGui::InputText("##key",&app.settings.players[p].keys[b]);ImGui::PopID();}
            ImGui::EndTable();
        }
        if(app.recording==p)ImGui::TextColored(accent,"Press %s (%d/5). Esc cancels.",button_names[app.record_count],app.record_count+1);
        ImGui::PopID();ImGui::Spacing();
    }
    ImGui::Separator();ImGui::Checkbox("Enable game controllers",&app.settings.gamepads);
    if(ImGui::Button("Use R1 + D-pad preset")) {
        for(auto& pad:app.settings.pads)pad.buttons=Pad{}.buttons;
        app.settings.gamepads=true;
        app.status="Preset applied: R1/RB red; Up blue, Right orange, Down green, Left yellow. Save to keep it.";
    }
    ImGui::TextWrapped("Default: R1/RB red; D-pad Up blue, Right orange, Down green, Left yellow. START is reserved for the PC menu.");
    ImGui::TextWrapped("Windows: four XInput slots. Other SDL controller types are not yet wired into the game's Windows input adapter.");
    for(int i=0;i<4;++i) {
        ImGui::PushID(100+i);ImGui::Text("Controller slot %d",i+1);ImGui::SameLine();
        auto label=app.settings.pads[i].player<0?std::string("Unassigned"):"Player "+std::to_string(app.settings.pads[i].player+1);
        ImGui::SetNextItemWidth(145);
        if(ImGui::BeginCombo("##player",label.c_str())) {
            if(ImGui::Selectable("Unassigned",app.settings.pads[i].player<0))app.settings.pads[i].player=-1;
            for(int p=0;p<app.settings.places;++p)if(ImGui::Selectable(("Player "+std::to_string(p+1)).c_str(),app.settings.pads[i].player==p))app.settings.pads[i].player=p;
            ImGui::EndCombo();
        }
        if(ImGui::BeginTable("pad_buttons",5,ImGuiTableFlags_SizingStretchSame)) {
            for(int b=0;b<5;++b) {ImGui::TableNextColumn();ImGui::TextColored(button_colors[b],"%s",button_names[b]);}
            ImGui::TableNextRow();
            for(int b=0;b<5;++b) {ImGui::TableNextColumn();ImGui::PushID(b);ImGui::SetNextItemWidth(-1);if(ImGui::BeginCombo("##button",app.settings.pads[i].buttons[b].c_str())) {for(const auto* name:pad_names)if(ImGui::Selectable(name,app.settings.pads[i].buttons[b]==name))app.settings.pads[i].buttons[b]=name;ImGui::EndCombo();}ImGui::PopID();}
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
}
void preview_tab(App& app,bool focused) {
    heading("BUTTON TEST","Inputs are observed only while this launcher is focused. Nothing is injected into the desktop or game.");
    ImGui::TextColored(focused?accent:ImVec4(1,.65f,.25f,1),"%s",focused?"Input test active":"Paused - launcher is not focused");
    std::array<uint8_t,8> masks{};
    try {
        auto config=input_config(app.settings);auto pads=app.pads.sample(focused&&!app.smoke);
        if(focused&&!app.smoke)for(int p=0;p<app.settings.places;++p)for(int b=0;b<5;++b)if(config.keys[p][b]&&app.held.count(config.keys[p][b]))masks[p]|=uint8_t(1u<<b);
        if(app.settings.gamepads)for(int i=0;i<4;++i)if(config.pad_player[i]>=0)masks[config.pad_player[i]]|=buzz_input::map_pad(pads[i],config.pad_buttons[i]);
        for(int i=0;i<4;++i)if(app.assign_player>=0&&(pads[i]&0x10)&&!(app.previous[i]&0x10)) {
            bool duplicate=false;for(int j=0;j<4;++j)if(j!=i&&app.settings.pads[j].player==app.assign_player)duplicate=true;
            if(duplicate)app.status="That player already has a controller. Unassign it in Controls first.";
            else {app.settings.pads[i].player=app.assign_player;app.status="Controller assigned to Player "+std::to_string(app.assign_player+1);}
            app.assign_player=-1;
        }
        app.previous=pads;
    } catch(const std::exception& e) {ImGui::TextWrapped("Mapping needs attention: %s",e.what());}
    for(int p=0;p<app.settings.places;++p) {
        ImGui::PushID(p);ImGui::Text("P%d  %s",p+1,app.settings.players[p].name.c_str());
        for(int b=0;b<5;++b) {if(b)ImGui::SameLine();auto color=button_colors[b];bool active=(masks[p]&(1<<b))!=0;if(!active){color.x*=.25f;color.y*=.25f;color.z*=.25f;}
            ImGui::PushStyleColor(ImGuiCol_Button,color);ImGui::Button((std::string(button_names[b])+"##"+std::to_string(b)).c_str(),ImVec2(100,30));ImGui::PopStyleColor();}
        ImGui::PopID();ImGui::Spacing();
    }
    ImGui::Separator();
    for(int i=0;i<4;++i)ImGui::Text("Slot %d: %s",i+1,app.pads.opened[i]?SDL_GetGamepadName(app.pads.opened[i]):"not connected");
    ImGui::SetNextItemWidth(145);const char* names[]={"Player 1","Player 2","Player 3","Player 4","Player 5","Player 6","Player 7","Player 8"};
    ImGui::Combo("Assign controller to",&app.assign_choice,names,app.settings.places);ImGui::SameLine();
    if(ImGui::Button("Press START to identify")) {app.assign_player=app.assign_choice;app.status="Press START on the controller. Esc cancels.";}
    if(app.assign_player>=0)ImGui::TextColored(accent,"Waiting for START for Player %d...",app.assign_player+1);
}
void draw(App& app,SDL_Window* window,bool focused,int forced_tab) {
    auto& io=ImGui::GetIO();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Buzz! Quiz World Launcher",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(accent,"BUZZ! QUIZ WORLD");ImGui::SameLine();ImGui::TextDisabled(" / PC launcher");ImGui::Spacing();
    ImGui::BeginDisabled(app.busy()||app.game_running()||!app.load_error.empty());
    if(ImGui::BeginTabBar("tabs")) {
        const char* tabs[]={"Game","Controls","Button test"};
        for(int i=0;i<3;++i)if(ImGui::BeginTabItem(tabs[i],nullptr,forced_tab==i?ImGuiTabItemFlags_SetSelected:0)) {
            if(app.current_tab!=i){app.recording=-1;app.assign_player=-1;app.held.clear();app.current_tab=i;}
            ImGui::BeginChild(tabs[i],ImVec2(0,-130),ImGuiChildFlags_None);
            if(i==0)game_tab(app,window);else if(i==1)controls_tab(app);else preview_tab(app,focused);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if(!app.load_error.empty())ImGui::TextWrapped("Existing settings could not be loaded: %s",app.load_error.c_str());
    else if(app.recording>=0)ImGui::TextColored(accent,"Recording: press %s. Esc cancels.",button_names[app.record_count]);
    else ImGui::TextWrapped("%s",app.status.c_str());
    ImGui::BeginDisabled(app.busy()||app.game_running()||!app.load_error.empty()||app.smoke);
    try {
        if(ImGui::Button("Check files",ImVec2(125,32)))app.check_files(false);ImGui::SameLine();
        if(ImGui::Button("Save",ImVec2(100,32)))app.check_files(false,true);ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.12f,.42f,.58f,1));
        bool running=app.child&&app.child->running();ImGui::BeginDisabled(running);
        if(ImGui::Button(running?"Game running":"PLAY",ImVec2(145,32))) {
            if(warnings(app.settings).empty())app.check_files(true);else ImGui::OpenPopup("Review input mappings");
        }
        ImGui::EndDisabled();ImGui::PopStyleColor();
    } catch(const std::exception& e) {app.status=e.what();}
    ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(app.busy()||app.game_running()||app.smoke);
    if(ImGui::Button("Reload saved",ImVec2(125,32))){try{app.reload();}catch(const std::exception& e){app.status=e.what();}}
    ImGui::EndDisabled();
    if(ImGui::BeginPopupModal("Review input mappings",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        try {for(auto& warning:warnings(app.settings))ImGui::TextWrapped("%s",warning.c_str());}
        catch(const std::exception& e) {ImGui::TextWrapped("%s",e.what());}
        if(ImGui::Button("Continue to game")){try{app.check_files(true);ImGui::CloseCurrentPopup();}catch(const std::exception& e){app.status=e.what();}}
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("Native C++ / SDL3 / ImGui - no Python or PowerShell needed for play");ImGui::End();
}
}
int run_ui(const fs::path& project,bool smoke,const fs::path& capture,int smoke_tab) {
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI,"0");SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT,"0");SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT,"0");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO|(smoke?0:SDL_INIT_GAMEPAD)))throw std::runtime_error(SDL_GetError());
    auto window=SDL_CreateWindow("Buzz! Quiz World Launcher",1000,760,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!window){SDL_Quit();throw std::runtime_error(SDL_GetError());}
    SDL_SetWindowMinimumSize(window,760,540);
    auto renderer=SDL_CreateRenderer(window,smoke?"software":nullptr);
    if(!renderer){SDL_DestroyWindow(window);SDL_Quit();throw std::runtime_error(SDL_GetError());}
    SDL_SetRenderVSync(renderer,1);
    IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    auto& style=ImGui::GetStyle();ImGui::StyleColorsDark();style.WindowPadding=ImVec2(22,18);style.ItemSpacing=ImVec2(10,9);style.FramePadding=ImVec2(9,6);style.FrameRounding=5;style.Colors[ImGuiCol_WindowBg]=ImVec4(.045f,.065f,.1f,1);style.FontSizeBase=17;
#ifdef _WIN32
    const char* system_font="C:/Windows/Fonts/segoeui.ttf";
#else
    const char* system_font="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
    if(fs::is_regular_file(path(system_font)))io.Fonts->AddFontFromFileTTF(system_font,17);
    ImGui_ImplSDL3_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer3_Init(renderer);
    if(!smoke)SDL_ShowWindow(window);
    {
        App app(project,smoke);if(smoke)synthetic_input_check(app);bool quit=false;int frames=0;
        while(!quit) {
            SDL_Event e;
            while(SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if(e.type==SDL_EVENT_QUIT||(e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED&&e.window.windowID==SDL_GetWindowID(window)))quit=true;
                if(!smoke)app.event(e);
            }
            app.update();bool focused=(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)!=0;
            if(!focused)app.held.clear();
            if(!smoke)app.pads.refresh();
            ImGui_ImplSDLRenderer3_NewFrame();ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
            draw(app,window,focused,smoke?smoke_tab:-1);ImGui::Render();
            SDL_SetRenderScale(renderer,io.DisplayFramebufferScale.x,io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer,10,16,26,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
            if(smoke&&frames==3&&!capture.empty()) {
                auto surface=SDL_RenderReadPixels(renderer,nullptr);
                if(!surface||!SDL_SaveBMP(surface,utf8(capture).c_str())) {if(surface)SDL_DestroySurface(surface);throw std::runtime_error(SDL_GetError());}
                SDL_DestroySurface(surface);
            }
            SDL_RenderPresent(renderer);++frames;if(smoke&&frames>=4)quit=true;
            SDL_Delay(smoke?1:16);
        }
    }
    ImGui_ImplSDLRenderer3_Shutdown();ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    return 0;
}
}
