# Compiler scheduling experiment for the Deck's Zen 2 host. No fast-math,
# additional ISA requirement or changes to the large generated PPU units.
option(BUZZ_ZEN2_TUNE "Tune Linux x86-64 runtime and native host paths for Zen 2 with -O3" OFF)
if(NOT BUZZ_ZEN2_TUNE)
    return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
   NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU)$" OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|GNU)$")
    message(FATAL_ERROR "BUZZ_ZEN2_TUNE requires Linux x86-64 and Clang or GCC")
endif()
target_compile_options(ps3recomp_runtime PRIVATE -O3 -mtune=znver2 -ffp-contract=off)
set_property(SOURCE ${PS3RECOMP_SCAFFOLD} native_geometry.cpp native_geometry_job.cpp
    native_geometry_job_flow.cpp native_animation_runtime.cpp
    APPEND PROPERTY COMPILE_OPTIONS -O3 -mtune=znver2 -ffp-contract=off)
