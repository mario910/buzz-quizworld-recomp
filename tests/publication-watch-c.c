#define BUZZ_PRODUCER_DEPENDENCY_TRACE 1
#include "renderer_wait_trace.h"

void publication_watch_from_c(uint32_t address) {
    buzz_publication_watch(address);
}
int publication_watched_from_c(uint32_t address) {
    return buzz_publication_watched(address);
}
const void* publication_watch_address_from_c(void) {
    return buzz_publication_lines;
}
