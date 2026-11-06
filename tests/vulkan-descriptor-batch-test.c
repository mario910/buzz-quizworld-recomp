#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {VkDevice device;} s_vk;
static struct {VkDescriptorPool context_pool[2];uint32_t limit;} s_batch;
static unsigned allocated[4],capacity[4],calls,resets,fail_all,reset_failure,next_pool=1;
static int fail_host;
static uintptr_t serial=100,handle_layout[65536];
#define HANDLE(type,n) ((type)(uintptr_t)(n))
static unsigned pool_id(VkDescriptorPool pool){return (unsigned)(uintptr_t)pool;}
static VkResult pvkCreateDescriptorPool(VkDevice device,const VkDescriptorPoolCreateInfo* info,const VkAllocationCallbacks* cb,VkDescriptorPool* out) {
    assert(next_pool<4);capacity[next_pool]=info->maxSets;
    for(unsigned i=0;i<3;i++)assert(info->pPoolSizes[i].descriptorCount==info->maxSets*(i?16u:2u));
    *out=HANDLE(VkDescriptorPool,next_pool++);return VK_SUCCESS;
}
static VkResult pvkAllocateDescriptorSets(VkDevice device,const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* out) {
    calls++;unsigned p=pool_id(info->descriptorPool);assert(p<4);
    if(fail_all){memset(out,0,info->descriptorSetCount*sizeof *out);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    assert(info->descriptorSetCount<=capacity[p]-allocated[p]);allocated[p]+=info->descriptorSetCount;
    for(unsigned i=0;i<info->descriptorSetCount;i++) {
        assert(info->pSetLayouts[i]);assert(++serial<65536);
        out[i]=HANDLE(VkDescriptorSet,serial);handle_layout[serial]=(uintptr_t)info->pSetLayouts[i];
    }
    return VK_SUCCESS;
}
static VkResult pvkResetDescriptorPool(VkDevice device,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags) {
    resets++;if(reset_failure)return VK_ERROR_DEVICE_LOST;
    allocated[pool_id(pool)]=0;return VK_SUCCESS;
}
static void pvkDestroyDescriptorPool(VkDevice device,VkDescriptorPool pool,const VkAllocationCallbacks* cb){allocated[pool_id(pool)]=0;}
static void* host_calloc(size_t n,size_t size){if(fail_host && !--fail_host)return NULL;return calloc(n,size);}
static void pvkUpdateDescriptorSets(VkDevice d,uint32_t n,const VkWriteDescriptorSet* w,uint32_t c,const VkCopyDescriptorSet* p){assert(0);}
static void pvkDestroyImageView(VkDevice d,VkImageView v,const VkAllocationCallbacks* a){}
static void pvkDestroySampler(VkDevice d,VkSampler v,const VkAllocationCallbacks* a){}
static void pvkDestroyBuffer(VkDevice d,VkBuffer v,const VkAllocationCallbacks* a){}
#define BUZZ_VK_DESCRIPTOR_WRITES_DEFAULT 1
#include "vulkan_descriptor_writes.h"
#define calloc host_calloc
#include "vulkan_descriptor_batch.h"
#undef calloc
static VkResult take(unsigned pool,unsigned layout,VkDescriptorSet* out) {
    VkDescriptorSetLayout l=HANDLE(VkDescriptorSetLayout,layout);
    VkDescriptorSetAllocateInfo info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=HANDLE(VkDescriptorPool,pool),.descriptorSetCount=1,.pSetLayouts=&l};
    VkResult result=buzz_descriptor_allocate(&info,out);
    if(result==VK_SUCCESS)assert(handle_layout[(uintptr_t)*out]==layout);
    return result;
}
int main(void) {
    assert(buzz_descriptor_batch_enabled());s_batch.limit=65;
    VkDescriptorPoolSize sizes[3]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,130},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1040},{VK_DESCRIPTOR_TYPE_SAMPLER,1040}};
    VkDescriptorPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=65,.poolSizeCount=3,.pPoolSizes=sizes};
    for(unsigned i=0;i<2;i++)assert(buzz_descriptor_create(s_vk.device,&ci,NULL,&s_batch.context_pool[i])==VK_SUCCESS);
    assert(capacity[1]==130 && capacity[2]==130);
    VkDescriptorSet retained[2][65][2]={0};
    for(unsigned round=0;round<20;round++) {
        VkDescriptorSet seen[130];unsigned count=0;calls=0;
        for(unsigned i=0;i<65;i++)for(unsigned p=1;p<=2;p++) {
            unsigned kind=(i+round)%2;VkDescriptorSet out;
            assert(take(p,10+kind,&out)==VK_SUCCESS);
            assert(buzz_descriptor_pools[p-1].contents && buzz_descriptor_pending && buzz_descriptor_pending->set==out);
            for(unsigned j=0;j<count;j++)assert(out!=seen[j]);seen[count++]=out;
            if(round<2)retained[p-1][i][kind]=out;
            else assert(out==retained[p-1][i][kind]);
        }
        assert(calls==(round<2?130u:0u));
        VkDescriptorSet out;assert(take(1,10,&out)==VK_ERROR_OUT_OF_POOL_MEMORY && !out);
        for(unsigned p=1;p<=2;p++)assert(pvkResetDescriptorPool(s_vk.device,HANDLE(VkDescriptorPool,p),0)==VK_SUCCESS);
        assert(resets==0); /* Retired handles survive; no driver allocation/reset in steady state. */
    }
    for(unsigned p=1;p<=2;p++){pvkDestroyDescriptorPool(s_vk.device,HANDLE(VkDescriptorPool,p),NULL);assert(!allocated[p] && !buzz_descriptor_pools[p-1].sets && !buzz_descriptor_pools[p-1].contents && !buzz_descriptor_pending);}
    VkDescriptorSet out;fail_all=1;assert(take(1,10,&out)==VK_ERROR_OUT_OF_DEVICE_MEMORY && !out);fail_all=0;
    assert(pvkResetDescriptorPool(s_vk.device,s_batch.context_pool[0],0)==VK_SUCCESS && resets==1);
    assert(take(1,10,&out)==VK_SUCCESS);assert(take(1,11,&out)==VK_SUCCESS);
    assert(take(1,12,&out)==VK_ERROR_OUT_OF_POOL_MEMORY && !out);
    reset_failure=1;assert(pvkResetDescriptorPool(s_vk.device,s_batch.context_pool[0],0)==VK_ERROR_DEVICE_LOST);
    assert(!buzz_descriptor_pools[0].pool);reset_failure=0;
    assert(pvkResetDescriptorPool(s_vk.device,s_batch.context_pool[0],0)==VK_SUCCESS);
    s_batch.limit=1;assert(take(1,12,&out)==VK_SUCCESS);
    pvkDestroyDescriptorPool(s_vk.device,s_batch.context_pool[0],NULL);
    fail_host=1;assert(take(1,10,&out)==VK_SUCCESS && !buzz_descriptor_pools[0].sets);
    assert(pvkResetDescriptorPool(s_vk.device,s_batch.context_pool[0],0)==VK_SUCCESS && !allocated[1]);
    fail_host=2;assert(take(1,10,&out)==VK_SUCCESS && buzz_descriptor_pools[0].sets && !buzz_descriptor_pools[0].contents && !buzz_descriptor_pending);
    pvkDestroyDescriptorPool(s_vk.device,s_batch.context_pool[0],NULL);
    capacity[3]=1;assert(take(3,10,&out)==VK_SUCCESS && allocated[3]==1);
    puts("PASS descriptor recycling: unique live handles, changing layouts, isolated contexts, 20 retirements, bounded capacity, destroy/recreation, host/device allocation/reset failure, limit=1 and foreign pool");
}
