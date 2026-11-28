#include "../port/spurs_geometry_input_copy.h"
#include "geometry-test-memory.h"
#include <cassert>
#include <initializer_list>
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
    // The 256-byte geometry descriptor ends exactly at the source guard.
    // Its context and reserved suffix must obey the SDK clear mode.
    const unsigned descriptor = capacity - 1024;
    const unsigned context = descriptor - 64;
    auto* ls = destination_end - capacity;
    for (int clear = 0; clear <= 1; ++clear) {
        memset(ls, 0xa5, capacity);
        buzz_spurs_geometry_descriptor256(ls, context, descriptor, capacity, source_end - 256, clear);
        assert(!memcmp(ls + descriptor, source_end - 256, 256));
        for (unsigned i = 0; i < capacity; ++i) {
            if (i >= descriptor && i < descriptor + 256) continue;
            assert(ls[i] == (clear && i >= context ? 0 : 0xa5));
        }
    }
    for (int clear = 0; clear <= 1; ++clear) {
        memset(ls, 0xa5, capacity);
        buzz_spurs_geometry_io_context256(ls + context, source_end - 256, clear, 0x12345678);
        assert(!memcmp(ls + descriptor, source_end - 256, 256));
        const uint8_t first[20] = {0x12,0x34,0x56,0x78};
        assert(!memcmp(ls + context, first, sizeof(first)));
        for (unsigned i = 0; i < capacity; ++i) {
            if ((i >= context && i < context + 20) || (i >= descriptor && i < descriptor + 256)) continue;
            assert(ls[i] == (clear && i >= context ? 0 : 0xa5));
        }
    }
    for (int family : {40, 41}) {
        for (unsigned size : {0u, 1u, 255u, 256u, 257u, 383u, 384u, 385u, 639u, 640u, 641u}) {
            auto* out = destination_end - size;
            out[-1] = 0xa5;
            buzz_spurs_copy_job_image(out, source_end - size, size, family);
            assert(out[-1] == 0xa5 && !memcmp(out, source_end - size, size));
            buzz_spurs_zero_job_region(out, size, family);
            assert(out[-1] == 0xa5);
            for (unsigned i = 0; i < size; ++i) assert(out[i] == 0);
        }
    }
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    puts("PASS all 32768 DMA copy lengths, guarded descriptor source and both context clear modes");
}
