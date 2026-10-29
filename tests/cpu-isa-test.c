#include "../port/cpu_isa.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint32_t ecx=(1u<<26)|(1u<<27)|(1u<<28),ebx=(1u<<5)|(1u<<16)|(1u<<30)|(1u<<31);
    assert(buzz_avx512_fields(ecx,ebx,0xe6));
    for(unsigned n=0;n<32;++n){if(ecx&(1u<<n))assert(!buzz_avx512_fields(ecx&~(1u<<n),ebx,0xe6));
        if(ebx&(1u<<n))assert(!buzz_avx512_fields(ecx,ebx&~(1u<<n),0xe6));}
    for(unsigned n=0;n<8;++n)if(0xe6&(1u<<n))assert(!buzz_avx512_fields(ecx,ebx,0xe6&~(1u<<n)));
    assert(!buzz_avx512_fields(ecx,ebx,0x6)); /* AVX2 OS support alone is insufficient. */
    puts("PASS: every required CPU feature and XCR0 state bit is independently checked");
    int available=buzz_avx512_host_supported();
    printf("Current CPU/OS: AVX2 + AVX512F/BW/VL + saved register state = %s\n",available?"supported":"unavailable");
    return available?0:2;
}
