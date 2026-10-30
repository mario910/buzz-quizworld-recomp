#include "core.h"
#include "pc_settings_file.h"
#include <SDL3/SDL.h>
#include <picosha2.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace buzz_launcher {
namespace {
void require(bool ok, const std::string& message) { if(!ok) throw std::runtime_error(message); }
std::string upper(std::string s) {
    for(auto& c:s) if(c>='a'&&c<='z') c=char(c-'a'+'A');
    return s;
}
std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n");
    return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
void text_ok(const std::string& s, std::size_t max, const char* field, bool nonempty=false) {
    require(s.size()<=max && s.find('\0')==std::string::npos && (!nonempty||!trim(s).empty()), std::string("Invalid ")+field);
}
std::string unique_id() {
    std::random_device rng;
    std::ostringstream s;
    s<<std::hex<<std::chrono::system_clock::now().time_since_epoch().count()<<'-'<<rng()<<rng();
    return s.str();
}
bool below(const fs::path& child,const fs::path& root) {
    auto r=fs::weakly_canonical(root), p=fs::weakly_canonical(child);
    auto a=r.begin(),b=p.begin();
    for(;a!=r.end();++a,++b) if(b==p.end()||*a!=*b) return false;
    return true;
}
bool digest_ok(const std::string& s) {
    return s.size()==64 && s.find_first_not_of("0123456789abcdef")==std::string::npos;
}
void shape(const Json& j,const std::set<std::string>& names,const char* field) {
    require(j.is_object()&&j.size()==names.size(),std::string("Invalid ")+field+" structure");
    for(auto& [key,value]:j.items()) { (void)value; require(names.count(key)!=0,std::string("Unknown ")+field+" field: "+key); }
}
int integer(const Json& j,const char* field) {
    require(j.is_number_integer(),std::string("Invalid integer ")+field);
    auto v=j.get<std::int64_t>();
    require(v>=-1&&v<=65536,std::string("Integer out of range: ")+field);
    return int(v);
}
bool boolean(const Json& j) { require(j.is_boolean(),"Invalid boolean setting");return j.get<bool>(); }
}
std::string utf8(const fs::path& p) { return p.u8string(); }
fs::path path(const std::string& s) { return fs::u8path(s); }
fs::path resolve(const fs::path& root,const std::string& value) {
    require(!trim(value).empty(),"Select paths to the game, firmware and EBOOT.elf");
    return fs::absolute(path(value).is_absolute()?path(value):root/path(value)).lexically_normal();
}
std::string read_text(const fs::path& p,std::size_t limit) {
    require(fs::is_regular_file(p)&&fs::file_size(p)<=limit,"Missing or oversized file: "+utf8(p));
    std::ifstream in(p,std::ios::binary);
    require(bool(in),"Cannot read: "+utf8(p));
    std::string s{std::istreambuf_iterator<char>(in),{}};
    require(s.size()<=limit,"File changed while reading: "+utf8(p));
    return s;
}
void atomic_text(const fs::path& p,const std::string& text) {
    buzz_pc_write_file_atomic(p,text);
}
std::string sha256(const fs::path& p) {
    std::ifstream in(p,std::ios::binary);require(bool(in),"Cannot hash: "+utf8(p));
    picosha2::hash256_one_by_one hash;
    std::array<unsigned char,65536> buffer{};
    while(in) { in.read(reinterpret_cast<char*>(buffer.data()),buffer.size());auto n=in.gcount();hash.process(buffer.begin(),buffer.begin()+n); }
    require(in.eof(),"Read error while hashing: "+utf8(p));
    hash.finish();return picosha2::get_hash_hex_string(hash);
}
std::string sha256_text(const std::string& s) { return picosha2::hash256_hex_string(s); }
Settings::Settings() {
    pc.fps_limit=0; // No implicit change from the SDK's legacy vsync path.
    const char* keys[4][5]={{"Space","4","3","2","1"},{"Q","W","E","R","T"},{"A","S","D","F","G"},{"Z","X","C","V","B"}};
    for(int i=0;i<8;++i) { players[i].name="Player "+std::to_string(i+1);for(int b=0;b<5;++b)players[i].keys[b]=i<4?keys[i][b]:"NONE"; }
    for(int i=0;i<4;++i)pads[i].player=i;
}
unsigned key_code(const std::string& text) {
    auto s=upper(trim(text));std::array<unsigned,5> keys{};
    auto decimal=[](const std::string& n,unsigned maximum)->unsigned {
        if(n.empty()||n.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid numeric key");
        auto value=std::stoul(n,nullptr,10);require(value>=1&&value<=maximum,"Numeric key out of range");return unsigned(value);
    };
    if(s.size()>1&&s[0]=='F'&&s.find_first_not_of("0123456789",1)==std::string::npos)return 111+decimal(s.substr(1),24);
    if(s.size()>1&&s.find_first_not_of("0123456789")==std::string::npos)return decimal(s,255);
    if(s.size()>1&&s[0]=='+')return decimal(s.substr(1),255);
    // Reuse the game's parser, including its canonical hexadecimal VK contract.
    require(buzz_input::parse_keys(s+",NONE,NONE,NONE,NONE",keys),"Unknown key: "+text);
    return keys[0];
}
std::string key_label(unsigned code) {
    const std::pair<unsigned,const char*> names[]={{0,"NONE"},{32,"Space"},{13,"ENTER"},{9,"TAB"},{27,"ESC"},{8,"BACKSPACE"},{38,"UP"},{40,"DOWN"},{37,"LEFT"},{39,"RIGHT"},{160,"LSHIFT"},{161,"RSHIFT"},{162,"LCTRL"},{163,"RCTRL"}};
    for(auto& v:names)if(v.first==code)return v.second;
    if((code>='0'&&code<='9')||(code>='A'&&code<='Z'))return std::string(1,char(code));
    if(code>=112&&code<=135)return "F"+std::to_string(code-111);
    std::ostringstream s;s<<"0x"<<std::uppercase<<std::hex<<std::setw(2)<<std::setfill('0')<<code;return s.str();
}
void validate_settings(const Settings& s) {
    std::string error;require(buzz_pc::validate(s.pc,error),error);
    require(s.places==4||s.places==8,"Player slots must be 4 or 8");
    require(s.pulse_ms>=10&&s.pulse_ms<=5000,"Pulse duration must be 10-5000 ms");
    require(s.paths.size()==3,"Invalid game paths");
    for(const char* key:{"game_root","dev_flash","eboot_elf"})text_ok(s.paths.at(key),32768,"path");
    for(auto& p:s.players) {
        text_ok(p.name,128,"player name",true);
        require(SDL_utf8strlen(p.name.c_str())<=32,"Player name must have at most 32 characters");
        require(std::none_of(p.name.begin(),p.name.end(),[](unsigned char c){return c<32;}),"Control character in player name");
        std::set<unsigned> used;
        for(auto& k:p.keys) { text_ok(k,32,"key");auto code=key_code(k);require(!code||used.insert(code).second,"Duplicate button within one buzzer"); }
    }
    std::set<int> assigned;
    for(auto& p:s.pads) {
        require(p.player>=-1&&p.player<8,"Invalid controller assignment");
        require(p.player<0||assigned.insert(p.player).second,"Two controllers assigned to the same player");
        std::set<std::string> used;
        for(auto& b:p.buttons) {
            require(std::find(std::begin(pad_names),std::end(pad_names),b)!=std::end(pad_names),"Unknown controller button");
            require(used.insert(b).second,"Duplicate controller button");
        }
    }
}
Json settings_json(const Settings& s) {
    validate_settings(s);Json players=Json::array(),pads=Json::array();
    for(auto& p:s.players)players.push_back({{"name",p.name},{"keys",p.keys}});
    for(auto& p:s.pads)pads.push_back({{"player",p.player<0?Json(nullptr):Json(p.player)},{"buttons",p.buttons}});
    return {{"version",1},{"paths",s.paths},{"places",s.places},{"pulse_ms",s.pulse_ms},
            {"offline",s.offline},{"hide_store",s.hide_store},{"gamepads",s.gamepads},{"background",s.background},{"players",players},{"pads",pads}};
}
Settings settings_from_json(const Json& j) {
    shape(j,{"version","paths","places","pulse_ms","offline","hide_store","gamepads","background","players","pads"},"settings");
    require(integer(j.at("version"),"version")==1,"Unsupported settings version");
    Settings s;shape(j.at("paths"),{"game_root","dev_flash","eboot_elf"},"paths");s.paths=j.at("paths").get<decltype(s.paths)>();
    s.places=integer(j.at("places"),"slots");s.pulse_ms=integer(j.at("pulse_ms"),"pulse duration");
    s.offline=boolean(j.at("offline"));s.hide_store=boolean(j.at("hide_store"));s.gamepads=boolean(j.at("gamepads"));s.background=boolean(j.at("background"));
    require(j.at("players").is_array()&&j.at("players").size()==8&&j.at("pads").is_array()&&j.at("pads").size()==4,"Expected 8 player rows and 4 controllers");
    for(int i=0;i<8;++i) { auto& p=j.at("players").at(i);shape(p,{"name","keys"},"player");require(p.at("keys").is_array()&&p.at("keys").size()==5,"Expected five keys");s.players[i]={p.at("name").get<std::string>(),p.at("keys").get<std::array<std::string,5>>()};if(s.players[i].name=="Gracz "+std::to_string(i+1))s.players[i].name="Player "+std::to_string(i+1); }
    for(int i=0;i<4;++i) { auto& p=j.at("pads").at(i);shape(p,{"player","buttons"},"controller");require(p.at("buttons").is_array()&&p.at("buttons").size()==5,"Expected five controller buttons");s.pads[i]={p.at("player").is_null()?-1:integer(p.at("player"),"controller player"),p.at("buttons").get<std::array<std::string,5>>()}; }
    validate_settings(s);return s;
}
Settings load_settings(const fs::path& root) {
    auto saved=root/"runtime/launcher/settings.json";
    Settings s;
    if(fs::exists(saved))s=settings_from_json(Json::parse(read_text(saved)));
    else if(fs::exists(root/"buzz-paths.json")) {
        auto j=Json::parse(read_text(root/"buzz-paths.json"));
        for(auto& [k,v]:s.paths) v=j.value(k,std::string{});
    }
    validate_settings(s);return load_shared_settings(root,s);
}
std::string input_ini(const Settings& s) {
    validate_settings(s);std::ostringstream out;
    out<<"# Native Buzz launcher; red,yellow,green,orange,blue.\nplayers="<<s.places<<"\npulse_ms="<<s.pulse_ms<<"\ngamepads="<<s.gamepads<<"\nbackground_gamepads="<<s.background_gamepads<<'\n';
    for(int p=0;p<8;++p) { out<<"keyboard."<<p+1<<'=';for(int b=0;b<5;++b) { if(b)out<<',';auto k=key_code(s.players[p].keys[b]);if(k)out<<"0x"<<std::hex<<std::uppercase<<std::setw(2)<<std::setfill('0')<<k<<std::dec;else out<<"NONE"; }out<<'\n'; }
    for(int i=0;i<4;++i) { out<<"gamepad."<<i+1<<'=';auto& p=s.pads[i];if(p.player<0)out<<"NONE";else {out<<p.player+1;for(auto& b:p.buttons)out<<','<<b;}out<<'\n'; }
    return out.str();
}
buzz_input::Config input_config(const Settings& s) {
    auto c=buzz_input::defaults();std::string error;require(buzz_input::parse_config(input_ini(s),c,error),error);return c;
}
std::vector<std::string> warnings(const Settings& s) {
    validate_settings(s);std::vector<std::string> out;std::map<unsigned,int> owners;
    for(int i=0;i<s.places;++i) {
        bool missing=false;for(auto& k:s.players[i].keys) { auto code=key_code(k);if(code)++owners[code];else missing=true; }
        if(missing)out.push_back(s.players[i].name+": some keyboard buttons are unassigned");
    }
    for(auto& [k,n]:owners)if(n>1)out.push_back("Key shared between players: "+key_label(k));
    return out;
}
std::string title_id(const std::string& s) {
    auto u16=[&](size_t p)->uint32_t {require(p<=s.size()&&s.size()-p>=2,"Corrupt PARAM.SFO");return uint8_t(s[p])|(uint32_t(uint8_t(s[p+1]))<<8);};
    auto u32=[&](size_t p)->uint32_t {return u16(p)|(u16(p+2)<<16);};
    require(s.size()>=20&&s.compare(0,4,std::string("\0PSF",4))==0,"Invalid PARAM.SFO");
    auto keys=u32(8),values=u32(12),count=u32(16);
    require(count<=256&&20+uint64_t(count)*16<=s.size()&&keys<values&&values<s.size(),"Corrupt PARAM.SFO table");
    for(uint32_t n=0;n<count;++n) {
        size_t at=20+size_t(n)*16;uint64_t key=uint64_t(keys)+u16(at),length=u32(at+4),offset=uint64_t(values)+u32(at+12);
        auto end=s.find('\0',size_t(key));
        require(key<values&&end!=std::string::npos&&end<values&&length<=u32(at+8)&&offset<=s.size()&&length<=s.size()-offset,"Invalid PARAM.SFO field");
        if(s.substr(size_t(key),end-size_t(key))=="TITLE_ID") {
            require(u16(at+2)==0x204&&length<=32,"Invalid TITLE_ID");
            auto title=s.substr(size_t(offset),size_t(length));while(!title.empty()&&title.back()=='\0')title.pop_back();return title;
        }
    }
    throw std::runtime_error("PARAM.SFO has no TITLE_ID");
}
Profile identify(const std::string& title,const std::string& digest) {
    bool eu=title=="BCES00647"||title=="BCES00644"||title=="BCES00645";
    if(eu&&digest==elf100)return {"quizworld-eu100","EU 1.00"};
    if(title=="BCUS98209"&&digest==elfUS)return {"quizworld-us100","US 1.00"};
    if(title=="BCES00647"&&digest==elf102)return {"quizworld-102-verified","EU 1.02"};
    throw std::runtime_error("Unsupported title/executable SHA-256. Prepare a supported build; hash guards cannot be bypassed.");
}
void validate_prepared_content(const Profile& profile,const fs::path& root) {
    if(profile.family!="quizworld-102-verified")return;
    const auto patched=root/"runtime/dev_hdd0/game/BCES00647/USRDIR/Patched";
    for(const char* language:{"GLOBAL","CRO","CZE","GRC","POL","RUS"}) {
        for(const char* suffix:{".EDAT","SCRIPTS.SDAT"}) {
            const auto file=patched/(std::string(language)+suffix);
            require(fs::is_regular_file(file)&&fs::file_size(file)>0&&fs::file_size(file)<=128*1024*1024,
                "EU 1.02 needs its complete Patched update assets, not only EBOOT. Prepare from your own update PKG or EBOOT with a sibling Patched folder. Missing: "+utf8(file));
        }
    }
}
Build validate_build(const Settings& s,const fs::path& catalog) {
    validate_settings(s);auto game=resolve(catalog,s.paths.at("game_root")),flash=resolve(catalog,s.paths.at("dev_flash")),elf=resolve(catalog,s.paths.at("eboot_elf"));
    require(fs::is_directory(game/"PS3_GAME/USRDIR"),"Game root must contain PS3_GAME/USRDIR");
    require(fs::is_directory(flash/"data/font"),"Firmware must contain data/font");
    require(fs::is_regular_file(flash/"sys/external/flashATRAC.pic"),"Firmware is missing sys/external/flashATRAC.pic (required for audio)");
    require(fs::is_regular_file(elf)&&fs::file_size(elf)<=256*1024*1024,"Select a supported decrypted EBOOT.elf");
    auto digest=sha256(elf);auto profile=identify(title_id(read_text(game/"PS3_GAME/PARAM.SFO",1024*1024)),digest);
    std::vector<fs::path> roots={fs::canonical(catalog)};
    auto session_root=fs::canonical(catalog);
    if(session_root.filename()=="workspace"&&session_root.parent_path().parent_path().filename()=="sessions") {
        auto setup=session_root.parent_path().parent_path().parent_path();
        if(setup.filename()=="setup"&&setup.parent_path().filename()=="runtime")session_root=setup.parent_path().parent_path();
    }
    auto sessions=session_root/"runtime/setup/sessions";
    if(fs::is_directory(sessions)) {
        std::vector<fs::path> candidates;
        for(auto& entry:fs::directory_iterator(sessions))if(entry.is_directory()&&below(entry.path(),sessions)&&below(entry.path()/"workspace",entry.path()))candidates.push_back(entry.path()/"workspace");
        std::sort(candidates.rbegin(),candidates.rend());roots.insert(roots.end(),candidates.begin(),candidates.end());
    }
#ifdef _WIN32
    // Vulkan is the default renderer; the D3D12 build is the fallback, and
    // the first choice when the per-build PC settings ask for it.
    const char* exe_name="BuzzQuizWorldRecomp.exe";
    const std::vector<const char*> build_dirs=s.pc.renderer==buzz_pc::Renderer::d3d12
        ?std::vector<const char*>{"build-windows","build-vulkan-windows"}
        :std::vector<const char*>{"build-vulkan-windows","build-windows"};
#else
    const char* exe_name="BuzzQuizWorldRecomp";const std::vector<const char*> build_dirs{"build-linux"};
#endif
    std::vector<fs::path> dirs;
    std::string rejection;
    for(auto& root:roots){dirs.clear();dirs.push_back(root/"game");for(auto d:build_dirs)dirs.push_back(root/d);
    for(const fs::path& dir:dirs) {
        auto exe=dir/exe_name,manifest=dir/"game-build.json";
        if(!fs::is_regular_file(exe)||!fs::is_regular_file(manifest))continue;
        try {
            auto j=Json::parse(read_text(manifest));
            if(j.at("elf_sha256")!=digest)continue;
            require(j.at("family")==profile.family,"Manifest adapter family mismatch");
            auto expected=j.at("exe_sha256").get<std::string>();
            require(digest_ok(expected)&&sha256(exe)==expected,"Game EXE does not match its manifest; rebuild it");
            auto defaults=j.value("runtime_defaults",Json::object());
            require(defaults.is_object(),"Invalid build runtime defaults");
            for(auto& [key,value]:defaults.items())require(key=="BUZZ_NATIVE_COMPLETION"&&value.is_string()&&value.get<std::string>()=="2","Unknown build-bound runtime option");
            if(profile.family!="quizworld-102-verified")require(defaults.value("BUZZ_NATIVE_COMPLETION",std::string{})=="2","Missing version-bound completion mode");
            validate_prepared_content(profile,root);
            return {root,exe,j,profile};
        } catch(const std::exception& e) { rejection=e.what(); }
    }}
    throw std::runtime_error(rejection.empty()?"No matching verified native build. Prepare the game with the developer tools first.":rejection);
}
Environment runtime_environment(const Settings& s,const Build& b,const fs::path& session,Environment env) {
    auto def=[&](const char* k,const std::string& v,bool empty=true){auto it=env.find(k);if(it==env.end()||(empty&&it->second.empty()))env[k]=v;};
    // UI-owned input is isolated from shell/script leftovers; performance
    // overrides retain the original run-windows null-vs-empty semantics.
    for(const char* k:{"BUZZ_KEYS","BUZZ_KEYS_2","BUZZ_KEYS_3","BUZZ_KEYS_4","BUZZ_PULSE_MS","BUZZ_SCRIPT","BUZZ_INPUT_CONFIG","SPURS_JOB_ABI_STACK","SPURS_JOB_KERNEL_MIRROR"})env.erase(k);
    env["PS3_TITLE"]="Buzz! Quiz World";env["CULL_FLIP"]="1";env["PAD_NO_KEYBOARD"]="1";
    env["BUZZ_OFFLINE"]=s.offline?"1":"0";env["BUZZ_HIDE_PSN_STORE"]=s.hide_store?"1":"0";
    env["BUZZ_INPUT_CONFIG"]=utf8(session/"input.ini");env["BUZZ_NO_FMV"]="0";
    env["SPURS_JOB_ABI_STAGE_IMAGE"]="42";env["SPURS_JOB_KERNEL_MIRROR"]="1";env["GCM_STRICT_PARK"]="1";
    env["RSX_VERTEX_CAPACITY"]="1048576";env["RSX_DRAW_CAPACITY"]="8192";env["RSX_TEXTURE_CAPACITY"]="256";
    const std::pair<const char*,const char*> defaults[]={
        {"PS3_VERBOSE","0"},{"GCM_SECOND_V","1"},{"SPURS_JOB_ABI_MIN_IMAGE","32"},
        {"GCM_FIFO_STOP_AT_FLIP","1"},{"RSX_FLIP_BOUNDARY","1"},{"SPURS_JOB_LIST_WORKERS","4"},
        {"SPURS_JOB_LIST_IMAGE","41"},{"SPURS_JOB_LS_EXCHANGE","1"},{"RSX_PIPELINE_CAPACITY","1024"},
        {"BUZZ_COMPLETION_QUEUE","1"},{"RSX_TRACK_ZETA","1"},{"BUZZ_STAGEF","1"},{"SPURS_POOL_SEQUENCE","1"},{"SPURS_POOL_SPIN_US","1000"},
        {"SPU_INTERP_UNLIFTED","1"},{"PS3_USLEEP_TIMER_TAIL_US","150"},{"SPURS_JOB_CACHE_ENV","1"},
        {"RSX_CACHE_ENV","1"},{"SPURS_GUARD_POLL_ONLY","0"},{"SPURS_JTS_WAKE","1"},{"SPURS_JTS_DMA_WAKE","1"},
        {"SPURS_MERGE_READY_LISTS","1"},{"SPURS_NATIVE_EXTRA_FP","1"},{"RSX_GUEST_SAMPLERS","1"},{"RSX_PIXEL_WINDOW","1"}};
    for(auto& [k,v]:defaults)def(k,v);
    if(b.executable.parent_path().filename().string().find("vulkan")!=std::string::npos) {
        // Measured-best Vulkan set (as scripts/run-windows.ps1): without the
        // batching options the Vulkan build runs a few FPS.
        const std::pair<const char*,const char*> vulkan[]={
            {"PS3RECOMP_VK_DRAW_BATCH","512"},{"PS3RECOMP_VK_DRAW_RING","1"},{"PS3RECOMP_VK_DRAW_PASSES","1"},
            {"PS3RECOMP_VK_DRAW_CLEARS","1"},{"PS3RECOMP_VK_DRAW_ASYNC","1"},{"PS3RECOMP_VK_PRESENT_MODE","mailbox"},
            {"PS3RECOMP_VK_PRESENT_READBACK","0"},{"PS3RECOMP_VK_TEXTURE_REUSE","1"},{"PS3RECOMP_ENG_TEXTURE_ROWS","1"}};
        for(auto& [k,v]:vulkan)def(k,v);
    }
    def("SPURS_JOB_LIST_EXTRA_IMAGES","40",false);def("SPURS_JOB_LS_PUBLISH","mbox",false);
    def("RSX_SHADER_CACHE_DIR",utf8(b.root/"runtime/shader-cache"),false);
    const auto bound_defaults=b.manifest.value("runtime_defaults",Json::object());
    for(auto& [key,value]:bound_defaults.items())def(key.c_str(),value.get<std::string>());
    for(const char* k:{"PS3_VFS_ROOT","PS3_DISC_ROOT"})env[k]=utf8(resolve(b.root,s.paths.at("game_root")));
    env["PS3_DEV_FLASH"]=utf8(resolve(b.root,s.paths.at("dev_flash")));
    env["PS3_HDD0_ROOT"]=utf8(b.root/"runtime/dev_hdd0");env["PS3_HDD1_ROOT"]=utf8(b.root/"runtime/dev_hdd1");
    if(s.background)env["RSX_WINDOW_BACKGROUND"]="1";else env.erase("RSX_WINDOW_BACKGROUND");
    // Capturing raw SPU misses is diagnostic, not a requirement for playing.
    // Respect an explicitly configured capture path without creating one here.
    return env;
}
LaunchPlan plan_launch(const Settings& s,const Build& b,Environment inherited) {
    verify_shared_settings(b.root,s);
    validate_prepared_content(b.profile,b.root);
    // Resolve portable relative paths in the selected catalog before this call.
    auto session=b.root/"runtime/launcher/sessions"/unique_id();
    fs::create_directories(session.parent_path());require(fs::create_directory(session),"Could not create unique launch session");
    fs::create_directories(b.root/"runtime/dev_hdd0");fs::create_directories(b.root/"runtime/dev_hdd1");
    Json paths=s.paths;atomic_text(session/"paths.json",paths.dump(2)+"\n");atomic_text(session/"input.ini",input_ini(s));
    return {b.executable,path(s.paths.at("eboot_elf")),b.root/"runtime",session/"stdout.log",session/"stderr.log",session,runtime_environment(s,b,session,std::move(inherited))};
}
}
