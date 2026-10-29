#include "../port/buzzer_protocol.h"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace buzz_usb;
    assert((report({0,0,0,0})==std::array<uint8_t,5>{0x7f,0x7f,0,0,0xf0}));
    for(unsigned player=0;player<4;++player)for(unsigned key=0;key<5;++key){
        std::array<uint8_t,4> keys{};
        keys[player]=uint8_t(1u<<key);
        auto r=report(keys);
        uint32_t bits=r[2]|(uint32_t(r[3])<<8)|(uint32_t(r[4]&15)<<16);
        assert(bits==(1u<<(5*player+key)));
    }
    assert((report({31,31,31,31})==std::array<uint8_t,5>{0x7f,0x7f,0xff,0xff,0xff}));
    for(uint32_t mask=0;mask<0x200000u;++mask){
        auto r=report_mask(mask);
        uint32_t decoded=r[2]|(uint32_t(r[3])<<8)|(uint32_t(r[4]&15)<<16);
        assert(decoded==(mask&0xfffffu));
        assert(r[0]==0x7f && r[1]==0x7f && (r[4]&0xf0)==0xf0);
    }
    unsigned descriptors_seen=0;
    for(unsigned i=0;i<sizeof(descriptors);){
        assert(descriptors[i]>=2 && i+descriptors[i]<=sizeof(descriptors));
        i+=descriptors[i];++descriptors_seen;
    }
    assert(descriptors_seen==5 && sizeof(descriptors)==52);
    assert((descriptors[8]|(unsigned(descriptors[9])<<8))==0x054c);
    assert((descriptors[10]|(unsigned(descriptors[11])<<8))==0x1000);
    assert(descriptors[20]==sizeof(descriptors)-18);
    std::puts("buzzer protocol: all 20-bit simultaneous masks, high-bit truncation + USB descriptors OK");
}
