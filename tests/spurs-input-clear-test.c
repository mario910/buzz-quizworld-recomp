/* Run real SDK staging, hash every LS byte at entry, then dirty the reused
 * store. The runner compares original SDK and generated adapter OFF/ON. */
#define main unused_handoff_main
#include "../ps3recomp/runtime/spu/tests/test_spu_interpreter_handoff.c"
#undef main
#include "../ps3recomp/runtime/spu/spu_workload.h"

static uint8_t guest[2*1024*1024];
static uint32_t rng=0x23750217, serial;
static uint64_t result=UINT64_C(1469598103934665603);
static unsigned entered;
static uint32_t random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void be16(unsigned a,unsigned n){guest[a]=n>>8;guest[a+1]=n;}
static void be32(unsigned a,uint32_t n){for(unsigned i=0;i<4;++i)guest[a+i]=(uint8_t)(n>>(24-i*8));}
static void be64(unsigned a,uint64_t n){be32(a,(uint32_t)(n>>32));be32(a+4,(uint32_t)n);}
static void hash(const void* data,size_t n){const uint8_t* b=data;while(n--){result^=*b++;result*=UINT64_C(1099511628211);}}
static void staged(spu_context* c){
    assert(c->ls==c->ls_store);
    hash(c->ls,SPU_LS_SIZE);hash(c->gpr,sizeof c->gpr);++entered;
    /* Every subsequent job must clear gaps but preserve undeclared LS. */
    memset(c->ls,(int)(0x81+(serial%113)),SPU_LS_SIZE);
    c->status=SPU_STATUS_STOPPED_BY_STOP;spu_halt(c);
}
int main(void){
    vm_base=guest;ppu_vm_size=sizeof guest;
    for(unsigned a=0;a<sizeof guest;++a)guest[a]=(uint8_t)random_word();
    for(serial=0;serial<4096;++serial){
        const uint32_t job=0x10000,bin=0x20000;
        memset(guest+job,0,512);
        unsigned binary=(serial%11==0?0x3a000:16+(random_word()%0x4000))*16;
        if(binary>SPU_LS_SIZE)binary=0x3a000;
        unsigned io=serial%13==0?0:random_word()%0x5000;
        unsigned output=random_word()%0x2000, scratch=random_word()%0x200;
        if(serial%37==0)io=0xfffffff0u; /* wrapped SDK alignment retains fallback */
        if(serial%41==0)output=0xfffffff0u;
        unsigned stack=serial%7==0?0:random_word()%0x100;
        unsigned n=serial%17==0?0:random_word()%12;
        be64(job,bin);be16(job+8,binary/16);be16(job+10,n*8);
        be32(job+0x10,serial&1);be32(job+0x14,io);be32(job+0x18,output);
        be16(job+0x1c,stack);be16(job+0x1e,scratch);
        const unsigned caches=serial%6; /* SDK clamps 5 entries to 4 */
        be32(job+0x24,caches*8);
        for(unsigned i=0;i<n+caches;++i){
            unsigned bytes=random_word()%0x3000,ea=0x80000+(random_word()%0x60000);
            if(serial%9==0)bytes=0;
            if(serial%19==0)ea=0;
            if(i==0 && serial%23==0)ea=job; /* input aliases descriptor in guest memory */
            if(i==1 && serial%29==0)bytes=1;
            if(i==2 && serial%31==0)bytes=31;
            be64(job+0x30+i*8,((uint64_t)bytes<<32)|ea);
        }
        if(serial%5==0)memcpy(guest+bin+0x30,"JOBCRT Ver13",12);
        else memset(guest+bin+0x30,0x55,12);
        assert(spu_run_spurs_job(staged,99,job,128u<<(serial%3))==0);
    }
    assert(entered==4096);
    printf("PASS real SPURS staging: %u jobs; complete LS/GPR hash=%016llx\n",entered,(unsigned long long)result);
    return 0;
}
