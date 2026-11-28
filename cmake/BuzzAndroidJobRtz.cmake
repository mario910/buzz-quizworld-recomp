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
# The specialized loop handles copying and padding. The SDK reads each
# descriptor and owns deferred clearing, cache inputs and publication.
option(BUZZ_SPURS_DIRECT_INPUTS "Stage the 21-entry geometry DMA list without lookahead" OFF)
if(BUZZ_SPURS_DIRECT_INPUTS)
    function(buzz_replace_android_job_transfer original replacement)
        string(FIND "${job_text}" "${original}" first)
        string(FIND "${job_text}" "${original}" last REVERSE)
        if(first LESS 0 OR NOT first EQUAL last)
            message(FATAL_ERROR "Android SPURS adapter requires exactly one SDK transfer: ${original}")
        endif()
        string(REPLACE "${original}" "${replacement}" updated "${job_text}")
        set(job_text "${updated}" PARENT_SCOPE)
    endfunction()
    buzz_replace_android_job_transfer(
        "memcpy(ls + from, vm_base + ea_bin + from, size_bin - from);"
        "buzz_spurs_copy_job_image(ls + from, vm_base + ea_bin + from, size_bin - from, image_id);")
    buzz_replace_android_job_transfer(
        "memset(ls + size_bin, 0, io_ls - size_bin);"
        "buzz_spurs_zero_job_region(ls + size_bin, io_ls - size_bin, image_id);")
    buzz_replace_android_job_transfer(
        "memset(ls + from, 0, margin - from);"
        "buzz_spurs_zero_job_region(ls + from, margin - from, image_id);")
    set(loop "    for (uint32_t i = 0; i < n_dma; i++) {")
    set(anchor "    uint32_t io_cur = io_ls, io_used = 0;
${loop}")
    string(FIND "${job_text}" "${anchor}" first)
    string(FIND "${job_text}" "${anchor}" last REVERSE)
    if(first LESS 0 OR NOT first EQUAL last)
        message(FATAL_ERROR "Android direct inputs require the audited SDK staging loop")
    endif()
    string(PREPEND job_text "#include \"spurs_geometry_input_copy.h\"\n")
    string(REPLACE "${anchor}" "    uint32_t io_cur = io_ls, io_used = 0;
    if (image_id == 41 && n_dma == 21) {
        io_used = buzz_spurs_stage_geometry_inputs(ls, vm_base, io_ls, SPU_LS_SIZE, dma_base, lazy_zero, g64);
        io_cur += io_used;
    } else
${loop}" job_text "${job_text}")
    set(descriptor_stage "    if (ls_zero) memset(ls + ctx_ls, 0, SPU_LS_SIZE - ctx_ls);
    memcpy(ls + desc_ls, vm_base + job_ea, dsz);")
    set(context_pointers "    ls32(ls, ctx_ls + JC_IO_BUFFER, io_ls);
    for (int i = 0; i < 4; i++)
        ls32(ls, ctx_ls + JC_CACHE_BUFFER + (uint32_t)i * 4u, cache_ls[i]);")
    set(anchor "${descriptor_stage}

${context_pointers}")
    string(FIND "${job_text}" "${anchor}" first)
    string(FIND "${job_text}" "${anchor}" last REVERSE)
    if(first LESS 0 OR NOT first EQUAL last)
        message(FATAL_ERROR "Android context staging requires the audited SDK copy and pointer stores")
    endif()
    string(REPLACE "${anchor}" "    if (image_id == 41 && n_dma == 21 && dsz == 256 && !n_cache &&
        desc_ls - ctx_ls == 64 && SPU_LS_SIZE - desc_ls == 1024) {
        buzz_spurs_geometry_io_context256(ls + ctx_ls, vm_base + job_ea, ls_zero, io_ls);
    } else {
        if (image_id == 41 && n_dma == 21 && dsz == 256) {
            buzz_spurs_geometry_descriptor256(ls, ctx_ls, desc_ls, SPU_LS_SIZE, vm_base + job_ea, ls_zero);
        } else {
            ${descriptor_stage}
        }
        ${context_pointers}
    }" job_text "${job_text}")
endif()
file(CONFIGURE OUTPUT "${spurs_job_source}" CONTENT "${job_text}" @ONLY)
get_target_property(sdk_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM sdk_sources "${sdk_job}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${sdk_sources}")
target_sources(ps3recomp_runtime PRIVATE "${spurs_job_source}")
set_source_files_properties("${spurs_job_source}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
