/* Native image-41 outer flow. Library calls retain their SPU ABI and kernel
 * switches; cached metadata, integer arithmetic and pipeline decisions live
 * in C++. No original main-driver block is dispatched on this path. */
#pragma once
#include "native_geometry_job.h"
#include "native_geometry_job_call.h"
#include "native_geometry_publish.h"
#include "native_geometry_host_output_job.h"
#include "native_geometry_host_job.h"

namespace buzz {
class GeometryPipeline {
    spu_context& ctx;
    const buzz_geometry_job_functions& functions;
    const GeometryJobSetup& setup;
    const u128 frame, context;
    const u128 palette_input, skin_input, palette_bytes, blend_flag, position_flag, pipeline;
    const u128 command, output_parameters, flags, range;
    GeometryOutputLayout output_layout;
    bool cached_output_layout = false;
    const GeometryHostOutputRecipe* host_output;
    const GeometryHostJobWorkspace* host_job;

    void call(buzz_geometry_job_fn fn, uint32_t link)
    {
        ctx.gpr[0] = spu_link(link);
        const int32_t image = ctx.image_id;
        ++ctx.host_depth;
        fn(&ctx);
        spu_drain_call(&ctx, link);
        --ctx.host_depth;
        spu_img_restore(&ctx, image);
    }
    template<class Function> void call_native(Function function, uint32_t link)
    {
        ctx.gpr[0] = spu_link(link);
        const int32_t image = ctx.image_id;
        ++ctx.host_depth;
        function();
        ctx.pc = ctx.gpr[0]._u32[0]; // Ordinary SPU_RET while this host call frame is live.
        spu_drain_call(&ctx, link);
        --ctx.host_depth;
        spu_img_restore(&ctx, image);
    }
    u128 load(unsigned offset) const { return spu_ls_read128(&ctx, setup.entry.frame_lsa + offset); }
    void store(unsigned offset, u128 value) { spu_ls_write128(&ctx, setup.entry.frame_lsa + offset, value); }
    void initialize_output()
    {
        // No metadata GET is admitted. The null 1AD8 path always selects mode
        // 0, without DMA or LS writes. Retain its complete ABI exit vectors.
        ctx.gpr[65] = spu_ceqi(ctx.gpr[90], 0);
        ctx.gpr[15] = spu_ai(frame, 0x70); ctx.gpr[66] = load(0x30);
        ctx.gpr[90] = spu_ai(frame, 0x90);
        ctx.gpr[27] = spu_ls_read128(&ctx, setup.entry.context_lsa + 0x24);
        ctx.gpr[3] = spu_andc(ctx.gpr[15], ctx.gpr[65]);
        ctx.gpr[4] = ctx.gpr[90]; ctx.gpr[5] = ctx.gpr[100]; ctx.gpr[6] = ctx.gpr[102];
        ctx.gpr[7] = spu_ai(frame, 0x30); ctx.gpr[8] = spu_rotqby(ctx.gpr[27], ctx.gpr[80]);
        ctx.gpr[26] = ctx.gpr[66]; ctx.gpr[26]._u32[0] = 0; store(0x30, ctx.gpr[26]);
        ctx.gpr[12] = spu_il(0); ctx.gpr[13] = spu_cwd(frame, 0);
        ctx.gpr[19] = spu_ila(0xffff); ctx.gpr[14] = spu_cwd(frame, 8);
        ctx.gpr[9] = ctx.gpr[3]; ctx.gpr[18] = spu_cwd(frame, 12);
        ctx.gpr[11] = ctx.gpr[5]; ctx.gpr[15] = spu_cwd(frame, 4);
        ctx.gpr[16] = spu_il(0); ctx.gpr[17] = spu_cbd(frame, 4);
        ctx.gpr[3] = spu_il(0); ctx.gpr[0] = spu_link(0x20c4); ctx.pc = 0x20c4;
        ctx.gpr[11] = spu_il(0); ctx.gpr[81] = spu_il(0);
    }
    void decode_attributes()
    {
        ctx.gpr[80] = ctx.gpr[93];
        ctx.gpr[3] = context; ctx.gpr[4] = ctx.gpr[104]; ctx.gpr[5] = ctx.gpr[107];
        ctx.gpr[6] = ctx.gpr[87]; ctx.gpr[7] = ctx.gpr[88];
        call(functions.attributes, 0x20f4);
        if (!ctx.gpr[93]._u16[0]) return;

        // SPU reciprocal/refinement and RTZ semantics for additional streams.
        // Non-preferred lanes also survive in downstream callee spills.
        ctx.gpr[79] = spu_iohl(spu_ilhu(0x46ff), 0xfe00);
        ctx.gpr[7] = ctx.gpr[109]; ctx.gpr[14] = spu_rotmi(ctx.gpr[93], -16);
        ctx.gpr[6] = spu_shlqbyi(ctx.gpr[93], 4);
        ctx.gpr[5] = spu_and(ctx.gpr[86], ctx.gpr[80]); ctx.gpr[20] = load(0x20);
        ctx.gpr[3] = context; ctx.gpr[109] = spu_frest(ctx.gpr[79]);
        ctx.gpr[87] = spu_csflt(ctx.gpr[14], 155); ctx.gpr[8] = spu_ai(frame, 0x20);
        ctx.gpr[107] = spu_fi(ctx.gpr[79], ctx.gpr[109]);
        ctx.gpr[88] = ctx.gpr[20]; ctx.gpr[88]._u32[0] = 0; store(0x20, ctx.gpr[88]);
        ctx.gpr[93] = spu_fm(ctx.gpr[87], ctx.gpr[107]);
        ctx.gpr[104] = spu_fnms(ctx.gpr[93], ctx.gpr[79], ctx.gpr[87]);
        ctx.gpr[4] = spu_fma(ctx.gpr[104], ctx.gpr[107], ctx.gpr[93]);
        call(functions.blend, 0x25c4);
    }
    void skin_and_indices()
    {
        // Palette bytes / 48. Preserve the nonvolatile partial products the
        // SPU-ABI callees spill, while computing the quotient as a host value.
        u128 matrices{};
        for (unsigned lane = 0; lane < 4; ++lane) {
            const uint32_t bytes = palette_bytes._u32[lane];
            const uint32_t low = bytes & 0xffff, high = bytes >> 16;
            const uint64_t partial = uint64_t(low * 0xaaabu) + (uint64_t(low * 0xaaaau) << 16);
            const uint64_t product = uint64_t(bytes) * 0xaaaaaaabull;
            ctx.gpr[84]._u32[lane] = uint32_t(partial);
            ctx.gpr[106]._u32[lane] = high * 0xaaaau + uint32_t(partial >> 32);
            ctx.gpr[99]._u32[lane] = (high * 0xaaabu) >> 16;
            ctx.gpr[105]._u32[lane] = uint32_t(product >> 32);
            matrices._u32[lane] = uint32_t(product >> 37);
        }
        ctx.gpr[3] = context; ctx.gpr[4] = palette_input; ctx.gpr[5] = skin_input;
        ctx.gpr[6] = matrices; ctx.gpr[7] = spu_ai(frame, 0xe0);
        ctx.gpr[8] = spu_ai(frame, 0x120); ctx.gpr[9] = blend_flag; ctx.gpr[10] = position_flag;
        call(functions.skin, 0x2168);
        ctx.gpr[3] = context; ctx.gpr[4] = ctx.gpr[108];
        call(functions.indices, 0x2178);
        ctx.gpr[3] = context; ctx.gpr[4] = pipeline;
        call(functions.transform, 0x2184);
    }
    void empty_output()
    {
        ctx.gpr[83] = spu_ai(frame, 0x60);
        ctx.gpr[3] = context; ctx.gpr[4] = ctx.gpr[83]; ctx.gpr[5] = output_parameters;
        ctx.gpr[6] = spu_il(0); ctx.gpr[7] = spu_il(0);
        call(functions.empty_convert, 0x23c0);
        ctx.gpr[3] = context; ctx.gpr[4] = ctx.gpr[83]; ctx.gpr[5] = output_parameters;
        ctx.gpr[6] = spu_il(0); ctx.gpr[7] = spu_il(0);
        call(functions.empty_publish, 0x23d8);
    }
    bool allocate_output(u128 visible)
    {
        ctx.gpr[4] = visible; ctx.gpr[3] = context;
        if (buzz_geometry_job_layout_enabled()) {
            GeometryOutputSize size;
            const bool supported = read_geometry_output_size(ctx, size);
            buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_OUTPUT_SIZE, supported, 0);
            if (supported) {
                output_layout = size.layout; cached_output_layout = true;
                call_native([&] { measure_geometry_output(ctx, size); }, 0x21b0);
            } else call(functions.vertex_bytes, 0x21b0);
        } else call(functions.vertex_bytes, 0x21b0);
        const u128 bytes = ctx.gpr[3];
        // Mode 0 ignores the unused metadata capacity records. Its complete
        // nonvolatile values reduce to these constants/selections.
        ctx.gpr[108] = bytes; ctx.gpr[86] = spu_il(0);
        ctx.gpr[101] = ctx.gpr[94]; ctx.gpr[81] = spu_il(0);
        const u128 ring = spu_rotqbyi(spu_ls_read128(&ctx, 0x1c0), 8); // 2718
        ctx.gpr[0] = spu_link(0x21f8); ctx.gpr[3] = ring; ctx.pc = 0x21f8;
        ctx.gpr[4] = ctx.gpr[101]; ctx.gpr[5] = bytes;
        ctx.gpr[7] = ring; ctx.gpr[6] = ctx.gpr[90]; ctx.gpr[3] = context;
        call(functions.allocate_output, 0x2210);
        ctx.gpr[94] = spu_ceqbi(ctx.gpr[3], 0); ctx.gpr[103] = spu_xsbh(ctx.gpr[94]);
        return ctx.gpr[3]._u8[0] != 0;
    }
    void publish_output()
    {
        ctx.gpr[80] = spu_il(0); // Mode 0 never issues metadata summaries.
        ctx.gpr[3] = context; ctx.gpr[4] = load(0x30); ctx.gpr[5] = ctx.gpr[90]; ctx.gpr[6] = ctx.gpr[100];
        if (buzz_geometry_job_publish_enabled()) {
            GeometryIndexPublication plan;
            const bool supported = read_geometry_index_publication(ctx, plan);
            buzz_geometry_job_publish_result(supported);
            if (supported) call_native([&] { publish_geometry_indices(ctx, plan); }, 0x2240);
            else call(functions.put_indices, 0x2240);
        } else call(functions.put_indices, 0x2240);
        ctx.gpr[3] = context;
        if (host_job) {
            commit_geometry_host_job_vertices(ctx,setup.entry,*host_job);
        } else if (host_output) {
            call_native([&] { emit_geometry_host_output(ctx, setup.entry, *host_output); }, 0x2248);
            buzz_geometry_host_output_result();
        } else call(functions.emit_vertices, 0x2248);
        ctx.gpr[3] = context; ctx.gpr[4] = ctx.gpr[90]; ctx.gpr[5] = ctx.gpr[102];
        if (buzz_geometry_job_vertex_enabled()) {
            GeometryVertexPublication plan;
            const bool supported = read_geometry_vertex_publication(ctx, plan, cached_output_layout ? &output_layout : nullptr);
            buzz_geometry_job_output_result(BUZZ_GEOMETRY_JOB_VERTEX_PUBLISH, supported, supported && plan.reused_layout);
            if (supported) call_native([&] { publish_geometry_vertices(ctx, plan); }, 0x2258);
            else call(functions.put_vertices, 0x2258);
        } else call(functions.put_vertices, 0x2258);

        ctx.gpr[100] = spu_xsbh(ctx.gpr[95]);
        ctx.gpr[102] = spu_shlqbyi(flags, 3); ctx.gpr[10] = spu_andi(ctx.gpr[102], 1);
        const u128 vertex_record = load(0x40), index_record = load(0x50), index_count = load(0x30);
        ctx.gpr[3] = context; ctx.gpr[4] = output_parameters; ctx.gpr[5] = ctx.gpr[90];
        ctx.gpr[6] = vertex_record; ctx.gpr[7] = index_record; ctx.gpr[8] = index_count;
        ctx.gpr[9] = command; ctx.gpr[11] = flags; ctx.gpr[12] = range;
        call(setup.entry.position_only() ? functions.position_commands : functions.commands,
             setup.entry.position_only() ? 0x2714 : 0x2298);
    }
    void restore_caller()
    {
        ctx.gpr[0] = load(0x530); ctx.gpr[1] = spu_ai(frame, 0x520);
        for (unsigned reg = 80; reg <= 109; ++reg)
            ctx.gpr[reg] = spu_ls_read128(&ctx, ctx.gpr[1]._u32[0] - (reg - 79) * 16);
        ctx.gpr[126] = spu_ls_read128(&ctx, ctx.gpr[1]._u32[0] - 0x1f0);
    }
public:
    GeometryPipeline(spu_context& ctx, const buzz_geometry_job_functions& functions, const GeometryJobSetup& setup,
        const GeometryHostOutputRecipe* host_output = nullptr,const GeometryHostJobWorkspace* host_job = nullptr)
        : ctx(ctx), functions(functions), setup(setup), frame(ctx.gpr[1]), context(ctx.gpr[82]),
          palette_input(ctx.gpr[101]), skin_input(ctx.gpr[99]), palette_bytes(spu_a(ctx.gpr[105], ctx.gpr[106])),
          blend_flag(ctx.gpr[97]), position_flag(ctx.gpr[98]), pipeline(ctx.gpr[103]),
          command(ctx.gpr[92]), output_parameters(ctx.gpr[96]), flags(ctx.gpr[89]), range(ctx.gpr[91]), host_output(host_output),host_job(host_job) {}
    void run()
    {
        initialize_output();
        if (host_job) {
            commit_geometry_host_job_data(ctx,setup.entry,*host_job);
            ctx.gpr[3]=spu_ila(host_job->indices.triangles*3);
        } else {
            decode_attributes();
            skin_and_indices();
        }
        const u128 visible = ctx.gpr[3];
        ctx.gpr[98] = load(0x30); ctx.gpr[97] = ctx.gpr[98]; ctx.gpr[97]._u32[0] = visible._u32[0];
        store(0x30, ctx.gpr[97]);
        if (!visible._u32[0]) empty_output();
        else if (!allocate_output(visible)) empty_output();
        else publish_output();
        restore_caller();
    }
};
} // namespace buzz
