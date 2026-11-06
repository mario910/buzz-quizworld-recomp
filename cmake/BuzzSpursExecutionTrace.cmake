option(BUZZ_SPURS_EXECUTION_TRACE "Diagnostic SPURS execution CPU counters and native-thread channel waits" OFF)
if(NOT BUZZ_SPURS_EXECUTION_TRACE)
    return()
endif()
set(exec_job_input "${spurs_job_source}")
file(READ "${exec_job_input}" exec_job_text)
set(exec_job_anchor "    spurs_job_enter(entry, image_id, ctx);")
string(FIND "${exec_job_text}" "${exec_job_anchor}" exec_found)
if(exec_found LESS 0)
    message(FATAL_ERROR "SPURS execution trace no longer matches job entry")
endif()
string(REPLACE "${exec_job_anchor}" [=[
    const uint64_t exec_start = PS3_FT_ON() ? ps3_ft_now() : 0;
    const uint64_t exec_cpu = exec_start ? buzz_spurs_cpu_stamp() : 0;
    spurs_job_enter(entry, image_id, ctx);
    buzz_spurs_execution_end(image_id, exec_start, exec_cpu);
]=] exec_job_text "${exec_job_text}")
set(spurs_job_source "${CMAKE_CURRENT_BINARY_DIR}/buzz_spurs_execution.c")
file(CONFIGURE OUTPUT "${spurs_job_source}" CONTENT "#include \"spurs_execution_trace.h\"\n${exec_job_text}" @ONLY)
set(exec_ch_input "${PS3RECOMP_DIR}/runtime/spu/spu_channels.c")
file(READ "${exec_ch_input}" exec_ch_text)
set(exec_ch_anchor "    if (ft_t0) spu_ft_wait_span(ctx, channel, ft_t0);")
string(FIND "${exec_ch_text}" "${exec_ch_anchor}" exec_found)
if(exec_found LESS 0)
    message(FATAL_ERROR "SPURS execution trace no longer matches channel wait")
endif()
string(REPLACE "${exec_ch_anchor}" [=[
    if (ft_t0) {
        ps3_ft_span(118, GetCurrentThreadId(), channel, ctx->pc & SPU_LS_MASK, ft_t0, ps3_ft_now());
        spu_ft_wait_span(ctx, channel, ft_t0);
    }
]=] exec_ch_text "${exec_ch_text}")
set(exec_ch_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_spu_channel_trace.c")
file(CONFIGURE OUTPUT "${exec_ch_output}" CONTENT "${exec_ch_text}" @ONLY)
get_target_property(exec_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM exec_sources "${exec_job_input}" "${exec_ch_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${exec_sources}")
target_sources(ps3recomp_runtime PRIVATE "${spurs_job_source}" "${exec_ch_output}")
set_source_files_properties("${spurs_job_source}" "${exec_ch_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${exec_job_input}" "${exec_ch_input}")
