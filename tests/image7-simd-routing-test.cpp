#include "spu_context.h"
#include <immintrin.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void buzz_image7_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*));
void buzz_image7_run_layout(spu_context*, void (*)(spu_context*), void (*)(spu_context*), unsigned);
}
static unsigned native_calls, simd_calls, other_calls;
static void scoped() { assert((_mm_getcsr() & ~63u) == 0xffc0); }
extern "C" void buzz_image7_8638_native(spu_context*) { scoped(); ++native_calls; }
bool buzz_image7_try_simd(spu_context*) { scoped(); ++simd_calls; return false; }
static void other(spu_context*) { scoped(); ++other_calls; }
static void reference(spu_context*) { assert(false && "unexpected reference call"); }

int main()
{
    const unsigned incoming = 0x3f81;
    _mm_setcsr(incoming);
    // Stubs never inspect ctx; these calls test selection and FP scope only.
    buzz_image7_run(nullptr, reference, buzz_image7_8638_native);
    buzz_image7_run_layout(nullptr, reference, buzz_image7_8638_native, 0x8638);
    buzz_image7_run_layout(nullptr, reference, buzz_image7_8638_native, 0);
    buzz_image7_run_layout(nullptr, reference, other, 0x86b8);
    assert(native_calls == 3 && simd_calls == 0 && other_calls == 1);
    buzz_image7_run_layout(nullptr, reference, buzz_image7_8638_native, 0x86b8);
    const char* setting = getenv("BUZZ_IMAGE7_SIMD");
    assert(simd_calls == unsigned(!setting || strcmp(setting, "0")));
    assert(native_calls == 4 && other_calls == 1);
    assert(_mm_getcsr() == incoming);
    puts("PASS image-7 layout/callback/legacy routing and MXCSR restoration");
}
