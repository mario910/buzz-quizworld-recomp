#include "buzzer_hid_win32.h"
#include "buzzer_hid.h"
#include "buzzer_input.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
struct Device {
    HANDLE handle=nullptr;
    std::wstring key;
    unsigned product=0,report_bytes=0,max_buttons=0;
    std::vector<DWORD> preparsed;
    buzz_hid::Layout layout;
    bool active=false,faulted=false;
    uint32_t neutral_seen=0;
    PHIDP_PREPARSED_DATA data(){return reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());}
};
std::array<Device,2> devices;
std::unordered_set<HANDLE> ignored;
std::vector<DWORD> raw_buffer;
HWND owner=nullptr;
std::atomic<bool> registered{false};
std::mutex hid_mutex;
void shutdown_locked();

void suspend(unsigned slot) {
    auto& d=devices[slot];
    if(!d.faulted)fprintf(stderr,"[buzz-hid] receiver %u: invalid input; release all buttons to recover\n",slot+1);
    d.faulted=true;d.neutral_seen=0;d.layout.clear();buzz_input_physical_state(slot,0,false);
}
void remove(unsigned slot) {
    auto& d=devices[slot];if(!d.active)return;
    buzz_input_physical_state(slot,0,false);d.active=false;d.handle=nullptr;d.preparsed.clear();
    fprintf(stderr,"[buzz-hid] receiver %u disconnected\n",slot+1);
}
int find(HANDLE handle){for(unsigned i=0;i<devices.size();++i)if(devices[i].active&&devices[i].handle==handle)return int(i);return -1;}
bool identify(HANDLE handle,Device& candidate) {
    RID_DEVICE_INFO info{};info.cbSize=sizeof(info);UINT size=sizeof(info);
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICEINFO,&info,&size)==UINT(-1)||info.dwType!=RIM_TYPEHID||
       !buzz_hid::supported(info.hid.dwVendorId,info.hid.dwProductId))return false;
    if(info.hid.usUsagePage!=1||(info.hid.usUsage!=4&&info.hid.usUsage!=5))return false;
    UINT chars=0;
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&chars)==UINT(-1)||!chars||chars>4096)return false;
    std::vector<wchar_t> name(chars+1,0);
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,name.data(),&chars)==UINT(-1))return false;
    candidate.key=name.data();for(auto& c:candidate.key)c=wchar_t(std::towlower(c));
    candidate.handle=handle;candidate.product=info.hid.dwProductId;
    return !candidate.key.empty();
}
bool capabilities(Device& d) {
    UINT bytes=0;
    if(GetRawInputDeviceInfoW(d.handle,RIDI_PREPARSEDDATA,nullptr,&bytes)==UINT(-1)||!bytes||bytes>65536)return false;
    d.preparsed.resize((bytes+sizeof(DWORD)-1)/sizeof(DWORD));
    if(GetRawInputDeviceInfoW(d.handle,RIDI_PREPARSEDDATA,d.preparsed.data(),&bytes)==UINT(-1))return false;
    HIDP_CAPS caps{};
    if(HidP_GetCaps(d.data(),&caps)!=HIDP_STATUS_SUCCESS||caps.InputReportByteLength<2||caps.InputReportByteLength>buzz_hid::max_report_bytes||
       !caps.NumberInputButtonCaps||caps.NumberInputButtonCaps>64)return false;
    USHORT count=caps.NumberInputButtonCaps;std::vector<HIDP_BUTTON_CAPS> buttons(count);
    if(HidP_GetButtonCaps(HidP_Input,buttons.data(),&count,d.data())!=HIDP_STATUS_SUCCESS||count>buttons.size())return false;
    for(unsigned i=0;i<count;++i) {
        const auto& b=buttons[i];if(b.UsagePage!=9)continue;
        const auto low=b.IsRange?b.Range.UsageMin:b.NotRange.Usage;
        const auto high=b.IsRange?b.Range.UsageMax:b.NotRange.Usage;
        if(!d.layout.add(b.ReportID,low,high))return false;
    }
    d.report_bytes=caps.InputReportByteLength;d.max_buttons=HidP_MaxUsageListLength(HidP_Input,9,d.data());
    return d.layout.valid()&&d.max_buttons&&d.max_buttons<=buzz_hid::max_usages;
}
void add(Device candidate) {
    if(find(candidate.handle)>=0)return;
    for(const auto& d:devices)if(d.active&&d.key==candidate.key)return;
    int slot=-1;
    for(unsigned i=0;i<devices.size();++i)if(!devices[i].active&&devices[i].key==candidate.key){slot=int(i);break;}
    if(slot<0)for(unsigned i=0;i<devices.size();++i)if(!devices[i].active){slot=int(i);break;}
    if(slot<0||!capabilities(candidate)) {
        if(ignored.size()<256)ignored.insert(candidate.handle);
        fprintf(stderr,"[buzz-hid] ignored supported VID/PID: %s\n",slot<0?"two receiver slots already occupied":"unsupported/invalid HID capabilities");return;
    }
    candidate.active=true;devices[unsigned(slot)]=std::move(candidate);
    buzz_input_physical_state(unsigned(slot),0,true);
    fprintf(stderr,"[buzz-hid] receiver %d connected: 054c:%04x -> P%d-P%d (hardware-unverified)\n",slot+1,devices[unsigned(slot)].product,slot*4+1,slot*4+4);
}
void rescan() {
    UINT count=0;if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1)||count>4096)return;
    std::vector<RAWINPUTDEVICELIST> list(count);
    UINT copied=count?GetRawInputDeviceList(list.data(),&count,sizeof(RAWINPUTDEVICELIST)):0;
    if(copied==UINT(-1)||copied>list.size())return;
    std::vector<Device> candidates;
    for(UINT i=0;i<copied;++i)if(list[i].dwType==RIM_TYPEHID&&find(list[i].hDevice)<0) {
        Device d;if(identify(list[i].hDevice,d))candidates.push_back(std::move(d));
    }
    std::sort(candidates.begin(),candidates.end(),[](const Device& a,const Device& b){return a.key<b.key;});
    for(auto& d:candidates)add(std::move(d));
}
void attach(HWND window) {
    if(owner==window)return;
    shutdown_locked();owner=window;
    const char* enabled=getenv("BUZZ_PHYSICAL_BUZZERS");
    if(enabled&&std::string(enabled)=="0"){fprintf(stderr,"[buzz-hid] disabled by developer override\n");return;}
    // Only joystick/gamepad collections, never keyboard or mouse. INPUTSINK
    // observes releases in the background; the input router never sends them
    // to gameplay while unfocused. No NOLEGACY/capture/exclusive flags.
    RAWINPUTDEVICE registrations[]={{1,4,RIDEV_DEVNOTIFY|RIDEV_INPUTSINK,window},{1,5,RIDEV_DEVNOTIFY|RIDEV_INPUTSINK,window}};
    if(!RegisterRawInputDevices(registrations,2,sizeof(RAWINPUTDEVICE))){fprintf(stderr,"[buzz-hid] RawInput registration failed (%lu)\n",GetLastError());return;}
    registered=true;fprintf(stderr,"[buzz-hid] RawInput ready; original receivers enabled (hardware-unverified)\n");rescan();
}
void report(unsigned slot,const uint8_t* bytes,size_t count) {
    auto& d=devices[slot];std::array<uint8_t,buzz_hid::max_report_bytes> prepared{};uint8_t id=0;
    // Unrelated numbered reports (e.g. status) are not button releases.
    if(count&&!d.layout.unnumbered()&&d.layout.index(bytes[0])<0)return;
    if(!buzz_hid::prepare_report(d.layout,d.report_bytes,bytes,count,prepared,id)){suspend(slot);return;}
    std::array<USAGE,buzz_hid::max_usages> usages{};ULONG length=d.max_buttons;
    const auto result=HidP_GetUsages(HidP_Input,9,0,usages.data(),&length,d.data(),reinterpret_cast<PCHAR>(prepared.data()),d.report_bytes);
    uint32_t buttons=0;
    if(result!=HIDP_STATUS_SUCCESS||length>d.max_buttons||!d.layout.update(id,usages.data(),length,buttons)){suspend(slot);return;}
    if(d.faulted) {
        const uint32_t bit=1u<<d.layout.index(id);
        if(!d.layout.report_state(id))d.neutral_seen|=bit;else d.neutral_seen&=~bit;
        if(d.neutral_seen!=d.layout.all_reports())return;
        d.faulted=false;fprintf(stderr,"[buzz-hid] receiver %u recovered after neutral input\n",slot+1);
    }
    buzz_input_physical_state(slot,buttons,true);
}
void input(HRAWINPUT handle) {
    RAWINPUTHEADER header{};UINT size=sizeof(header);
    if(GetRawInputData(handle,RID_HEADER,&header,&size,sizeof(header))!=sizeof(header)||header.dwType!=RIM_TYPEHID)return;
    int slot=find(header.hDevice);
    if(slot<0) {
        if(ignored.count(header.hDevice))return;
        Device candidate;if(identify(header.hDevice,candidate))add(std::move(candidate));
        else if(ignored.size()<256)ignored.insert(header.hDevice);
        slot=find(header.hDevice);if(slot<0)return;
    }
    size=0;
    constexpr size_t offset=offsetof(RAWINPUT,data.hid.bRawData);
    if(GetRawInputData(handle,RID_INPUT,nullptr,&size,sizeof(header))==UINT(-1)||size<offset||size>65536){suspend(unsigned(slot));return;}
    raw_buffer.resize((size+sizeof(DWORD)-1)/sizeof(DWORD));
    const UINT copied=GetRawInputData(handle,RID_INPUT,raw_buffer.data(),&size,sizeof(header));
    if(copied==UINT(-1)||copied<offset||copied>raw_buffer.size()*sizeof(DWORD)){suspend(unsigned(slot));return;}
    const auto& raw=*reinterpret_cast<const RAWINPUT*>(raw_buffer.data());
    const auto& hid=raw.data.hid;
    if(raw.header.dwType!=RIM_TYPEHID||raw.header.hDevice!=header.hDevice||raw.header.dwSize>copied||raw.header.dwSize<offset||
       !hid.dwSizeHid||hid.dwSizeHid>buzz_hid::max_report_bytes||!hid.dwCount||hid.dwCount>512||hid.dwCount>(raw.header.dwSize-offset)/hid.dwSizeHid){suspend(unsigned(slot));return;}
    for(unsigned i=0;i<hid.dwCount;++i)report(unsigned(slot),hid.bRawData+size_t(i)*hid.dwSizeHid,hid.dwSizeHid);
}
void shutdown_locked() {
    if(registered.exchange(false)) {
        RAWINPUTDEVICE registrations[]={{1,4,RIDEV_REMOVE,nullptr},{1,5,RIDEV_REMOVE,nullptr}};
        RegisterRawInputDevices(registrations,2,sizeof(RAWINPUTDEVICE));
    }
    for(unsigned i=0;i<devices.size();++i)remove(i);
    ignored.clear();raw_buffer.clear();owner=nullptr;
}
}
void buzz_hid_host_message(void* window,unsigned message,uintptr_t wparam,intptr_t lparam) {
    std::lock_guard lock(hid_mutex);
    try {
        if(message==WM_CREATE)attach(static_cast<HWND>(window));
        if(window!=owner||!registered)return;
        if(message==WM_INPUT)input(reinterpret_cast<HRAWINPUT>(lparam));
        else if(message==WM_INPUT_DEVICE_CHANGE) {
            ignored.erase(reinterpret_cast<HANDLE>(lparam));
            if(wparam==GIDC_REMOVAL){int slot=find(reinterpret_cast<HANDLE>(lparam));if(slot>=0)remove(unsigned(slot));}
            rescan();
        }else if(message==WM_DESTROY)shutdown_locked();
    }catch(const std::exception& e){fprintf(stderr,"[buzz-hid] input stopped: %s\n",e.what());shutdown_locked();}
}
void buzz_hid_host_shutdown() {
    std::lock_guard lock(hid_mutex);shutdown_locked();
}
bool buzz_hid_host_registered(){return registered;}
#else
void buzz_hid_host_message(void*,unsigned,uintptr_t,intptr_t){}
void buzz_hid_host_shutdown(){}
bool buzz_hid_host_registered(){return false;}
#endif
