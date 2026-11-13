# Keep the pinned SDK immutable. This adapter changes only pre-entry LS stores.
option(BUZZ_SPURS_INPUT_CLEAR "Skip clearing SPURS input bytes immediately overwritten by staging" OFF)
if(NOT BUZZ_SPURS_INPUT_CLEAR)
    return()
endif()
set(input_clear_source "${spurs_job_source}")
file(READ "${input_clear_source}" input_clear_text)
string(REPLACE "\r\n" "\n" input_clear_text "${input_clear_text}")
function(buzz_input_clear_replace old new)
    string(FIND "${input_clear_text}" "${old}" first)
    if(first LESS 0)
        message(FATAL_ERROR "SPURS input clear no longer matches the pinned staging code")
    endif()
    string(LENGTH "${old}" width)
    math(EXPR after "${first}+${width}")
    string(SUBSTRING "${input_clear_text}" ${after} -1 remainder)
    string(FIND "${remainder}" "${old}" duplicate)
    if(NOT duplicate EQUAL -1)
        message(FATAL_ERROR "Ambiguous SPURS input clear anchor")
    endif()
    string(REPLACE "${old}" "${new}" input_clear_text "${input_clear_text}")
    set(input_clear_text "${input_clear_text}" PARENT_SCOPE)
endfunction()
buzz_input_clear_replace([=[    if (ls_zero == 1 && p > size_bin)
        memset(ls + size_bin, 0, (p < SPU_LS_SIZE ? p : SPU_LS_SIZE) - size_bin);]=]
    [=[    buzz_spurs_input_clear input_clear = buzz_spurs_clear_regions(
        ls, size_bin, io_ls, size_io, p, ls_zero, buzz_spurs_input_clear_enabled);]=])
buzz_input_clear_replace([=[        memcpy(ls + io_cur, vm_base + eal, sz);]=]
    [=[        buzz_spurs_clear_input_gap(ls, &input_clear, io_cur, sz);
        memcpy(ls + io_cur, vm_base + eal, sz);]=])
buzz_input_clear_replace([=[    /* ---- context + descriptor, parked above the job's memory region ------]=]
    [=[    buzz_spurs_clear_input_tail(ls, &input_clear);

    /* ---- context + descriptor, parked above the job's memory region ------]=])
set(input_clear_header [=[
#include "spurs_input_clear.h"
#include <stdio.h>
#include <stdlib.h>
static int buzz_spurs_input_clear_enabled;
__attribute__((constructor)) static void buzz_spurs_input_clear_init(void) {
    const char* value=getenv("BUZZ_SPURS_INPUT_CLEAR");
    if(value && ((value[0]!='0' && value[0]!='1') || value[1]))abort();
    buzz_spurs_input_clear_enabled=!value || value[0]=='1';
    fprintf(stderr,"[spurs-input-clear] skip-overwritten-input=%d\n",buzz_spurs_input_clear_enabled);
}
]=])
set(spurs_job_source "${CMAKE_CURRENT_BINARY_DIR}/buzz_spurs_input_clear.c")
file(CONFIGURE OUTPUT "${spurs_job_source}" CONTENT "${input_clear_header}\n${input_clear_text}" @ONLY)
get_target_property(input_clear_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM input_clear_sources "${input_clear_source}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${input_clear_sources}")
target_sources(ps3recomp_runtime PRIVATE "${spurs_job_source}")
set_source_files_properties("${spurs_job_source}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input_clear_source}")
