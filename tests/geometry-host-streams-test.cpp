#define SPU_LS_DEBUG_HOOKS 0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../port/native_geometry_host_streams.h"
#include "../port/native_geometry_host_unit_vectors.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace buzz;
static unsigned random_word(unsigned& state)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SYSTEM_INFO info; GetSystemInfo(&info); const unsigned page = info.dwPageSize;
    auto* allocation = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(allocation); DWORD previous;
    assert(VirtualProtect(allocation + page, page, PAGE_NOACCESS, &previous));
    constexpr unsigned special[] = {0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x37800000,
        0x38000000, 0x3f800000, 0xbf800000, 0x7f7fffff, 0xff7fffff, 0x7f800000,
        0xff800000, 0x7fc01234, 0x7fffffff, 0xffffffff};
    unsigned state = 0x167392bc, cases = 0, positions = 0;
    for (unsigned trial = 0; trial < 512; ++trial) for (unsigned count = 0; count <= 8; ++count)
        for (unsigned components = 1; components <= 4; ++components)
            for (unsigned stride : {components * 4, components * 4 + 3, 32u, 64u}) {
                auto* source = allocation + page - count * stride;
                unsigned expected[4][8]{};
                for (unsigned vertex = 0; vertex < count; ++vertex) for (unsigned component = 0; component < components; ++component) {
                    const unsigned word = trial < 16 ? special[(trial + vertex + component) % 16] : random_word(state);
                    expected[component][vertex] = word;
                    auto* at = source + vertex * stride + component * 4;
                    at[0] = uint8_t(word >> 24); at[1] = uint8_t(word >> 16); at[2] = uint8_t(word >> 8); at[3] = uint8_t(word);
                }
                // The first inactive gather starts in PAGE_NOACCESS. Count0
                // passes an entirely inaccessible source; every lane is masked.
                const auto actual = decode_geometry_host_floats8(source, stride, components, count);
                for (unsigned component = 0; component < 4; ++component) {
                    unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), actual.component[component]);
                    for (unsigned vertex = 0; vertex < 8; ++vertex) assert(words[vertex] == expected[component][vertex]);
                }
                ++cases;
            }
    for (unsigned count = 0; count <= 8; ++count) {
        auto* source = allocation + page - count * 12; unsigned expected[3][8]{};
        for (unsigned vertex = 0; vertex < count; ++vertex) for (unsigned component = 0; component < 3; ++component) {
            const unsigned word = special[(vertex * 3 + component) % 16]; expected[component][vertex] = word;
            auto* at = source + vertex * 12 + component * 4;
            at[0] = uint8_t(word >> 24); at[1] = uint8_t(word >> 16); at[2] = uint8_t(word >> 8); at[3] = uint8_t(word);
        }
        const auto actual = decode_geometry_host_positions8(source, count);
        for (unsigned component = 0; component < 4; ++component) {
            unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<__m256i*>(words), actual.component[component]);
            for (unsigned vertex = 0; vertex < 8; ++vertex)
                assert(words[vertex] == (component < 3 ? expected[component][vertex] : 0x3f800000u));
        }
        ++positions;
    }
    unsigned half_shapes = 0, unit_shapes = 0;
    for (unsigned count = 0; count <= 8; ++count) for (unsigned components = 1; components <= 4; ++components)
        for (unsigned stride : {components * 2, components * 2 + 3, 32u, 64u}) {
            const unsigned span = count ? (count - 1) * stride + components * 2 : 0;
            const unsigned field = page - span;
            for (unsigned at = field & ~15u; at < page; ++at) allocation[at] = uint8_t(random_word(state));
            const auto actual = decode_geometry_host_half_stream8(allocation, field, stride, components, count);
            // Mathematical correctness is checked against original complete
            // codecs separately. Here the inaccessible page detects wide or
            // inactive-row overreads, including count0 at the guard address.
            for (unsigned component = 0; component < 4; ++component) {
                unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<v8*>(words), actual.component[component]);
                for (unsigned vertex = 0; vertex < 8; ++vertex)
                    if (vertex >= count || component >= components) assert(words[vertex] == 0);
            }
            ++half_shapes;
        }
    for (unsigned count = 0; count <= 8; ++count) for (unsigned stride : {3u,4u,7u,10u,11u,32u}) {
        const unsigned span = count ? (count - 1) * stride + 3 : 0;
        auto* source = allocation + page - span;
        for (unsigned vertex = 0; vertex < count; ++vertex) {
            source[vertex * stride] = uint8_t(random_word(state));
            source[vertex * stride + 1] = uint8_t(random_word(state));
            source[vertex * stride + 2] = uint8_t((random_word(state) & 0xf0) | (vertex % 12));
        }
        const auto actual = decode_geometry_host_unit_stream8(source, stride, count);
        for (unsigned component = 0; component < 4; ++component) {
            unsigned words[8]; _mm256_storeu_si256(reinterpret_cast<v8*>(words), actual.component[component]);
            for (unsigned vertex = count; vertex < 8; ++vertex) assert(words[vertex] == 0);
        }
        ++unit_shapes;
    }
    assert(VirtualFree(allocation, 0, MEM_RELEASE));
    printf("PASS host float streams: %u guarded source shapes including unaligned strides/all0..8tails; %u raw-position shapes with exact W=1;raw NaN/zero/extended bits preserved\n", cases, positions);
    printf("PASS host packed source boundaries: %u half and %u unit-vector shapes;final active byte immediately before PAGE_NOACCESS;all0..8tails\n", half_shapes, unit_shapes);
}
