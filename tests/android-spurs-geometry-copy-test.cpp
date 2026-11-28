#include "../port/spurs_geometry_input_copy.h"
#include "geometry-test-memory.h"
#include <cassert>
#include <cstdio>

// Each SPURS size is a 15-bit byte count. End both spans immediately before
// guard pages so even a vector load past the last source byte is observable.
int main()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const unsigned page = info.dwPageSize;
    const unsigned capacity = ((32768u + page - 1) / page) * page;
    const unsigned region = capacity + page;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * region,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    auto* source_end = memory + capacity;
    auto* destination_end = memory + region + capacity;
    DWORD previous;
    assert(VirtualProtect(source_end, page, PAGE_NOACCESS, &previous));
    assert(VirtualProtect(destination_end, page, PAGE_NOACCESS, &previous));
    for (unsigned i = 0; i < capacity; ++i)
        memory[i] = static_cast<uint8_t>(i * 37u + (i >> 8));

    for (unsigned size = 0; size <= 32767; ++size) {
        auto* source = source_end - size;
        auto* destination = destination_end - size;
        // Switching lengths changes source alignment and fixed-copy dispatch.
        // Poison every destination byte so an omitted chunk cannot pass.
        for (unsigned i = 0; i < size; ++i)
            destination[i] = static_cast<uint8_t>(~source[i]);
        destination[-1] = 0xa5;
        buzz_spurs_geometry_input_copy(destination, source, size);
        assert(!memcmp(destination, source, size) && destination[-1] == 0xa5);
    }
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    puts("PASS all 32768 DMA copy lengths, exact source and destination ends");
}
