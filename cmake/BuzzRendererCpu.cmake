# Port-owned adapter; keep the pinned SDK source immutable on every platform.
option(BUZZ_RENDERER_CPU_REFINEMENTS "Experimental shader metadata and vertex preparation refinements" OFF)
option(BUZZ_RENDERER_FETCH_PROFILE "Diagnostic vertex-format counters" OFF)
option(BUZZ_RENDERER_VERTEX_FETCH "Specialized vertex conversion" OFF)
set(renderer_fetch_profile 0)
if(BUZZ_RENDERER_FETCH_PROFILE)
    set(renderer_fetch_profile 1)
endif()
set(renderer_vertex_fetch 0)
if(BUZZ_RENDERER_VERTEX_FETCH)
    set(renderer_vertex_fetch 1)
endif()
set(sdk_engine "${PS3RECOMP_DIR}/libs/video/rsx_draw_engine.c")
set(patched_engine "${CMAKE_CURRENT_BINARY_DIR}/buzz_rsx_draw_engine.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_engine}")
file(READ "${sdk_engine}" engine_text)
string(REPLACE "\r\n" "\n" engine_text "${engine_text}")
set(anchor "rsx_vp_analyze_inputs(uc, instrs * 16u, &analysis)")
string(FIND "${engine_text}" "${anchor}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Renderer vertex input cache no longer matches the SDK")
endif()
string(REPLACE "${anchor}" "buzz_vp_analyze_cached(&buzz_renderer_vp_cache, uc, instrs * 16u, &analysis)" engine_text "${engine_text}")
set(constants_anchor "    memset(&g.fp_constants, 0, sizeof g.fp_constants);\n    if (!fixed && rsx_fp_collect_constants(fp_uc, fp_size, &g.fp_constants) < 0)")
string(FIND "${engine_text}" "${constants_anchor}" constants_found)
if(constants_found EQUAL -1)
    message(FATAL_ERROR "Renderer constant collection no longer matches the SDK")
endif()
# collect_constants clears the entire block itself for every non-null program,
# including malformed programs. Preserve the clear for fixed/null programs.
string(REPLACE "${constants_anchor}"
    "    if (fixed || !fp_uc) memset(&g.fp_constants, 0, sizeof g.fp_constants);\n    if (!fixed && rsx_fp_collect_constants(fp_uc, fp_size, &g.fp_constants) < 0)"
    engine_text "${engine_text}")
set(fp_hash_anchor "rsx_fp_structural_hash(fp_uc, fp_size, key)")
string(FIND "${engine_text}" "${fp_hash_anchor}" fp_hash_found)
if(fp_hash_found EQUAL -1)
    message(FATAL_ERROR "Renderer fragment hash no longer matches the SDK")
endif()
string(REPLACE "${fp_hash_anchor}" "buzz_renderer_fp_hash(fp_uc, fp_size, key)" engine_text "${engine_text}")
set(renderer_refinements 0)
if(BUZZ_RENDERER_CPU_REFINEMENTS)
set(renderer_refinements 1)
function(buzz_renderer_replace old new)
    string(FIND "${engine_text}" "${old}" location)
    if(location EQUAL -1)
        message(FATAL_ERROR "Renderer CPU adapter no longer matches the SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" updated "${engine_text}")
    set(engine_text "${updated}" PARENT_SCOPE)
endfunction()
buzz_renderer_replace("const u32 sampled_units = fixed ? 0 : rsx_fp_texture_units(fp_uc, fp_size);"
    "const buzz_fp_usage usage = buzz_fp_usage_read(fixed ? NULL : fp_uc, fp_size);\n    const u32 sampled_units = usage.sampled;")
buzz_renderer_replace("rsx_fp_texture_gamma_units(fp_uc, fp_size, gamma_masks)"
    "buzz_fp_usage_mask(usage.gamma, gamma_masks)")
buzz_renderer_replace("rsx_fp_texture_expand_units(fp_uc, fp_size, expand_masks)"
    "buzz_fp_usage_mask(usage.expand, expand_masks)")
