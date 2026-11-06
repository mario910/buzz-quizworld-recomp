#include "../port/vblank_clock.h"
#include <cassert>
#include <cstdio>
int main(){
 buzz::VblankClock c(123);assert(c.due(123)&&!c.due(122));
 for(uint64_t i=1;i<=60*60*60;++i){c.advance();assert(c.deadline()==123+i*1000000000ull/60);}
 c.reset(4000000000000ull);for(unsigned i=0;i<60;++i)c.advance();assert(c.deadline()==4001000000000ull);
 assert(c.wait_ms(c.deadline(),4)==0);
 assert(c.wait_ms(c.deadline()-1,4)==1);
 assert(c.wait_ms(c.deadline()-1000001,4)==2);
 assert(c.wait_ms(c.deadline()-100000000,4)==4);
 assert(c.wait_ms(c.deadline()-1,0)==0);
 puts("PASS 60 Hz clock: one-hour drift, due boundary and pause reset");
}
