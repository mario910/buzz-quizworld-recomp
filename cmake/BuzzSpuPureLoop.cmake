# Add the port's bounded-loop accounting beside the pinned drain's private
# history buffers. Do not modify or fork the SDK's execution context ABI.
set(sdk_drain "${PS3RECOMP_DIR}/runtime/spu/spu_drain.c")
set(pure_loop_runtime "${CMAKE_CURRENT_SOURCE_DIR}/spu_pure_loop_runtime.inc")
set(patched_drain "${CMAKE_CURRENT_BINARY_DIR}/buzz_spu_drain.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_drain}" "${pure_loop_runtime}")
file(READ "${sdk_drain}" drain_text)
string(REPLACE "\r\n" "\n" drain_text "${drain_text}")
foreach(anchor IN ITEMS "static _Thread_local uint32_t t_recent[32];" "static int spu_launch_check_slow(void)" "static int spu_launch_no_ls0(void)")
    string(FIND "${drain_text}" "${anchor}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pure-loop accounting no longer matches the pinned SPU drain")
    endif()
endforeach()
# These private fields are read together on every SPU block. Keep them in one
# TLS object so Android resolves one address instead of two dynamic TLS slots.
# Public trampoline/history symbols and all hook semantics remain unchanged.
set(history_fields "static _Thread_local uint32_t t_recent[32];\nstatic _Thread_local uint32_t t_recent_n;")
string(FIND "${drain_text}" "${history_fields}" history_found)
if(history_found EQUAL -1)
    message(FATAL_ERROR "SPU private history layout no longer matches the SDK")
endif()
string(REPLACE "${history_fields}" "static _Thread_local struct { uint32_t recent[32]; uint32_t count; } buzz_drain_history;\n#define t_recent buzz_drain_history.recent\n#define t_recent_n buzz_drain_history.count" drain_text "${drain_text}")
file(READ "${pure_loop_runtime}" pure_loop_text)
file(CONFIGURE OUTPUT "${patched_drain}" CONTENT "${drain_text}\n${pure_loop_text}" @ONLY)
get_target_property(sdk_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM sdk_sources "${sdk_drain}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${sdk_sources}")
target_sources(ps3recomp_runtime PRIVATE "${patched_drain}")
set_source_files_properties("${patched_drain}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu")
