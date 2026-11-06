#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HANDLE(type,n) ((type)(uintptr_t)(n))
typedef uint8_t u8;
static struct {u8 vs_shadow[128],ps_shadow[256];} s_e2;
static struct {struct {VkBuffer buf;void* ptr;} ring_constants;unsigned active_context,limit;} s_batch;
static void pvkUpdateDescriptorSets(VkDevice d,uint32_t n,const VkWriteDescriptorSet* w,uint32_t c,const VkCopyDescriptorSet* p){assert(0);}
static void pvkDestroyImageView(VkDevice d,VkImageView v,const VkAllocationCallbacks* a){}
static void pvkDestroySampler(VkDevice d,VkSampler v,const VkAllocationCallbacks* a){}
static void pvkDestroyBuffer(VkDevice d,VkBuffer v,const VkAllocationCallbacks* a){}
#define BUZZ_VK_UNIFORM_REUSE_DEFAULT 1
#include "vulkan_descriptor_writes.h"
#include "vulkan_uniform_reuse.h"
#include "vulkan_uniform_sets.h"

int main(void) {
    VkDescriptorBufferInfo buffers[2]={{HANDLE(VkBuffer,1),0,128},{HANDLE(VkBuffer,1),128,256}};
    VkDescriptorImageInfo samplers[16]={0},images[16]={0};
    for(unsigned i=0;i<16;i++) {
        samplers[i].sampler=HANDLE(VkSampler,10+i);
        images[i].imageView=HANDLE(VkImageView,100+i);
        images[i].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    const VkDescriptorSet set=HANDLE(VkDescriptorSet,42);
    assert(buzz_uniform_sets_enabled());
    assert(!buzz_uniform_set_find(0,buffers,samplers,images));
    for(int split=0;split<2;split++) {
        buzz_uniform_set_remember(set,split,buffers,samplers,images);
        assert(!buzz_uniform_set_find(!split,buffers,samplers,images));
        for(unsigned repeat=0;repeat<100;repeat++)assert(buzz_uniform_set_find(split,buffers,samplers,images)==set);
        for(unsigned i=0;i<2;i++) {
            VkDescriptorBufferInfo saved=buffers[i];
            buffers[i].buffer=HANDLE(VkBuffer,2);
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));buffers[i]=saved;
            ++buffers[i].offset;
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));buffers[i]=saved;
            ++buffers[i].range;
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));buffers[i]=saved;
        }
        for(unsigned i=0;i<16;i++) {
            VkDescriptorImageInfo image=images[i],sampler=samplers[i];
            samplers[i].sampler=HANDLE(VkSampler,777);
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));samplers[i]=sampler;
            images[i].imageView=HANDLE(VkImageView,888);
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));images[i]=image;
            images[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));images[i]=image;
            // Vulkan ignores these fields for separate sampler/image descriptors.
            images[i].sampler=HANDLE(VkSampler,999);samplers[i].imageView=HANDLE(VkImageView,999);
            samplers[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
            assert(buzz_uniform_set_find(split,buffers,samplers,images)==set);
        }
        // Destruction/recreation with the same handle bits invalidates sharing.
        for(unsigned kind=0;kind<3;kind++) {
            buzz_uniform_set_remember(set,split,buffers,samplers,images);
            if(kind==0)pvkDestroyBuffer(VK_NULL_HANDLE,buffers[0].buffer,NULL);
            if(kind==1)pvkDestroyImageView(VK_NULL_HANDLE,images[0].imageView,NULL);
            if(kind==2)pvkDestroySampler(VK_NULL_HANDLE,samplers[0].sampler,NULL);
            assert(!buzz_uniform_set_find(split,buffers,samplers,images));
        }
        buzz_uniform_set_remember(set,split,buffers,samplers,images);
        buzz_uniform_reset(); // includes retirement, context reuse and reinitialization
        assert(!buzz_uniform_set_find(split,buffers,samplers,images));
    }
    buzz_uniform_generation=UINT64_MAX;buzz_uniform_reset();
    buzz_uniform_set_remember(set,0,buffers,samplers,images);
    assert(!buzz_uniform_set_find(0,buffers,samplers,images));
    buzz_uniform_generation_exhausted=0;
    buzz_descriptor_resource_epochs[1]=UINT64_MAX;
    buzz_descriptor_invalidate_resources(1);
    buzz_uniform_set_remember(set,0,buffers,samplers,images);
    assert(!buzz_uniform_set_find(0,buffers,samplers,images));
    puts("PASS immutable descriptor sharing: all binding fields, layouts, resource-handle ABA, batch retirement, epoch/generation overflow");
}
