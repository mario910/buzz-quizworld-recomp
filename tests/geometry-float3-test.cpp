#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include "../port/native_geometry_ls.h"
#include <cassert>
#include <chrono>
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n=0,buzz_geometry_debug=0;
void buzz_job_6eba_spu_func_00009498(spu_context*){}
}
#include "../analysis/native-geometry/phase2/test-float3-kernels.inc"
static uint32_t rng=0x97a80041;
static uint32_t word(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void compare(spu_context*a,spu_context*b){buzz_job_6eba_spu_func_000097A8_reference(a);assert(a->pc==0x9498 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00009498);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;assert(buzz_job_6eba_spu_func_000097A8_native(b));auto diff=buzz::compare_function_state(*a,*b);
 if(!diff.exact()||next!=g_spu_trampoline_fn){fprintf(stderr,"FLOAT3 DIFF r%d LS%05X %u bytes state%s\n",diff.first_register,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");abort();}}
int main(int argc,char**argv){
 auto*seed=new spu_context{};seed->ls=seed->ls_store;
 for(unsigned t=0;t<440;++t){
  for(auto&r:seed->gpr)for(auto&w:r._u32)w=word();for(unsigned i=0;i<SPU_LS_SIZE;i+=4){auto w=word();memcpy(seed->ls+i,&w,4);}
  float3_seed_constants(seed);unsigned groups=t<400?4+t%63:512;
  seed->gpr[9]._u32[0]=0-groups*64;seed->gpr[14]._u32[0]=0x10000;
  unsigned regs[4]={8,13,12,11};for(unsigned j=0;j<4;++j)seed->gpr[regs[j]]._u32[0]=0x20000+groups*64+(t%5?j*16:0);
  auto*a=buzz::clone_function_state(seed);auto*b=buzz::clone_function_state(seed);compare(a,b);buzz::release_function_state(a);buzz::release_function_state(b);
 }
  for(unsigned variant=0;variant<16;++variant){auto*rejected=buzz::clone_function_state(seed);
  if(variant==0)rejected->gpr[9]._u32[0]=0;
  if(variant==1)rejected->gpr[9]._u32[0]=0-3*64;
  if(variant==2)rejected->gpr[9]._u32[0]=0x80000000;
  if(variant==3)++rejected->gpr[9]._u32[0];
  if(variant==4)++rejected->gpr[14]._u32[0];
  if(variant==5)rejected->gpr[14]._u32[0]=SPU_LS_SIZE-16;
  if(variant==6)rejected->gpr[14]._u32[0]=0xe670;
  if(variant==7)rejected->gpr[8]._u32[0]=0;
  if(variant==8)rejected->gpr[8]._u32[0]=rejected->gpr[14]._u32[0]-rejected->gpr[9]._u32[0];
  if(variant==9)++rejected->gpr[8]._u32[0];
  if(variant>=10 && variant<13)rejected->gpr[variant==10?15:variant==11?21:22]._u32[0]^=1;
  if(variant==13){rejected->int_enable=1;rejected->event_mask=rejected->event_status=1;}
  if(variant==14)rejected->ls=seed->ls;
  if(variant==15)buzz_geometry_debug=1;
  auto*saved=buzz::clone_function_state(rejected);auto continuation=g_spu_trampoline_fn;
  assert(!buzz_job_6eba_spu_func_000097A8_native(rejected));assert(buzz::compare_function_state(*saved,*rejected).exact() && continuation==g_spu_trampoline_fn);
  buzz_geometry_debug=0;buzz::release_function_state(saved);buzz::release_function_state(rejected);
 }
 puts("97A8: 440 strict states, 4..512 groups, sixteen unchanged-state rejections PASS");
 if(argc>1){FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(seed->gpr,sizeof(seed->gpr),1,f)==1);assert(fread(seed->ls,SPU_LS_SIZE,1,f)==1);fclose(f);auto*a=buzz::clone_function_state(seed);auto*b=buzz::clone_function_state(seed);compare(a,b);buzz::release_function_state(a);buzz::release_function_state(b);
  for(unsigned round=0;round<4;++round)for(unsigned pass=0;pass<2;++pass){bool native=(pass^(round&1))!=0;auto*c=buzz::clone_function_state(seed);auto start=std::chrono::steady_clock::now();for(unsigned j=0;j<100000;++j){memcpy(c->gpr,seed->gpr,sizeof(seed->gpr));if(native)buzz_job_6eba_spu_func_000097A8_native(c);else buzz_job_6eba_spu_func_000097A8_reference(c);}printf("97A8 micro %u %s %.1f ns\n",round,native?"native":"reference",std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/100000);buzz::release_function_state(c);}
 }
 delete seed;
}
