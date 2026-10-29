#include "../ps3recomp/libs/video/rsx_frame_refs.h"
#include <assert.h>
#include <stdio.h>
typedef struct { unsigned refs, alive, id; } Object;
static void retain(void* p) { Object* o=p; assert(o->alive && o->refs); ++o->refs; }
static void release(void* p) { Object* o=p; assert(o->alive && o->refs); if (!--o->refs) o->alive=0; }
int main(void) {
    assert(!rsx_fence_reached(9,10));
    assert(rsx_fence_reached(10,10) && rsx_fence_reached(11,10));
    assert(!rsx_fence_reached(UINT64_MAX,10));
    rsx_frame_refs frame={0};
    assert(rsx_frame_refs_init(&frame,8192));
    assert(!rsx_frame_refs_init(&frame,8193));
    Object objects[600];
    for (unsigned round=0; round<100; ++round) {
        for (unsigned i=0; i<600; ++i) objects[i]=(Object){1,1,i};
        /* More unique pipelines than the real FIFO cache; evict the cache's
         * reference immediately, including repeated draws of the same PSO. */
        for (unsigned i=0; i<600; ++i) {
            assert(rsx_frame_refs_pin(&frame,i*2,&objects[i],retain));
            assert(rsx_frame_refs_pin(&frame,i*2+1,&objects[i],retain));
            release(&objects[i]);
        }
        assert(!rsx_frame_refs_pin(&frame,0,&objects[0],retain));
        assert(!rsx_frame_refs_pin(&frame,8192,&objects[0],retain));
        assert(!rsx_frame_refs_drain(&frame,0,release)); /* timeout is NOT completion */
        for (unsigned i=0; i<1200; ++i) {
            Object* o=rsx_frame_refs_get(&frame,i);
            assert(o==&objects[i/2] && o->alive && o->refs==2);
        }
        assert(rsx_frame_refs_get(&frame,8192)==NULL);
        assert(rsx_frame_refs_drain(&frame,1,release));
        for (unsigned i=0; i<600; ++i) assert(!objects[i].alive && !objects[i].refs);
        assert(frame.used==0 && !rsx_frame_refs_get(&frame,0));
        assert(rsx_frame_refs_drain(&frame,1,release)); /* exactly once */
    }
    free(frame.slots);
    puts("PASS: 100 frames / 120000 draw pins, FIFO eviction, timeout and exactly-once release");
    return 0;
}
