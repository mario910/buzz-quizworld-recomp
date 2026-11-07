/* One owned host data pipeline, prepared before any job mutation. The worker
 * retains this storage through final publication; no LS float4 bridge. */
#pragma once
#include "native_geometry_host_vertex_job.h"
#include "native_geometry_host_cull_job.h"
#include "native_geometry_host_index_output.h"
#include "native_geometry_coarse_clip.h"
#include "native_geometry_job.h"

namespace buzz {
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
template<bool CoarseClip, bool LazyAttributes = false, bool BoundedProjection = false, bool ZeroVertexPadding = false>
inline bool prepare_geometry_host_job_impl(const spu_context& c,const GeometryJobEntry& e,
    GeometryHostJobWorkspace& w,const char** reason=nullptr)
{
    w.ready=false;
    w.bounded_projection=false;
    // The outer driver can fetch additional weighted blend streams after
    // ordinary attributes. Those jobs need the complete lifted flow until
    // their host data operation is supported; the base streams alone are
    // not the job's input, even when its primary descriptor is recognized.
    if (geometry_job_be32(e.user.data()+8)&0xffff) {
        if(reason)*reason="host job additional blend streams";return false;
    }
    if (!read_geometry_host_input_recipe(c,e,w.input,reason) ||
        !read_geometry_host_vertex_recipe(c,e,w.input,w.vertices,reason) ||
        !read_geometry_host_skin_recipe(c,e,w.input,w.skin,reason)) return false;
    const bool deferred=LazyAttributes && !w.skin.active;
    if(deferred) {
        if(!capture_geometry_host_inputs_phase<GeometryHostInputPhase::Positions>(c,w.input,w.data,reason) ||
           !skin_geometry_host_inputs_phase<GeometryHostInputPhase::Positions>(c,e,w.input,w.data,w.skin_workspace,reason))return false;
    } else if(!capture_geometry_host_inputs(c,w.input,w.data,reason) ||
              !skin_geometry_host_inputs(c,e,w.input,w.data,w.skin_workspace,reason))return false;
    if constexpr(CoarseClip) {
        if(!decode_geometry_host_indices(c,e,w.indices,reason))return false;
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
            if(!project_geometry_host_inputs(c,e,w.input,w.data,w.clips,reason,&parameters,bounded) ||
               !cull_geometry_host_triangles(c,e,w.clips,w.indices,reason))return false;
        }
    } else if (
        !project_geometry_host_inputs(c,e,w.input,w.data,w.clips,reason) ||
        !decode_geometry_host_indices(c,e,w.indices,reason) ||
        !cull_geometry_host_triangles(c,e,w.clips,w.indices,reason)) return false;
    if(deferred && w.indices.triangles &&
       (!capture_geometry_host_inputs_phase<GeometryHostInputPhase::Attributes>(c,w.input,w.data,reason) ||
        !skin_geometry_host_inputs_phase<GeometryHostInputPhase::Attributes>(c,e,w.input,w.data,w.skin_workspace,reason)))return false;
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
    // Paired strict audits retain the original padding payload/ABI contract.
    // The relaxed policy belongs only to the ordinary native job controller.
    if constexpr(zero_padding) {
        if(buzz_geometry_job_mode()==1)
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
