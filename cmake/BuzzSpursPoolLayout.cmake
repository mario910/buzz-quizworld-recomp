# Separate the polling hints from lock/bookkeeping writes. No scheduling,
# ownership, queue ordering or memory-order rules are changed.
option(BUZZ_SPURS_POOL_LAYOUT "Separate SPURS polling and bookkeeping cache lines" OFF)
if(NOT BUZZ_SPURS_POOL_LAYOUT)
    return()
endif()
set(pool_layout_input "${PS3RECOMP_DIR}/runtime/spu/spurs_job_pool.c")
file(READ "${pool_layout_input}" pool_layout_text)
string(REPLACE "\r\n" "\n" pool_layout_text "${pool_layout_text}")
set(pool_layout_old [=[
    SRWLOCK admission, lock;
    CONDITION_VARIABLE work, done;
    HANDLE threads[5];
    unsigned workers;
    uint32_t instance;
    int initialized, stopping;
    Batch* head;
    Batch* tail;
    unsigned active_batches;
    unsigned busy_lanes, borrowed_lanes;
    int caller_help;
]=])
string(FIND "${pool_layout_text}" "${pool_layout_old}" pool_layout_found)
if(pool_layout_found EQUAL -1)
    message(FATAL_ERROR "SPURS pool layout no longer matches the pinned SDK")
endif()
string(REPLACE "${pool_layout_old}" [=[
    _Alignas(64) SRWLOCK admission;
    _Alignas(64) SRWLOCK lock;
    CONDITION_VARIABLE work, done;
    HANDLE threads[5];
    unsigned workers;
    uint32_t instance;
    int initialized;
    /* Pollers share one mostly-read line. Claim/completion bookkeeping must
     * not invalidate this line on every job while the queue is empty. */
    _Alignas(64) int stopping;
    Batch* head;
    unsigned borrowed_lanes;
    _Alignas(64) Batch* tail;
    unsigned active_batches;
    unsigned busy_lanes;
    int caller_help;
]=] pool_layout_text "${pool_layout_text}")
set(pool_layout_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_spurs_pool_layout.c")
file(CONFIGURE OUTPUT "${pool_layout_output}" CONTENT "${pool_layout_text}" @ONLY)
get_target_property(pool_layout_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM pool_layout_sources "${pool_layout_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${pool_layout_sources}")
target_sources(ps3recomp_runtime PRIVATE "${pool_layout_output}")
set_source_files_properties("${pool_layout_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu")
