#include "native_image7_simd.h"
#include <cassert>
#include <cstdio>

int main()
{
    // Probed or non-AVX2 builds reject without touching even the context pointer.
    assert(!buzz_image7_simd::try_convert(nullptr));
    puts("PASS image-7 diagnostic/ISA fallback");
}
