/* Exercise the real SDK completion guard used by the host present path.
 * A present and a later vblank may both tick: only the executed request can
 * complete, once. The callback remains deferred to the guest pump. */
#define main sdk_flip_status_test
#include "../ps3recomp/libs/video/tests/test_gcm_flip_status.c"
#undef main

int main(void)
{
    sdk_flip_status_test();
    unsigned expected=flips;
    for(unsigned n=0;n<10000;++n) {
        cellGcmResetFlipStatus();
        cellGcmTickFlip(); /* Request alone is not execution. */
        ppu_gcm_pump();
        assert(flips==expected && cellGcmGetFlipStatus()==CELL_GCM_FLIP_STATUS_WAITING);
        complete_direct_flip(); /* Host present. */
        cellGcmTickFlip();       /* Vblank before guest delivery. */
        ppu_gcm_pump();
        assert(flips==++expected && cellGcmGetFlipStatus()==CELL_GCM_FLIP_STATUS_DONE);
        cellGcmTickFlip();       /* Another vblank must not duplicate callback. */
        ppu_gcm_pump();
        assert(flips==expected);
    }
    puts("PASS 10000 executed flips: deferred delivery, no premature or duplicate completion");
    return 0;
}
