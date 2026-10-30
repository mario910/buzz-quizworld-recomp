// Synthetic guest memory and a disposable host tree; no game/firmware inputs.
#include "cellGame.h"
#include "ps3emu/guest_call.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
extern "C" {
unsigned char* vm_base;
const char* ppu_vfs_root=".";
ps3_guest_caller_fn g_ps3_guest_caller=nullptr;
int g_resv_store_active=0;
uint32_t g_ww_lo=0,g_ww_hi=0;
void ps3_ww_report_inline(uint32_t,uint64_t,int) {std::abort();}
int spu_coh_is_reserved(uint32_t) {return 0;}
void spu_coh_notify_write(uint32_t) {std::abort();}
void spu_lockline_lock() {std::abort();}
void spu_lockline_unlock() {std::abort();}
void ppu_resv_break_store(uint64_t) {std::abort();}
#ifdef _WIN32
volatile uint32_t g_ppu_wake_ea[2]={0,0};
void ps3_wake_address_hle(uint32_t) {std::abort();}
#endif
}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    const auto base=fs::temp_directory_path();
    const auto root=base/("Buzz-content-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(fs::create_directory(root),"Cannot create fresh synthetic directory");
    struct Cleanup{fs::path root,base;~Cleanup(){std::error_code e;auto p=fs::weakly_canonical(root,e);if(!e&&p.parent_path()==fs::weakly_canonical(base,e)&&p.filename().u8string().rfind("Buzz-content-",0)==0)fs::remove_all(p,e);}} cleanup{root,base};
    auto hdd=root;
    while(hdd.u8string().size()<145)hdd/="long-host-directory";
    check(fs::create_directories(hdd),"Cannot create long host fixture path");
    check(hdd.u8string().size()>128,"Fixture must exceed guest ABI path length");
#ifdef _WIN32
    check(_wputenv_s(L"PS3_HDD0_ROOT",hdd.c_str())==0,"Cannot set synthetic HDD root");
#else
    check(setenv("PS3_HDD0_ROOT",hdd.c_str(),1)==0,"Cannot set synthetic HDD root");
#endif
    unsigned char memory[4096]{};vm_base=memory;
    auto guest=[](unsigned address){return reinterpret_cast<char*>(static_cast<uintptr_t>(address));};
    auto* init=reinterpret_cast<CellGameSetInitParams*>(guest(0x400));
    std::memcpy(memory+0x100,"TEST00001",10);
    check(cellGameBootCheck(nullptr,nullptr,nullptr,nullptr)==CELL_OK,"Boot check failed");
    check(cellGameDataCheck(CELL_GAME_GAMETYPE_GAMEDATA,guest(0x100),nullptr)==CELL_GAME_RET_NONE,"Fresh game data must not exist");
    check(cellGameCreateGameData(init,guest(0x200),guest(0x300))==CELL_OK,"Create game data failed");
    check(fs::is_directory(hdd/"game/TEST00001/USRDIR"),"Host game data was created under a truncated/wrong root");
    check(std::strcmp(reinterpret_cast<char*>(memory+0x200),"/dev_hdd0/game/TEST00001")==0,"Guest content path ABI changed");
    check(std::strcmp(reinterpret_cast<char*>(memory+0x300),"/dev_hdd0/game/TEST00001/USRDIR")==0,"Guest USRDIR path ABI changed");
    check(cellGameDataCheck(CELL_GAME_GAMETYPE_GAMEDATA,guest(0x100),nullptr)==CELL_OK,"Created data cannot be found");
    // ContentPermit must use the active checked title, not BootCheck's title
    // or an unrelated fallback VFS root.
    std::memcpy(memory+0x100,"TEST00002",10);
    check(cellGameDataCheck(CELL_GAME_GAMETYPE_GAMEDATA,guest(0x100),nullptr)==CELL_GAME_RET_NONE,"Second data unexpectedly exists");
    check(cellGameContentPermit(guest(0x200),guest(0x300))==CELL_OK,"Content permit failed");
    check(fs::is_directory(hdd/"game/TEST00002/USRDIR"),"Content permit did not use the active HDD/title");
    check(!fs::exists(hdd/"game/BLES00000"),"Boot title received unrelated content directories");
    const std::string too_long(1100,'x');cellGame_set_content_path(too_long.c_str());
    check(cellGameBootCheck(nullptr,nullptr,nullptr,nullptr)==CELL_GAME_ERROR_PARAM,"Oversized root was silently truncated");
    check(cellGameCreateGameData(init,guest(0x200),guest(0x300))==CELL_GAME_ERROR_PARAM,"Invalid root was used for writes");
    const auto custom=root/"explicit-root";cellGame_set_content_path(custom.u8string().c_str());
    check(cellGameCreateGameData(init,guest(0x200),guest(0x300))==CELL_OK,"Valid explicit override did not recover");
    check(fs::is_directory(custom/"TEST00002/USRDIR"),"Explicit override points at the wrong host tree");
    std::cout<<"PASS long host content paths, unchanged guest ABI, active title and oversized-root rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
