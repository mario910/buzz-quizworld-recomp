#include "../ps3recomp/libs/video/rsx_vertex_fetch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u8 memory[512 * 1024];
u8* vm_base = memory;
static unsigned resolves;
u32 cellGcmResolveLocated(int local, u32 offset) { ++resolves; return offset + (local ? 0 : 256 * 1024); }
u32 cellGcmResolveOffset(u32 offset) { return offset; }
static u32 random_word(void)
{
    static u32 s = 0x7134b51;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}
int main(void)
{
    rsx_state* state = calloc(1, sizeof *state);
    rsx_vertex_cache* cache = malloc(sizeof *cache);
    assert(state && cache);
    for (unsigned n = 0; n < sizeof memory; ++n) memory[n] = (u8)random_word();
    unsigned tested = 0;
    for (unsigned round = 0; round < 10000; ++round) {
        for (unsigned n = 0; n < 16; ++n) {
            rsx_vertex_attrib* a = &state->vertex_attribs[n];
            a->enabled = random_word() & 1;
            a->type = 1 + random_word() % 7;
            a->size = random_word() % 6;
            a->stride = (random_word() % 5) * 8;
            a->offset = n * 8192 + ((random_word() & 1) ? 0x80000000u : 0);
            a->frequency = random_word() % 5;
            for (unsigned c = 0; c < 4; ++c) {
                u32 bits = random_word();
                memcpy(&state->vertex_data4f[n][c], &bits, 4);
            }
        }
        state->frequency_divider_op = random_word();
        rsx_vertex_cache_init(cache, state);
        for (unsigned k = 0; k < 16; ++k) {
            u32 index = random_word() & 127;
            float reference[16][4], actual[16][4], cached[16][4];
            for (unsigned n = 0; n < 16; ++n) rsx_fetch_attrib(state, n, index, reference[n]);
            rsx_fetch_vertex(state, index, actual);
            assert(!memcmp(reference, actual, sizeof actual));
            rsx_fetch_vertex_cached(cache, index, cached);
            assert(!memcmp(reference, cached, sizeof cached));
            unsigned before = resolves;
            rsx_fetch_vertex_cached(cache, index, cached);
            assert(resolves == before && !memcmp(reference, cached, sizeof cached));
            ++tested;
        }
    }
    memset(state->vertex_attribs, 0, sizeof state->vertex_attribs);
    rsx_vertex_cache_init(cache, state);
    float out[16][4];
    rsx_fetch_vertex_cached(cache, 0xffffffffu, out);
    assert(!memcmp(out, state->vertex_data4f, sizeof out));
    state->vertex_data4f[0][0] = 123;
    rsx_vertex_cache_init(cache, state); /* later draw must observe new registers */
    rsx_fetch_vertex_cached(cache, 0xffffffffu, out);
    assert(out[0][0] == 123);
    vm_base = NULL;
    rsx_fetch_vertex(state, 0, out);
    assert(!memcmp(out, state->vertex_data4f, sizeof out));
    free(cache); free(state);
    printf("PASS: %u bitwise vertex comparisons, all formats, constants, frequency, LOCAL/MAIN, cache hits and draw reset\n", tested);
}
