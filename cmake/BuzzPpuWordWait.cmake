option(BUZZ_PPU_WORD_WAIT "Experimental portable DMA notification for registered PPU busy-word waits" OFF)
option(BUZZ_PPU_WORD_WAIT_TRACE "Diagnostic PPU wait and exact-word DMA timeline" OFF)
if(NOT BUZZ_PPU_WORD_WAIT)
    if(BUZZ_PPU_WORD_WAIT_TRACE)
        message(FATAL_ERROR "PPU wait trace requires BUZZ_PPU_WORD_WAIT")
    endif()
    return()
endif()
set(ppu_timer_input "${PS3RECOMP_DIR}/runtime/syscalls/sys_timer.c")
if(WIN32 AND BUZZ_WINDOWS_TIMER_REUSE)
    set(ppu_timer_input "${timer_output}")
endif()
file(READ "${ppu_timer_input}" ppu_timer_text)
string(REPLACE "\r\n" "\n" ppu_timer_text "${ppu_timer_text}")
set(ppu_start "#ifdef _WIN32\n    /* Registered wait sites (sys_timer_register_usleep_wait):")
set(ppu_end "    /* PS3_USLEEP_MIN_US=<n>:")
string(FIND "${ppu_timer_text}" "${ppu_start}" ppu_begin)
string(FIND "${ppu_timer_text}" "${ppu_end}" ppu_stop)
if(ppu_begin LESS 0 OR ppu_stop LESS ppu_begin)
    message(FATAL_ERROR "Portable PPU wait no longer matches syscall")
endif()
string(SUBSTRING "${ppu_timer_text}" 0 ${ppu_begin} ppu_prefix)
string(SUBSTRING "${ppu_timer_text}" ${ppu_stop} -1 ppu_suffix)
set(ppu_hook [=[
    /* Admit only the reviewed registered polling sites, and only while the
     * post-DMA notification route is actually installed. */
    if (s_wait_site_n && usec <= 50 && g_spu_put_notify == spurs_command_dma_written) {
        for (int i=0; i<s_wait_site_n; ++i) {
            if ((uint32_t)ctx->lr != s_wait_site[i].lr) continue;
            const uint32_t ea=(uint32_t)ctx->gpr[s_wait_site[i].reg]+s_wait_site[i].offset;
            if (buzz_ppu_word_wait(vm_base,ea,(uint32_t)ctx->lr)) return CELL_OK;
            break;
        }
    }
#ifdef _WIN32
]=])
set(ppu_timer_text "${ppu_prefix}${ppu_hook}\n${ppu_suffix}")
set(ppu_option "s_wait_sites_off=!(e && e[0]=='1');")
string(FIND "${ppu_timer_text}" "${ppu_option}" ppu_found)
if(ppu_found LESS 0)
    message(FATAL_ERROR "Portable PPU wait option no longer matches")
endif()
string(REPLACE "${ppu_option}" "s_wait_sites_off=e ? e[0]!='1' : 0;" ppu_timer_text "${ppu_timer_text}")
set(ppu_timer_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_ppu_sys_timer.c")
file(CONFIGURE OUTPUT "${ppu_timer_output}" CONTENT "#include \"ppu_word_wait.h\"\nextern void (*g_spu_put_notify)(uint32_t,uint32_t);\nextern void spurs_command_dma_written(uint32_t,uint32_t);\n${ppu_timer_text}" @ONLY)

set(ppu_notify_input "${PS3RECOMP_DIR}/runtime/spu/spurs_command_wait.c")
if(BUZZ_RSX_PARK_WAKE)
    set(ppu_notify_input "${park_notify_output}")
elseif(BUZZ_PRODUCER_DEPENDENCY_TRACE)
    set(ppu_notify_input "${dep_output}")
endif()
file(READ "${ppu_notify_input}" ppu_notify_text)
set(ppu_fence "    __atomic_thread_fence(__ATOMIC_SEQ_CST);")
string(FIND "${ppu_notify_text}" "${ppu_fence}" ppu_found)
if(ppu_found LESS 0)
    message(FATAL_ERROR "Portable PPU notification no longer matches post-DMA fence")
endif()
string(REPLACE "${ppu_fence}" "${ppu_fence}\n    buzz_ppu_word_dma_written(ea,size);" ppu_notify_text "${ppu_notify_text}")
set(ppu_notify_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_ppu_dma_notify.c")
file(CONFIGURE OUTPUT "${ppu_notify_output}" CONTENT "#include \"ppu_word_wait.h\"\n${ppu_notify_text}" @ONLY)
get_target_property(ppu_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM ppu_sources "${ppu_timer_input}" "${ppu_notify_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${ppu_sources}")
target_sources(ps3recomp_runtime PRIVATE "${ppu_timer_output}" "${ppu_notify_output}" "${CMAKE_CURRENT_SOURCE_DIR}/ppu_word_wait.c")
set_source_files_properties("${ppu_timer_output}" "${ppu_notify_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/syscalls;${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
if(BUZZ_PPU_WORD_WAIT_TRACE)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ppu_word_wait.c" TARGET_DIRECTORY ps3recomp_runtime
        APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_PPU_WORD_WAIT_TRACE=1)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ppu_timer_input}" "${ppu_notify_input}")
