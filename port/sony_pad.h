#pragma once
// DualShock 4 / DualSense input reports -> XInput-style button bits, so the
// Windows RawInput adapter can feed Sony pads (which are not XInput devices)
// through the same slot/mapping path as XInput controllers. Read-only: no
// feature/output reports are sent, so the pad stays in its default mode.
// Report layouts: Linux drivers/hid/hid-playstation.c and SDL's HIDAPI PS4/PS5
// drivers (USB report 0x01, Bluetooth "simple" 0x01, Bluetooth full 0x11/0x31).
#include <cstddef>
#include <cstdint>
#include <string>

namespace buzz_sony {
enum class Model : uint8_t { none, ds4, dualsense };
constexpr Model model(uint32_t vendor,uint32_t product) {
    if(vendor!=0x054c)return Model::none;
    switch(product) {
    case 0x05c4: case 0x09cc: return Model::ds4;        // DualShock 4 v1 / v2
    case 0x0ce6: case 0x0df2: return Model::dualsense;  // DualSense / DualSense Edge
    default: return Model::none; // incl. buzz receivers, PS3 pads, DS4 USB dongle
    }
}
constexpr const char* name(Model m){return m==Model::ds4?"DualShock 4":m==Model::dualsense?"DualSense":"unknown";}
// Windows HID device paths of Bluetooth Classic HID devices carry the HID
// service class GUID. The key passed in is already lower-case.
inline bool bluetooth_path(const std::wstring& lower_path) {
    return lower_path.find(L"00001124-0000-1000-8000-00805f9b34fb")!=std::wstring::npos;
}
// Offset of the three button bytes (hat+face, shoulders+menu, PS+touchpad),
// counted from the report-ID byte, or -1 for reports carrying no pad state.
constexpr int button_offset(Model m,bool bluetooth,uint8_t report_id) {
    if(m==Model::ds4)return report_id==0x01?5:report_id==0x11?7:-1;
    if(m==Model::dualsense)return report_id==0x01?(bluetooth?5:8):report_id==0x31?9:-1;
    return -1;
}
// XInput wButtons bits, matching buzz_input pad mapping names.
constexpr uint16_t up=0x1,down=0x2,left=0x4,right=0x8,start=0x10,back=0x20,ls=0x40,rs=0x80,
                   lb=0x100,rb=0x200,a=0x1000,b=0x2000,x=0x4000,y=0x8000;
constexpr uint16_t to_xinput(uint8_t face,uint8_t shoulders) {
    constexpr uint16_t hat[8]={up,up|right,right,down|right,down,down|left,left,up|left};
    uint16_t result=(face&15)<8?hat[face&15]:0; // 8 = neutral; 9..15 invalid
    if(face&0x10)result|=x;          // square
    if(face&0x20)result|=a;          // cross
    if(face&0x40)result|=b;          // circle
    if(face&0x80)result|=y;          // triangle
    if(shoulders&0x01)result|=lb;    // L1
    if(shoulders&0x02)result|=rb;    // R1
    if(shoulders&0x10)result|=back;  // Share / Create
    if(shoulders&0x20)result|=start; // Options
    if(shoulders&0x40)result|=ls;    // L3
    if(shoulders&0x80)result|=rs;    // R3
    return result;
}
// false: not a state report of this pad (ignore it, keep the previous state).
inline bool parse(Model m,bool bluetooth,const uint8_t* report,size_t count,uint16_t& buttons) {
    if(!report||!count)return false;
    const int offset=button_offset(m,bluetooth,report[0]);
    if(offset<0||count<size_t(offset)+3)return false;
    buttons=to_xinput(report[offset],report[offset+1]);
    return true;
}
}
