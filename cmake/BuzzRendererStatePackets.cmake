# A bounded FIFO packet path, shared by Windows, Linux and Android. The SDK
# remains pinned; fail closed if the adapter's insertion point changes.
option(BUZZ_RENDERER_STATE_PACKETS "Batch pure RSX register state packets" OFF)
if(NOT BUZZ_RENDERER_STATE_PACKETS)
    return()
endif()
file(READ "${patched_engine}" constant_engine)
string(APPEND constant_engine [=[

#include "renderer_state_packets.h"
void buzz_engine_state_packet(rsx_state* state,u32 method,u32 step,const u32* words,u32 count) {
    if(g.ready && s_eng_perf.on)s_eng_perf.methods+=count;
    buzz_state_packet_apply(g.ready?&g.rsx:NULL,state,method,step,words,count);
}
]=])
file(CONFIGURE OUTPUT "${patched_engine}" CONTENT "${constant_engine}" @ONLY)
set(constant_gcm_input "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
if(BUZZ_RENDERER_WAIT_TRACE)
    set(constant_gcm_input "${wait_gcm_output}")
endif()
file(READ "${constant_gcm_input}" constant_gcm)
string(REPLACE "\r\n" "\n" constant_gcm "${constant_gcm}")
set(constant_anchor "            for (u32 i = 0; i < count; i++) {\n                u32 dea = gcm_io2ea(s_fifo_getoff + 4 + i * 4);")
string(FIND "${constant_gcm}" "${constant_anchor}" constant_found)
if(constant_found EQUAL -1)
    message(FATAL_ERROR "State packet adapter no longer matches the SDK")
endif()
set(constant_hook [=[
            if(buzz_state_packet_eligible(subch,type,method,count)) {
                static int enabled=-1;
                if(enabled<0) {
                    const char* option=getenv("BUZZ_STATE_PACKETS");
                    enabled=!(option && !strcmp(option,"0")) && rsx_draw_engine_enabled() && !rsx_live_draw_enabled();
                    /* These diagnostics observe individual methods. Keep the
                     * original path and their exact accounting when requested. */
                    const char* debug[]={"RSX_TRACE","RSX_CONST_LOG","TCONST_DBG","SEQ_DBG","LOAD_DBG",
                        "GCM_SUBCH7","GCM_OBJDBG","GCM_SUBCH_GATE","GCM_VAL_WATCH",
                        "GCM_METHOD_HIST","RSX_LIVE_FEED_DBG"};
                    for(unsigned k=0;k<sizeof debug/sizeof debug[0];++k)
                        if(getenv(debug[k]))enabled=0;
                    fprintf(stderr,"[rsx] state packets=%d\n",enabled);
                }
                if(enabled) {
                    u32 words[33],valid=0;
                    for(;valid<count;++valid) {
                        const u32 dea=gcm_io2ea(s_fifo_getoff+4+valid*4);
                        if(!dea)break;
                        words[valid]=vm_read32(dea);
                    }
                    buzz_engine_state_packet(&s_state,method,type==0?4:0,words,valid);
                    /* The SDK consumes the packet even if its mapped prefix
                     * ends early. Keep that behavior and each page lookup. */
                    s_fifo_getoff+=4+count*4;
                    continue;
                }
            }
]=])
string(REPLACE "${constant_anchor}" "${constant_hook}\n${constant_anchor}" constant_gcm "${constant_gcm}")
set(constant_gcm "#include \"renderer_state_packets.h\"\nextern void buzz_engine_state_packet(rsx_state*,u32,u32,const u32*,u32);\n${constant_gcm}")
set(constant_gcm_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_gcm_constants.c")
file(CONFIGURE OUTPUT "${constant_gcm_output}" CONTENT "${constant_gcm}" @ONLY)
get_target_property(constant_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM constant_sources "${constant_gcm_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${constant_sources}")
target_sources(ps3recomp_runtime PRIVATE "${constant_gcm_output}")
set_source_files_properties("${constant_gcm_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
