# Retain the pinned SDK. Compose with the platform adapter before wait tracing.
option(BUZZ_VK_BATCH_UPLOAD "Queue immutable texture uploads with draw batches" OFF)
option(BUZZ_VK_DESCRIPTOR_BATCH "Reuse retired immutable draw descriptor sets" OFF)
option(BUZZ_VK_DESCRIPTOR_WRITES "Skip unchanged bindings of retired draw descriptor sets" OFF)
option(BUZZ_VK_UNIFORM_REUSE "Reuse identical immutable constants within a draw batch" OFF)
option(BUZZ_VK_DRAW_STATE "Reuse exact draw state within one render pass" OFF)
if(BUZZ_EXPERIMENTAL_VULKAN)
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(upload_input "${patched_vulkan}")
    else()
        set(upload_input "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c")
    endif()
    file(READ "${upload_input}" upload_text)
    string(REPLACE "\r\n" "\n" upload_text "${upload_text}")
    macro(buzz_upload_replace old new)
        string(FIND "${upload_text}" "${old}" upload_found)
        if(upload_found EQUAL -1)
            message(FATAL_ERROR "Vulkan upload adapter no longer matches SDK: ${old}")
        endif()
        string(REPLACE "${old}" "${new}" upload_text "${upload_text}")
    endmacro()
    if(WIN32)
        # Win32 WSI queries inherit the calling thread's DPI context, not the
        # per-monitor-aware HWND's. Keep physical pixels for the WHOLE query /
        # create operation, and restore the caller even on VK_CHECK failures.
        buzz_upload_replace("static int vk_create_swapchain(void)"
            "static int vk_create_swapchain_pixels(void)")
        buzz_upload_replace("static void vk_swapchain_lost(void)" [=[
static int vk_create_swapchain(void)
{
    void* previous_dpi = rsx_host_window_dpi_enter();
    const int result = vk_create_swapchain_pixels();
    rsx_host_window_dpi_leave(previous_dpi);
    return result;
}

static void vk_swapchain_lost(void)
]=])
        buzz_upload_replace("        RECT client={0};GetClientRect(s_vk.window,&client);w=client.right;hh=client.bottom;" [=[
        unsigned cw = 0, ch = 0;
        if (!rsx_host_window_client(&s_host_window, &cw, &ch)) return -1;
        w = (int)cw; hh = (int)ch;
]=])
        buzz_upload_replace("      s_vk.sc_client_w=cw;s_vk.sc_client_h=ch; }" [=[
      s_vk.sc_client_w=cw;s_vk.sc_client_h=ch;
      VK_LOG("physical client %ux%u, swapchain extent %ux%u (Windows DPI excluded)\\n", cw, ch, ext.width, ext.height); }
]=])
    endif()
    buzz_upload_replace("    int async_requested,async_ready;"
        "    int async_requested,async_ready;\n    int upload_mode;\n    vk_buffer upload_ring[2];\n    VkDeviceSize upload_head[2];")
    buzz_upload_replace("    s_batch.context_inflight[i]=0;"
        "    s_batch.context_inflight[i]=0;\n    s_batch.upload_head[i]=0; /* fence completed */")
    buzz_upload_replace("        if(result){s_batch.failed=1;return -1;}"
        "        if(result){s_batch.failed=1;return -1;}\n        if(!asynchronous)s_batch.upload_head[s_batch.active_context]=0;")
    buzz_upload_replace("/* Copy `rows` rows of `w` texels"
        "#include \"vulkan_upload_batch.h\"\n\n/* Copy `rows` rows of `w` texels")
    buzz_upload_replace("    if (tight > row_bytes) tight = row_bytes;\n    vk_buffer st = {0};"
        "    if (tight > row_bytes) tight = row_bytes;\n    const int queued=buzz_vk_upload_batch(img,layer,mip,x,y,z,w,h,tight,rows,src,row_bytes);\n    if(queued)return queued>0?0:-1;\n    vk_buffer st = {0};")
    # Device idle here also covers error cleanup; normal lifetime is fence-owned.
    buzz_upload_replace("        vk_device_idle();\n        for(u32 i=0;i<2;i++){"
        "        vk_device_idle();\n        for(u32 i=0;i<2;i++){\n            vk_destroy_buffer(&s_batch.upload_ring[i]);\n            s_batch.upload_head[i]=0;")
    buzz_upload_replace("static void vk_destroy_image(vk_image* im)"
        "#include \"vulkan_descriptor_writes.h\"\n\nstatic void vk_destroy_image(vk_image* im)")
    buzz_upload_replace("    pvkUpdateDescriptorSets(s_vk.device, n, w, 0, NULL);"
        "    buzz_descriptor_write(s_vk.device, set, split, bi, si, ii, n, w);")
    buzz_upload_replace("static int vk_batch_async_init(void)"
        "#include \"vulkan_descriptor_batch.h\"\n\nstatic int vk_batch_async_init(void)")
    buzz_upload_replace("pvkAllocateDescriptorSets(s_vk.device, &ai, &set)"
        "buzz_descriptor_allocate(&ai, &set)")
    buzz_upload_replace("pvkCreateDescriptorPool(s_vk.device, &ci, NULL, &s_batch.descriptors)"
        "buzz_descriptor_create(s_vk.device, &ci, NULL, &s_batch.descriptors)")
    if(BUZZ_VK_DESCRIPTOR_BATCH)
        set(descriptor_default 1)
    else()
        set(descriptor_default 0)
    endif()
    set(upload_text "#ifndef BUZZ_VK_DESCRIPTOR_BATCH_DEFAULT\n#define BUZZ_VK_DESCRIPTOR_BATCH_DEFAULT ${descriptor_default}\n#endif\n${upload_text}")
    if(BUZZ_VK_DESCRIPTOR_WRITES)
        set(descriptor_writes_default 1)
    else()
        set(descriptor_writes_default 0)
    endif()
    set(upload_text "#ifndef BUZZ_VK_DESCRIPTOR_WRITES_DEFAULT\n#define BUZZ_VK_DESCRIPTOR_WRITES_DEFAULT ${descriptor_writes_default}\n#endif\n${upload_text}")
    set(upload_output "${CMAKE_CURRENT_BINARY_DIR}/buzz_rsx_vulkan_upload.c")
    buzz_upload_replace("static int vk_batch_flush(void);"
        "static void buzz_uniform_reset(void);\nstatic int vk_batch_flush(void);")
    buzz_upload_replace("        s_batch.draws=s_batch.clear_commands=s_batch.sets=0;"
        "        s_batch.draws=s_batch.clear_commands=s_batch.sets=0;\n        buzz_uniform_reset();")
    buzz_upload_replace("    memset(&s_batch, 0, sizeof s_batch);"
        "    memset(&s_batch, 0, sizeof s_batch);\n    buzz_uniform_reset();")
    buzz_upload_replace("    vk_destroy_buffer(&s_batch.ring_constants);"
        "    buzz_uniform_reset();\n    vk_destroy_buffer(&s_batch.ring_constants);")
    buzz_upload_replace("static void vk_eng_bind_vs_constants("
        "#include \"vulkan_uniform_reuse.h\"\n\nstatic void vk_eng_bind_vs_constants(")
    buzz_upload_replace("    memcpy(s_batch.limit ? s_e2.vs_shadow : s_g.ubo.ptr, data, bytes);"
        "    buzz_uniform_update(0,s_batch.limit ? s_e2.vs_shadow : s_g.ubo.ptr,data,bytes);")
    buzz_upload_replace("    memcpy(s_batch.limit ? s_e2.ps_shadow : (u8*)s_g.ubo.ptr + s_g.ps_off, data, bytes);"
        "    buzz_uniform_update(1,s_batch.limit ? s_e2.ps_shadow : (u8*)s_g.ubo.ptr + s_g.ps_off,data,bytes);")
    buzz_upload_replace("        { constants, base + s_g.ps_off, s_e2.ps_bytes ? s_e2.ps_bytes : 16u },\n    };"
        "        { constants, base + s_g.ps_off, s_e2.ps_bytes ? s_e2.ps_bytes : 16u },\n    };\n    buzz_uniform_bind(ring,bi);")
    buzz_upload_replace("static VkDescriptorSet vk_eng_write_descriptors("
        "#include \"vulkan_uniform_sets.h\"\n\nstatic VkDescriptorSet vk_eng_write_descriptors(")
    buzz_upload_replace("    buzz_descriptor_write(s_vk.device, set, split, bi, si, ii, n, w);"
        "    if(ring && override && buzz_uniform_sets_enabled()) {\n        const VkDescriptorSet shared=buzz_uniform_set_find(split,bi,si,ii);\n        if(shared){buzz_descriptor_pending=NULL;return shared;}\n    }\n    buzz_descriptor_write(s_vk.device, set, split, bi, si, ii, n, w);\n    if(ring && override && buzz_uniform_sets_enabled())buzz_uniform_set_remember(set,split,bi,si,ii);")
    buzz_upload_replace("    memcpy((u8*)s_batch.ring_constants.ptr + uniform, s_e2.vs_shadow,\n           s_e2.vs_bytes ? s_e2.vs_bytes : 16u);\n    memcpy((u8*)s_batch.ring_constants.ptr + uniform + s_g.ps_off, s_e2.ps_shadow,\n           s_e2.ps_bytes ? s_e2.ps_bytes : 16u);"
        "    if(!buzz_uniform_enabled()) {\n        memcpy((u8*)s_batch.ring_constants.ptr + uniform, s_e2.vs_shadow,\n               s_e2.vs_bytes ? s_e2.vs_bytes : 16u);\n        memcpy((u8*)s_batch.ring_constants.ptr + uniform + s_g.ps_off, s_e2.ps_shadow,\n               s_e2.ps_bytes ? s_e2.ps_bytes : 16u);\n    }")
    if(BUZZ_VK_DRAW_STATE)
        buzz_upload_replace("static void vk_eng_draw(void* user, rsx_topology topology, const void* vertices, u32 vertex_count,"
            "#include \"vulkan_draw_state.h\"\n\nstatic void vk_eng_draw(void* user, rsx_topology topology, const void* vertices, u32 vertex_count,")
        buzz_upload_replace("    } else __atomic_fetch_add(&s_batch.reused_passes,1,__ATOMIC_RELAXED);\n    pvkCmdBindPipeline(s_vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);"
            "    } else __atomic_fetch_add(&s_batch.reused_passes,1,__ATOMIC_RELAXED);\n    buzz_draw_state_begin(reuse_pass,pipe);")
        buzz_upload_replace("    pvkCmdSetViewport(s_vk.cmd, 0, 1, &vp);\n    pvkCmdSetScissor(s_vk.cmd, 0, 1, &sc);\n    pvkCmdSetStencilReference(s_vk.cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s_e2.stencil_ref & 0xFFu);\n    if (s_vk.depth_bounds_ok)\n        pvkCmdSetDepthBounds(s_vk.cmd, s_e2.depth_bounds[0], s_e2.depth_bounds[1]);\n    pvkCmdBindDescriptorSets(s_vk.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,\n                             (P->cube_mask | P->vol_mask) ? s_e2.split_pipe_layout : s_g.pipe_layout,\n                             0, 1, &set, 0, NULL);"
            "    buzz_draw_viewport(&vp);\n    buzz_draw_scissor(&sc);\n    buzz_draw_stencil(s_e2.stencil_ref & 0xFFu);\n    if(s_vk.depth_bounds_ok)buzz_draw_depth(s_e2.depth_bounds[0],s_e2.depth_bounds[1]);\n    buzz_draw_descriptor((P->cube_mask | P->vol_mask) ? s_e2.split_pipe_layout : s_g.pipe_layout,set);")
        set(upload_text "#ifndef BUZZ_VK_DRAW_STATE_DEFAULT\n#define BUZZ_VK_DRAW_STATE_DEFAULT 1\n#endif\n${upload_text}")
    endif()
    if(BUZZ_VK_UNIFORM_REUSE)
        set(uniform_default 1)
    else()
        set(uniform_default 0)
    endif()
    set(upload_text "#ifndef BUZZ_VK_UNIFORM_REUSE_DEFAULT\n#define BUZZ_VK_UNIFORM_REUSE_DEFAULT ${uniform_default}\n#endif\n${upload_text}")
    if(BUZZ_VK_BATCH_UPLOAD)
        set(upload_default 1)
    else()
        set(upload_default 0)
    endif()
    set(upload_text "#ifndef BUZZ_VK_BATCH_UPLOAD_DEFAULT\n#define BUZZ_VK_BATCH_UPLOAD_DEFAULT ${upload_default}\n#endif\n${upload_text}")
    file(CONFIGURE OUTPUT "${upload_output}" CONTENT "${upload_text}" @ONLY)
    get_target_property(upload_sources ps3recomp_runtime SOURCES)
    list(REMOVE_ITEM upload_sources "${upload_input}")
    set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${upload_sources}")
    target_sources(ps3recomp_runtime PRIVATE "${upload_output}")
    set_source_files_properties("${upload_output}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video;${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${upload_input}")
endif()
