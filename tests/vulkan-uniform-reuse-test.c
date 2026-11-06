#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8;
typedef uint64_t VkBuffer, VkDeviceSize;
typedef struct {VkBuffer buffer;VkDeviceSize offset,range;} VkDescriptorBufferInfo;
static struct {u8 vs_shadow[128],ps_shadow[256];} s_e2;
static struct {struct {VkBuffer buf;void* ptr;} ring_constants;unsigned active_context,limit;} s_batch;
#define BUZZ_VK_UNIFORM_REUSE_DEFAULT 1
#include "vulkan_uniform_reuse.h"

static u8 gpu[2*64*512];
static u8 vs_source[128],ps_source[256];
static struct {VkDescriptorBufferInfo bi[2];u8 vs[128],ps[256];} submitted[64];
static unsigned random_state=0x297e26aa;
static unsigned random_word(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void verify(unsigned count){
    for(unsigned i=0;i<count;i++) {
        assert(!memcmp(gpu+submitted[i].bi[0].offset,submitted[i].vs,(size_t)submitted[i].bi[0].range));
        assert(!memcmp(gpu+submitted[i].bi[1].offset,submitted[i].ps,(size_t)submitted[i].bi[1].range));
    }
}
int main(void) {
    s_batch.ring_constants.buf=1;s_batch.ring_constants.ptr=gpu;s_batch.limit=64;
    unsigned hits[2]={0};
    for(unsigned batch=0;batch<100;++batch) {
        buzz_uniform_reset();s_batch.active_context=batch%2;
        unsigned vb=128,pb=256;
        for(unsigned draw=0;draw<64;++draw) {
            const VkDeviceSize base=(s_batch.active_context*64+draw)*512;
            if(draw%3==0)vs_source[random_word()%128]^=(u8)random_word();
            if(draw%5==0)ps_source[random_word()%256]^=(u8)random_word();
            if(draw%7==0)vb=16*(1+random_word()%8);
            if(draw%11==0)pb=16*(1+random_word()%16);
            buzz_uniform_update(0,s_e2.vs_shadow,vs_source,vb);
            buzz_uniform_update(1,s_e2.ps_shadow,ps_source,pb);
            assert(!memcmp(s_e2.vs_shadow,vs_source,vb)&&!memcmp(s_e2.ps_shadow,ps_source,pb));
            VkDescriptorBufferInfo bi[2]={{1,base,vb},{1,base+128,pb}};
            buzz_uniform_bind(1,bi);
            for(unsigned stage=0;stage<2;++stage)hits[stage]+=bi[stage].offset!=base+stage*128;
            memcpy(submitted[draw].bi,bi,sizeof bi);
            memcpy(submitted[draw].vs,vs_source,vb);memcpy(submitted[draw].ps,ps_source,pb);
            verify(draw+1); // every earlier descriptor still sees its original bytes
        }
    }
    assert(hits[0]>1000&&hits[1]>1000);
    // Every byte, including NaN payload/sign bits, participates in identity.
    for(unsigned stage=0;stage<2;++stage)for(unsigned byte=0;byte<(stage?256u:128u);++byte) {
        buzz_uniform_reset();
        VkDescriptorBufferInfo first[2]={{1,0,128},{1,128,256}};
        buzz_uniform_bind(1,first);
        u8* source=stage?ps_source:vs_source;
        memcpy(source,stage?s_e2.ps_shadow:s_e2.vs_shadow,stage?256:128);
        source[byte]^=1;
        buzz_uniform_update(stage,stage?s_e2.ps_shadow:s_e2.vs_shadow,source,stage?256:128);
        VkDescriptorBufferInfo second[2]={{1,512,128},{1,640,256}};
        buzz_uniform_bind(1,second);
        assert(second[stage].offset==512+stage*128);
        assert(second[stage^1].offset==(stage^1)*128);
        assert(!memcmp(gpu+second[stage].offset,source,stage?256:128));
    }
    // New batches, buffer replacement (even same handle after reset), and
    // context changes cannot reuse pointers into a retired/recycled slot.
    for(unsigned reason=0;reason<3;++reason) {
        buzz_uniform_reset();VkDescriptorBufferInfo first[2]={{1,0,128},{1,128,256}};
        buzz_uniform_bind(1,first);
        if(reason==0)buzz_uniform_reset();
        if(reason==1)++s_batch.ring_constants.buf;
        if(reason==2)s_batch.active_context^=1;
        VkDescriptorBufferInfo next[2]={{s_batch.ring_constants.buf,512,128},{s_batch.ring_constants.buf,640,256}};
        buzz_uniform_bind(1,next);assert(next[0].offset==512&&next[1].offset==640);
    }
    VkDescriptorBufferInfo legacy[2]={{77,0,16},{77,128,16}},before[2];memcpy(before,legacy,sizeof legacy);
    buzz_uniform_bind(0,legacy);assert(!memcmp(before,legacy,sizeof legacy));
    printf("PASS immutable uniforms: 6400 draws, independent stages (%u/%u hits), all-byte mutations, lengths, context/buffer/reset boundaries\n",hits[0],hits[1]);
}
