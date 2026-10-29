#include "../ps3recomp/runtime/ppu/ppu_context.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#define PS3EMU_PS3_FTRACE_H
#define PS3_FT_ON() enabled
enum {PS3_FT_PPU_SCOPE=26};
static bool enabled;
static uint64_t clock_value;
static uint64_t ps3_ft_now(){return clock_value+=100;}
struct Event {int kind;uint32_t tid,pc,lr;uint64_t start,end;};
static Event events[8];static unsigned count;
static void ps3_ft_span(int kind,uint32_t tid,uint32_t pc,uint32_t lr,uint64_t start,uint64_t end){events[count++]={kind,tid,pc,lr,start,end};}
#include "../port/producer_probe.h"
int main()
{
    ppu_context ctx;memset(&ctx,0x73,sizeof ctx);ctx.thread_id=3;ctx.lr=0x28a6f0;
    ppu_context before=ctx;
    {BuzzProducerProbeScope off(&ctx,0x2898a4);}
    assert(!count && !clock_value && !memcmp(&ctx,&before,sizeof ctx));
    enabled=true;
    {BuzzProducerProbeScope outer(&ctx,0x28a61c);
        {BuzzProducerProbeScope inner(&ctx,0x2898a4);}
        assert(!memcmp(&ctx,&before,sizeof ctx));
    }
    assert(count==2 && !memcmp(&ctx,&before,sizeof ctx));
    assert(events[0].kind==26 && events[0].tid==3 && events[0].pc==0x2898a4 && events[0].lr==0x28a6f0);
    assert(events[0].start==200 && events[0].end==300 && events[1].start==100 && events[1].end==400);
    {BuzzProducerProbeScope incoming_lr(&ctx,0x26beec);ctx.lr=0x1234;}
    assert(events[2].lr==0x28a6f0 && ctx.lr==0x1234);
    puts("PASS: producer scope is guest-state read-only, disabled no-op, nested timing, stable incoming LR");
}
