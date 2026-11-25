/* Read-only observer of the guest's own draws, inserted into the port's copy
 * of the SDK draw engine (cmake/BuzzRendererObserver.cmake). Lets port UI
 * follow game geometry, such as button hints beside the answer boards.
 * Called on the RSX thread just before each draw that runs the guest's own
 * programs; everything is the guest's data in host order and must not be
 * retained past the call. NULL (the default) observes nothing. */
#ifndef BUZZ_DRAW_OBSERVER_H
#define BUZZ_DRAW_OBSERVER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct buzz_draw_observation {
    const uint32_t* vs_constants;  /* transform constants, float4 bit patterns */
    uint32_t vs_constant_count;
    const uint32_t* fs_constants;  /* the fragment program's inline constants  */
    uint32_t fs_constant_count;
    const uint8_t* vertices;       /* fetched vertices, one float4 per attribute */
    uint32_t vertex_count, vertex_stride;
    int32_t position_offset;       /* byte offset of attribute 0, -1 if unread */
    float target_width, target_height; /* guest pixels of the bound target     */
} buzz_draw_observation;
typedef void (*buzz_draw_observer_fn)(const buzz_draw_observation* draw);
extern buzz_draw_observer_fn buzz_draw_observer;
#ifdef __cplusplus
}
#endif
#endif
