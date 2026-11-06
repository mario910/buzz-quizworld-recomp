#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HANDLE(type,n) ((type)(uintptr_t)(n))
static unsigned calls,descriptors,last_count,destroys;
static uint32_t bindings[34],elements[34],counts[34];
static int driver_split,omit_cache;
static struct {VkDescriptorBufferInfo buffers[2];VkSampler samplers[16];VkImageView views[16];VkImageLayout layouts[16];} driver_sets[64];
static void pvkUpdateDescriptorSets(VkDevice d,uint32_t n,const VkWriteDescriptorSet* w,uint32_t c,const VkCopyDescriptorSet* p) {
    assert(!c && !p);calls++;last_count=n;
    assert(n<=34);
    for(unsigned i=0;i<n;i++){
        const uintptr_t set=(uintptr_t)w[i].dstSet;assert(set && set<64);
        bindings[i]=w[i].dstBinding;elements[i]=w[i].dstArrayElement;counts[i]=w[i].descriptorCount;descriptors+=w[i].descriptorCount;
        if(w[i].dstBinding<2)driver_sets[set].buffers[w[i].dstBinding]=w[i].pBufferInfo[0];
        else for(unsigned j=0;j<w[i].descriptorCount;j++) {
            unsigned element=w[i].dstArrayElement+j;
            if(w[i].dstBinding==2){assert(element<16);driver_sets[set].samplers[element]=w[i].pImageInfo[j].sampler;}
            else {
                if(driver_split)element+=w[i].dstBinding-3;
                assert(element<16);driver_sets[set].views[element]=w[i].pImageInfo[j].imageView;
                driver_sets[set].layouts[element]=w[i].pImageInfo[j].imageLayout;
            }
        }
    }
}
static void pvkDestroyImageView(VkDevice d,VkImageView v,const VkAllocationCallbacks* a){destroys++;}
static void pvkDestroySampler(VkDevice d,VkSampler v,const VkAllocationCallbacks* a){destroys++;}
static void pvkDestroyBuffer(VkDevice d,VkBuffer v,const VkAllocationCallbacks* a){destroys++;}
#include "vulkan_descriptor_writes.h"
static VkDescriptorBufferInfo buffers[2];
static VkDescriptorImageInfo images[16],samplers[16];
static void update(buzz_descriptor_contents* cache,int split) {
    VkWriteDescriptorSet w[19]={0};unsigned n=split?19:4;
    for(unsigned i=0;i<n;i++)w[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=cache->set,.dstBinding=i,.descriptorCount=1};
    w[0].descriptorType=w[1].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo=&buffers[0];w[1].pBufferInfo=&buffers[1];
    w[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;w[2].descriptorCount=16;w[2].pImageInfo=samplers;
    for(unsigned i=3;i<n;i++){w[i].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w[i].pImageInfo=&images[split?i-3:0];}
    if(!split)w[3].descriptorCount=16;
    calls=descriptors=last_count=0;buzz_descriptor_pending=omit_cache?NULL:cache;driver_split=split;
    buzz_descriptor_write(VK_NULL_HANDLE,cache->set,split,buffers,samplers,images,n,w);
    assert(!buzz_descriptor_pending);
    const uintptr_t set=(uintptr_t)cache->set;
    for(unsigned i=0;i<2;i++)assert(driver_sets[set].buffers[i].buffer==buffers[i].buffer && driver_sets[set].buffers[i].offset==buffers[i].offset && driver_sets[set].buffers[i].range==buffers[i].range);
    for(unsigned i=0;i<16;i++)assert(driver_sets[set].samplers[i]==samplers[i].sampler && driver_sets[set].views[i]==images[i].imageView && driver_sets[set].layouts[i]==images[i].imageLayout);
}
int main(void) {
    buffers[0]=(VkDescriptorBufferInfo){HANDLE(VkBuffer,1),0,16};buffers[1]=buffers[0];buffers[1].offset=256;
    for(unsigned i=0;i<16;i++) {
        images[i]=(VkDescriptorImageInfo){VK_NULL_HANDLE,HANDLE(VkImageView,10+i),VK_IMAGE_LAYOUT_GENERAL};
        samplers[i]=(VkDescriptorImageInfo){HANDLE(VkSampler,100+i),VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
    }
    for(int split=0;split<2;split++) {
        buzz_descriptor_contents cache={.set=HANDLE(VkDescriptorSet,42)};
        update(&cache,split);assert(calls==1 && descriptors==34);
        for(unsigned repeat=0;repeat<100;repeat++){update(&cache,split);assert(!calls);}
        buffers[0].offset+=16;update(&cache,split);assert(descriptors==1 && bindings[0]==0);
        buffers[1].range+=16;update(&cache,split);assert(descriptors==1 && bindings[0]==1);
        buffers[1].buffer=HANDLE(VkBuffer,2);update(&cache,split);assert(descriptors==1 && bindings[0]==1);
        buffers[1].buffer=HANDLE(VkBuffer,1);update(&cache,split);assert(descriptors==1);
        samplers[7].sampler=HANDLE(VkSampler,777);update(&cache,split);assert(descriptors==1 && bindings[0]==2 && elements[0]==7);
        samplers[7].sampler=HANDLE(VkSampler,107);update(&cache,split);assert(descriptors==1);
        images[9].imageView=HANDLE(VkImageView,999);update(&cache,split);assert(descriptors==1 && bindings[0]==(split?12u:3u) && elements[0]==(split?0u:9u));
        images[9].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;update(&cache,split);assert(descriptors==1);
        // Fields ignored by these Vulkan descriptor types do not require writes.
        images[2].sampler=HANDLE(VkSampler,9);samplers[3].imageView=HANDLE(VkImageView,5);
        update(&cache,split);assert(!calls);
        // Destroy/recreate with IDENTICAL handle bits must refresh all bindings.
        pvkDestroyImageView(VK_NULL_HANDLE,images[9].imageView,NULL);update(&cache,split);assert(descriptors==16 && bindings[0]==3);
        pvkDestroySampler(VK_NULL_HANDLE,samplers[0].sampler,NULL);update(&cache,split);assert(descriptors==16 && bindings[0]==2);
        pvkDestroyBuffer(VK_NULL_HANDLE,buffers[0].buffer,NULL);update(&cache,split);assert(descriptors==2 && bindings[0]==0 && bindings[1]==1);
        pvkDestroyBuffer(VK_NULL_HANDLE,VK_NULL_HANDLE,NULL);update(&cache,split);assert(!calls);
        buzz_descriptor_contents other={.set=HANDLE(VkDescriptorSet,43)};update(&other,split);assert(descriptors==34);
        update(&cache,split);assert(!calls);
        memset(cache.epochs,0,sizeof cache.epochs);update(&cache,split);assert(descriptors==34);
        samplers[4].sampler=HANDLE(VkSampler,404);samplers[5].sampler=HANDLE(VkSampler,405);
        update(&cache,split);assert(descriptors==2 && last_count==1 && elements[0]==4 && counts[0]==2);
        for(unsigned i=0;i<16;i++) {
            if(!(i%2))samplers[i].sampler=HANDLE(VkSampler,500+i);
            images[i].imageView=HANDLE(VkImageView,600+i);
        }
        buffers[0].range+=16;buffers[1].range+=16;update(&cache,split);
        assert(descriptors==26 && last_count==(split?26u:11u));
        update(&cache,!split);assert(descriptors==34);
        for(unsigned i=0;i<16;i++){images[i].imageView=HANDLE(VkImageView,10+i);samplers[i].sampler=HANDLE(VkSampler,100+i);}
        images[9].imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    }
    buzz_descriptor_contents cache={.set=HANDLE(VkDescriptorSet,44)};
    omit_cache=1;update(&cache,0);assert(descriptors==34);update(&cache,0);assert(descriptors==34);omit_cache=0;
    update(&cache,0);assert(descriptors==34);
    buzz_descriptor_resource_epochs[0]=UINT64_MAX;buzz_descriptor_invalidate_resources(0);
    update(&cache,0);assert(descriptors==34);update(&cache,0);assert(descriptors==34);
    assert(destroys==8);
    puts("PASS descriptor writes: exact fields, array/split layouts, independent sets, partial writes, 100 reuse cycles, resource-handle ABA and epoch overflow");
}
