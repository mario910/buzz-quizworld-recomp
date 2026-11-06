# Specialize the FIFO bridge once per packet, preserving both method consumers.
option(BUZZ_RSX_METHOD_FASTPATH "Experimental compact FIFO method dispatch loop" OFF)
if(NOT BUZZ_RSX_METHOD_FASTPATH)
    return()
endif()
set(method_input "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
if(BUZZ_RSX_PARK_WAKE)
    set(method_input "${park_output}")
elseif(BUZZ_RSX_READY_CHECK)
    set(method_input "${ready_output}")
elseif(BUZZ_RENDERER_STATE_PACKETS)
    set(method_input "${constant_gcm_output}")
elseif(BUZZ_RENDERER_WAIT_TRACE)
    set(method_input "${wait_gcm_output}")
endif()
file(READ "${method_input}" method_text)
string(REPLACE "\r\n" "\n" method_text "${method_text}")
set(method_anchor "            for (u32 i = 0; i < count; i++) {\n                u32 dea = gcm_io2ea(s_fifo_getoff + 4 + i * 4);")
string(FIND "${method_text}" "${method_anchor}" method_found)
if(method_found EQUAL -1)
    message(FATAL_ERROR "FIFO method adapter no longer matches SDK")
endif()
set(method_hook [=[
            if (buzz_fifo_packet_eligible(subch, type, method, count)) {
                static int fast = -1;
                if (fast < 0) {
                    const char* option = getenv("BUZZ_RSX_METHOD_FASTPATH");
                    fast = !(option && !strcmp(option, "0")) &&
                        rsx_draw_engine_enabled() && !rsx_live_draw_enabled();
                    /* The cold path retains all per-word diagnostic reads,
                     * counters and output when any of them is requested. */
                    const char* debug[] = {"GCM_SUBCH7", "GCM_OBJDBG", "GCM_SUBCH_GATE",
                        "GCM_VAL_WATCH", "GCM_METHOD_HIST", "RSX_LIVE_FEED_DBG"};
                    for (unsigned k = 0; k < sizeof debug / sizeof debug[0]; ++k)
                        if (getenv(debug[k])) fast = 0;
                    fprintf(stderr, "[rsx] compact FIFO methods=%d\n", fast);
                }
                if (fast) {
                    for (u32 i = 0; i < count; ++i) {
                        const u32 dea = gcm_io2ea(s_fifo_getoff + 4 + i * 4);
                        if (!dea) break;
                        const u32 m = type == 0 ? method + i * 4 : method;
                        rsx_draw_engine_method(m, vm_read32(dea));
                        /* Keep the second read: a callback may publish to the
                         * same guest memory. No command snapshot or caching. */
                        rsx_process_method(&s_state, m, vm_read32(dea));
                    }
                    s_fifo_getoff += 4 + count * 4;
                    continue;
                }
            }
]=])
string(REPLACE "${method_anchor}" "${method_hook}\n${method_anchor}" method_text "${method_text}")
set(method_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_gcm_methods.c")
file(CONFIGURE OUTPUT "${method_output}" CONTENT "#include \"renderer_fifo_packet.h\"\n${method_text}" @ONLY)
get_target_property(method_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM method_sources "${method_input}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${method_sources}")
target_sources(ps3recomp_runtime PRIVATE "${method_output}")
set_source_files_properties("${method_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
