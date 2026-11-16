#include "native_image7_simd.h"

bool buzz_image7_try_simd(spu_context* ctx)
{
    return buzz_image7_simd::try_convert(ctx);
}
