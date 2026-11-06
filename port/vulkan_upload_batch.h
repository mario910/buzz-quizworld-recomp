/* Included in the port-owned Vulkan source after its buffer/batch helpers.
 * Immutable upload slices belong to the command context, not the texture.
 * Only a completed fence permits the context's head to return to zero. */
#define BUZZ_VK_UPLOAD_BYTES (4u * 1024u * 1024u)

static int buzz_vk_upload_batch(VkImage img, u32 layer, u32 mip,
    u32 x, u32 y, u32 z, u32 w, u32 h, size_t tight, u32 rows,
    const void* src, u32 row_bytes)
{
    if (!s_batch.upload_mode) {
        const char* value = getenv("BUZZ_VK_BATCH_UPLOAD");
        s_batch.upload_mode = value && *value ? (!strcmp(value, "1") ? 1 : -1) :
            (BUZZ_VK_BATCH_UPLOAD_DEFAULT ? 1 : -1);
        VK_LOG("batched texture uploads=%d (up to 4 MiB per context)\n", s_batch.upload_mode > 0);
    }
    if (s_batch.upload_mode < 0 || !s_batch.async_ready || !s_batch.limit ||
        s_batch.failed || !src || !tight || !rows ||
        tight > BUZZ_VK_UPLOAD_BYTES / rows) return 0;

    const VkDeviceSize bytes = (VkDeviceSize)tight * rows;
    u32 context = s_batch.active_context;
    /* Covers the largest uncompressed texel and compressed block alignment.
     * No assumption about the destination image's allocation alignment. */
    VkDeviceSize offset = (s_batch.upload_head[context] + 15u) & ~(VkDeviceSize)15u;
    if (bytes > BUZZ_VK_UPLOAD_BYTES - offset) {
        if (vk_batch_flush_as(8)) return -1;
        context = s_batch.active_context;
        offset = 0; /* the selected context was retired before switching */
    }
    vk_buffer* staging = &s_batch.upload_ring[context];
    const u64 staging_start = vk_perf_now();
    if (!staging->buf &&
        (vk_create_host_buffer(staging, BUZZ_VK_UPLOAD_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
         !staging->coherent)) {
        vk_destroy_buffer(staging);
        s_batch.upload_mode = -1;
        vk_perf_add(&s_perf.upload_staging_ns, staging_start);
        return 0; /* original synchronous path remains available */
    }
    vk_perf_add(&s_perf.upload_staging_ns, staging_start);
    const u64 copy_start = vk_perf_now();
    for (u32 row = 0; row < rows; ++row)
        memcpy((u8*)staging->ptr + offset + (size_t)row * tight,
               (const u8*)src + (size_t)row * row_bytes, tight);
    vk_perf_add(&s_perf.upload_copy_ns, copy_start);

    const u64 sync_start = vk_perf_now();
    if (!s_batch.recording) {
        if (vk_begin_draw_batch()) return -1;
        s_batch.recording = 1; /* upload-only batches must also be submitted */
    }
    if (s_batch.pass_active) {
        pvkCmdEndRenderPass(s_vk.cmd);
        s_batch.pass_active = 0;
    }
    /* The execution dependency retires earlier image reads as well as writes.
     * The second barrier publishes the transfer to later sampling/rendering,
     * including uses in the following command buffer on this same queue. */
    vk_barrier_image(img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    VkBufferImageCopy region = {
        .bufferOffset = offset,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1 },
        .imageOffset = { (int32_t)x, (int32_t)y, (int32_t)z },
        .imageExtent = { w, h, 1 },
    };
    pvkCmdCopyBufferToImage(s_vk.cmd, staging->buf, img, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    vk_barrier_image(img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    s_batch.upload_head[context] = offset + bytes;
    vk_perf_add(&s_perf.upload_sync_ns, sync_start);
    if (s_perf.on) {
        __atomic_fetch_add(&s_perf.upload_calls, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&s_perf.upload_bytes, bytes, __ATOMIC_RELAXED);
    }
    return 1;
}
