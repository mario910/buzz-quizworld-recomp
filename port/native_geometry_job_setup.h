/* Native whole-driver preamble. Produces the LS entry frame and host call
 * arguments without executing translated blocks or changing the caller GPRs.
 * The eventual native driver owns register materialization at its call/exit
 * boundaries. This helper alone is not a whole-job implementation. */
#pragma once
#include "native_geometry_context.h"
#include "native_geometry_input_layout.h"
#include "spu_helpers.h"

namespace buzz {
struct GeometryJobSetup {
    GeometryJobEntry entry;
    GeometryContextInit initialization;
};

inline bool read_geometry_job_setup(const spu_context& ctx, GeometryJobSetup& output,
                                    const char** reason = nullptr,bool input_parents=false)
{
    auto reject = [&](const char* value) { if (reason) *reason = value; return false; };
    GeometryJobSetup setup;
    if (!read_geometry_job_entry(ctx, setup.entry, reason)) return false;
    const auto& entry = setup.entry;
    const auto& object = entry.input[GeometryJobEntry::object];
    const auto& viewport = entry.input[GeometryJobEntry::viewport];
    const auto& descriptor = entry.input[GeometryJobEntry::output_descriptor];
    if (geometry_job_be32(ctx.ls + entry.descriptor_lsa + 0xf4))
        return reject("optional metadata GET requires native I/O flow");
    if (ctx.ls[entry.descriptor_lsa + 0xfa] & 1)
        return reject("driver STOP flag");
    if (object.size < 48 || (object.lsa & 15) || viewport.size < 128 || (viewport.lsa & 15))
        return reject("object/viewport source family");
    if (entry.position_only() && (descriptor.size < 3 || (descriptor.lsa & 15)))
        return reject("position-only output descriptor");
    if(input_parents && !read_geometry_input_layout(ctx,entry,reason))return false;

    setup.initialization = {entry.geometry_context_lsa, entry.input[16].lsa,
        entry.scratch_lsa, entry.scratch_bytes, entry.workspace_lsa, entry.workspace_bytes,
        entry.dma_tag, viewport.lsa, object.lsa, entry.frame_lsa + 0xb0};
    output = setup;
    if (reason) *reason = nullptr;
    return true;
}

inline bool prepare_geometry_job(spu_context& ctx, GeometryJobSetup& output,
                                 const char** reason = nullptr,bool input_parents=false)
{
    GeometryJobSetup setup;
    if (!read_geometry_job_setup(ctx, setup, reason,input_parents)) return false;
    const auto& entry = setup.entry;
    const auto& object = entry.input[GeometryJobEntry::object];
    const auto& descriptor = entry.input[GeometryJobEntry::output_descriptor];
    // Eligibility is complete for this preamble before the first write. The
    // whole driver's codec, allocator and output paths need further checks.
    const uint32_t frame = entry.frame_lsa, sp = ctx.gpr[1]._u32[0];
    spu_ls_write128(&ctx, sp + 16, ctx.gpr[0]);
    for (unsigned reg = 80; reg <= 109; ++reg)
        spu_ls_write128(&ctx, sp - (reg - 79) * 16, ctx.gpr[reg]);
    spu_ls_write128(&ctx, sp - 0x1f0, ctx.gpr[126]);
    spu_ls_write128(&ctx, frame, ctx.gpr[1]);
    for (unsigned i = 0; i < entry.input.size(); ++i)
        geometry_job_put32(ctx.ls + 0xe500 + i * 4, entry.input[i].lsa);
    // These four globals retain complete quadwords, including non-preferred
    // words from the original ABI. The native pointer table above is scalar.
    spu_ls_write128(&ctx, 0xe570, ctx.gpr[3]);
    spu_ls_write128(&ctx, 0xe580, ctx.gpr[4]);
    spu_ls_write128(&ctx, 0xe590, spu_ai(ctx.gpr[4], 48));
    const u128 list = spu_rotqby(spu_ls_read128(&ctx, entry.descriptor_lsa + 10), spu_ai(ctx.gpr[4], 24));
    spu_ls_write128(&ctx, 0xe5a0, spu_and(spu_rothmi(list, -3), spu_il(8191)));

    memcpy(ctx.ls + frame + 0x160, ctx.ls + 0xcd30, 48);
    memset(ctx.ls + frame + 0x190, 0, 16);
    if (entry.flags & 0x10000000) {
        // The blend path saves the input object in E0..10F and builds records
        // in 120..14F with zero fourth words. The defaults in 160..18F survive.
        memcpy(ctx.ls + frame + 0xe0, ctx.ls + object.lsa, 48);
        memcpy(ctx.ls + frame + 0x110, ctx.ls + 0xcd60, 16);
        for (unsigned i = 0; i < 3; ++i) {
            memcpy(ctx.ls + frame + 0x120 + i * 16, ctx.ls + frame + 0xe0 + i * 16, 12);
            memset(ctx.ls + frame + 0x12c + i * 16, 0, 4);
        }
        memcpy(ctx.ls + frame + 0x150, ctx.ls + 0xcd60, 16);
    }
    memcpy(ctx.ls + object.lsa, ctx.ls + frame + 0x160, 48);

    memset(ctx.ls + frame + 0xb0, 0, 32);
    memset(ctx.ls + frame + 0xd0, 0, 8); // Upper half of this quadword survives.
    geometry_job_put32(ctx.ls + frame + 0xb0, entry.input[19].lsa);
    geometry_job_put32(ctx.ls + frame + 0xb4, entry.input[11].size_flags ? entry.input[20].lsa : 0);
    geometry_job_put32(ctx.ls + frame + 0xb8, descriptor.lsa);
    if (entry.position_only()) {
        ctx.ls[descriptor.lsa] = 1;
        ctx.ls[descriptor.lsa + 1] = 12;
        ctx.ls[descriptor.lsa + 2] = 1;
    }
    output = setup;
    if (reason) *reason = nullptr;
    return true;
}
} // namespace buzz
