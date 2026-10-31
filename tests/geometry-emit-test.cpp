#define SPU_LS_DEBUG_HOOKS 0
#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <chrono>
extern "C"{
SPU_THREAD_LOCAL void(*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n=0,buzz_geometry_debug=0;
void buzz_job_6eba_spu_func_000046FC(spu_context*){}
void buzz_job_6eba_spu_func_00004750(spu_context*){}
void buzz_job_6eba_spu_func_00004808(spu_context*){}
void buzz_job_6eba_spu_func_000047F0(spu_context*){}
void buzz_job_6eba_spu_func_00004610(spu_context*){}
}
#include "../analysis/native-geometry/phase2/test-emit-kernels.inc"
static uint32_t rng=0x47504808;
static uint32_t word(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void compare(spu_context*a,spu_context*b){buzz_job_6eba_spu_func_000046FC_reference(a);assert(a->pc==0x4610 && g_spu_trampoline_fn==buzz_job_6eba_spu_func_00004610);auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;assert(buzz_job_6eba_spu_func_000046FC_native(b));auto diff=buzz::compare_function_state(*a,*b);
 if(!diff.exact()||next!=g_spu_trampoline_fn){fprintf(stderr,"EMIT DIFF r%d LS%05X %u bytes state%s\n",diff.first_register,diff.first_ls,diff.ls_bytes,diff.first_state?diff.first_state:"equal");abort();}}
int main(int argc,char**argv){auto*seed=new spu_context{};seed->ls=seed->ls_store;
 for(unsigned test=0;test<440;++test){for(auto&r:seed->gpr)for(auto&w:r._u32)w=word();for(unsigned i=0;i<SPU_LS_SIZE;i+=4){auto w=word();memcpy(seed->ls+i,&w,4);}emit_seed_constants(seed);
  unsigned columns=2+test%15,column=test%(columns-1),stride=12+4*(test%14),groups=2+test%5;
  seed->gpr[32]._u32[0]=column;seed->gpr[60]._u32[0]=columns;seed->gpr[44]._u32[0]=column*16;seed->gpr[19]._u32[0]=0x20000;seed->gpr[22]._u32[0]=0x20010;
  seed->gpr[28]._u32[0]=256;seed->gpr[27]._u32[0]=512;seed->gpr[26]._u32[0]=768;
  for(unsigned j=0;j<4;++j){seed->gpr[52]._u32[j]=0x10000+test%16+j*256;seed->gpr[25]._u32[j]=0x10000+test%16+(groups-2)*1024+255;seed->gpr[55]._u32[j]=stride;seed->gpr[53]._u32[j]=4*(test%17);seed->gpr[29]._u32[j]=1024;seed->gpr[31]._u32[j]=test%3?64*stride:16;seed->gpr[30]._u32[j]=0x16000;}
  auto*a=buzz::clone_function_state(seed);auto*b=buzz::clone_function_state(seed);compare(a,b);buzz::release_function_state(a);buzz::release_function_state(b);
 }
  for(unsigned variant=0;variant<20;++variant){auto*rejected=buzz::clone_function_state(seed);
  if(variant==0)rejected->gpr[60]._u32[0]=0;
  if(variant==1)rejected->gpr[32]._u32[0]=rejected->gpr[60]._u32[0]-1;
  if(variant==2)++rejected->gpr[44]._u32[0];
  if(variant==3)++rejected->gpr[19]._u32[0];
  if(variant==4)rejected->gpr[19]._u32[0]=0xe670;
  if(variant==5)++rejected->gpr[22]._u32[0];
  if(variant==6)rejected->gpr[55]._u32[0]=8;
  if(variant==7)rejected->gpr[29]._u32[0]=0;
  if(variant==8)++rejected->gpr[31]._u32[0];
  if(variant==9)rejected->gpr[45]._u32[0]^=1;
  if(variant==10)rejected->gpr[52]._u32[0]=SPU_LS_SIZE-16;
  if(variant==11)rejected->gpr[25]._u32[0]=SPU_LS_SIZE;
  if(variant==12)++rejected->gpr[28]._u32[0];
  if(variant==13)++rejected->gpr[55]._u32[3];
  if(variant==14){rejected->gpr[19]._u32[0]=rejected->gpr[52]._u32[0]&~15u;rejected->gpr[22]._u32[0]=rejected->gpr[19]._u32[0]+16;}
  if(variant==15)for(unsigned j=0;j<4;++j)rejected->gpr[30]._u32[j]=rejected->gpr[55]._u32[0]*rejected->gpr[32]._u32[0]+rejected->gpr[53]._u32[0];
  if(variant==16){rejected->int_enable=1;rejected->event_mask=rejected->event_status=1;}
  if(variant==17)rejected->ls=seed->ls;
  if(variant==18)buzz_geometry_debug=1;
  if(variant==19)rejected->gpr[52]._u32[3]=0xffffffff;
  auto*saved=buzz::clone_function_state(rejected);auto continuation=g_spu_trampoline_fn;
  assert(!buzz_job_6eba_spu_func_000046FC_native(rejected));assert(buzz::compare_function_state(*saved,*rejected).exact() && continuation==g_spu_trampoline_fn);
  buzz_geometry_debug=0;buzz::release_function_state(saved);buzz::release_function_state(rejected);
 }
 puts("46FC/4750/4808: 440 strict states, both alignment arms, mixed sentinels, overlapping output groups, twenty unchanged-state rejections PASS");
 if(argc>1){FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(seed->gpr,sizeof(seed->gpr),1,f)==1);assert(fread(seed->ls,SPU_LS_SIZE,1,f)==1);fclose(f);auto*a=buzz::clone_function_state(seed);auto*b=buzz::clone_function_state(seed);compare(a,b);buzz::release_function_state(a);buzz::release_function_state(b);
 for(unsigned round=0;round<4;++round)for(unsigned pass=0;pass<2;++pass){bool native=(pass^(round&1))!=0;auto*c=buzz::clone_function_state(seed);auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<100000;++i){memcpy(c->gpr,seed->gpr,sizeof(seed->gpr));if(native)buzz_job_6eba_spu_func_000046FC_native(c);else buzz_job_6eba_spu_func_000046FC_reference(c);}printf("EMIT micro %u %s %.1f ns\n",round,native?"native":"reference",std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/100000);buzz::release_function_state(c);}}
 delete seed;
}
