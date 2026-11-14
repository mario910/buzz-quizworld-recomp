# Thin front for spu_wrch (runtime/spu/spu_channels.c, SPU_WRCH_WRAP): the MFC
# parameter channel writes of every DMA command are answered before the full
# channel writer. Wrapped at link time so neither the lifted SPU sources nor
# the profiled body of spu_wrch change.
# Measured 2026-11-14: replay and menu FPS within noise; opt-in.
option(BUZZ_SPU_WRCH_WRAP "Link-time fast front for MFC parameter channel writes" OFF)
if(NOT BUZZ_SPU_WRCH_WRAP)
    return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU)$")
    message(FATAL_ERROR "BUZZ_SPU_WRCH_WRAP requires Linux and Clang or GCC")
endif()
target_compile_definitions(ps3recomp_runtime PRIVATE SPU_WRCH_WRAP=1)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=spu_wrch)
