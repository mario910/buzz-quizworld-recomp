# Keep the pinned DMA implementation; gate only its unconditional poison log scan.
option(BUZZ_DMA_DIAGNOSTICS_OPTIN "Make the DMA poison logging scan opt-in" OFF)
if(NOT BUZZ_DMA_DIAGNOSTICS_OPTIN)
    return()
endif()
if(NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU)$")
    message(FATAL_ERROR "DMA diagnostic adapter requires GCC or Clang constructors")
endif()
set(dma_input "${PS3RECOMP_DIR}/runtime/spu/spu_dma.h")
file(READ "${dma_input}" dma_text)
string(REPLACE "\r\n" "\n" dma_text "${dma_text}")
string(SHA256 dma_hash "${dma_text}")
if(NOT dma_hash STREQUAL "397a150063fa25dc2642918fe28f99c67056a57f6ac9575a3893daca570ce543")
    message(FATAL_ERROR "DMA diagnostic adapter requires the pinned DMA header")
endif()
set(dma_anchor "        { uint32_t e = (uint32_t)ea;\n          int hitregion =")
string(FIND "${dma_text}" "${dma_anchor}" dma_at)
if(dma_at LESS 0)
    message(FATAL_ERROR "DMA poison diagnostic anchor missing")
endif()
# Immutable after process startup; avoids introducing a lazy-init race in workers.
set(dma_preamble [=[
static int buzz_dma_poison_scan_enabled;
__attribute__((constructor)) static void buzz_dma_poison_scan_init(void) {
    const char* value = getenv("BUZZ_DMA_POISON_SCAN");
    buzz_dma_poison_scan_enabled = value && value[0] && strcmp(value,"0");
}
]=])
string(REPLACE "${dma_anchor}" "        if (buzz_dma_poison_scan_enabled) { uint32_t e = (uint32_t)ea;\n          int hitregion =" dma_text "${dma_text}")
set(dma_directory "${CMAKE_CURRENT_BINARY_DIR}/buzz_dma_diagnostics")
file(MAKE_DIRECTORY "${dma_directory}")
file(CONFIGURE OUTPUT "${dma_directory}/spu_dma.h" CONTENT "#ifndef BUZZ_DMA_DIAGNOSTICS_HEADER\n#define BUZZ_DMA_DIAGNOSTICS_HEADER\n#include <stdlib.h>\n#include <string.h>\n${dma_preamble}\n${dma_text}\n#endif\n" @ONLY)
set(dma_channel_input "${PS3RECOMP_DIR}/runtime/spu/spu_channels.c")
if(BUZZ_SPU_JOB_RETURN)
    set(dma_channel_input "${job_return_output}")
elseif(BUZZ_SPURS_EXECUTION_TRACE)
    set(dma_channel_input "${exec_ch_output}")
endif()
file(READ "${dma_channel_input}" dma_channel_text)
string(REPLACE "#include \"spu_dma.h\"" "#include \"${dma_directory}/spu_dma.h\"" dma_channel_text "${dma_channel_text}")
set(dma_channel_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_dma_channels.c")
file(CONFIGURE OUTPUT "${dma_channel_output}" CONTENT "${dma_channel_text}" @ONLY)
get_target_property(dma_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM dma_sources "${dma_channel_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${dma_sources}")
target_sources(ps3recomp_runtime PRIVATE "${dma_channel_output}")
set_source_files_properties("${dma_channel_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(SOURCE native_geometry_job.cpp native_animation_runtime.cpp
    APPEND PROPERTY INCLUDE_DIRECTORIES "${dma_directory}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dma_input}" "${dma_channel_input}")
