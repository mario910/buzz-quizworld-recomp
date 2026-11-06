# A consumer-side readiness hint, with no producer-store hooks or polling loop.
option(BUZZ_RSX_READY_CHECK "Avoid sleeping when a completed FIFO drain has new work" OFF)
if(NOT BUZZ_RSX_READY_CHECK)
    return()
endif()
set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_RSX_READY_CHECK=1)
set(ready_input "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
if(BUZZ_RENDERER_STATE_PACKETS)
    set(ready_input "${constant_gcm_output}")
elseif(BUZZ_RENDERER_WAIT_TRACE)
    set(ready_input "${wait_gcm_output}")
endif()
file(READ "${ready_input}" ready_text)
string(REPLACE "\r\n" "\n" ready_text "${ready_text}")
function(buzz_ready_replace old new)
    string(FIND "${ready_text}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "FIFO readiness adapter no longer matches SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" ready_text "${ready_text}")
    set(ready_text "${ready_text}" PARENT_SCOPE)
endfunction()
buzz_ready_replace("static void gcm_rsx_process_fifo_unlocked(void);" [=[
static int buzz_fifo_retry;
/* Only the FIFO lock owner writes/reads the hint and the actual consumer head.
 * A failed/parked/acquire/budget drain never makes GET != PUT sufficient. */
int buzz_gcm_fifo_ready(void) {
    if(!TryAcquireSRWLockExclusive(&s_gcm_fifo_lock))return 0;
    const int ready=buzz_fifo_retry && s_fifo_getoff!=vm_read32(GCM_CONTROL_GUEST_ADDR);
    ReleaseSRWLockExclusive(&s_gcm_fifo_lock);
    return ready;
}
static void gcm_rsx_process_fifo_unlocked(void);
]=])
buzz_ready_replace("static void gcm_rsx_process_fifo_unlocked(void)\n{"
    "static void gcm_rsx_process_fifo_unlocked(void)\n{\n    buzz_fifo_retry=0;")
set(ready_anchor "    /* A drain stopped on a semaphore acquire (GCM_SEMA_ACQUIRE) holds `get`")
buzz_ready_replace("${ready_anchor}" [=[
    buzz_fifo_retry=!s_sema_blocked_pass && !s_park_pass &&
        (s_fifo_getoff==put || !strcmp(why,"flip"));
    /* A drain stopped on a semaphore acquire (GCM_SEMA_ACQUIRE) holds `get`
]=])
set(ready_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_gcm_ready.c")
file(CONFIGURE OUTPUT "${ready_output}" CONTENT "${ready_text}" @ONLY)
get_target_property(ready_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM ready_sources "${ready_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${ready_sources}")
target_sources(ps3recomp_runtime PRIVATE "${ready_output}")
set_source_files_properties("${ready_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
