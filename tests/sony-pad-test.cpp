// DualShock 4 / DualSense report decoding and gamepad slot choice. Synthetic
// reports only; layouts follow Linux hid-playstation and SDL's HIDAPI drivers.
#include "../port/sony_pad.h"
#include "../port/buzzer_hid.h"
#include "../port/buzzer_input.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::vector<uint8_t> report(uint8_t id,size_t size,size_t offset,uint8_t face,uint8_t shoulders,uint8_t extra=0) {
    std::vector<uint8_t> r(size,0x80);r[0]=id;r[offset]=face;r[offset+1]=shoulders;r[offset+2]=extra;return r;
}
}
int main(){try {
    using namespace buzz_sony;
    check(model(0x054c,0x05c4)==Model::ds4&&model(0x054c,0x09cc)==Model::ds4,"DualShock 4 v1/v2");
    check(model(0x054c,0x0ce6)==Model::dualsense&&model(0x054c,0x0df2)==Model::dualsense,"DualSense / Edge");
    // Buzz receivers stay buzz receivers; PS3 pads, the DS4 dongle and foreign VIDs are not Sony pads here.
    for(uint32_t product:{0x0002u,0x1000u,0x0268u,0x0ba0u})check(model(0x054c,product)==Model::none,"Non-pad Sony product");
    check(model(0x045e,0x05c4)==Model::none,"Vendor must be Sony");
    for(uint32_t product:{0x05c4u,0x09ccu,0x0ce6u,0x0df2u})check(!buzz_hid::supported(0x054c,product),"Buzz receiver filter must not claim pads");

    check(bluetooth_path(L"\\\\?\\hid#{00001124-0000-1000-8000-00805f9b34fb}_vid&0002054c_pid&05c4#9&1&0000#{4d1e55b2}"),"Bluetooth HID path");
    check(!bluetooth_path(L"\\\\?\\hid#vid_054c&pid_09cc&mi_03#8&1&0000#{4d1e55b2}"),"USB HID path");

    // Hat: 0..7 clockwise from up, 8 neutral, junk ignored.
    const uint16_t hats[]={up,up|right,right,down|right,down,down|left,left,up|left,0,0,0,0,0,0,0,0};
    for(uint8_t h=0;h<16;++h)check(to_xinput(h,0)==hats[h],"Hat decoding");
    check(to_xinput(0x18,0)==x&&to_xinput(0x28,0)==a&&to_xinput(0x48,0)==b&&to_xinput(0x88,0)==y,"Square/Cross/Circle/Triangle");
    check(to_xinput(8,0x01)==lb&&to_xinput(8,0x02)==rb,"L1/R1");
    check(to_xinput(8,0x04|0x08)==0,"L2/R2 are analog triggers, like XInput");
    check(to_xinput(8,0x10)==back&&to_xinput(8,0x20)==start,"Share/Create -> BACK, Options -> START");
    check(to_xinput(8,0x40)==ls&&to_xinput(8,0x80)==rs,"L3/R3");

    struct Case {Model m;bool bt;uint8_t id;size_t size,offset;const char* what;};
    const Case cases[]={
        {Model::ds4,false,0x01,64,5,"DS4 USB"},{Model::ds4,true,0x01,10,5,"DS4 Bluetooth simple"},
        {Model::ds4,true,0x11,78,7,"DS4 Bluetooth full"},{Model::dualsense,false,0x01,64,8,"DualSense USB"},
        {Model::dualsense,true,0x01,10,5,"DualSense Bluetooth simple"},{Model::dualsense,true,0x31,78,9,"DualSense Bluetooth full"}};
    for(const auto& c:cases) {
        uint16_t buttons=0xffff;
        auto r=report(c.id,c.size,c.offset,8,0);check(parse(c.m,c.bt,r.data(),r.size(),buttons)&&buttons==0,c.what);
        r=report(c.id,c.size,c.offset,0x22,0x02,0x01);  // right + cross, R1, PS
        check(parse(c.m,c.bt,r.data(),r.size(),buttons)&&buttons==(right|a|rb),c.what);
        // Truncated reports and foreign report IDs are ignored, never misread.
        const auto old=buttons;
        check(!parse(c.m,c.bt,r.data(),c.offset+2,buttons)&&buttons==old,"Truncated report");
        r[0]=0x05;check(!parse(c.m,c.bt,r.data(),r.size(),buttons)&&buttons==old,"Unrelated report ID");
    }
    uint16_t buttons=0;
    check(!parse(Model::none,false,report(1,64,5,0x20,0).data(),64,buttons),"Unknown model");
    check(!parse(Model::ds4,false,nullptr,64,buttons)&&!parse(Model::ds4,false,report(1,64,5,8,0).data(),0,buttons),"Empty input");

    // Default buzzer preset (RPCS3/PCSX2): L1 or R1 red, Triangle/Square/Circle/Cross
    // = blue/orange/green/yellow, the handset's colours from top to bottom.
    const auto defaults=buzz_input::defaults();
    const auto& map=defaults.pad_buttons;
    check(buzz_input::map_pad(to_xinput(8,0x01),map)==1u<<buzz_input::red,"L1 is red");
    check(buzz_input::map_pad(to_xinput(8,0x02),map)==1u<<buzz_input::red,"R1 is red too");
    check(buzz_input::map_pad(to_xinput(0x88,0),map)==1u<<buzz_input::blue,"Triangle is blue");
    check(buzz_input::map_pad(to_xinput(0x18,0),map)==1u<<buzz_input::orange,"Square is orange");
    check(buzz_input::map_pad(to_xinput(0x48,0),map)==1u<<buzz_input::green,"Circle is green");
    check(buzz_input::map_pad(to_xinput(0x28,0),map)==1u<<buzz_input::yellow,"Cross is yellow");
    check(buzz_input::map_pad(to_xinput(8,0x20),defaults.pad_buttons)==0,"Options (START) stays reserved for the PC menu");

    using buzz_input::pick_pad_slot;
    check(pick_pad_slot(0)==0&&pick_pad_slot(0b0001)==1&&pick_pad_slot(0b1011)==2&&pick_pad_slot(0b0111)==3,"Lowest free slot");
    check(pick_pad_slot(0b1111)==-1&&pick_pad_slot(0xff)==-1,"All four slots in use");
    std::cout<<"PASS DualShock 4 / DualSense: USB + Bluetooth reports, button mapping, buzzer filter isolation, slot choice\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
