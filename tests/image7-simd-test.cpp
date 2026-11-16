#include "native_image7_simd.h"
#include "spu_scoped_rtz.h"
#include <filesystem>
#include <memory>
#include <vector>
#include <cassert>
#include <cstdio>

extern "C" {
void buzz_image7_8638_oracle(spu_context*);
void buzz_image7_run_layout(spu_context*, void (*)(spu_context*), void (*)(spu_context*), unsigned);
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_0006_at_007FF780_spu_func_00008FF0(spu_context*) {}
}
static unsigned admitted;
static void fast(spu_context* c)
{
    assert(buzz_image7_simd::try_convert(c));
    ++admitted;
}
static void equal(const spu_context& a, const spu_context& b)
{
    if (memcmp(a.ls, b.ls, SPU_LS_SIZE)) {
        unsigned at = 0;
        while (a.ls[at] == b.ls[at]) ++at;
        fprintf(stderr, "LS mismatch at %x: %02x %02x\n", at, a.ls[at], b.ls[at]);
        abort();
    }
    assert(a.pc == b.pc && !memcmp(a.gpr, b.gpr, sizeof a.gpr));
    auto copy = std::make_unique<spu_context>(b);
    copy->ls = a.ls;
    assert(!memcmp(&a, copy.get(), sizeof a));
}
static void reset(spu_context& to, const spu_context& from)
{
    to = from;
    to.ls = to.ls_store;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: image7-simd-test CAPTURE_DIRECTORY\n");
        return 2;
    }
    std::vector<std::unique_ptr<spu_context>> cases;
    for (const auto& f : std::filesystem::directory_iterator(argv[1])) {
        if (f.path().extension() != ".bin") continue;
        FILE* file = fopen(f.path().c_str(), "rb");
        assert(file);
        uint32_t h[8];
        assert(fread(h, sizeof h, 1, file) == 1 && h[0] == 0x37474d49 &&
               h[1] == 1 && h[2] == 7 && h[3] == 0x86b8 && h[6] == SPU_LS_SIZE && h[7] == 2048);
        auto c = std::make_unique<spu_context>();
        *c = {};
        c->ls = c->ls_store;
        c->pc = h[3];
        c->image_id = h[2];
        assert(fread(c->gpr, 2048, 1, file) == 1 && fread(c->ls, SPU_LS_SIZE, 1, file) == 1);
        assert(fgetc(file) == EOF);
        fclose(file);
        cases.push_back(std::move(c));
    }
    assert(!cases.empty());
    auto a = std::make_unique<spu_context>();
    auto b = std::make_unique<spu_context>();
    for (const auto& c : cases) {
        reset(*a, *c);
        reset(*b, *c);
        g_spu_trampoline_fn = nullptr;
        buzz_image7_8638_oracle(a.get());
        const auto next = g_spu_trampoline_fn;
        g_spu_trampoline_fn = nullptr;
        buzz_spu_scoped_rtz(b.get(), buzz_image7_8638_native);
        assert(next == g_spu_trampoline_fn);
        equal(*a, *b);
        reset(*b, *c);
        g_spu_trampoline_fn = nullptr;
        buzz_spu_scoped_rtz(b.get(), fast);
        assert(next == g_spu_trampoline_fn);
        equal(*a, *b);
        reset(*b, *c);
        g_spu_trampoline_fn = nullptr;
        buzz_image7_run_layout(b.get(), buzz_image7_8638_oracle, buzz_image7_8638_native, 0x86b8);
        assert(next == g_spu_trampoline_fn);
        equal(*a, *b);
    }

    uint32_t rng = 0x98654721;
    const auto random = [&]() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    };
    unsigned synthetic = 0, mutations = 0;
    for (unsigned count : {1u,2u,3u,4u,5u,6u,7u,8u,9u,20u,31u,32u,63u,64u,65u,128u,255u,256u,512u}) {
        for (unsigned trial = 0; trial < 4; ++trial) {
            reset(*a, *cases[0]);
            for (unsigned reg = 0; reg < 128; ++reg)
                for (auto& word : a->gpr[reg]._u32) word = random();
            memcpy(&a->gpr[100], &cases[0]->gpr[100], 7*16);
            a->gpr[109]._u32[0] = 32;
            a->gpr[110]._u32[0] = 16;
            a->gpr[112]._u32[0] = 0x1000-32;
            a->gpr[113]._u32[0] = 0x4000-32;
            a->gpr[114]._u32[0] = 0x7000-16;
            a->gpr[115]._u32[0] = 0x9000-16;
            a->gpr[108]._u32[0] = 0x10000;
            a->gpr[107]._u32[0] = 0x20000;
            a->gpr[111]._u32[0] = trial == 0 ? 0 : trial == 1 ? 0xfffffffe : random();
            a->gpr[119]._u32[0] = a->gpr[111]._u32[0] + count;
            for (unsigned base : {0x1000u, 0x4000u})
                for (unsigned i = 0; i < count*32; ++i) a->ls[base+i] = random();
            for (unsigned base : {0x7000u, 0x9000u})
                for (unsigned i = 0; i < count*16; ++i) a->ls[base+i] = random();
            reset(*b, *a);
            buzz_image7_8638_oracle(a.get());
            const auto next = g_spu_trampoline_fn;
            const unsigned incoming = 0x1f80u | ((trial & 3) << 13) | trial;
            _mm_setcsr(incoming);
            buzz_spu_scoped_rtz(b.get(), fast);
            assert(_mm_getcsr() == incoming && g_spu_trampoline_fn == next);
            equal(*a, *b);
            ++synthetic;
        }
    }

    // Every guarded coefficient/shuffle byte must cause a mutation-free fallback.
    const unsigned table_addresses[] = {
        0xc9b0,0xc9c0,0xc9d0,0xc9e0,0xc9f0,0xca00,0xca10,0xca20,
        0xca90,0xcaa0,0xcab0,0xcac0,0xcad0,0xcae0,0xcaf0,0xcb00,
        0xcb10,0xcb20,0xcb30,0xcb40,0xcb50,0xcb60,0xcb70,0xcb80,
        0xcb90,0xcba0,0xcbb0,0xcbc0,0xcbd0,0xcbe0,0xcbf0,0xcc00,0xcc10,0xcc20,
    };
    const auto rejected = [&]() {
        reset(*b, *a);
        const auto incoming_next = g_spu_trampoline_fn;
        const unsigned incoming_fp = _mm_getcsr();
        assert(!buzz_image7_simd::try_convert(b.get()));
        assert(g_spu_trampoline_fn == incoming_next && _mm_getcsr() == incoming_fp);
        equal(*a, *b);
        ++mutations;
    };
    for (unsigned byte = 0; byte < 7*16; ++byte) {
        reset(*a, *cases[0]);
        reinterpret_cast<uint8_t*>(&a->gpr[100])[byte] ^= 1;
        rejected();
    }
    for (unsigned address : table_addresses) {
        for (unsigned byte = 0; byte < 16; ++byte) {
            reset(*a, *cases[0]);
            a->ls[address+byte] ^= 1;
            rejected();
        }
    }
    for (unsigned bad = 0; bad < 9; ++bad) {
        reset(*a, *cases[0]);
        switch (bad) {
        case 0: a->gpr[119]._u32[0] = a->gpr[111]._u32[0]; break;
        case 1: a->gpr[119]._u32[0] = a->gpr[111]._u32[0]+1025; break;
        case 2: a->gpr[107]._u32[0] |= 1; break;
        case 3: a->gpr[108]._u32[0] = a->gpr[107]._u32[0]; break;
        case 4: a->gpr[108]._u32[0] = 0xc9b0; break;
        case 5: a->gpr[108]._u32[0] = a->gpr[112]._u32[0]; break;
        case 6: a->gpr[112]._u32[0] = SPU_LS_SIZE-16; break;
        case 7: a->gpr[109]._u32[0] = 0xfffffff0; break;
        case 8: a->gpr[108]._u32[0] = SPU_LS_SIZE-16; break;
        }
        rejected();
    }
    reset(*a, *cases[0]);
    *b = *a; // External LS: pointer intentionally refers to a's storage.
    assert(!buzz_image7_simd::try_convert(b.get()));
    equal(*a, *b);
    ++mutations;
    printf("PASS synthetic=%u full states, mutation-free rejections=%u, incoming MXCSR restored\n",
           synthetic, mutations);
    printf("PASS %zu captured full states; native, SIMD and generated-layout dispatch against integer oracle; fast admissions=%u\n",
           cases.size(), admitted);
}
