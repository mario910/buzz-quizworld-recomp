#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>

static std::array<uint32_t, 2048> memory{};
static uint32_t vm_read32(uint32_t address) { return memory.at(address / 4); }
#include "studio_effects.h"

static void write_words(uint32_t address, std::initializer_list<uint32_t> words)
{
    for (uint32_t word : words) {
        memory.at(address / 4) = word;
        address += 4;
    }
}

template<class Predicate>
static void check_mutations(uint32_t start, uint32_t count, Predicate admitted)
{
    for (uint32_t i = 0; i < count; ++i) {
        auto& word = memory.at(start / 4 + i);
        word ^= 1;
        assert(!admitted());
        word ^= 1;
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const bool balanced = std::strcmp(argv[1], "balanced") == 0;
    const bool effects = balanced || std::strcmp(argv[1], "effects") == 0;
    constexpr uint32_t program = 0x100, records = program + 0x3c0;
    write_words(program + 12, {198});
    write_words(program + 28, {0x3c0});
    write_words(records, {0x19, 0x4746585f, 0x52454e44, 0x45524d49, 0x58534554});

    for (uint32_t index : {134u, 163u}) {
        const uint32_t command = records + index * 16;
        const uint32_t extent = index == 134 ? 24 : 34;
        write_words(command, {0x1c, extent << 16, 0x123400u, 1, 5,
                              index == 134 ? 2u : 10u, index == 134 ? 0x70010u : 0xf0010u, 0xff});
        write_words(command + extent * 16, {0x18});
        auto admitted = [&] { return buzz::skip_studio_effect(program, records, index); };
        assert(admitted() == effects);
        assert(!buzz::skip_studio_effect(program, records + 4, index));
        check_mutations(program + 12, 1, admitted);
        check_mutations(program + 28, 1, admitted);
        check_mutations(records, 5, admitted);
        check_mutations(command, 8, admitted);
        check_mutations(command + extent * 16, 1, admitted);
    }

    const uint32_t base = records + 77 * 16, draw = records + 84 * 16;
    write_words(base, {0x0e, 0x8000, 0, 0});
    write_words(draw, {0x0f, 0x8000, 0, 0});
    for (const uint32_t marker : {draw - 32, draw + 16})
        write_words(marker, {marker < draw ? 0x19u : 0x1au, 0x4746585f, 0x41554449,
                             0x454e4345, 0x4c495450, 0x41535300});

    auto admitted = [&] { return buzz::skip_audience_light(program, records, 84); };
    assert(admitted() == balanced);
    check_mutations(base, 4, admitted);
    check_mutations(draw - 32, 6, admitted);
    check_mutations(draw, 4, admitted);
    check_mutations(draw + 16, 6, admitted);
    check_mutations(records, 5, admitted);
    for (uint32_t index = 0; index < 198; ++index) {
        if (index != 84) assert(!buzz::skip_audience_light(program, records, index));
        if (index != 134 && index != 163) assert(!buzz::skip_studio_effect(program, records, index));
    }
}
