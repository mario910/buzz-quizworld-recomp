# Read-only draw observer (port/draw_observer.h) in the port's engine copy.
# The pinned SDK source stays untouched; the call sits just before the draw
# is issued, where constants, fetched vertices and the target size are final.
set(observer_anchor "    const u32 uploaded = indexed ? dc.n_verts : n_draw;\n")
string(FIND "${engine_text}" "${observer_anchor}" observer_found)
if(observer_found EQUAL -1)
    message(FATAL_ERROR "Renderer draw observer no longer matches the SDK")
endif()
string(REPLACE "${observer_anchor}" "${observer_anchor}    if (buzz_draw_observer && !pipeline_is_fixed) {
        buzz_draw_observation o;
        o.vs_constants = &g.rsx.constants[0][0];
        o.vs_constant_count = RSX_DSP_NUM_CONSTANTS;
        o.fs_constants = &g.fp_constants.values[0][0];
        o.fs_constant_count = g.fp_constants.count;
        o.vertices = dc.verts;
        o.vertex_count = uploaded;
        o.vertex_stride = layout.stride;
        o.position_offset = layout.slot_by_attr[0] >= 0 ? (int32_t)layout.slot_by_attr[0] * 16 : -1;
        o.target_width = W;
        o.target_height = H;
        buzz_draw_observer(&o);
    }
" engine_text "${engine_text}")
string(APPEND renderer_prefix "#include \"draw_observer.h\"\nbuzz_draw_observer_fn buzz_draw_observer;\n")