# The shadow loop already visits exactly the sampled units. Derive the two
# colour masks from that same immutable texture state; unused masks stay zero.
buzz_renderer_replace("    u8 gamma_masks[RSX_DSP_NUM_TEXTURES] = {0};\n    if (!fixed) for (u32 u = 0; u < RSX_DSP_NUM_TEXTURES; ++u) {\n        rsx_dsp_texture t; rsx_dsp_get_texture(&g.rsx, u, &t);\n        if (t.enabled) gamma_masks[u] = rsx_texture_gamma_mask(t.format, t.remap, t.filter, t.wrap);\n    }"
    "")
buzz_renderer_replace("    u8 expand_masks[RSX_DSP_NUM_TEXTURES] = {0};\n    if (!fixed) for (u32 u = 0; u < RSX_DSP_NUM_TEXTURES; ++u) {\n        rsx_dsp_texture t; rsx_dsp_get_texture(&g.rsx, u, &t);\n        if (t.enabled) expand_masks[u] = rsx_texture_expand_mask(t.format, t.remap, t.filter, t.wrap);\n    }"
    "")
buzz_renderer_replace("    rsx_fp_shadow_desc shadow[RSX_DSP_NUM_TEXTURES] = {{0}};"
    "    u8 gamma_masks[RSX_DSP_NUM_TEXTURES] = {0};\n    u8 expand_masks[RSX_DSP_NUM_TEXTURES] = {0};\n    rsx_fp_shadow_desc shadow[RSX_DSP_NUM_TEXTURES] = {{0}};")
buzz_renderer_replace("        if (shadow[u].enabled) shadow_mask |= 1u << u;"
    "        if (shadow[u].enabled) shadow_mask |= 1u << u;\n        if (t.enabled) {\n            gamma_masks[u] = rsx_texture_gamma_mask(t.format, t.remap, t.filter, t.wrap);\n            expand_masks[u] = rsx_texture_expand_mask(t.format, t.remap, t.filter, t.wrap);\n        }")
buzz_renderer_replace("eng_fnv1a(vp_uc, vp_instrs * 16u, key)"
    "buzz_program_hash_cached(&buzz_renderer_vertex_hash, vp_uc, vp_instrs * 16u, key)")
buzz_renderer_replace("rsx_fp_collect_constants(fp_uc, fp_size, &g.fp_constants)"
    "buzz_renderer_collect_constants(fp_uc, fp_size, &g.fp_constants)")
