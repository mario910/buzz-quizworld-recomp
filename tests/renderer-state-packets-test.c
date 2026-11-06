#include "renderer_state_packets.h"
#undef NDEBUG /* Assertions stay active when testing the Release adapter. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* The SDK oracle must never reach an externally visible command here. */
void vm_write32(u32 address,u32 value) {(void)address;(void)value;abort();}
u32 gcm_io_offset_to_ea(u32 offset) {(void)offset;abort();}
unsigned long long ps3_ms_now(void) {abort();}
u32 ppu_hle_inject_base;
void ps3_ms(const char* key) {(void)key;abort();}
static rsx_dispatch expected_r,actual_r;
static rsx_state expected_s,actual_s;
static u32 rng=0x9badf00d,checks;
static u32 random32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void check(u32 method,u32 step,u32 count,int ready) {
    u32 words[33];
    for(u32 i=0;i<33;++i)words[i]=random32();
    /* Include NaNs, negative zero and infinities: compare their raw bits. */
    words[0]=0x7fa12345;words[1]=0x80000000;words[2]=0x7f800000;
    if(method==0x1efc)words[0]=expected_r.regs[0x1efc/4];
    memcpy(&actual_r,&expected_r,sizeof actual_r);
    memcpy(&actual_s,&expected_s,sizeof actual_s);
    for(u32 i=0;i<count;++i) {
        if(ready)rsx_dispatch_method(&expected_r,method+i*step,words[i]);
        int result=rsx_process_method(&expected_s,method+i*step,words[i]);
        assert(result==(method+i*step==0x1714?-1:0));
    }
    buzz_state_packet_apply(ready?&actual_r:NULL,&actual_s,method,step,words,count);
    assert(!memcmp(&actual_r,&expected_r,sizeof actual_r));
    assert(!memcmp(&actual_s,&expected_s,sizeof actual_s));
    ++checks;
}
int main(void) {
    const u32 loads[]={0,1,255,256,460,461,467,468,511,512,0xfffffff8,0xffffffff};
    for(u32 type=0;type<=2;type+=2)for(u32 m=0x1efc;m<=0x1f7c;m+=4)
    for(u32 count=1;count<=33;++count)if(buzz_state_packet_eligible(0,type,m,count))
    for(u32 load=0;load<sizeof loads/sizeof loads[0];++load)for(int ready=0;ready<2;++ready) {
        /* Different LOAD values, dirty extents, begin/end and counter wrap. */
        memset(&expected_r,0xa5,sizeof expected_r);
        memset(&expected_s,0x5a,sizeof expected_s);
        expected_r.regs[0x1efc/4]=loads[load];
        expected_s.transform_constant_load=loads[(load+3)%12];
        expected_s.vertex_constants_dirty=load&1;
        for(u32 j=0x1efc/4;j<=0x1f7c/4;++j)expected_r.seen[j]=0xffffffff;
        check(m,type==0?4:0,count,ready);
        /* An unmapped payload may yield any shorter prefix, including empty. */
        check(m,type==0?4:0,random32()%(count+1),ready);
    }
    const u32 bases[]={0x08e4,0x1680,0x1714,0x1740};
    for(u32 b=0;b<4;++b)for(u32 type=0;type<=2;type+=2)
    for(u32 m=bases[b];m<bases[b]+64;m+=4)for(u32 count=1;count<=33;++count)
    if(buzz_state_packet_eligible(0,type,m,count))for(u32 trial=0;trial<16;++trial) {
        memset(&expected_r,0xa5,sizeof expected_r);memset(&expected_s,0x5a,sizeof expected_s);
        check(m,type==0?4:0,count,trial&1);
    }
    for(u32 sc=0;sc<8;++sc)for(u32 type=0;type<8;++type)
    for(u32 m=0;m<=0x2004;++m)for(u32 count=0;count<=34;++count) {
        int valid=sc==0&&(type==0||type==2)&&count&&count<=33&&!(m&3);
        u32 limit=m>=0x1efc&&m<=0x1f7c?0x1f80:m>=0x1740&&m<=0x177c?0x1780:
            m>=0x1680&&m<=0x16bc?0x16c0:m==0x08e4||m==0x1714?m+4:0;
        for(u32 i=0;i<count&&valid;++i) {
            u32 at=m+(type==0?4*i:0);
            valid=limit&&at<limit;
        }
        assert(buzz_state_packet_eligible(sc,type,m,count)==valid);
    }
    puts("State packet eligibility and complete SDK state comparisons passed.");
    printf("%u packet/state comparisons (initialized/uninitialized, NI, truncated prefix, overflow).\n",checks);
    return 0;
}
