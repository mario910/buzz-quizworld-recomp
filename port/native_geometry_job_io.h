/* Channel transcript used by the phase-3 shadow job. Only the retained job
 * touches guest RAM. The shadow receives the same channel inputs and DMA
 * reads, and must reproduce channel arguments, list bytes and every write.
 * This is not a DMA engine or a native job implementation. */
#pragma once
#include "native_function_check.h"
#include <cstdint>
#include <vector>

namespace buzz {

// Same architectural controls as compare_function_state, except PC: a driver
// may combine blocks between channel operations. Its final PC is checked by
// the full-state comparator. Host MFC pointers, locks and clocks are never
// copied to the shadow. Channels compare their logical queue contents.
#define BUZZ_JOB_CONTROL_FIELDS(X) \
    X(status) X(stop_code) X(decrementer) X(srr0) \
    X(event_status) X(event_mask) X(rcv_evt) X(rcv_evt_n) X(rcv_evt_i) \
    X(mfc_lsa) X(mfc_eah) X(mfc_eal) X(mfc_size) X(mfc_tag) \
    X(mfc_tag_mask) X(mfc_tag_status) X(resv_ea) X(resv_valid) \
    X(atomic_stat) X(resv_line) X(int_enable) X(list_stall_mask) \
    X(list_stall_stat) X(list_stall_elem_lsa) X(list_stall_remaining) \
    X(list_stall_dest_lsa) X(list_stall_ea_base) X(list_stall_cmd) \
    X(irq_saved) X(irq_resume_pc) X(irq_gpr)
#define BUZZ_JOB_CHANNEL_FIELDS(X) \
    X(ch_out_mbox) X(ch_in_mbox) X(ch_out_intr_mbox) \
    X(ch_sig_notify[0]) X(ch_sig_notify[1])

struct GeometryJobChannel {
    uint32_t value = 0, count = 0, head = 0;
    uint32_t q[SPU_CHANNEL_CAP]{};

    static GeometryJobChannel capture(const spu_channel& channel)
    {
        GeometryJobChannel state;
        state.value = channel.value;
        state.count = channel.count;
        state.head = channel.head;
        memcpy(state.q, channel.q, sizeof(state.q));
        return state;
    }
    bool matches(const spu_channel& channel) const
    {
        if (count != channel.count || value != channel.value) return false;
        for (unsigned i = 0; i < count && i < SPU_CHANNEL_CAP; ++i)
            if (q[(head + i) % SPU_CHANNEL_CAP] !=
                channel.q[(channel.head + i) % SPU_CHANNEL_CAP]) return false;
        return true;
    }
    void apply(spu_channel& channel) const
    {
        channel.value = value;
        channel.count = count;
        channel.head = head;
        memcpy(channel.q, q, sizeof(q));
        // channel.lock belongs to this host context, not to the SPU state.
    }
};

struct GeometryJobControls {
#define DECLARE(name) decltype(spu_context::name) name{};
    BUZZ_JOB_CONTROL_FIELDS(DECLARE)
#undef DECLARE
    GeometryJobChannel ch_out_mbox{}, ch_in_mbox{}, ch_out_intr_mbox{}, ch_sig_notify[2]{};

    static GeometryJobControls capture(const spu_context& ctx)
    {
        GeometryJobControls state;
#define COPY(name) memcpy(&state.name, &ctx.name, sizeof(ctx.name));
        BUZZ_JOB_CONTROL_FIELDS(COPY)
#undef COPY
#define COPY_CHANNEL(name) state.name = GeometryJobChannel::capture(ctx.name);
        BUZZ_JOB_CHANNEL_FIELDS(COPY_CHANNEL)
#undef COPY_CHANNEL
        return state;
    }

    const char* difference(const spu_context& ctx) const
    {
#define COMPARE(name) if (memcmp(&name, &ctx.name, sizeof(name))) return #name;
        BUZZ_JOB_CONTROL_FIELDS(COMPARE)
#undef COMPARE
#define COMPARE_CHANNEL(name) if (!name.matches(ctx.name)) return #name;
        BUZZ_JOB_CHANNEL_FIELDS(COMPARE_CHANNEL)
#undef COMPARE_CHANNEL
        return nullptr;
    }

