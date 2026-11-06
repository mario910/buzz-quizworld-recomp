#include "spu_context.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void buzz_image7_8638_native(spu_context*);
void buzz_image7_8638_oracle(spu_context*);
void buzz_image7_run(spu_context*, void (*)(spu_context*), void (*)(spu_context*));
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_0006_at_007FF780_spu_func_00008FF0(spu_context* ctx) { (void)ctx; }

static spu_context reference, native;
static uint8_t external_reference[SPU_LS_SIZE], external_native[SPU_LS_SIZE];
static uint32_t rng = 0x86b88ff0;
static uint32_t word(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

int main(void)
{
    for (unsigned n = 0; n < 4000; ++n) {
        memset(&reference, 0, sizeof reference);
        reference.ls = reference.ls_store;
        if ((n & 3) == 0) reference.ls = external_reference;
        for (unsigned r = 0; r < 128; ++r)
            for (unsigned lane = 0; lane < 4; ++lane)
                reference.gpr[r]._u32[lane] = word();
        for (unsigned offset = 0; offset < SPU_LS_SIZE; offset += 4) {
            uint32_t value = word();
            memcpy(reference.ls + offset, &value, sizeof value);
        }
        reference.gpr[111]._u32[0] = 0;
        reference.gpr[119]._u32[0] = 1 + n % 4;
        memcpy(&native, &reference, sizeof native);
        native.ls = native.ls_store;
        if ((n & 3) == 0) {
            native.ls = external_native;
            memcpy(native.ls, reference.ls, SPU_LS_SIZE);
        }
        g_spu_trampoline_fn = NULL;
        buzz_image7_8638_oracle(&reference);
        void (*expected)(spu_context*) = g_spu_trampoline_fn;
        g_spu_trampoline_fn = NULL;
        buzz_image7_run(&native, buzz_image7_8638_oracle, buzz_image7_8638_native);
        assert(g_spu_trampoline_fn == expected && native.pc == 0x8ff0);
        assert(!memcmp(reference.ls, native.ls, SPU_LS_SIZE));
        native.ls = reference.ls;
        assert(!memcmp(&reference, &native, sizeof reference));
    }
    puts("PASS Image 7 NEON: 4000 full SPU states against exact oracle");
}
