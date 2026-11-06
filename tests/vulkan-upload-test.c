/* Real device test: include the generated adapter, retain only used functions.
 * No game assets or recompiled guest code. See test-vulkan-upload.ps1. */
#include <assert.h>
#include "vulkan-upload-private.c"

static void fixture_init(void)
{
    vk_mutex_init(&s_vk.queue_mu);
    assert(vk_load_library() == 0);
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="Buzz upload lifetime test", .apiVersion=VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ci = { .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app };
    assert(pvkCreateInstance(&ci,NULL,&s_vk.instance) == VK_SUCCESS);
    assert(vk_load_instance_functions() == 0 && vk_pick_device() == 0);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = { .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=s_vk.queue_family, .queueCount=1, .pQueuePriorities=&priority };
    VkDeviceCreateInfo di = { .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1, .pQueueCreateInfos=&qi };
    assert(pvkCreateDevice(s_vk.phys,&di,NULL,&s_vk.device) == VK_SUCCESS);
    assert(vk_load_device_functions() == 0);
    pvkGetDeviceQueue(s_vk.device,s_vk.queue_family,0,&s_vk.queue);
    VkCommandPoolCreateInfo pi = { .sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex=s_vk.queue_family };
    assert(pvkCreateCommandPool(s_vk.device,&pi,NULL,&s_vk.pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo ai = { .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=s_vk.pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1 };
    assert(pvkAllocateCommandBuffers(s_vk.device,&ai,&s_vk.cmd) == VK_SUCCESS);
    VkFenceCreateInfo fi = { .sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    assert(pvkCreateFence(s_vk.device,&fi,NULL,&s_vk.fence) == VK_SUCCESS);
    s_batch.limit=8; s_batch.async_requested=1;
    assert(vk_batch_async_init() == 0);
}

static void snapshot(VkImage image, vk_buffer* out, u32 width, u32 height)
{
    if (!s_batch.recording) { assert(vk_begin_draw_batch() == 0); s_batch.recording=1; }
    vk_barrier_image(image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_GENERAL);
    VkBufferImageCopy r = { .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageExtent={width,height,1} };
    pvkCmdCopyImageToBuffer(s_vk.cmd,image,VK_IMAGE_LAYOUT_GENERAL,out->buf,1,&r);
    VkBufferMemoryBarrier b = { .sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=out->buf, .size=VK_WHOLE_SIZE };
    pvkCmdPipelineBarrier(s_vk.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        0,0,NULL,1,&b,0,NULL);
}

static void check(vk_buffer* out, size_t bytes, unsigned char expected)
{
    vk_invalidate(out);
    for (size_t i=0;i<bytes;i++) assert(((unsigned char*)out->ptr)[i] == expected);
}

static void exercise(u32 width, u32 height, int asynchronous)
{
    const size_t bytes=(size_t)width*height*4, pitch=width*4+16;
    unsigned char* input=malloc(pitch*height);
    assert(input);
    vk_image image={0};
    assert(vk_create_image(&image,VK_FORMAT_R8G8B8A8_UNORM,width,height,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT) == 0);
    assert(vk_begin() == 0);
    vk_barrier_image(image.img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED);
    assert(vk_submit_and_wait() == 0);
    vk_buffer out[12]={0};
    s_batch.async_ready=asynchronous;
    for (unsigned round=0;round<5;round++) {
        for (unsigned i=0;i<12;i++) {
            if (!out[i].buf) assert(vk_create_host_buffer(&out[i],bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0);
            unsigned char value=(unsigned char)(round*13+i+1);
            memset(input,value,pitch*height);
            assert(vk_eng_upload_region(image.img,0,0,0,0,width,height,width*4,height,input,(u32)pitch) == 0);
            /* The caller immediately reuses its bytes, before submission. */
            memset(input,0xcc,pitch*height);
            snapshot(image.img,&out[i],width,height);
            if (i%3 == 2) assert(vk_batch_flush_as(8) == 0);
        }
        assert(vk_batch_flush() == 0);
        for (unsigned i=0;i<12;i++) check(&out[i],bytes,(unsigned char)(round*13+i+1));
        assert(s_batch.upload_head[0] == 0 && s_batch.upload_head[1] == 0);
    }
    for (unsigned i=0;i<12;i++) vk_destroy_buffer(&out[i]);
    /* Pending upload must complete before resource retirement as well. */
    memset(input,0x42,pitch*height);
    assert(vk_eng_upload_region(image.img,0,0,0,0,width,height,width*4,height,input,(u32)pitch) == 0);
    vk_destroy_image(&image);
    free(input);
    s_batch.async_ready=1;
}

int main(void)
{
    fixture_init();
    exercise(17,19,1);       /* padded rows; distinct uploads in one batch */
    exercise(1024,512,1);   /* capacity flush and alternating context reuse */
    exercise(1025,1024,1);  /* larger than arena: synchronous fallback */
    exercise(31,23,0);      /* async unavailable: original path */
    assert(pvkDeviceWaitIdle(s_vk.device) == VK_SUCCESS);
    for (unsigned i=0;i<2;i++) {
        vk_destroy_buffer(&s_batch.upload_ring[i]);
        pvkDestroyFence(s_vk.device,s_batch.context_fence[i],NULL);
    }
    pvkDestroyFence(s_vk.device,s_vk.fence,NULL);
    pvkDestroyCommandPool(s_vk.device,s_vk.pool,NULL);
    pvkDestroyDevice(s_vk.device,NULL);
    pvkDestroyInstance(s_vk.instance,NULL);
    puts("PASS: 240 GPU readbacks, immutable inputs, row padding, context reuse, capacity and fallback");
    return 0;
}
