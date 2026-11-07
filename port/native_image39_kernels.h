/* Included by the hash-pinned image39 Stage F generator. The descriptor
 * request in native_image39_request.h remains a separate I/O path. */
#ifndef BUZZ_IMAGE39_NATIVE_BOUNDS
#define BUZZ_IMAGE39_NATIVE_BOUNDS 0
#endif
#if BUZZ_IMAGE39_NATIVE_BOUNDS && BUZZ_IMAGE39_BOUNDS_PROFILE == 100
#ifdef __cplusplus
extern "C" int buzz_image39_bounds(spu_context*);
#else
extern int buzz_image39_bounds(spu_context*);
#endif
#endif

/* Historical hook name; EU 1.00 uses entry 0x05C0, loop 0x0750. */
static int buzz_image39_native_05d0(spu_context* ctx)
{
#if BUZZ_IMAGE39_NATIVE_BOUNDS && BUZZ_IMAGE39_BOUNDS_PROFILE == 100
    return buzz_image39_bounds(ctx);
#else
    (void)ctx;
    return 0;
#endif
}
