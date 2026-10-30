#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <vector>
extern "C" { SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*); }

int main()
{
    auto* a = buzz::clone_function_state([] {
        static spu_context c{}; c.ls = c.ls_store; return &c;
    }());
    // Exchanged/external LS must be copied from the active pointer.
    std::vector<uint8_t> external(SPU_LS_SIZE, 0x5a);
    a->ls = external.data();
    auto* b = buzz::clone_function_state(a);
    assert(b->ls != a->ls && b->ls[0] == 0x5a);
    assert(buzz::compare_function_state(*a, *b).exact());
    b->gpr[79]._u8[15] ^= 1;
    auto d = buzz::compare_function_state(*a, *b);
    assert(!d.exact() && d.first_register == 79 && d.first_register_byte == 15);
    b->gpr[79]._u8[15] ^= 1;
    b->ls[SPU_LS_SIZE - 1] ^= 1;
    d = buzz::compare_function_state(*a, *b);
    assert(d.ls_bytes == 1 && d.first_ls == SPU_LS_SIZE - 1);
    b->ls[SPU_LS_SIZE - 1] ^= 1;
    b->mfc_tag_mask = 42;
    assert(!strcmp(buzz::compare_function_state(*a, *b).first_state, "mfc_tag_mask"));
    b->mfc_tag_mask = 0;
    b->status = 1;
    assert(!strcmp(buzz::compare_function_state(*a, *b).first_state, "status"));
    b->status = 0;
    b->ch_out_mbox.count = 1;
    assert(!strcmp(buzz::compare_function_state(*a, *b).first_state, "ch_out_mbox"));
    b->ch_out_mbox.count = 0;
    bool compared = false;
    auto reference = [](spu_context* c) { c->gpr[3]._u32[0] = 123; c->ls[7] = 9; };
    auto native = [](spu_context* c) { c->gpr[3]._u32[0] = 123; c->ls[7] = 9; return 1; };
    assert(buzz::check_function(a, reference, native,
        [&](const spu_context& x, const spu_context& y, bool continuation) {
            compared = true; assert(continuation && buzz::compare_function_state(x, y).exact());
        }));
    assert(compared && a->gpr[3]._u32[0] == 123 && a->ls[7] == 9);
    compared = false;
    assert(!buzz::check_function(a, reference, [](spu_context*) { return 0; },
        [&](const spu_context&, const spu_context&, bool) { compared = true; }));
    assert(!compared);
    buzz::release_function_state(a); buzz::release_function_state(b);
    puts("native-function-check: PASS (external LS, first differences, state, paired execution, fallback)");
}
