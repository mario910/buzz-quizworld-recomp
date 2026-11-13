# Local training experiment. Profiles are private artifacts, never source assets.
set(BUZZ_PGO_MODE "OFF" CACHE STRING "Scoped runtime/native PGO: OFF, GENERATE, USE")
set_property(CACHE BUZZ_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(BUZZ_PGO_PROFILE "" CACHE FILEPATH "Merged Clang instrumentation profile")
if(BUZZ_PGO_MODE STREQUAL "OFF")
    return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
   NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Scoped PGO requires Linux x86-64 Clang")
endif()
if(BUZZ_PGO_MODE STREQUAL "GENERATE")
    set(buzz_pgo_flags -fprofile-generate -fprofile-update=atomic)
    target_link_options(${PROJECT_NAME} PRIVATE -fprofile-generate)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_PGO_GENERATE=1)
elseif(BUZZ_PGO_MODE STREQUAL "USE")
    if(NOT EXISTS "${BUZZ_PGO_PROFILE}")
        message(FATAL_ERROR "Scoped PGO USE requires a merged profile")
    endif()
    set(buzz_pgo_flags "-fprofile-use=${BUZZ_PGO_PROFILE}")
else()
    message(FATAL_ERROR "Unknown BUZZ_PGO_MODE: ${BUZZ_PGO_MODE}")
endif()
target_compile_options(ps3recomp_runtime PRIVATE ${buzz_pgo_flags})
# Keep the very large generated PPU/SPU translation units outside this experiment.
set_property(SOURCE ${PS3RECOMP_SCAFFOLD} native_geometry.cpp native_geometry_job.cpp
    native_geometry_job_flow.cpp native_animation_runtime.cpp
    APPEND PROPERTY COMPILE_OPTIONS ${buzz_pgo_flags})

# BUZZ_PGO_SPU (default ON): also train/use the lifted SPU job and taskset
# translation units (serial chain images, media, geometry fallback). They are
# hot on the Deck's critical path and were outside the original scope.
option(BUZZ_PGO_SPU "Apply scoped PGO to lifted SPU sources" ON)
if(BUZZ_PGO_SPU AND SPU_LIFTED)
    set_property(SOURCE ${SPU_LIFTED} APPEND PROPERTY COMPILE_OPTIONS ${buzz_pgo_flags})
endif()
