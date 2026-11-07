option(BUZZ_SPU_JOB_RETURN "Complete admitted SPURS job-manager returns on the native driver stack" OFF)
if(NOT BUZZ_SPU_JOB_RETURN)
    return()
endif()
set(job_return_input "${PS3RECOMP_DIR}/runtime/spu/spu_channels.c")
if(BUZZ_SPURS_EXECUTION_TRACE)
    set(job_return_input "${exec_ch_output}")
endif()
file(READ "${job_return_input}" job_return_text)
function(buzz_job_return_require_unique text_variable fragment_variable)
    string(FIND "${${text_variable}}" "${${fragment_variable}}" fragment_at)
    if(fragment_at LESS 0)
        message(FATAL_ERROR "SPU job return adapter does not match pinned SDK: ${fragment_variable}")
    endif()
    math(EXPR fragment_after "${fragment_at}+1")
    string(SUBSTRING "${${text_variable}}" ${fragment_after} -1 fragment_remainder)
    string(FIND "${fragment_remainder}" "${${fragment_variable}}" fragment_duplicate)
    if(NOT fragment_duplicate LESS 0)
        message(FATAL_ERROR "SPU job return adapter has an ambiguous SDK anchor: ${fragment_variable}")
    endif()
endfunction()
set(job_return_block [=[
    if (!ctx->policy_mode && ctx->pc == SPU_JOB_RETURN_LS) {
        static int _n = 0;
        if (PS3_LOG_FIRST(_n, 4))
            fprintf(stderr, "[spurs-job] img=%d returned to the job manager \n",
                    ctx->image_id);
        spu_halt(ctx);
        return;
    }
]=])
set(job_return_anchor [=[
    spu_fn fn = NULL;
    int code_owner = 0;
    for (unsigned slot = 0; slot < 4; ++slot) {
]=])
foreach(job_return_fragment IN ITEMS job_return_block job_return_anchor)
    buzz_job_return_require_unique(job_return_text ${job_return_fragment})
endforeach()
# Everything before this point, including IRQ restoration, diagnostics and
# task/overlay residency, still runs. The old completion block remains for
# same-EXE rollback; successful completion never uses the lookup's result.
string(REPLACE "${job_return_anchor}"
    "    if (buzz_spu_job_return_enabled) {\n${job_return_block}    }\n${job_return_anchor}"
    job_return_text "${job_return_text}")
set(job_return_fast "${job_return_block}")
string(REPLACE "        spu_halt(ctx);" "        if (!buzz_spu_complete_job_return(ctx,job_return_direct)) spu_halt(ctx);"
    job_return_fast "${job_return_fast}")
# Replace the newly inserted copy only; the original fallback stays unchanged.
string(REPLACE "    if (buzz_spu_job_return_enabled) {\n${job_return_block}    }"
    "    if (buzz_spu_job_return_enabled) {\n${job_return_fast}    }" job_return_text "${job_return_text}")
set(job_return_driver [=[
int spu_run_with_halt(void (*entry)(spu_context*), spu_context* ctx)
{
    int halted = 0;
]=])
set(job_return_driver_new "${job_return_driver}    volatile buzz_spu_job_frame job_frame = {ctx,buzz_spu_job_current,0,0};\n    if (buzz_spu_job_return_enabled) buzz_spu_job_current=&job_frame;\n")
buzz_job_return_require_unique(job_return_text job_return_driver)
string(REPLACE "${job_return_driver}" "${job_return_driver_new}" job_return_text "${job_return_text}")
set(job_return_result "    return halted;")
buzz_job_return_require_unique(job_return_text job_return_result)
string(REPLACE "    return halted;"
    "    if (buzz_spu_job_return_enabled) {\n        if (job_frame.completed) halted=1;\n        buzz_spu_job_current=job_frame.parent;\n    }\n    return halted;"
    job_return_text "${job_return_text}")
set(job_return_cleanup "    yz_lockstep_unregister(ctx);")
buzz_job_return_require_unique(job_return_text job_return_cleanup)
string(REPLACE "${job_return_cleanup}"
    "    if (buzz_spu_job_return_enabled && job_frame.completed) buzz_spu_restore_job_fp(s_spu_halt_env);\n    yz_lockstep_unregister(ctx);"
    job_return_text "${job_return_text}")
set(job_return_resolver "void spu_indirect_branch(spu_context* ctx)\n{")
buzz_job_return_require_unique(job_return_text job_return_resolver)
string(REPLACE "${job_return_resolver}"
    "${job_return_resolver}\n    const int job_return_direct=buzz_spu_job_return_enabled ? buzz_spu_take_dispatch(ctx) : 0;"
    job_return_text "${job_return_text}")
# Instrument only this TU's actual driver drain, after its original observer,
# interrupt and PC-history hooks. Consume the marker at resolver entry so a
# resolver called by another guest callback cannot inherit driver ownership.
file(READ "${PS3RECOMP_DIR}/runtime/spu/spu_context.h" job_return_context)
set(job_return_drain_anchor "#define SPU_DRAIN(ctx)")
buzz_job_return_require_unique(job_return_context job_return_drain_anchor)
string(FIND "${job_return_context}" "#define SPU_DRAIN(ctx)" job_return_macro_at)
if(job_return_macro_at LESS 0)
    message(FATAL_ERROR "SPU job return adapter cannot find driver drain")
endif()
string(SUBSTRING "${job_return_context}" ${job_return_macro_at} -1 job_return_macro_tail)
string(FIND "${job_return_macro_tail}" "} while (0)" job_return_macro_end)
if(job_return_macro_end LESS 0)
    message(FATAL_ERROR "SPU job return adapter cannot find drain boundary")
endif()
math(EXPR job_return_macro_size "${job_return_macro_end}+11")
string(SUBSTRING "${job_return_macro_tail}" 0 ${job_return_macro_size} job_return_macro)
set(job_return_call "_tf(ctx);")
buzz_job_return_require_unique(job_return_macro job_return_call)
set(job_return_dispatch_hook [=[buzz_spu_mark_dispatch((ctx),_tf);                  \
            _tf(ctx);                                           \
            buzz_spu_clear_dispatch((ctx));]=])
string(REPLACE "_tf(ctx);" "${job_return_dispatch_hook}" job_return_macro "${job_return_macro}")
set(job_return_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_spu_job_return.c")
file(CONFIGURE OUTPUT "${job_return_output}" CONTENT "#include \"spu_job_return.h\"\n#undef SPU_DRAIN\n${job_return_macro}\n${job_return_text}" @ONLY)
get_target_property(job_return_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM job_return_sources "${job_return_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${job_return_sources}")
target_sources(ps3recomp_runtime PRIVATE "${job_return_output}")
set_source_files_properties("${job_return_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${job_return_input}" "${PS3RECOMP_DIR}/runtime/spu/spu_context.h")
