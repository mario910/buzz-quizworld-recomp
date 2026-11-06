option(BUZZ_RENDERER_CONSTANT_RANGES "Clear only previously active shader constants" OFF)
if(NOT BUZZ_RENDERER_CONSTANT_RANGES)
    return()
endif()
function(buzz_constant_range_replace old new)
    string(FIND "${engine_text}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Constant range adapter no longer matches SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" engine_text "${engine_text}")
    set(engine_text "${engine_text}" PARENT_SCOPE)
endfunction()
if(NOT BUZZ_RENDERER_CPU_REFINEMENTS)
    buzz_constant_range_replace("rsx_fp_collect_constants(fp_uc, fp_size, &g.fp_constants)"
        "buzz_renderer_collect_constants(fp_uc, fp_size, &g.fp_constants)")
    # The collector already exists in the broader experimental group. Enable
    # it independently, without changing shader hash/usage/texture policies.
    set(guarded_include "#if BUZZ_RENDERER_CPU_REFINEMENTS\n#include \"renderer_program_data.h\"\n#endif")
    string(FIND "${renderer_prefix}" "${guarded_include}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Constant range adapter cannot find program data include")
    endif()
    string(REPLACE "${guarded_include}"
        "#include \"renderer_program_data.h\"" renderer_prefix "${renderer_prefix}")
endif()
buzz_constant_range_replace("if (fixed || !fp_uc) memset(&g.fp_constants, 0, sizeof g.fp_constants);"
    "if (fixed || !fp_uc) buzz_renderer_clear_constants(&g.fp_constants);")
# The live payload is overwritten by memcpy and the complete alpha vec4 by
# stores below. Only the dummy first slot of a constant-free shader needs zero.
buzz_constant_range_replace("    memset(g.fp_cb, 0, fp_bytes);"
    "    if (!g.fp_constants.count) memset(g.fp_cb, 0, 16u);")
