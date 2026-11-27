# Preserve the SDK's job selection and HALT handling. Adapt only its FP scope
# on the Android build, leaving the pinned SDK checkout and desktop TU intact.
set(sdk_job "${spurs_job_source}")
set(spurs_job_source "${CMAKE_CURRENT_BINARY_DIR}/android_spurs_job.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_job}")
file(READ "${sdk_job}" job_text)
foreach(anchor IN ITEMS "#include <immintrin.h>" "unsigned saved_mxcsr = _mm_getcsr();" "_mm_setcsr(saved_mxcsr);")
    string(FIND "${job_text}" "${anchor}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Android SPURS FP adapter no longer matches the SDK")
    endif()
endforeach()
string(REPLACE "#include <immintrin.h>" "#include \"spu_arm_fp_state.h\"" job_text "${job_text}")
string(REPLACE "unsigned saved_mxcsr = _mm_getcsr();" "buzz_arm_fp_state saved_fp = buzz_arm_fp_save();" job_text "${job_text}")
string(REPLACE "_mm_setcsr((saved_mxcsr & ~0x6000u) | 0x6000u | 0x8040u | 0x1f80u);" "buzz_arm_fp_enter_rtz(saved_fp);" job_text "${job_text}")
string(REPLACE "_mm_setcsr(saved_mxcsr);" "buzz_arm_fp_restore(saved_fp);" job_text "${job_text}")
# The captured image-41 list has 21 slots and few consecutive sources.
# Stage each nonempty entry directly. The SDK still owns the copy, padding,
# bounds, deferred clear, cache inputs and publication.
option(BUZZ_SPURS_DIRECT_INPUTS "Stage the 21-entry geometry DMA list without lookahead" OFF)
if(BUZZ_SPURS_DIRECT_INPUTS)
    set(anchor "        while (!(sz & 15u) && i + 1 < n_dma) {")
    set(prefetch "        if (i + 1 < n_dma) {")
    set(loop "    uint32_t io_cur = io_ls, io_used = 0;")
    set(input_copy "        memcpy(ls + io_cur, vm_base + eal, sz);")
    foreach(pattern IN ITEMS "${loop}" "${anchor}" "${prefetch}" "${input_copy}")
        string(FIND "${job_text}" "${pattern}" first)
        string(FIND "${job_text}" "${pattern}" last REVERSE)
        if(first LESS 0 OR NOT first EQUAL last)
            message(FATAL_ERROR "Android direct inputs require the audited SDK staging loop")
        endif()
    endforeach()
    string(REPLACE "${loop}" "${loop}
    const int direct_inputs = image_id == 41 && n_dma == 21;" job_text "${job_text}")
    string(REPLACE "${anchor}" "        while (!direct_inputs && !(sz & 15u) && i + 1 < n_dma) {" job_text "${job_text}")
    string(REPLACE "${prefetch}" "        if (!direct_inputs && i + 1 < n_dma) {" job_text "${job_text}")
    string(PREPEND job_text "#include \"spurs_geometry_input_copy.h\"\n")
    string(REPLACE "${input_copy}"
        "        if (direct_inputs) buzz_spurs_geometry_input_copy(ls + io_cur, vm_base + eal, sz);\n        else memcpy(ls + io_cur, vm_base + eal, sz);"
        job_text "${job_text}")
endif()
file(CONFIGURE OUTPUT "${spurs_job_source}" CONTENT "${job_text}" @ONLY)
get_target_property(sdk_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM sdk_sources "${sdk_job}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${sdk_sources}")
target_sources(ps3recomp_runtime PRIVATE "${spurs_job_source}")
set_source_files_properties("${spurs_job_source}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
