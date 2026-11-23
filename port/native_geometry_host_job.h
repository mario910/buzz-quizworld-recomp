/* One owned host data pipeline, prepared before any job mutation. The worker
 * retains this storage through final publication; no LS float4 bridge. */
#pragma once
#include "native_geometry_host_vertex_job.h"
#include "native_geometry_host_cull_job.h"
#include "native_geometry_host_index_output.h"
#include "native_geometry_coarse_clip.h"
#include "native_geometry_job.h"
#include "native_geometry_host_blend.h"

namespace buzz {
// Set by the eligibility caller for the duration of one preparation: the
// mutable job context and setup that a blend-stream job needs. Other callers
// leave it null and blend jobs keep the whole lifted flow.
struct GeometryHostBlendRequest { spu_context* context = nullptr; const GeometryJobSetup* setup = nullptr; };
inline GeometryHostBlendRequest& geometry_host_blend_request()
{
    static thread_local GeometryHostBlendRequest request;
    return request;
}
struct GeometryHostJobWorkspace {
    GeometryHostInputData data;
    GeometryHostSkinWorkspace skin_workspace;
    GeometryHostClipData clips;
    GeometryHostIndexData indices;
    GeometryHostInputRecipe input;
    GeometryHostSkinRecipe skin;
    GeometryHostVertexRecipe vertices;
    GeometryHostVertexTail tail;
    unsigned index_source = 0, index_bytes = 0;
    bool ready = false, bounded_projection = false;
};
template<bool CoarseClip, bool LazyAttributes = false, bool BoundedProjection = false,
    bool ZeroVertexPadding = false, bool ExactProjection = false>
inline bool prepare_geometry_host_job_impl(const spu_context& c,const GeometryJobEntry& e,
    GeometryHostJobWorkspace& w,const char** reason=nullptr)
{
    w.ready=false;
    w.bounded_projection=false;
    // The outer driver can fetch additional weighted blend streams after
    // ordinary attributes (native_geometry_host_blend.h). Without a mutable
    // context from the eligibility caller they keep the complete lifted flow.
    const bool blend=(geometry_job_be32(e.user.data()+8)&0xffff)!=0;
    const GeometryHostBlendRequest request=geometry_host_blend_request();
    if (blend && (!request.context || request.context!=&c || !request.setup)) {
        if(reason)*reason="host job additional blend streams";return false;
    }
    if (!read_geometry_host_input_recipe(c,e,w.input,reason) ||
        !read_geometry_host_vertex_recipe(c,e,w.input,w.vertices,reason) ||
        !read_geometry_host_skin_recipe(c,e,w.input,w.skin,reason)) return false;
    // The position-only writer consumes slot zero. Still validate every
    // packed selector in the positions phase, including on rejected draws.
    const bool only_positions = ZeroVertexPadding && e.position_only() && !w.skin.active && !blend &&
        w.vertices.output.fields == 1 && w.vertices.output.field[0].slot == 0;
    const bool deferred=(LazyAttributes || only_positions) && !w.skin.active && !blend;
    if(blend && !admit_geometry_host_blend(*request.setup,w.input,reason))return false;
    // The original fills vertex alignment bytes from an extra formatted
    // position of its blended, renormalized tables. Under the zero-padding
    // policy (only unindexed alignment bytes, see geometry-vertex-padding.md)
    // those bytes are zero for every job; otherwise blend jobs with alignment
    // bytes keep the lift. Decided from the recipe, before any blend transfer.
    if(blend && !ZeroVertexPadding &&
       w.vertices.output.bytes!=w.vertices.output.vertices*w.vertices.output.stride) {
        if(reason)*reason="host blend vertex padding";return false;
    }
    if(blend) {
        if(!capture_geometry_host_inputs(c,w.input,w.data,reason))return false;
        if(!run_geometry_host_blend(*request.context,*request.setup,w.input,w.data,reason) ||
           !skin_geometry_host_inputs(c,e,w.input,w.data,w.skin_workspace,reason))return false;
    } else if(deferred) {
        if(!capture_geometry_host_inputs_phase<GeometryHostInputPhase::Positions>(c,w.input,w.data,reason) ||
           !skin_geometry_host_inputs_phase<GeometryHostInputPhase::Positions>(c,e,w.input,w.data,w.skin_workspace,reason,&w.skin))return false;
    } else if(!capture_geometry_host_inputs(c,w.input,w.data,reason) ||
              !skin_geometry_host_inputs_phase(c,e,w.input,w.data,w.skin_workspace,reason,&w.skin))return false;
    if constexpr(CoarseClip) {
        const uint16_t* cached_indices=nullptr;
        if(!decode_geometry_host_indices(c,e,w.indices,reason,&cached_indices))return false;
        // Keep the culler's range admission even when no per-vertex clip
        // records will be built. Offscreen positions cannot admit bad indices.
        if(w.indices.maximum_index>=e.vertices) {
            if(reason)*reason="host cull source readiness/range";return false;
        }
        // The viewport and codec are immutable during this read-only job
        // preparation. Compose them once for both interval and vertex tests;
        // no prepared parameters survive this call or cross a guest mutation.
        GeometryHostClipParameters parameters;
        if(!read_geometry_host_clip_parameters(c,e,parameters,reason))return false;
        bool bounded=false;
        if(geometry_host_trivially_outside(c,e,w.input,w.data,&parameters,BoundedProjection?&bounded:nullptr)) {
            w.indices.triangles=w.indices.maximum_index=w.indices.final_group_kept=0;
            w.clips.ready=false;
        } else {
            w.bounded_projection=bounded;
            bool no_clip_flags=false;
            if(!project_geometry_host_inputs<ExactProjection>(c,e,w.input,w.data,w.clips,reason,&parameters,bounded,&no_clip_flags) ||
               !cull_geometry_host_triangles(c,e,w.clips,w.indices,reason,cached_indices,no_clip_flags))return false;
        }
    } else if (
        !project_geometry_host_inputs<ExactProjection>(c,e,w.input,w.data,w.clips,reason) ||
        !decode_geometry_host_indices(c,e,w.indices,reason) ||
        !cull_geometry_host_triangles(c,e,w.clips,w.indices,reason)) return false;
    if(deferred && !only_positions && w.indices.triangles &&
       (!capture_geometry_host_inputs_phase<GeometryHostInputPhase::Attributes>(c,w.input,w.data,reason) ||
        !skin_geometry_host_inputs_phase<GeometryHostInputPhase::Attributes>(c,e,w.input,w.data,w.skin_workspace,reason,&w.skin)))return false;
    if(w.indices.triangles) {
        if constexpr(ZeroVertexPadding) {
            if(!prepare_geometry_host_zero_vertex_tail(w.input,w.vertices,w.tail,reason))return false;
        } else if(!prepare_geometry_host_vertex_tail(c,e,w.input,w.skin,w.skin_workspace,w.vertices,w.tail,reason))return false;
    }
    w.index_source=e.input[GeometryJobEntry::indices].lsa;
    w.index_bytes=(w.indices.triangles*6+15)&~15u;
    const uint64_t end=uint64_t(w.index_source)+w.index_bytes;
    const uint64_t io_end=uint64_t(e.io_lsa)+e.io_bytes;
    if ((w.index_source&15) || w.index_source<e.workspace_lsa ||
        end+(w.indices.triangles?w.vertices.output.bytes:0)>io_end || io_end>SPU_LS_SIZE) {
        if(reason)*reason="host job final publication span";return false;
    }

    w.vertices.position_only_ready=only_positions && w.indices.triangles;
    w.ready=true;if(reason)*reason=nullptr;return true;
}
inline bool prepare_geometry_host_job(const spu_context& c,const GeometryJobEntry& e,
    GeometryHostJobWorkspace& w,const char** reason=nullptr)
{
    constexpr bool lazy=
#if defined(BUZZ_GEOMETRY_LAZY_ATTRIBUTES) && BUZZ_GEOMETRY_LAZY_ATTRIBUTES
        true;
#else
        false;
#endif
    constexpr bool zero_padding=
#if defined(BUZZ_GEOMETRY_ZERO_VERTEX_PADDING) && BUZZ_GEOMETRY_ZERO_VERTEX_PADDING
        true;
#else
        false;
#endif
    constexpr bool coarse=
#if defined(BUZZ_GEOMETRY_COARSE_CLIP) && BUZZ_GEOMETRY_COARSE_CLIP
        true;
#else
        false;
#endif
    constexpr bool native_reciprocal=
#if defined(BUZZ_GEOMETRY_NATIVE_RECIPROCAL) && BUZZ_GEOMETRY_NATIVE_RECIPROCAL
        true;
#else
        false;
#endif
    // Strict published-byte audits retain reference projection and padding.
    // A relaxed reciprocal can change a thin triangle's zero-area decision.
    if constexpr(zero_padding || native_reciprocal) {
        const int mode=buzz_geometry_job_mode();
        if constexpr(native_reciprocal) {
            if(mode==2)
                return prepare_geometry_host_job_impl<coarse,lazy,coarse,false,true>(c,e,w,reason);
        }
        if(zero_padding && mode==1)
            return prepare_geometry_host_job_impl<coarse,lazy,coarse,true>(c,e,w,reason);
    }
    return prepare_geometry_host_job_impl<coarse,lazy,coarse,false>(c,e,w,reason);
}
inline void commit_geometry_host_job_data(spu_context& c,const GeometryJobEntry& e,
    const GeometryHostJobWorkspace& w)
{
    const unsigned manager=e.geometry_context_lsa;
    const char* reason=nullptr;
    if (!w.ready || !emit_geometry_host_indices(w.indices,c.ls+w.index_source,w.index_bytes,&reason))
        buzz_geometry_job_native_fail(&c,reason?reason:"host job data not ready");
    geometry_job_put32(c.ls+manager+0x50,w.index_source);
    geometry_job_put32(c.ls+manager+0x58,w.index_source+w.index_bytes);
    c.ls[manager+0x180]=uint8_t((w.indices.triangles*3)>>8);
    c.ls[manager+0x181]=uint8_t(w.indices.triangles*3);
    // Semantic bindings used by the remaining publication/command flow.
    // Their pointed-to float4 tables are not constructed or consumed.
    const unsigned table_stride=((e.vertices+7)&~7u)*16;
    for (unsigned field=0;field<w.input.fields;++field) {
        const unsigned semantic=w.input.field[field].semantic;
        c.ls[manager+0x160+field]=uint8_t(semantic);
        if (semantic>=1 && semantic<=4)
            geometry_job_put32(c.ls+manager+0x58+semantic*4,e.scratch_lsa+field*table_stride);
    }
}
inline void commit_geometry_host_job_vertices(spu_context& c,const GeometryJobEntry& e,
    const GeometryHostJobWorkspace& w)
{
    const unsigned manager=e.geometry_context_lsa;
    const unsigned destination=geometry_job_be32(c.ls+manager+0x58);
    const char* reason=nullptr;
    if (destination!=w.index_source+w.index_bytes ||
        !emit_geometry_host_vertices(w.input,w.data,w.vertices,w.tail,c.ls+destination,w.vertices.output.bytes,&reason))
        buzz_geometry_job_native_fail(&c,reason?reason:"host vertex cursor invariant");
    geometry_job_put32(c.ls+manager+0x54,destination);
    geometry_job_put32(c.ls+manager+0x58,destination+w.vertices.output.bytes);
}
} // namespace buzz
