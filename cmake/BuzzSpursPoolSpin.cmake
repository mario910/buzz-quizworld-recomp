# Limit only automatic worker polling. Small CPUs still park immediately;
# explicit environment settings and the independent JTS policy retain priority.
set(BUZZ_SPURS_POOL_AUTO_SPIN_US "-1" CACHE STRING "Automatic SPURS worker poll on 12+ logical CPUs (-1: SDK policy, 0..1000 us)")
if(NOT BUZZ_SPURS_POOL_AUTO_SPIN_US MATCHES "^(-1|0|[1-9][0-9]*)$" OR
   BUZZ_SPURS_POOL_AUTO_SPIN_US GREATER 1000)
    message(FATAL_ERROR "BUZZ_SPURS_POOL_AUTO_SPIN_US must be -1 or 0..1000")
endif()
if(BUZZ_SPURS_POOL_AUTO_SPIN_US LESS 0)
    return()
endif()
set(pool_spin_input "${PS3RECOMP_DIR}/runtime/spu/spurs_job_pool.c")
if(BUZZ_SPURS_POOL_LAYOUT)
    set(pool_spin_input "${pool_layout_output}")
endif()
file(READ "${pool_spin_input}" pool_spin_text)
set(pool_spin_anchor "const long us = automatic ? (cpus >= 12 ? 1000 : 0) : atol(e);")
string(FIND "${pool_spin_text}" "${pool_spin_anchor}" pool_spin_found)
if(pool_spin_found LESS 0)
    message(FATAL_ERROR "Automatic SPURS worker poll no longer matches SDK")
endif()
string(REPLACE "${pool_spin_anchor}"
    "const long us = automatic ? (cpus >= 12 ? ${BUZZ_SPURS_POOL_AUTO_SPIN_US} : 0) : atol(e);"
    pool_spin_text "${pool_spin_text}")
set(pool_spin_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_spurs_pool_spin.c")
file(CONFIGURE OUTPUT "${pool_spin_output}" CONTENT "${pool_spin_text}" @ONLY)
get_target_property(pool_spin_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM pool_spin_sources "${pool_spin_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${pool_spin_sources}")
target_sources(ps3recomp_runtime PRIVATE "${pool_spin_output}")
set_source_files_properties("${pool_spin_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pool_spin_input}")
