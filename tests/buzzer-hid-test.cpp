#include "../port/buzzer_hid.h"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok){if(!ok)throw std::runtime_error("HID usage/report contract regression");}
}
int main(){try {
    using namespace buzz_hid;
    check(supported(0x054c,0x0002)&&supported(0x054c,0x1000));
    check(!supported(0x054c,0x0268)&&!supported(0x1234,0x0002));
    uint32_t output=123;
    for(uint16_t u=1;u<=20;++u){check(usages_to_buttons(&u,1,output));check(output==(1u<<(u-1)));}
    uint16_t all[20];for(unsigned i=0;i<20;++i)all[i]=uint16_t(i+1);
    check(usages_to_buttons(all,20,output)&&output==button_mask);
    uint16_t extra[]={0,1,1,21,65535};check(usages_to_buttons(extra,5,output)&&output==1);
    const auto old=output;check(!usages_to_buttons(nullptr,1,output)&&output==old);
    check(!usages_to_buttons(all,max_usages+1,output)&&output==old);
    check(usages_to_buttons(nullptr,0,output)&&output==0);
    Layout plain;check(plain.add(0,1,20)&&plain.valid()&&plain.unnumbered());
    std::array<uint8_t,max_report_bytes> prepared{};uint8_t id=99;
    uint8_t raw[]={0x7f,0x7f,1,0,0xf0};
    check(prepare_report(plain,6,raw,5,prepared,id)&&id==0&&prepared[0]==0&&prepared[1]==0x7f);
    uint8_t prefixed[]={0,0x7f,0x7f,1,0,0xf0};
    check(prepare_report(plain,6,prefixed,6,prepared,id));
    check(!prepare_report(plain,6,raw,4,prepared,id));check(!prepare_report(plain,5,raw,5,prepared,id));
    check(!prepare_report(plain,1025,raw,5,prepared,id));check(!prepare_report(plain,6,nullptr,5,prepared,id));
    Layout split;check(split.add(1,1,10)&&split.add(2,11,20)&&split.valid());
    uint16_t first[]={1,10},second[]={11,20};
    check(split.update(1,first,2,output)&&output==((1<<0)|(1<<9)));
    check(split.update(2,second,2,output)&&output==((1<<0)|(1<<9)|(1<<10)|(1<<19)));
    check(split.update(1,nullptr,0,output)&&output==((1<<10)|(1<<19))); // report 1 cannot release report 2
    const auto before=output;check(!split.update(3,first,2,output)&&output==before);
    check(!split.update(1,second,2,output)&&output==before&&split.state()==before);
    uint8_t numbered[]={2,0,0,0,0,0};check(prepare_report(split,6,numbered,6,prepared,id)&&id==2);
    check(!prepare_report(split,6,numbered,5,prepared,id));
    numbered[0]=3;check(!prepare_report(split,6,numbered,6,prepared,id));
    check(split.all_reports()==3&&split.index(2)==1);split.clear();check(split.state()==0);
    Layout incomplete;check(incomplete.add(0,1,19)&&!incomplete.valid());
    Layout overlap;check(overlap.add(1,1,15)&&!overlap.add(2,10,20));
    Layout mixed;check(mixed.add(0,1,10)&&mixed.add(1,11,20)&&!mixed.valid());
    Layout too_many;for(uint8_t i=1;i<=8;++i)check(too_many.add(i,i,i));check(!too_many.add(9,9,9));
    // Bounded deterministic malformed-input sweep, also run under ASan/UBSan.
    std::array<uint8_t,1025> noise{};uint32_t random=0x1256abcd;
    for(unsigned sample=0;sample<4096;++sample) {
        for(auto& byte:noise){random=random*1664525u+1013904223u;byte=uint8_t(random>>24);}
        const size_t count=sample%1026,expected=(sample*17)%1026;
        const bool ok=prepare_report(plain,expected,noise.data(),count,prepared,id);
        if(ok)check(expected<=1024&&((count+1==expected&&id==0)||(count==expected&&noise[0]==0)));
    }
    std::cout<<"PASS HID usage mapping, IDs, complete capability coverage, multi-report isolation and strict packet normalization\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