endif()
set(renderer_prefix [=[
#define BUZZ_RENDERER_CPU_REFINEMENTS @renderer_refinements@
#define BUZZ_RENDERER_FETCH_PROFILE @renderer_fetch_profile@
#define BUZZ_RENDERER_VERTEX_FETCH @renderer_vertex_fetch@
#include "vertex_program_cache.h"
#include "fragment_program_cache.h"
#include "renderer_vertex_remap.h"
#include "renderer_vertex_plan.h"
#include "renderer_vertex_fetch.h"
#if BUZZ_RENDERER_CPU_REFINEMENTS
#include "renderer_program_data.h"
#endif
#include <stdlib.h>
#include <stdio.h>
static buzz_vp_cache buzz_renderer_vp_cache;
static buzz_fp_hash_cache buzz_renderer_fp_cache;
static int buzz_renderer_fetch(const rsx_vertex_fetch_plan* plan,const rsx_vertex_ref* refs,u32 count,u8* out) {
    static int enabled=-1;
    if(enabled<0) {
        const char* option=getenv("BUZZ_VERTEX_FETCH");
        enabled=option&&*option?!strcmp(option,"1"):BUZZ_RENDERER_VERTEX_FETCH;
        fprintf(stderr,"[rsx engine] specialized vertex fetch=%d\n",enabled);
    }
#if BUZZ_RENDERER_FETCH_PROFILE
    buzz_vertex_fetch_profile(plan,count);
#endif
    return enabled?buzz_vertex_fetch_many(plan,refs,count,out):rsx_vertex_fetch_many(plan,refs,count,out);
}
static void buzz_renderer_vertex_plan(rsx_vertex_fetch_plan* plan,const rsx_dispatch* rsx,
    const rsx_vertex_layout_plan* layout,rsx_vertex_guest_ptr_fn guest_ptr,void* user) {
    static int enabled=-1;
    if(enabled<0) {
        const char* option=getenv("BUZZ_VERTEX_PLAN_ACTIVE_ONLY");
        enabled=option&&*option?!strcmp(option,"1"):BUZZ_RENDERER_CPU_REFINEMENTS;
        fprintf(stderr,"[rsx engine] active vertex descriptors=%d\n",enabled);
    }
    if(enabled)buzz_vertex_plan_init(plan,rsx,layout,guest_ptr,user);
    else rsx_vertex_fetch_plan_init(plan,rsx,layout,guest_ptr,user);
}
static int buzz_renderer_remap(rsx_vertex_remap* remap, rsx_vertex_ref* refs, u32 count, u32* unique) {
    static int enabled=-1;
    if(enabled<0) {
        const char* option=getenv("BUZZ_VERTEX_REMAP_SIMD");
        enabled=option&&*option?!strcmp(option,"1"):BUZZ_RENDERER_CPU_REFINEMENTS;
        fprintf(stderr,"[rsx engine] SIMD vertex remap=%d\n",enabled);
    }
    return enabled?buzz_vertex_remap_build(remap,refs,count,unique):rsx_vertex_remap_build(remap,refs,count,unique);
}
#if BUZZ_RENDERER_CPU_REFINEMENTS
static buzz_byte_hash_cache buzz_renderer_vertex_hash;
#endif
static u64 buzz_renderer_fp_hash(const u8* code,u32 bytes,u64 seed) {
    static int enabled=-1;
    if(enabled<0) {
        const char* option=getenv("BUZZ_FP_HASH_CACHE");
        enabled=!(option&&!strcmp(option,"0"));
        fprintf(stderr,"[rsx engine] exact fragment hash cache=%d\n",enabled);
    }
    return enabled?buzz_fp_hash_cached(&buzz_renderer_fp_cache,code,bytes,seed):rsx_fp_structural_hash(code,bytes,seed);
}
]=])
set(remap_call "rsx_vertex_remap_build(&dc.ref_remap, dc.refs, dc.n_refs, &unique)")
string(FIND "${engine_text}" "${remap_call}" remap_found)
if(remap_found EQUAL -1)
    message(FATAL_ERROR "Renderer vertex remap adapter no longer matches the SDK")
endif()
string(REPLACE "${remap_call}" "buzz_renderer_remap(&dc.ref_remap, dc.refs, dc.n_refs, &unique)" engine_text "${engine_text}")
set(plan_call "rsx_vertex_fetch_plan_init(&dc.fetch_plan, &g.rsx, layout, eng_guest_ptr, NULL)")
string(FIND "${engine_text}" "${plan_call}" plan_found)
if(plan_found EQUAL -1)
    message(FATAL_ERROR "Renderer vertex plan adapter no longer matches the SDK")
endif()
string(REPLACE "${plan_call}" "buzz_renderer_vertex_plan(&dc.fetch_plan, &g.rsx, layout, eng_guest_ptr, NULL)" engine_text "${engine_text}")
set(fetch_call "rsx_vertex_fetch_many(&dc.fetch_plan,dc.refs,dc.n_refs,dc.verts)")
string(FIND "${engine_text}" "${fetch_call}" fetch_found)
if(fetch_found EQUAL -1)
    message(FATAL_ERROR "Renderer vertex fetch adapter no longer matches the SDK")
endif()
string(REPLACE "${fetch_call}" "buzz_renderer_fetch(&dc.fetch_plan,dc.refs,dc.n_refs,dc.verts)" engine_text "${engine_text}")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererTextures.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererIndex.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererDirectDraw.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererConstantRanges.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererVertexStream.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererPipelineLookup.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BuzzRendererIndexPrepare.cmake")
file(CONFIGURE OUTPUT "${patched_engine}" CONTENT "${renderer_prefix}\n${engine_text}" @ONLY)
get_target_property(sdk_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM sdk_sources "${sdk_engine}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${sdk_sources}")
target_sources(ps3recomp_runtime PRIVATE "${patched_engine}")
set_source_files_properties("${patched_engine}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
