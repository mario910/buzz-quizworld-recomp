# A bounded wake hint for the exact self-JUMP currently blocking the consumer.
option(BUZZ_RSX_PARK_WAKE "Wake RSX when DMA replaces its observed parked jump" OFF)
if(NOT BUZZ_RSX_PARK_WAKE)
    return()
endif()
set(park_input "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
if(BUZZ_RSX_READY_CHECK)
    set(park_input "${ready_output}")
elseif(BUZZ_RENDERER_STATE_PACKETS)
    set(park_input "${constant_gcm_output}")
elseif(BUZZ_RENDERER_WAIT_TRACE)
    set(park_input "${wait_gcm_output}")
endif()
file(READ "${park_input}" park_text)
string(REPLACE "\r\n" "\n" park_text "${park_text}")
function(buzz_park_replace old new)
    string(FIND "${park_text}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Park wake adapter no longer matches SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" park_text "${park_text}")
    set(park_text "${park_text}" PARENT_SCOPE)
endfunction()
buzz_park_replace("static HANDLE s_gcm_kick_ev = NULL;" [=[
static HANDLE s_gcm_kick_ev = NULL;
/* One atomic token binds the address to its observed park word. Only the
 * lock-owning FIFO walker registers it; producers may clear it once. */
static uint64_t buzz_park_token;
static int buzz_park_enabled = -1;
void buzz_gcm_dma_published(uint32_t ea, uint32_t size) {
    uint64_t token=__atomic_load_n(&buzz_park_token,__ATOMIC_SEQ_CST);
    if(!token)return;
    const uint32_t watched=(uint32_t)(token>>32);
    if((uint64_t)ea>watched || (uint64_t)ea+size<(uint64_t)watched+4)return;
    if(vm_read32(watched)==(uint32_t)token)return;
    if(__atomic_compare_exchange_n(&buzz_park_token,&token,0,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))
        SetEvent(s_gcm_kick_ev);
}
static void buzz_gcm_watch_park(uint32_t ea,uint32_t word) {
    if(!buzz_park_enabled || !s_gcm_kick_ev)return;
    __atomic_store_n(&buzz_park_token,((uint64_t)ea<<32)|word,__ATOMIC_SEQ_CST);
    /* A producer that copied before registration need not notify us: observe
     * its changed word here. Later copies take the DMA notification path.
     * A kick is just a hint; only the normal FIFO walker consumes commands. */
    buzz_gcm_dma_published(ea,4);
}
]=])
buzz_park_replace("static void gcm_rsx_process_fifo_unlocked(void)\n{" [=[
static void gcm_rsx_process_fifo_unlocked(void)
{
    __atomic_store_n(&buzz_park_token,0,__ATOMIC_SEQ_CST);
    if(buzz_park_enabled<0) {
        const char* e=getenv("BUZZ_RSX_PARK_WAKE");
        buzz_park_enabled=!(e && e[0]=='0');
    }
]=])
buzz_park_replace("                  s_park_pass = 1;" "                  buzz_gcm_watch_park(ea,w);\n                  s_park_pass = 1;")
set(park_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_gcm_park.c")
file(CONFIGURE OUTPUT "${park_output}" CONTENT "${park_text}" @ONLY)
get_target_property(park_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM park_sources "${park_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${park_sources}")
target_sources(ps3recomp_runtime PRIVATE "${park_output}")
set_source_files_properties("${park_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
set(park_notify_input "${PS3RECOMP_DIR}/runtime/spu/spurs_command_wait.c")
if(BUZZ_PRODUCER_DEPENDENCY_TRACE)
    set(park_notify_input "${dep_output}")
endif()
file(READ "${park_notify_input}" park_text)
string(REPLACE "\r\n" "\n" park_text "${park_text}")
buzz_park_replace("    __atomic_thread_fence(__ATOMIC_SEQ_CST);" [=[
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    { extern void buzz_gcm_dma_published(uint32_t,uint32_t);
      buzz_gcm_dma_published(ea,size); }
]=])
set(park_notify_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_spurs_park_notify.c")
file(CONFIGURE OUTPUT "${park_notify_output}" CONTENT "${park_text}" @ONLY)
get_target_property(park_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM park_sources "${park_notify_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${park_sources}")
target_sources(ps3recomp_runtime PRIVATE "${park_notify_output}")
set_source_files_properties("${park_notify_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PS3RECOMP_DIR}/runtime/spu/spurs_command_wait.c" "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
