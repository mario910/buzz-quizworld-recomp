// Real RawInput adapter logic, synthetic Windows/HidP API reads. No device
// registration, OS messages, HID writes or physical hardware are used here.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
struct FakeDevice {HANDLE handle;unsigned vendor,product;std::wstring name;bool online=true;USHORT usage=4;};
std::vector<FakeDevice> fake_devices;
std::array<uint32_t,2> fake_buttons{};
std::array<bool,2> fake_online{};
std::array<uint16_t,4> fake_pad_buttons{};
std::array<bool,4> fake_pad_online{};
unsigned pad_events=0;
std::vector<uint8_t> fake_packet;
unsigned register_count=0,remove_count=0,body_reads=0,button_events=0;
bool fake_bad_caps=false;
HANDLE device_handle(unsigned id){return reinterpret_cast<HANDLE>(uintptr_t(id));}
FakeDevice* fake_device(HANDLE h){for(auto& d:fake_devices)if(d.handle==h&&d.online)return &d;return nullptr;}
FakeDevice& fake_device_any(HANDLE h){for(auto& d:fake_devices)if(d.handle==h)return d;throw std::runtime_error("No such fake device");}
BOOL fake_register(PCRAWINPUTDEVICE list,UINT count,UINT size){
    check(count==2&&size==sizeof(RAWINPUTDEVICE),"Registration dimensions");
    for(unsigned i=0;i<count;++i) {
        check(list[i].usUsagePage==1&&list[i].usUsage==4+i,"Never register keyboards/mice");
        if(list[i].dwFlags==RIDEV_REMOVE){check(!list[i].hwndTarget,"Remove target must be null");++remove_count;}
        else {check(list[i].dwFlags==(RIDEV_INPUTSINK|RIDEV_DEVNOTIFY),"No capture/exclusive/legacy suppression flags");check(list[i].hwndTarget!=nullptr,"Targeted to own window");++register_count;}
    }
    return TRUE;
}
UINT fake_list(PRAWINPUTDEVICELIST out,PUINT count,UINT size){
    check(size==sizeof(RAWINPUTDEVICELIST),"Device list element size");
    UINT needed=0;for(auto& d:fake_devices)if(d.online)++needed;
    if(!out){*count=needed;return 0;}if(*count<needed){*count=needed;return UINT(-1);}
    unsigned i=0;for(auto& d:fake_devices)if(d.online)out[i++]={d.handle,RIM_TYPEHID};*count=needed;return needed;
}
UINT fake_info(HANDLE h,UINT command,LPVOID output,PUINT size){
    auto* d=fake_device(h);if(!d)return UINT(-1);
    if(command==RIDI_DEVICEINFO){
        if(*size<sizeof(RID_DEVICE_INFO))return UINT(-1);
        auto& info=*static_cast<RID_DEVICE_INFO*>(output);check(info.cbSize==sizeof(info),"Device info cbSize");
        info.dwType=RIM_TYPEHID;info.hid.dwVendorId=d->vendor;info.hid.dwProductId=d->product;info.hid.usUsagePage=1;info.hid.usUsage=d->usage;
        return sizeof(info);
    }
    if(command==RIDI_DEVICENAME){UINT needed=UINT(d->name.size()+1);if(!output){*size=needed;return 0;}if(*size<needed)return UINT(-1);std::memcpy(output,d->name.c_str(),needed*sizeof(wchar_t));return needed;}
    if(command==RIDI_PREPARSEDDATA){if(!output){*size=sizeof(DWORD);return 0;}if(*size<sizeof(DWORD))return UINT(-1);*static_cast<DWORD*>(output)=0xb002;return sizeof(DWORD);}
    return UINT(-1);
}
NTSTATUS fake_caps(PHIDP_PREPARSED_DATA,HIDP_CAPS* caps){*caps={};caps->InputReportByteLength=6;caps->NumberInputButtonCaps=1;return HIDP_STATUS_SUCCESS;}
NTSTATUS fake_button_caps(HIDP_REPORT_TYPE type,PHIDP_BUTTON_CAPS caps,PUSHORT count,PHIDP_PREPARSED_DATA){
    check(type==HidP_Input&&*count>=1,"Button capability request");*caps={};caps->UsagePage=9;caps->IsRange=TRUE;
    caps->Range.UsageMin=1;caps->Range.UsageMax=fake_bad_caps?19:20;*count=1;return HIDP_STATUS_SUCCESS;
}
ULONG fake_max(HIDP_REPORT_TYPE type,USAGE page,PHIDP_PREPARSED_DATA){check(type==HidP_Input&&page==9,"Only button-page input");return 20;}
NTSTATUS fake_usages(HIDP_REPORT_TYPE type,USAGE page,USHORT collection,PUSAGE usages,PULONG count,PHIDP_PREPARSED_DATA,PCHAR report,ULONG length){
    check(type==HidP_Input&&page==9&&collection==0&&length==6&&report[0]==0,"Normalized HidP request");
    const auto* bytes=reinterpret_cast<const uint8_t*>(report);uint32_t mask=bytes[3]|(uint32_t(bytes[4])<<8)|((uint32_t(bytes[5])&15)<<16);
    ULONG n=0;for(unsigned i=0;i<20;++i)if(mask&(1u<<i)){check(n<*count,"Usage buffer capacity");usages[n++]=USHORT(i+1);}*count=n;return HIDP_STATUS_SUCCESS;
}
UINT fake_raw(HRAWINPUT,UINT command,LPVOID data,PUINT size,UINT header_size){
    check(header_size==sizeof(RAWINPUTHEADER),"Header ABI");
    if(command==RID_HEADER){check(*size>=sizeof(RAWINPUTHEADER),"Header buffer");std::memcpy(data,fake_packet.data(),sizeof(RAWINPUTHEADER));return sizeof(RAWINPUTHEADER);}
    check(command==RID_INPUT,"Raw input command");
    if(!data){*size=UINT(fake_packet.size());return 0;}if(*size<fake_packet.size())return UINT(-1);
    ++body_reads;std::memcpy(data,fake_packet.data(),fake_packet.size());*size=UINT(fake_packet.size());return *size;
}
void make_packet(HANDLE device,const std::vector<uint32_t>& masks){
    const auto offset=offsetof(RAWINPUT,data.hid.bRawData);fake_packet.assign(offset+5*masks.size(),0);
    auto& packet=*reinterpret_cast<RAWINPUT*>(fake_packet.data());packet.header.dwType=RIM_TYPEHID;packet.header.dwSize=UINT(fake_packet.size());packet.header.hDevice=device;
    packet.data.hid.dwCount=DWORD(masks.size());packet.data.hid.dwSizeHid=5;
    for(size_t i=0;i<masks.size();++i){auto* bytes=packet.data.hid.bRawData+i*5;bytes[0]=bytes[1]=0x7f;bytes[2]=uint8_t(masks[i]);bytes[3]=uint8_t(masks[i]>>8);bytes[4]=uint8_t((masks[i]>>16)|0xf0);}
}
}
void buzz_input_physical_state(unsigned slot,uint32_t buttons,bool online){check(slot<2,"Receiver slot bound");fake_buttons[slot]=buttons;fake_online[slot]=online;++button_events;}
void buzz_input_hid_pad(unsigned index,uint16_t buttons,bool online){check(index<4,"Sony pad index bound");fake_pad_buttons[index]=buttons;fake_pad_online[index]=online;++pad_events;}
// One RawInput packet holding several same-size reports, as Windows delivers them.
void make_raw_packet(HANDLE device,const std::vector<std::vector<uint8_t>>& reports){
    const auto offset=offsetof(RAWINPUT,data.hid.bRawData);const size_t stride=reports.at(0).size();
    fake_packet.assign(offset+stride*reports.size(),0);
    auto& packet=*reinterpret_cast<RAWINPUT*>(fake_packet.data());packet.header.dwType=RIM_TYPEHID;packet.header.dwSize=UINT(fake_packet.size());packet.header.hDevice=device;
    packet.data.hid.dwCount=DWORD(reports.size());packet.data.hid.dwSizeHid=DWORD(stride);
    for(size_t i=0;i<reports.size();++i){check(reports[i].size()==stride,"Equal report stride");std::memcpy(packet.data.hid.bRawData+i*stride,reports[i].data(),stride);}
}
std::vector<uint8_t> sony_report(uint8_t id,size_t size,size_t offset,uint8_t face,uint8_t shoulders){
    std::vector<uint8_t> r(size,0x80);r[0]=id;r[offset]=face;r[offset+1]=shoulders;r[offset+2]=0;return r;
}
#define RegisterRawInputDevices fake_register
#define GetRawInputDeviceList fake_list
#define GetRawInputDeviceInfoW fake_info
#define GetRawInputData fake_raw
#define HidP_GetCaps fake_caps
#define HidP_GetButtonCaps fake_button_caps
#define HidP_MaxUsageListLength fake_max
#define HidP_GetUsages fake_usages
#include "../port/buzzer_hid_win32.cpp"
#undef RegisterRawInputDevices
#undef GetRawInputDeviceList
#undef GetRawInputDeviceInfoW
#undef GetRawInputData
#undef HidP_GetCaps
#undef HidP_GetButtonCaps
#undef HidP_MaxUsageListLength
#undef HidP_GetUsages
int main(){try {
    _putenv_s("BUZZ_PHYSICAL_BUZZERS","");
    const auto a=device_handle(1),b=device_handle(2),other=device_handle(3);
    fake_devices={{b,0x054c,0x1000,L"receiver-B"},{other,0x1234,0x0002,L"not-a-buzzer"},{a,0x054c,0x0002,L"receiver-A"}};
    const auto window=reinterpret_cast<void*>(uintptr_t(100));
    buzz_hid_host_message(window,WM_CREATE,0,0);
    check(buzz_hid_host_registered()&&register_count==2&&fake_online[0]&&fake_online[1],"Both known receiver IDs attached");
    auto send=[&](HANDLE h,std::vector<uint32_t> masks){make_packet(h,masks);buzz_hid_host_message(window,WM_INPUT,0,123);};
    send(a,{1});send(b,{1u<<19});check(fake_buttons[0]==1&&fake_buttons[1]==(1u<<19),"Stable sorted receiver assignment");
    const auto before=body_reads;send(other,{0xfffff});check(body_reads==before,"Unrelated controller payload must not be read");
    send(a,{16,0});check(fake_buttons[0]==0,"A packet can contain multiple down/up reports");
    send(a,{1});make_packet(a,{1});reinterpret_cast<RAWINPUT*>(fake_packet.data())->data.hid.dwCount=999999;
    buzz_hid_host_message(window,WM_INPUT,0,123);check(!fake_online[0]&&fake_buttons[0]==0,"Malformed report suspends and releases input");
    send(a,{1});check(!fake_online[0],"Recovery must not reinterpret a still-held answer");
    send(a,{0});check(fake_online[0]&&fake_buttons[0]==0,"Neutral report recovers the receiver");
    send(a,{2});check(fake_buttons[0]==2,"Fresh answer after recovery");
    make_packet(a,{1});reinterpret_cast<RAWINPUT*>(fake_packet.data())->header.dwSize+=1000;
    buzz_hid_host_message(window,WM_INPUT,0,123);check(!fake_online[0],"Declared size cannot exceed copied buffer");send(a,{0});
    make_packet(a,{1});fake_packet.resize(sizeof(RAWINPUTHEADER));
    buzz_hid_host_message(window,WM_INPUT,0,123);check(!fake_online[0],"A header-only packet cannot be decoded as a HID body");send(a,{0});
    for(DWORD bad_size:{DWORD(0),DWORD(1025),DWORD(0xffffffff)}) {
        make_packet(a,{1});reinterpret_cast<RAWINPUT*>(fake_packet.data())->data.hid.dwSizeHid=bad_size;
        buzz_hid_host_message(window,WM_INPUT,0,123);check(!fake_online[0],"Invalid stride cannot reach HID decoding");send(a,{0});
    }
    check(fake_online[0],"Bounds failures remain recoverable without reconnecting");
    fake_device(a)->online=false;buzz_hid_host_message(window,WM_INPUT_DEVICE_CHANGE,GIDC_REMOVAL,reinterpret_cast<intptr_t>(a));
    check(!fake_online[0]&&fake_online[1],"Removing receiver one leaves receiver two connected");
    send(b,{4});check(fake_buttons[1]==4&&!fake_online[0],"Remaining receiver is never renumbered");
    fake_devices[2].online=true;buzz_hid_host_message(window,WM_INPUT_DEVICE_CHANGE,GIDC_ARRIVAL,reinterpret_cast<intptr_t>(a));
    send(a,{8});check(fake_buttons[0]==8&&fake_online[1],"Reconnect reclaims original slot");
    buzz_hid_host_shutdown();check(!buzz_hid_host_registered()&&!fake_online[0]&&!fake_online[1]&&remove_count==2,"Teardown removes only own joystick/gamepad registrations");
    fake_bad_caps=true;buzz_hid_host_message(window,WM_CREATE,0,0);check(!fake_online[0]&&!fake_online[1],"Incomplete descriptors are rejected");buzz_hid_host_shutdown();fake_bad_caps=false;
    // DualShock 4 (USB) and DualSense (Bluetooth): read through the same
    // registration, published as XInput-style buttons, never as receivers.
    const auto ds4=device_handle(4),ds5=device_handle(5);
    fake_devices.push_back({ds5,0x054c,0x0ce6,L"\\\\?\\HID#{00001124-0000-1000-8000-00805F9B34FB}_VID&0002054C_PID&0CE6#9&1&0000",true,5});
    fake_devices.push_back({ds4,0x054c,0x05c4,L"\\\\?\\HID#VID_054C&PID_05C4#8&1&0000",true,5});
    buzz_hid_host_message(window,WM_CREATE,0,0);
    check(fake_online[0]&&fake_online[1]&&fake_pad_online[0]&&fake_pad_online[1]&&!fake_pad_online[2],"Receivers and both Sony pads attached");
    const auto receiver_events=button_events;
    auto send_pad=[&](HANDLE h,std::vector<std::vector<uint8_t>> reports){make_raw_packet(h,reports);buzz_hid_host_message(window,WM_INPUT,0,123);};
    send_pad(ds4,{sony_report(0x01,64,5,0x08,0x02)});check(fake_pad_buttons[0]==0x200&&!fake_pad_buttons[1],"DS4 USB R1 -> RB on sorted index 0");
    send_pad(ds4,{sony_report(0x01,64,5,0x00,0x00),sony_report(0x01,64,5,0x28,0x00)});check(fake_pad_buttons[0]==0x1000,"Several DS4 reports per packet: last state wins");
    send_pad(ds5,{sony_report(0x31,78,9,0x06,0x20)});check(fake_pad_buttons[1]==(0x4|0x10),"DualSense Bluetooth full report: left + Options");
    send_pad(ds5,{sony_report(0x01,78,5,0x08,0x01)});check(fake_pad_buttons[1]==0x100,"DualSense Bluetooth simple report: L1");
    const auto before_unrelated=pad_events;send_pad(ds5,{sony_report(0x05,78,9,0x00,0xff)});
    check(pad_events==before_unrelated&&fake_pad_buttons[1]==0x100,"Unrelated pad reports keep the previous state");
    check(button_events==receiver_events&&fake_buttons[0]==0&&fake_buttons[1]==0,"Pad input never reaches buzz receivers");
    make_raw_packet(ds4,{sony_report(0x01,64,5,0x28,0x00)});reinterpret_cast<RAWINPUT*>(fake_packet.data())->data.hid.dwCount=999999;
    buzz_hid_host_message(window,WM_INPUT,0,123);check(fake_pad_online[0]&&fake_pad_buttons[0]==0,"Malformed pad packet releases buttons, keeps the pad");
    send_pad(ds4,{sony_report(0x01,64,5,0x28,0x00)});check(fake_pad_buttons[0]==0x1000,"Next valid report recovers the pad");
    fake_device(ds4)->online=false;buzz_hid_host_message(window,WM_INPUT_DEVICE_CHANGE,GIDC_REMOVAL,reinterpret_cast<intptr_t>(ds4));
    check(!fake_pad_online[0]&&fake_pad_buttons[0]==0&&fake_pad_online[1]&&fake_online[0],"Unplugging the DS4 leaves the rest");
    fake_device_any(ds4).online=true;buzz_hid_host_message(window,WM_INPUT_DEVICE_CHANGE,GIDC_ARRIVAL,reinterpret_cast<intptr_t>(ds4));
    check(fake_pad_online[0],"Replug reclaims the same pad index");
    buzz_hid_host_shutdown();check(!fake_pad_online[0]&&!fake_pad_online[1],"Teardown disconnects Sony pads");
    _putenv_s("BUZZ_SONY_PADS","0");buzz_hid_host_message(window,WM_CREATE,0,0);
    check(fake_online[0]&&!fake_pad_online[0]&&!fake_pad_online[1],"BUZZ_SONY_PADS=0 keeps only receivers");buzz_hid_host_shutdown();
    _putenv_s("BUZZ_SONY_PADS","");_putenv_s("BUZZ_PHYSICAL_BUZZERS","0");buzz_hid_host_message(window,WM_CREATE,0,0);
    check(buzz_hid_host_registered()&&!fake_online[0]&&!fake_online[1]&&fake_pad_online[0]&&fake_pad_online[1],"Disabling receivers keeps Sony pads");buzz_hid_host_shutdown();
    _putenv_s("BUZZ_SONY_PADS","0");const auto prior=register_count;buzz_hid_host_message(window,WM_CREATE,0,0);
    check(!buzz_hid_host_registered()&&register_count==prior,"Diagnostic disable of both never registers devices");buzz_hid_host_shutdown();
    std::cout<<"PASS real RawInput adapter with synthetic APIs: bounded reports, descriptor validation, two receivers, focus-safe registration, hotplug, fault recovery, device filtering and DualShock 4/DualSense pads\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
