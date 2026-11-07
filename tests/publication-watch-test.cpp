// The real C/C++ link must coalesce the diagnostic registration bitmap.
#ifdef NDEBUG
#error Assertions must be enabled
#endif
#define BUZZ_PRODUCER_DEPENDENCY_TRACE 1
#include "renderer_wait_trace.h"
#include <cassert>
#include <thread>
#include <vector>

extern "C" {
void publication_watch_from_c(uint32_t);
int publication_watched_from_c(uint32_t);
const void* publication_watch_address_from_c(void);
}

int main() {
    assert(publication_watch_address_from_c() == buzz_publication_lines);
    assert(!publication_watched_from_c(0x76a83268));
    assert(!buzz_publication_watched(0x701eb500));
    publication_watch_from_c(0x76a83268);
    assert(buzz_publication_watched(0x76a83268));
    buzz_publication_watch(0x701eb500);
    assert(publication_watched_from_c(0x701eb500));

    // Multiple native publishers must not lose concurrent registrations.
    memset(buzz_publication_lines, 0, sizeof buzz_publication_lines);
    std::vector<std::thread> writers;
    for (unsigned lane = 0; lane < 8; ++lane) {
        writers.emplace_back([lane] {
            for (unsigned bit = lane; bit < 16384; bit += 8) {
                const uint32_t address = bit << 7;
                if (lane & 1) publication_watch_from_c(address);
                else buzz_publication_watch(address);
            }
        });
    }
    for (auto& writer : writers) writer.join();
    for (unsigned bit = 0; bit < 16384; ++bit) {
        const uint32_t address = bit << 7;
        assert(publication_watched_from_c(address));
        assert(buzz_publication_watched(address));
    }
    puts("publication-watch: PASS (shared C/C++ bitmap, concurrent registration)");
}
