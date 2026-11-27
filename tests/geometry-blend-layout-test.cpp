#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_blend.h"
#include <cassert>
#include <random>
#include <sys/mman.h>
#include <unistd.h>

int main()
{
    const size_t page = sysconf(_SC_PAGESIZE);
    auto* mapping = static_cast<uint8_t*>(mmap(nullptr, 2 * page, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(mapping != MAP_FAILED && mprotect(mapping, page, PROT_READ | PROT_WRITE) == 0);
    std::mt19937 random(4108);
    for (unsigned trial = 0; trial < 128; ++trial) {
        alignas(32) uint32_t words[4][8];
        for (auto& component : words)
            for (auto& word : component) word = random();
        buzz::GeometryHostSoA8 block;
        for (unsigned k = 0; k < 4; ++k)
            block.component[k] = _mm256_load_si256(reinterpret_cast<const v8*>(words[k]));
        for (unsigned count = 0; count <= 8; ++count) {
            auto* table = mapping + page - count * 16;
            table[-1] = 0x7d;
            buzz::geometry_blend_soa_to_quads(block, table, count);
            assert(table[-1] == 0x7d);
            auto actual = block;
            for (unsigned lane = 0; lane < count; ++lane)
                for (unsigned k = 0; k < 4; ++k) {
                    auto* field = table + 16 * lane + 4 * k;
                    assert(buzz::geometry_job_be32(field) == words[k][lane]);
                    buzz::geometry_job_put32(field, ~words[k][lane]);
                }
            buzz::geometry_blend_quads_to_soa(table, actual, count);
            for (unsigned k = 0; k < 4; ++k) {
                alignas(32) uint32_t result[8];
                _mm256_store_si256(reinterpret_cast<v8*>(result), actual.component[k]);
                for (unsigned lane = 0; lane < 8; ++lane)
                    assert(result[lane] == (lane < count ? ~words[k][lane] : words[k][lane]));
            }
        }
    }
    assert(munmap(mapping, page * 2) == 0);
}