    void apply(spu_context& ctx) const
    {
#define COPY(name) memcpy(&ctx.name, &name, sizeof(name));
        BUZZ_JOB_CONTROL_FIELDS(COPY)
#undef COPY
#define COPY_CHANNEL(name) name.apply(ctx.name);
        BUZZ_JOB_CHANNEL_FIELDS(COPY_CHANNEL)
#undef COPY_CHANNEL
    }
};
#undef BUZZ_JOB_CONTROL_FIELDS
#undef BUZZ_JOB_CHANNEL_FIELDS

struct GeometryJobLsInput {
    uint32_t lsa = 0;
    std::vector<uint8_t> bytes;
};

// Kind numbers match the existing job-corpus DMA observer (including the
// attempted payload of a failed PUTLLC). A GET/GETLLAR is an environmental
// input; all other payloads are outputs to compare, never to publish twice.
struct GeometryJobTransfer {
    uint32_t kind = 0, lsa = 0, ea = 0;
    std::vector<uint8_t> bytes;
};

enum class GeometryJobIoKind { write, read, count };
struct GeometryJobIoOperation {
    GeometryJobIoKind kind = GeometryJobIoKind::write;
    uint32_t channel = 0, argument = 0;
    u128 result{};
    GeometryJobControls before, after;
    // Capture list-command descriptors BEFORE executing the real command.
    // Together with exact MFC registers/command, these establish identical
    // transfer shapes/order even when earlier elements alias later inputs.
    std::vector<GeometryJobLsInput> inputs;
    std::vector<GeometryJobTransfer> transfers;
};

class GeometryJobIoReplay {
public:
    // The controller must establish trace recordability before starting the
    // shadow. A consume/finish error is a sticky comparison failure, never a
    // reason to relabel an incorrect native execution as reference fallback.
    explicit GeometryJobIoReplay(const std::vector<GeometryJobIoOperation>& operations)
        : operations_(operations) {}

    bool consume(spu_context& ctx, GeometryJobIoKind kind, uint32_t channel,
                 uint32_t argument, u128& result)
    {
        if (error_) return false;
        if (cursor_ == operations_.size()) return fail("extra channel operation");
        const auto& op = operations_[cursor_];
        if (kind != op.kind || channel != op.channel) return fail("channel operation/order");
        // wrch consumes only the preferred word. Upper source-register lanes
        // remain covered by the final full-register comparison.
        if (kind == GeometryJobIoKind::write && argument != op.argument)
            return fail("channel argument");
        if (const char* field = op.before.difference(ctx)) return fail(field);
        for (const auto& input : op.inputs) {
            if (!range(input.lsa, input.bytes.size())) return fail("input LS range");
            if (!input.bytes.empty() &&
                memcmp(ctx.ls + input.lsa, input.bytes.data(), input.bytes.size()))
                return fail("DMA list/input bytes");
        }
        for (const auto& transfer : op.transfers) {
            if (transfer.kind < 1 || transfer.kind > 6) return fail("DMA kind");
            if (!range(transfer.lsa, transfer.bytes.size())) return fail("DMA LS range");
            if (transfer.bytes.empty()) continue;
            auto* local = ctx.ls + transfer.lsa;
            if (transfer.kind == 1 || transfer.kind == 3)
                memcpy(local, transfer.bytes.data(), transfer.bytes.size());
            else if (memcmp(local, transfer.bytes.data(), transfer.bytes.size()))
                return fail("DMA/atomic output bytes");
        }
        // The before-state check must precede this. Replaying a recorded
        // channel's effects must not hide a driver's invalid control writes.
        op.after.apply(ctx);
        result = op.result;
        ++cursor_;
        return true;
    }

    bool finish()
    {
        if (error_) return false;
        return cursor_ == operations_.size() || fail("missing channel operation");
    }
    const char* error() const { return error_; }
    size_t consumed() const { return cursor_; }

private:
    static bool range(uint32_t lsa, size_t size)
    {
        return lsa <= SPU_LS_SIZE && size <= SPU_LS_SIZE - lsa;
    }
    bool fail(const char* reason) { error_ = reason; return false; }
    const std::vector<GeometryJobIoOperation>& operations_;
    size_t cursor_ = 0;
    const char* error_ = nullptr;
};
} // namespace buzz
