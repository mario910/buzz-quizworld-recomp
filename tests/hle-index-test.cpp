#include "../ps3recomp/runtime/ppu/hle_nid_index.h"
#include "../ps3recomp/include/ps3emu/nid.h"
#include <cassert>
#include <cstdio>
int main() {
    HleNidIndex<8> index;
    assert(index.find(0) == -1);
    assert(index.insert(0, 3));
    assert(index.insert(0xffffffffu, 4));
    assert(index.insert(0, 7));
    assert(index.find(0) == 3);
    assert(index.find(0xffffffffu) == 4);
    for (unsigned i = 1; i <= 6; ++i) assert(index.insert(i, i + 10));
    for (unsigned i = 1; i <= 6; ++i) assert(index.find(i) == (int)i + 10);
    assert(!index.insert(1000, 20));
    assert(index.find(1000) == -1);
    // Mirror actual registration, including add's zero-on-success contract.
    ps3_nid_entry storage[4096];
    ps3_nid_table table;
    ps3_nid_table_init(&table, storage, 4096);
    HleNidIndex<8192> full;
    for (unsigned i = 0; i < 4096; ++i) {
        uint32_t nid = (i % 4000) * 2654435761u;
        unsigned position = table.count;
        if (ps3_nid_table_add(&table, nid, "test", nullptr) == 0)
            assert(full.insert(nid, position));
    }
    for (unsigned i = 0; i < 4096; ++i) {
        uint32_t nid = (i % 4000) * 2654435761u;
        assert(&storage[full.find(nid)] == ps3_nid_table_find(&table, nid));
    }
    assert(ps3_nid_table_add(&table, 99, "overflow", nullptr) == -1);
    std::puts("HLE NID index: misses, collisions, duplicate-first and full table PASS");
}
