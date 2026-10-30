#pragma once
// HID button-usage contract only. No OS handles, guest memory, copied report
// descriptors or device-driver code. Buttons 1..20 are four five-button sets:
// red, yellow, green, orange, blue. References: docs/physical-buzzers.md.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace buzz_hid {
constexpr uint32_t button_mask=0xfffff;
constexpr size_t max_report_bytes=1024,max_usages=256,max_reports=8;
constexpr bool supported(uint32_t vendor,uint32_t product) {
    return vendor==0x054c&&(product==0x0002||product==0x1000);
}
inline bool usages_to_buttons(const uint16_t* usages,size_t count,uint32_t& output) {
    if(count>max_usages||(!usages&&count))return false;
    uint32_t candidate=0;
    for(size_t i=0;i<count;++i)if(usages[i]>=1&&usages[i]<=20)candidate|=1u<<(usages[i]-1);
    output=candidate;return true;
}
class Layout {
    struct Group {uint8_t id=0;uint32_t allowed=0,state=0;};
    std::array<Group,max_reports> groups_{};
    size_t count_=0;
public:
    int index(uint8_t id)const{for(size_t i=0;i<count_;++i)if(groups_[i].id==id)return int(i);return -1;}
    // Capability ranges can be split across link collections. A usage must
    // not belong to two report IDs: that would ambiguously alias handsets.
    bool add(uint8_t id,uint16_t minimum,uint16_t maximum) {
        if(minimum>maximum)return false;
        uint32_t mask=0;
        for(unsigned usage=1;usage<=20;++usage)if(usage>=minimum&&usage<=maximum)mask|=1u<<(usage-1);
        if(!mask)return true;
        for(size_t i=0;i<count_;++i)if(groups_[i].id!=id&&(groups_[i].allowed&mask))return false;
        int at=index(id);
        if(at<0){if(count_==groups_.size())return false;at=int(count_++);groups_[size_t(at)].id=id;}
        groups_[size_t(at)].allowed|=mask;return true;
    }
    bool valid()const {
        uint32_t coverage=0;for(size_t i=0;i<count_;++i)coverage|=groups_[i].allowed;
        return coverage==button_mask&&!(count_>1&&index(0)>=0);
    }
    bool unnumbered()const{return count_==1&&groups_[0].id==0;}
    uint32_t all_reports()const{return (1u<<count_)-1;}
    uint32_t state()const{uint32_t result=0;for(size_t i=0;i<count_;++i)result|=groups_[i].state;return result;}
    uint32_t report_state(uint8_t id)const{int at=index(id);return at<0?0:groups_[size_t(at)].state;}
    void clear(){for(size_t i=0;i<count_;++i)groups_[i].state=0;}
    bool update(uint8_t id,const uint16_t* usages,size_t count,uint32_t& output) {
        int at=index(id);uint32_t buttons=0;
        if(!valid()||at<0||!usages_to_buttons(usages,count,buttons)||(buttons&~groups_[size_t(at)].allowed))return false;
        groups_[size_t(at)].state=buttons;output=state();return true;
    }
};
// HidP's input buffer includes the zero report-ID prefix even for an
// unnumbered report. RawInput can omit that prefix. Accept only these two
// unambiguous lengths; never guess offsets or pad truncated numbered reports.
inline bool prepare_report(const Layout& layout,size_t expected,const uint8_t* raw,size_t count,
                           std::array<uint8_t,max_report_bytes>& output,uint8_t& id) {
    if(!layout.valid()||!raw||!count||!expected||expected>output.size())return false;
    if(layout.unnumbered()&&count+1==expected) {
        output[0]=0;std::memcpy(output.data()+1,raw,count);id=0;return true;
    }
    if(count!=expected||layout.index(raw[0])<0)return false;
    std::memcpy(output.data(),raw,count);id=raw[0];return true;
}
}
