option(BUZZ_RENDERER_DIRECT_DRAW "Eliminate redundant array remap and triangle index copy" OFF)
if(NOT BUZZ_RENDERER_DIRECT_DRAW)
    return()
endif()
function(buzz_direct_draw_replace old new)
    string(FIND "${engine_text}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Direct draw adapter no longer matches SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" engine_text "${engine_text}")
    set(engine_text "${engine_text}" PARENT_SCOPE)
endfunction()
# A single array contains distinct u32 vertex IDs, including wrap. It needs
# no duplicate search; mixed sources/multiple batches retain the SDK path.
buzz_direct_draw_replace("    if (!buzz_fused && allow_remap && dc.n_refs > 1) {"
    "    if (!buzz_fused && allow_remap && dc.n_refs > 1 && !(dc.n_arr==1 && dc.n_idx==0)) {")
# A restart-free remapped triangle list already IS the final index list.
# Its lifetime lasts through the synchronous backend draw callback. Retain
# expansion for every other topology and cut configuration.
buzz_direct_draw_replace("    if (indexed) {\n        if (!dc_reserve_indices(n_draw)) return;"
    "    const u32* buzz_draw_indices=NULL;\n    if(indexed && prim==RSX_PRIMITIVE_TRIANGLES && !dc.n_cuts && dc.refs_remapped)\n        buzz_draw_indices=dc.ref_remap.occurrence_to_unique;\n    else if (indexed) {\n        if (!dc_reserve_indices(n_draw)) return;")
buzz_direct_draw_replace("indexed ? g.indices : NULL, indexed ? n_draw : 0"
    "indexed ? (buzz_draw_indices?buzz_draw_indices:g.indices) : NULL, indexed ? n_draw : 0")
