/* Execute the game's unmodified image-7 queue consumer with a deterministic
 * synchronous DMA fixture. No movie decoder/rendering or invented consumer. */
#include "../port/spu/spu_0006_at_007E7500/spu_recomp.c"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../port/video_queue_layout.h"
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n,g_spu_smc_watch,g_spu_ls_probe,g_wws_code_probe;
static unsigned char memory[65536];
static uint32_t lsa,eah,eal,length,tagmask;
static unsigned transfers,steps;
void spu_ls_watch_slow(uint32_t a,int b,const uint8_t* c,uint32_t d,uint32_t e) {abort();}
void spu_indirect_branch(spu_context* c) {abort();}
void spu_img_restore(spu_context* c,int32_t image) {assert(image==c->image_id);}
void spu_stop(spu_context* c) {abort();}
void spu_halt(spu_context* c) {abort();}
uint32_t spu_rchcnt(spu_context* c,uint32_t channel) {abort();}
void spu_drain_call(spu_context* c,uint32_t ret) {
    while(g_spu_trampoline_fn) {
        assert(++steps<100000);
        void (*fn)(spu_context*)=g_spu_trampoline_fn;g_spu_trampoline_fn=NULL;fn(c);
    }
    assert(c->pc==ret);
}
void spu_wrch(spu_context* c,uint32_t channel,u128 value) {
    uint32_t n=value._u32[0];
    switch(channel) {
    case MFC_LSA:lsa=n;break;
    case MFC_EAH:eah=n;break;
    case MFC_EAL:eal=n;break;
    case MFC_Size:length=n;break;
    case MFC_TagID:break;
    case MFC_WrTagMask:tagmask=n;break;
    case MFC_WrTagUpdate:break;
    case MFC_Cmd:
        assert(!eah && length && (uint64_t)eal+length<=sizeof(memory));
        assert((uint64_t)lsa+length<=SPU_LS_SIZE);++transfers;
        if(n==0xd0 || (n&0xfc)==0x40)memcpy(c->ls+lsa,memory+eal,length);
        else if(n==0xb4 || n==0xb0 || (n&0xfc)==0x20)memcpy(memory+eal,c->ls+lsa,length);
        else {fprintf(stderr,"unexpected MFC %x\n",n);abort();}
        break;
    default:fprintf(stderr,"unexpected write channel %u\n",channel);abort();
    }
}
u128 spu_rdch(spu_context* c,uint32_t channel) {
    u128 value={{0}};
    if(channel==MFC_RdTagStat)value._u32[0]=tagmask;
    else if(channel!=MFC_RdAtomicStat) {fprintf(stderr,"unexpected read channel %u\n",channel);abort();}
    return value;
}
static void be16(unsigned a,unsigned v){memory[a]=(uint8_t)(v>>8);memory[a+1]=(uint8_t)v;}
static void be32(unsigned a,unsigned v){be16(a,v>>16);be16(a+2,v);}
static unsigned r16(unsigned a){return (memory[a]<<8)|memory[a+1];}
static unsigned file32(const unsigned char* b){return ((unsigned)b[0]<<24)|((unsigned)b[1]<<16)|(b[2]<<8)|b[3];}
static unsigned file16(const unsigned char* b){return (b[0]<<8)|b[1];}
static void load_image(spu_context* c,const char* path) {
    FILE* f=fopen(path,"rb");assert(f);unsigned char b[262144];size_t size=fread(b,1,sizeof(b),f);fclose(f);
    assert(size>52 && !memcmp(b,"\177ELF",4) && b[4]==1 && b[5]==2);
    uint64_t fingerprint=1469598103934665603ull; /* The pinned SDK's basis. */
    for(size_t n=0;n<size;++n){fingerprint^=b[n];fingerprint*=1099511628211ull;}
    assert(fingerprint==0xf0ff8edd5aac68c6ull);
    unsigned ph=file32(b+28),stride=file16(b+42),count=file16(b+44);assert(stride>=32);
    for(unsigned n=0;n<count;++n) {
        unsigned p=ph+n*stride;assert((uint64_t)p+32<=size);
        if(file32(b+p)!=1)continue;
        unsigned off=file32(b+p+4),va=file32(b+p+8),bytes=file32(b+p+16),mem=file32(b+p+20);
        assert((uint64_t)off+bytes<=size && bytes<=mem && (uint64_t)va+mem<=SPU_LS_SIZE);
        memset(c->ls+va,0,mem);memcpy(c->ls+va,b+off,bytes);
    }
}
static void call(spu_context* c,void (*fn)(spu_context*)) {
    c->gpr[0]=spu_il(0x3f000);c->host_depth=1;steps=0;fn(c);spu_drain_call(c,0x3f000);
}
int main(int argc,char** argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    static spu_context context;
    context.ls=context.ls_store;context.image_id=7;context.gpr[1]=spu_il(0x37000);
    assert(argc==2);load_image(&context,argv[1]);
    unsigned q=0x2000,buffer=0x2080,work=0x1aa80,out=0x38b00,data=0x4000;
    assert(buzz_vq_init(memory,sizeof(memory),0x5000,q,buffer,3)==BUZZ_VQ_OK);
    unsigned published=0,consumed=0;
    for(unsigned iteration=0;iteration<1000;++iteration) {
      for(unsigned slot=0;slot<3;++slot) {
        be32(data,published);for(unsigned i=4;i<16;++i)memory[data+i]=(uint8_t)(published*17+i);
        int wake;assert(buzz_vq_push_locked(memory,sizeof(memory),q,data,&wake)==BUZZ_VQ_OK && wake==-1);++published;
      }
      uint8_t saved[176];memcpy(saved,memory+q,sizeof(saved));
      int wake;assert(buzz_vq_push_locked(memory,sizeof(memory),q,data,&wake)==BUZZ_VQ_AGAIN && wake==-1);
      assert(!memcmp(saved,memory+q,sizeof(saved)));
      for(unsigned slot=0;slot<3;++slot) {
    spu_ls_write32(&context,work,out);spu_ls_write32(&context,work+4,0);spu_ls_write32(&context,work+8,0);
    context.gpr[3]=spu_il(0);context.gpr[3]._u32[1]=q;
    context.gpr[4]=spu_il(work);context.gpr[5]=spu_il(0);
    call(&context,spu_0006_at_007E7500_spu_func_0000B638);
    assert(context.gpr[3]._u32[0]==0);
    context.gpr[3]=spu_il(0);context.gpr[3]._u32[1]=q;context.gpr[4]=spu_il(work);
    call(&context,spu_0006_at_007E7500_spu_func_0000B4A0);
    assert(context.gpr[3]._u32[0]==0);
    assert(spu_ls_read32(&context,out)==consumed);
    for(unsigned i=4;i<16;++i)assert(context.ls[out+i]==(uint8_t)(consumed*17+i));++consumed;
      }
    }
    assert(published==3000 && consumed==published && r16(q)==0 && r16(q+8)==0 && r16(q+14)==0);
    assert(buzz_vq_init(memory,sizeof(memory),0x5000,q,buffer,3)==BUZZ_VQ_OK);
    be16(q+0x30,1);be16(q+0x32,0);int wake;
    assert(buzz_vq_push_locked(memory,sizeof(memory),q,data,&wake)==BUZZ_VQ_OK && wake==0 && r16(q+0x30)==0x401);
    assert(buzz_vq_init(memory,sizeof(memory),0x5000,q,buffer,3)==BUZZ_VQ_OK);
    be32(q+0x0c,3); /* Regression: old initializer seeded the reserved pointer. */
    assert(buzz_vq_push_locked(memory,sizeof(memory),q,data,&wake)==BUZZ_VQ_UNSUPPORTED);
    assert(buzz_vq_init(memory,sizeof(memory),0x5000,q,buffer,32768)==BUZZ_VQ_INVALID);
    assert(buzz_vq_init(memory,sizeof(memory),0x5000,q,q+16,3)==BUZZ_VQ_INVALID);
    assert(buzz_vq_init(memory,sizeof(memory),0x5000,q,0xfffffff0u,3)==BUZZ_VQ_INVALID);
    printf("PASS: 3000 FIFO entries through real SPU consumer, 500 counter wraps, full/backpressure, notification token, old-layout rejection; %u DMA transfers\n",transfers);
}
