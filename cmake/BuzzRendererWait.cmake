# Optional wait attribution; never edit the pinned SDK or add timing overhead
# to normal builds. Apply after the existing Linux/Android Vulkan adapter.
option(BUZZ_RENDERER_WAIT_TRACE "Detailed RSX/FIFO/Vulkan wait timeline" OFF)
option(BUZZ_PRODUCER_DEPENDENCY_TRACE "Trace DMA publications and FIFO park addresses (diagnostics only)" OFF)
if(BUZZ_PRODUCER_DEPENDENCY_TRACE AND NOT BUZZ_RENDERER_WAIT_TRACE)
    message(FATAL_ERROR "Producer dependency trace requires renderer wait trace")
endif()
if(BUZZ_RENDERER_WAIT_TRACE)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_RENDERER_WAIT_TRACE=1)
    # Read-only FIFO attribution. GET != PUT alone cannot distinguish runnable
    # work from a self-JUMP waiting for a producer to patch the command buffer.
    set(wait_gcm_input "${PS3RECOMP_DIR}/libs/video/cellGcmSys.c")
    file(READ "${wait_gcm_input}" wait_gcm)
    string(REPLACE "\r\n" "\n" wait_gcm "${wait_gcm}")
    set(wait_stop_anchor "    /* A drain stopped on a semaphore acquire (GCM_SEMA_ACQUIRE) holds `get`")
    set(wait_kick_anchor "        SetEvent(s_gcm_kick_ev);       /* dry: ask the present thread to walk */")
    foreach(anchor IN ITEMS "${wait_stop_anchor}" "${wait_kick_anchor}")
        string(FIND "${wait_gcm}" "${anchor}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "FIFO wait instrumentation no longer matches SDK")
        endif()
    endforeach()
    set(wait_stop_hook [=[
    if (PS3_FT_ON()) {
        const unsigned reason = s_sema_blocked_pass ? 1 : s_park_pass ? 2 :
            !strcmp(why,"flip") ? 3 : s_fifo_getoff == put ? 4 :
            budget <= 0 ? 5 : !strcmp(why,"no-io-mapping") ? 6 : 7;
        const uint64_t now = ps3_ft_now();
        ps3_ft_span(BUZZ_FT_FIFO_STOP,reason,s_fifo_getoff,put,now,now);
    }
]=])
    string(REPLACE "${wait_stop_anchor}" "${wait_stop_hook}\n${wait_stop_anchor}" wait_gcm "${wait_gcm}")
    if(BUZZ_PRODUCER_DEPENDENCY_TRACE)
        set(park_anchor "                  s_park_pass = 1;")
        string(FIND "${wait_gcm}" "${park_anchor}" park_found)
        if(park_found EQUAL -1)
            message(FATAL_ERROR "FIFO park instrumentation no longer matches SDK")
        endif()
        string(REPLACE "${park_anchor}" [=[
                  if (PS3_FT_ON()) {
                      buzz_publication_watch(ea);
                      const uint64_t t = ps3_ft_now();
                      ps3_ft_span(110,0,ea,w,t,t);
                  }
                  s_park_pass = 1;
]=] wait_gcm "${wait_gcm}")
        string(FIND "${wait_gcm}" "              s_fifo_getoff = tgt;" jump_found)
        if(jump_found EQUAL -1)
            message(FATAL_ERROR "FIFO jump instrumentation no longer matches SDK")
        endif()
        string(REPLACE "              s_fifo_getoff = tgt;" [=[
              if (PS3_FT_ON()) {
                  const uint64_t t=ps3_ft_now();
                  ps3_ft_span(114,0,ea,w,t,t);
              }
              s_fifo_getoff = tgt;
]=] wait_gcm "${wait_gcm}")
        set(dep_input "${PS3RECOMP_DIR}/runtime/spu/spurs_command_wait.c")
        file(READ "${dep_input}" dep_text)
        string(REPLACE "\r\n" "\n" dep_text "${dep_text}")
        foreach(anchor IN ITEMS "void spurs_command_dma_written(uint32_t ea, uint32_t size)\n{" "void spurs_command_written(uint32_t address)\n{")
            string(FIND "${dep_text}" "${anchor}" dep_found)
            if(dep_found EQUAL -1)
                message(FATAL_ERROR "SPURS publication instrumentation no longer matches SDK")
            endif()
        endforeach()
        string(REPLACE "void spurs_command_dma_written(uint32_t ea, uint32_t size)\n{" [=[
void spurs_command_dma_written(uint32_t ea, uint32_t size)
{
    if (PS3_FT_ON() && (size == 4 || size == 8) && buzz_publication_watched(ea)) {
        const uint64_t t = ps3_ft_now();
        const uint32_t tid = GetCurrentThreadId();
        ps3_ft_span(109,tid,ea,size,t,t);
        /* Small publications include the value observed after the DMA copy.
         * This is a snapshot, not an atomic log of concurrent guest writers. */
        if (size == 4 || size == 8) {
            extern uint8_t* vm_base;
            uint32_t word;
            memcpy(&word,vm_base+ea,4);
            ps3_ft_span(112,tid,ea,__builtin_bswap32(word),t,t);
            if (size == 8) {
                memcpy(&word,vm_base+ea+4,4);
                ps3_ft_span(113,tid,ea+4,__builtin_bswap32(word),t,t);
            }
        }
    }
]=] dep_text "${dep_text}")
        string(REPLACE "void spurs_command_written(uint32_t address)\n{" [=[
void spurs_command_written(uint32_t address)
{
    if (PS3_FT_ON()) {
        const uint64_t t = ps3_ft_now();
        ps3_ft_span(111,GetCurrentThreadId(),address,8,t,t);
    }
]=] dep_text "${dep_text}")
        set(dep_wait_anchor "    const uint64_t ticks = !((pc | chain) & 7) ? spin_ticks() : 0;")
        string(FIND "${dep_text}" "${dep_wait_anchor}" dep_found)
        if(dep_found EQUAL -1)
            message(FATAL_ERROR "SPURS wait observation no longer matches SDK")
        endif()
        string(REPLACE "${dep_wait_anchor}" "    if (PS3_FT_ON()) { buzz_publication_watch(pc); buzz_publication_watch(chain); }\n${dep_wait_anchor}" dep_text "${dep_text}")
        set(dep_text "#define BUZZ_PRODUCER_DEPENDENCY_TRACE 1\n#include \"renderer_wait_trace.h\"\n${dep_text}")
        set(dep_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_spurs_publication_trace.c")
        file(CONFIGURE OUTPUT "${dep_output}" CONTENT "${dep_text}" @ONLY)
        get_target_property(dep_sources ps3recomp_runtime SOURCES)
        list(REMOVE_ITEM dep_sources "${dep_input}")
        set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${dep_sources}")
        target_sources(ps3recomp_runtime PRIVATE "${dep_output}")
        set_source_files_properties("${dep_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
            INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu;${CMAKE_CURRENT_SOURCE_DIR}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dep_input}")
    endif()
    string(REPLACE "${wait_kick_anchor}" [=[
    {
        SetEvent(s_gcm_kick_ev);
        if (PS3_FT_ON()) {
            const uint64_t now = ps3_ft_now();
            ps3_ft_span(BUZZ_FT_FIFO_KICK,0,0,0,now,now);
        }
    }
]=] wait_gcm "${wait_gcm}")
    set(wait_gcm "#include \"renderer_wait_trace.h\"\n${wait_gcm}")
    if(BUZZ_PRODUCER_DEPENDENCY_TRACE)
        set(wait_gcm "#define BUZZ_PRODUCER_DEPENDENCY_TRACE 1\n${wait_gcm}")
    endif()
    set(wait_gcm_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_gcm_wait.c")
    file(CONFIGURE OUTPUT "${wait_gcm_output}" CONTENT "${wait_gcm}" @ONLY)
    get_target_property(wait_gcm_sources ps3recomp_runtime SOURCES)
    list(REMOVE_ITEM wait_gcm_sources "${wait_gcm_input}")
    set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${wait_gcm_sources}")
    target_sources(ps3recomp_runtime PRIVATE "${wait_gcm_output}")
    set_source_files_properties("${wait_gcm_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${wait_gcm_input}")
    if(BUZZ_EXPERIMENTAL_VULKAN)
        if(DEFINED upload_output)
            set(wait_input "${upload_output}")
        elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(wait_input "${patched_vulkan}")
        else()
            set(wait_input "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c")
        endif()
        file(READ "${wait_input}" wait_text)
        string(REPLACE "\r\n" "\n" wait_text "${wait_text}")
        set(wait_anchor "static int vk_batch_flush(void);")
        string(FIND "${wait_text}" "${wait_anchor}" wait_found)
        if(wait_found EQUAL -1)
            message(FATAL_ERROR "Vulkan wait instrumentation no longer matches SDK")
        endif()
        set(wait_hooks [=[
#include "renderer_wait_trace.h"
static inline VkResult buzz_vk_wait_fences(VkDevice device,uint32_t count,const VkFence* fences,VkBool32 all,uint64_t timeout) {
    const uint64_t start=buzz_wait_trace_begin();
    uint32_t reason=0;
    if(start && count==1) {
        const VkFence f=fences[0];
        if(f==s_vk.fence)reason=1;
        else if(f==s_batch.context_fence[0])reason=2;
        else if(f==s_batch.context_fence[1])reason=3;
        else if(f==s_vk.present_fence)reason=4;
        else if(f==s_vk.window_fence)reason=5;
    }
    const VkResult result=pvkWaitForFences(device,count,fences,all,timeout);
    buzz_wait_trace_end(BUZZ_FT_VK_FENCE,reason,(uint32_t)result,start);
    return result;
}
static inline VkResult buzz_vk_acquire(VkDevice device,VkSwapchainKHR swapchain,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index) {
    const uint64_t start=buzz_wait_trace_begin();
    const VkResult result=pvkAcquireNextImageKHR(device,swapchain,timeout,semaphore,fence,index);
    buzz_wait_trace_end(BUZZ_FT_VK_ACQUIRE,0,(uint32_t)result,start);return result;
}
static inline VkResult buzz_vk_present(VkQueue queue,const VkPresentInfoKHR* present) {
    const uint64_t start=buzz_wait_trace_begin();
    const VkResult result=pvkQueuePresentKHR(queue,present);
    buzz_wait_trace_end(BUZZ_FT_VK_QUEUE_PRESENT,0,(uint32_t)result,start);return result;
}
#define pvkWaitForFences(...) buzz_vk_wait_fences(__VA_ARGS__)
#define pvkAcquireNextImageKHR(...) buzz_vk_acquire(__VA_ARGS__)
#define pvkQueuePresentKHR(...) buzz_vk_present(__VA_ARGS__)
]=])
        string(REPLACE "${wait_anchor}" "${wait_hooks}\n${wait_anchor}" wait_text "${wait_text}")
        set(wait_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_rsx_vulkan_wait.c")
        file(CONFIGURE OUTPUT "${wait_output}" CONTENT "${wait_text}" @ONLY)
        get_target_property(wait_sources ps3recomp_runtime SOURCES)
        list(REMOVE_ITEM wait_sources "${wait_input}")
        set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${wait_sources}")
        target_sources(ps3recomp_runtime PRIVATE "${wait_output}")
        set_source_files_properties("${wait_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
            INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${wait_input}")
    endif()
endif()
