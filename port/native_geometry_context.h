/* Semantic context initialization for the native geometry driver. This is
 * an internal LS/host-state operation, not a standalone SPU ABI replacement:
 * it deliberately does not materialize the lift's temporary GPR values. */
#pragma once
#include "native_geometry_job_input.h"

namespace buzz {
struct GeometryContextInit {
    uint32_t context = 0, configuration = 0;
    uint32_t scratch = 0, scratch_bytes = 0, workspace = 0, workspace_bytes = 0;
    uint32_t dma_tag = 0, viewport = 0, object = 0, custom = 0;
};

struct GeometryContextState {
    std::array<uint8_t, 16> configuration{};
    std::array<uint32_t, 16> tables{};
    uint32_t table_count = 0, table_stride = 0, table_end = 0;
    uint32_t vertices = 0, indices = 0;
};

inline void geometry_job_put32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = uint8_t(value >> 24); bytes[1] = uint8_t(value >> 16);
    bytes[2] = uint8_t(value >> 8); bytes[3] = uint8_t(value);
}

inline bool initialize_geometry_context(spu_context& ctx, const GeometryContextInit& in,
                                        GeometryContextState& output, const char** reason = nullptr)
{
    auto reject = [&](const char* value) { if (reason) *reason = value; return false; };
    auto range = [](uint32_t lsa, uint32_t size) {
        return lsa <= SPU_LS_SIZE && size <= SPU_LS_SIZE - lsa;
    };
    if (ctx.ls != ctx.ls_store || (in.context & 15) || !range(in.context, 0x190))
        return reject("context alignment/range/ownership");
    auto source = [&](uint32_t lsa, uint32_t size) {
        // The reviewed driver keeps sources outside the context object.
        // Reject aliasing before writes; do not silently change load order.
        return !(lsa & 15) && range(lsa, size) &&
               (uint64_t(lsa) + size <= in.context || uint64_t(in.context) + 0x190 <= lsa);
    };
    if (!source(in.configuration, 16) || (in.viewport && !source(in.viewport, 128)) ||
        (in.object && !source(in.object, 48)) || (in.custom && !source(in.custom, 48)))
        return reject("context source alignment/range/alias");
    if (!range(in.scratch, in.scratch_bytes) || !range(in.workspace, in.workspace_bytes))
        return reject("context allocator bounds");

    GeometryContextState state;
    memcpy(state.configuration.data(), ctx.ls + in.configuration, 16);
    state.vertices = geometry_job_be16(state.configuration.data() + 8);
    state.indices = geometry_job_be16(state.configuration.data() + 10);
    state.table_count = (state.configuration[0] & 15) + 1;
    state.table_stride = ((state.vertices + 7) & ~7u) * 16;
    for (unsigned i = 0; i < state.table_count; ++i)
        state.tables[i] = in.scratch + i * state.table_stride;
    state.table_end = in.scratch + state.table_count * state.table_stride;
    // Initialization writes pointers only. A whole-pipeline eligibility check
    // must additionally prove that the tables fit before writing their data.

    auto* context = ctx.ls + in.context;
    memcpy(context, state.configuration.data(), 16);
    for (unsigned i = 0; i < state.table_count; ++i)
        geometry_job_put32(context + 0x10 + 4 * i, state.tables[i]);
    memset(context + 0x50, 0, 16);
    memset(context + 0x60, 0, 12);
    memset(context + 0x160, 0, 16);
    geometry_job_put32(context + 0x170, in.scratch);
    geometry_job_put32(context + 0x174, in.scratch + in.scratch_bytes);
    geometry_job_put32(context + 0x178, in.workspace);
    geometry_job_put32(context + 0x17c, in.workspace + in.workspace_bytes);
    geometry_job_put32(context + 0x150, in.dma_tag);
    geometry_job_put32(context + 0x154, in.dma_tag);
    memcpy(context + 0x180, state.configuration.data() + 10, 2);
    if (in.viewport) memcpy(context + 0xd0, ctx.ls + in.viewport, 128);
    if (in.object) memcpy(context + 0xa0, ctx.ls + in.object, 48);
    // 0x3170 selects these words; custom+12 is unused and context+78 is zero.
    // The gaps at +94..9F and all other unnamed bytes retain their old values.
    static constexpr unsigned destinations[] = {0x6c, 0x70, 0x74, 0x7c, 0x80, 0x84, 0x88, 0x8c, 0x90};
    static constexpr unsigned sources[] = {0, 4, 8, 16, 20, 24, 28, 32, 36};
    for (unsigned i = 0; i < 9; ++i) {
        if (in.custom) memcpy(context + destinations[i], ctx.ls + in.custom + sources[i], 4);
        else memset(context + destinations[i], 0, 4);
    }
    memset(context + 0x78, 0, 4);
    output = state;
    if (reason) *reason = nullptr;
    return true;
}
} // namespace buzz
