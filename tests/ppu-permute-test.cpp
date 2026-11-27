#include "../port/native_ppu_permute.h"
#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using Vector = std::array<uint8_t, 16>;
    for (unsigned selector = 0; selector < 256; ++selector) {
        for (unsigned right = 0; right < 2; ++right) {
            for (unsigned control = 0; control < 3; ++control) {
                for (unsigned dest = 0; dest < 4; ++dest) {
                    std::array<Vector, 4> regs{};
                    for (unsigned lane = 0; lane < 16; ++lane) {
                        regs[0][lane] = uint8_t(3 * lane + 47);
                        regs[1][lane] = uint8_t(131 - 7 * lane);
                        regs[control][lane] = uint8_t(selector + lane);
                    }
                    auto expected = regs;
                    for (unsigned lane = 0; lane < 16; ++lane) {
                        unsigned index = regs[control][lane] & 31;
                        expected[dest][lane] = regs[index < 16 ? 0 : right][index & 15];
                    }
                    if (right)
                        buzz::ppu_permute_bytes<false>(regs[dest], regs[0], regs[right], regs[control]);
                    else
                        buzz::ppu_permute_bytes<true>(regs[dest], regs[0], regs[right], regs[control]);
                    assert(regs == expected);
                }
            }
        }
    }
}
