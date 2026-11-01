// Check external-bank execution against each test's independent lifted oracle.
#pragma once
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>

namespace geometry_output_register_test {
inline unsigned cases = 0;
inline void sentinel(spu_context*) { std::abort(); }

template<class Plan, class Core>
void compare(const char* name, const spu_context& entry,
             const spu_context& reference, const Plan& plan,
             void (*finish)(spu_context*), void (*skip)(spu_context*),
             void (*expected)(spu_context*), unsigned trial, Core core)
{
    auto* actual = buzz::clone_function_state(&entry);
    alignas(16) u128 bank[128], poison[128];
    memcpy(bank, entry.gpr, sizeof bank);
    for (unsigned reg = 0; reg < 128; ++reg)
        for (unsigned lane = 0; lane < 4; ++lane)
            poison[reg]._u32[lane] = 0xa5c39e71u ^ (reg * 0x9e3779b9u) ^
                                      (lane * 0x85ebca6bu) ^ trial;
    memcpy(actual->gpr, poison, sizeof poison);
    const auto saved = g_spu_trampoline_fn;
    g_spu_trampoline_fn = sentinel;
    const bool converted = core(*actual, bank, plan);
    assert(converted == bool(plan.columns));
    assert(!memcmp(actual->gpr, poison, sizeof poison));
    assert(actual->pc == entry.pc && g_spu_trampoline_fn == sentinel);
    assert(actual->ls == actual->ls_store);

    // Only the ABI caller publishes the bank and continuation. Compare the
    // resulting complete state with the original lift, not the native wrapper.
    memcpy(actual->gpr, bank, sizeof bank);
    actual->pc = converted ? 0x4610 : 0x4618;
    g_spu_trampoline_fn = converted ? finish : skip;
    const auto difference = buzz::compare_function_state(*actual, reference);
    if (!difference.exact()) {
        fprintf(stderr, "%s external-bank trial%u r%d byte%d LS%X/%u state%s\n",
                name, trial, difference.first_register, difference.first_register_byte,
                difference.first_ls, difference.ls_bytes,
                difference.first_state ? difference.first_state : "equal");
        std::abort();
    }
    assert(g_spu_trampoline_fn == expected);
    assert(actual->host_depth == reference.host_depth && actual->image_id == reference.image_id);
    assert(actual->policy_mode == reference.policy_mode && actual->resident_ovl == reference.resident_ovl);
    buzz::release_function_state(actual);
    g_spu_trampoline_fn = saved;
    ++cases;
}

inline void report(unsigned expected)
{
    assert(cases == expected && cases);
    printf("PASS external register bank: %u strict original-oracle cases; physical GPR poison/PC/FN preserved\n", cases);
}
}
