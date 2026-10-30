/* Full architectural-state comparison for synchronous, DMA-free SPU leaves.
 * Host pointers, locks, timing anchors and trampoline bookkeeping are not
 * architectural outputs. DMA/atomic functions require whole-job replay too;
 * this runner must never be used to execute their side effects twice. */
#pragma once
#include "spu_context.h"
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace buzz {
struct FunctionDifference {
    unsigned register_bytes = 0, ls_bytes = 0;
    int first_register = -1, first_register_byte = -1;
    unsigned first_ls = 0;
    const char* first_state = nullptr;
    bool exact() const { return !register_bytes && !ls_bytes && !first_state; }
};

inline bool same_channel(const spu_channel& a, const spu_channel& b)
{
    if (a.count != b.count || a.value != b.value) return false;
    // Compare logical queue entries, including the head, independent of rotation.
    for (unsigned i = 0; i < a.count && i < SPU_CHANNEL_CAP; ++i)
        if (a.q[(a.head + i) % SPU_CHANNEL_CAP] != b.q[(b.head + i) % SPU_CHANNEL_CAP]) return false;
    return true;
}

inline FunctionDifference compare_function_state(const spu_context& a, const spu_context& b)
{
    FunctionDifference d;
    for (unsigned r = 0; r < 128; ++r) {
        const auto* x = reinterpret_cast<const unsigned char*>(&a.gpr[r]);
        const auto* y = reinterpret_cast<const unsigned char*>(&b.gpr[r]);
        for (unsigned i = 0; i < 16; ++i) if (x[i] != y[i]) {
            if (d.first_register < 0) { d.first_register = r; d.first_register_byte = i; }
            ++d.register_bytes;
        }
    }
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) if (a.ls[i] != b.ls[i]) {
        if (!d.ls_bytes) d.first_ls = i;
        ++d.ls_bytes;
    }
#define BUZZ_CHECK_FIELD(field) \
    if (!d.first_state && memcmp(&a.field, &b.field, sizeof(a.field))) d.first_state = #field
    BUZZ_CHECK_FIELD(pc); BUZZ_CHECK_FIELD(status); BUZZ_CHECK_FIELD(stop_code);
    BUZZ_CHECK_FIELD(decrementer); BUZZ_CHECK_FIELD(srr0);
    BUZZ_CHECK_FIELD(event_status); BUZZ_CHECK_FIELD(event_mask);
    BUZZ_CHECK_FIELD(rcv_evt); BUZZ_CHECK_FIELD(rcv_evt_n); BUZZ_CHECK_FIELD(rcv_evt_i);
    BUZZ_CHECK_FIELD(mfc_lsa); BUZZ_CHECK_FIELD(mfc_eah); BUZZ_CHECK_FIELD(mfc_eal);
    BUZZ_CHECK_FIELD(mfc_size); BUZZ_CHECK_FIELD(mfc_tag);
    BUZZ_CHECK_FIELD(mfc_tag_mask); BUZZ_CHECK_FIELD(mfc_tag_status);
    BUZZ_CHECK_FIELD(resv_ea); BUZZ_CHECK_FIELD(resv_valid);
    BUZZ_CHECK_FIELD(atomic_stat); BUZZ_CHECK_FIELD(resv_line);
    BUZZ_CHECK_FIELD(int_enable); BUZZ_CHECK_FIELD(list_stall_mask);
    BUZZ_CHECK_FIELD(list_stall_stat); BUZZ_CHECK_FIELD(list_stall_elem_lsa);
    BUZZ_CHECK_FIELD(list_stall_remaining); BUZZ_CHECK_FIELD(list_stall_dest_lsa);
    BUZZ_CHECK_FIELD(list_stall_ea_base); BUZZ_CHECK_FIELD(list_stall_cmd);
    BUZZ_CHECK_FIELD(irq_saved); BUZZ_CHECK_FIELD(irq_resume_pc); BUZZ_CHECK_FIELD(irq_gpr);
#undef BUZZ_CHECK_FIELD
#define BUZZ_CHECK_CHANNEL(field) \
    if (!d.first_state && !same_channel(a.field, b.field)) d.first_state = #field
    BUZZ_CHECK_CHANNEL(ch_out_mbox); BUZZ_CHECK_CHANNEL(ch_in_mbox);
    BUZZ_CHECK_CHANNEL(ch_out_intr_mbox);
    BUZZ_CHECK_CHANNEL(ch_sig_notify[0]); BUZZ_CHECK_CHANNEL(ch_sig_notify[1]);
#undef BUZZ_CHECK_CHANNEL
    return d;
}

inline spu_context* clone_function_state(const spu_context* ctx)
{
#ifdef _WIN32
    auto* copy = static_cast<spu_context*>(_aligned_malloc(sizeof(spu_context), 16));
#else
    auto* copy = static_cast<spu_context*>(aligned_alloc(16, sizeof(spu_context)));
#endif
    if (!copy) std::abort();
    memcpy(copy, ctx, sizeof(*copy));
    copy->ls = copy->ls_store;
    memcpy(copy->ls, ctx->ls, SPU_LS_SIZE);
    return copy;
}

inline void release_function_state(spu_context* ctx)
{
#ifdef _WIN32
    _aligned_free(ctx);
#else
    free(ctx);
#endif
}

// Reference executes on the retained context. Each callback starts from the
// identical entry registers/LS/channel state. Native fallback is not a pass.
template<class Reference, class Native, class Compare>
bool check_function(spu_context* ctx, Reference reference, Native native, Compare compare)
{
    auto* copy = clone_function_state(ctx);
    reference(ctx);
    auto continuation = g_spu_trampoline_fn;
    g_spu_trampoline_fn = nullptr;
    const bool handled = native(copy) != 0;
    auto native_continuation = g_spu_trampoline_fn;
    g_spu_trampoline_fn = continuation;
    if (handled) compare(*ctx, *copy, continuation == native_continuation);
    release_function_state(copy);
    return handled;
}
} // namespace buzz
