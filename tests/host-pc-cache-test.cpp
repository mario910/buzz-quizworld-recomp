#include "../ps3recomp/runtime/ppu/host_pc_cache.h"
#include <cassert>
#include <cstdio>
#include <thread>

int main()
{
    HostPcCache<64> cache;
    uint32_t out = 123;
    assert(!cache.get(0, out) && out == 123);
    cache.put(0, 0);
    assert(cache.get(0, out) && out == 0);
    const uintptr_t top = ~(uintptr_t)0;
    cache.put(top, 0xffffffffu);
    assert(cache.get(top, out) && out == 0xffffffffu);
    uintptr_t first = 0x12345678, collision = first + 1;
    while (HostPcCache<64>::bucket(first) != HostPcCache<64>::bucket(collision)) ++collision;
    cache.put(first, 1);
    cache.put(collision, 2);
    assert(!cache.get(first, out));
    assert(cache.get(collision, out) && out == 2);
    cache.put(collision, 0);
    assert(cache.get(collision, out) && out == 0);
    /* A different 64-bit host address with identical low bits must miss. */
    if (sizeof(uintptr_t) == 8) {
        const uintptr_t wide = first + ((uintptr_t)1 << 32);
        cache.put(first, 3);
        assert(!cache.get(wide, out));
    }
    for (uintptr_t n = 0; n < 1000000; ++n) {
        const uintptr_t address = (n * (uintptr_t)2654435761u) ^ (n << 18);
        cache.put(address, (uint32_t)n);
        assert(cache.get(address, out) && out == (uint32_t)n);
    }
    /* Production uses thread-local instances: one thread's same-key value
     * must not become another thread's diagnostic breadcrumb. */
    auto worker = [](uint32_t value) {
        static thread_local HostPcCache<64> local;
        for (unsigned n = 0; n < 100000; ++n) {
            local.put(0x1234, value);
            uint32_t found;
            assert(local.get(0x1234, found) && found == value);
        }
    };
    std::thread one(worker, 11), two(worker, 22);
    one.join(); two.join();
    std::puts("Host PC memo: full-width keys, zero results, collisions, replacement, TLS PASS");
}
