#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_channel_setup.h"
#include "spu_dma.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

static uint32_t random_word(uint32_t& value)
{
    value ^= value << 13; value ^= value >> 17; value ^= value << 5; return value;
}
template<unsigned Channel>
static void compare(uint32_t value, unsigned seed)
{
    auto native = std::make_unique<spu_context>();
    auto sdk = std::make_unique<spu_context>();
    std::memset(native.get(), int(seed & 255), sizeof(spu_context));
    std::memcpy(sdk.get(), native.get(), sizeof(spu_context));
    mfc_engine engine{};
    const auto previous = engine;
    assert(buzz::set_geometry_mfc_argument(*native, Channel, value));
    // Constant channel: the real SDK's non-command case has no backend effects.
    mfc_channel_write(&engine, sdk.get(), Channel, value);
    assert(!std::memcmp(native.get(), sdk.get(), sizeof(spu_context)));
    assert(!std::memcmp(&engine, &previous, sizeof(engine)));
}
int main()
{
    uint32_t random = 0x674741u;
    constexpr uint32_t corners[] = {0,1,15,16,31,32,127,128,16384,0x3ffff,
        0xffffffffu,0x80000000u,0xffffffe0u};
    for (unsigned i = 0; i < 1037; ++i) {
        const auto value = i < 13 ? corners[i] : random_word(random);
        compare<MFC_LSA>(value,i);compare<MFC_EAH>(value,i);compare<MFC_EAL>(value,i);
        compare<MFC_Size>(value,i);compare<MFC_TagID>(value,i);
    }
    auto native = std::make_unique<spu_context>();
    auto previous = std::make_unique<spu_context>();
    std::memset(native.get(), 0xa5, sizeof(spu_context));
    std::memcpy(previous.get(), native.get(), sizeof(spu_context));
    for (unsigned channel = 0; channel < 256; ++channel) {
        if (channel >= MFC_LSA && channel <= MFC_TagID) continue;
        assert(!buzz::set_geometry_mfc_argument(*native,channel,0x12345678));
        assert(!std::memcmp(native.get(),previous.get(),sizeof(spu_context)));
    }
    std::puts("PASS MFC argument setup:5185 complete SDK context comparisons;251 non-argument channels unchanged; engine unchanged");
}
