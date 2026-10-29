#pragma once
#include <array>
#include <cstdint>

namespace buzz_usb {
inline constexpr uint8_t descriptors[] = {
    18,1,0,2,0,0,0,8, 0x4c,0x05,0x00,0x10,0,1,2,1,0,1,
    9,2,34,0,1,1,0,0x80,50,
    9,4,0,0,1,3,0,0,0,
    9,0x21,0x11,1,0,1,0x22,78,0,
    7,5,0x81,3,8,0,10
};
// Each handset has five bits: red, yellow, green, orange, blue.
constexpr std::array<uint8_t,5> report(std::array<uint8_t,4> handsets)
{
    uint32_t bits=0;
    for (unsigned i=0;i<handsets.size();++i)
        bits |= uint32_t(handsets[i]&31u) << (5*i);
    return {0x7f,0x7f,uint8_t(bits),uint8_t(bits>>8),uint8_t(0xf0|(bits>>16))};
}
constexpr std::array<uint8_t,5> report_mask(uint32_t bits)
{
    return report({uint8_t(bits&31u),uint8_t((bits>>5)&31u),
                   uint8_t((bits>>10)&31u),uint8_t((bits>>15)&31u)});
}
}
