// Lua boot probes and the explicit, optional no-FMV bootstrap mode.
#include "ppu_recomp.h"
#include "boot_probe_options.h"
#include "frontend_policy.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void ppu_register_function(uint64_t,void(*)(ppu_context*));
extern "C" PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern "C" unsigned long long ps3_qpc_us(void);
namespace {
bool trace_enabled=false;
unsigned long long trace_t0;
bool skip_fmv=false;
using Fn=void(*)(ppu_context*);
struct Probe {uint32_t pc;const char* name;Fn original;std::atomic<unsigned> calls{0};};
Probe probes[]={
    {0x119F04,"FadeFromBlack"}, {0x137AEC,"FadeToBlack"},
    {0x22D614,"GetTimeNow"}, {0x233F60,"SetWaitTimeSeconds"},
    {0x233E60,"AmWaitingForTime"}, {0x11F074,"Video.OpenStreamLocalised"},
    {0x1381A8,"Video.PlayStream"}, {0x12E17C,"Video.IsStreamPlaying"},
    {0x12E260,"Video.IsStreamCached"}, {0x12E09C,"Video.IsStreamDisplaying"},
    {0x12DFBC,"Video.IsStreamErrored"}, {0x23D8A0,"IncludeScript"},
    {0x23E1C8,"BackgroundMusic.CacheBackgroundMusic"},
    {0x22F168,"BackgroundMusic.IsBackgroundMusicLoaded"},
    {0x23DE2C,"BackgroundMusic.PlayBackgroundMusic"},
    {0x22FE3C,"BackgroundMusic.FadeBackgroundMusic"},
    {0x16EC4C,"NaviSlide.ReadyToContinue"}, {0x16E96C,"NaviSlide.PresentMenu"},
    {0x187698,"NaviSlide.AddTextItemToMenu"},
    {0x139DA8,"LoadingIcon.Begin"}, {0x11AEBC,"LoadingIcon.End"},
    {0x12DB40,"LoadingIcon.IsRunning"}, {0x22BD00,"PauseThreadedDisplay"},
    {0x22BCD0,"UnpauseThreadedDisplay"}, {0x22BD44,"TurnOffThreadedDisplay"}
};
void value(uint32_t ea)
{
    uint32_t bits=vm_read32(ea),type=vm_read32(ea+4);
    float n;memcpy(&n,&bits,4);
    fprintf(stderr," type=%u raw=%08X",type,bits);
    if(type==3)fprintf(stderr," number=%g",n);
    if(type==2)fprintf(stderr," integer=%d",(int32_t)bits);
    if(type==4 && bits>=0x10000 && bits<0xC0000000u){
        char s[160];unsigned k=0;
        for(;k<sizeof(s)-1;++k){s[k]=(char)vm_read8(bits+16+k);if(!s[k])break;}
        s[k]=0;fprintf(stderr," string=\"%s\"",s);
    }
}
template<unsigned I> void traced(ppu_context* c)
{
    auto& p=probes[I];
    unsigned n=trace_enabled ? ++p.calls : 0;
    bool show=trace_enabled && (n<=8 || n%1000==0 || (I==11 && n<=512));
    const uint32_t L=(uint32_t)c->gpr[3];
    const bool apply_frontend=I==11 && buzz_frontend_target_include(L);
    const bool valid=L>=0x10000 && L<0xD0000000u;
    if(show){
        fprintf(stderr,"[boot-lua] t=%llums %s #%u L=%08X",(ps3_qpc_us()-trace_t0)/1000,p.name,n,L);
        if(valid){
            uint32_t base=vm_read32(L+12),top=vm_read32(L+8);
            if(base>=0x10000 && top>=base && top-base<=1024){
                fprintf(stderr," argc=%u",(top-base)/8);
                for(uint32_t at=base;at<top && at<base+24;at+=8)value(at);
            }
        }
        fputc('\n',stderr);
    }
    // Wait.UntilVideoIsDisplaying also exits on IsStreamErrored. Report the
    // unsupported FMV explicitly; never claim that decoded video is displaying.
    // IsStreamPlaying must be false so the next script loop can terminate too.
    if(skip_fmv && (I==5 || I==6)) {
        // These Lua entry points normally return no results. No movie state
        // should be allocated or played in the explicitly selected bootstrap.
        c->gpr[3]=0;
    }else if(skip_fmv && (I==7 || I==10)){
        c->gpr[4]=(I==10)?1:0;
        func_0046B8F8(c);
        c->gpr[3]=1;
    }else p.original(c);
    while(g_trampoline_fn){auto fn=g_trampoline_fn;g_trampoline_fn=nullptr;fn(c);}
    if(apply_frontend)buzz_frontend_after_include(c,L);
    if(show && valid){
        fprintf(stderr,"[boot-lua] %s returned %d",p.name,(int32_t)c->gpr[3]);
        if(c->gpr[3]>0 && c->gpr[3]<=4){
            uint32_t top=vm_read32(L+8);
            if(top>=0x10008 && top<0xD0000000u)value(top-8);
        }
        fputc('\n',stderr);
    }
}
}
void buzz_register_boot_trace()
{
    trace_enabled=buzz_boot::enabled(getenv("BUZZ_BOOT_TRACE"));
    trace_t0=ps3_qpc_us();
    skip_fmv=buzz_boot::enabled(getenv("BUZZ_NO_FMV"));
    if(!trace_enabled && !skip_fmv && !buzz_frontend_needs_include())return;
    if(skip_fmv)fprintf(stderr,"[buzz-video] No-FMV bootstrap: report unsupported movies as errored, not playing.\n");
    const Fn hooks[]={traced<0>,traced<1>,traced<2>,traced<3>,traced<4>,
                      traced<5>,traced<6>,traced<7>,traced<8>,traced<9>,traced<10>,traced<11>,
                      traced<12>,traced<13>,traced<14>,traced<15>,traced<16>,traced<17>,traced<18>,
                      traced<19>,traced<20>,traced<21>,traced<22>,traced<23>,traced<24>};
    static_assert(sizeof(hooks)/sizeof(*hooks)==sizeof(probes)/sizeof(*probes),"one hook per probe");
    for(unsigned i=0;i<sizeof(probes)/sizeof(*probes);++i){
        if(!buzz_boot::needs_probe(i,trace_enabled,skip_fmv) && !(i==11&&buzz_frontend_needs_include()))continue;
        for(uint64_t k=0;k<function_table_count;++k)if(function_table[k].addr==probes[i].pc){
            probes[i].original=function_table[k].func;
            ppu_register_function(probes[i].pc,hooks[i]);
            break;
        }
    }
}
