#include "../ps3recomp/runtime/spu/spu_helpers.h"
#include "../port/native_geometry_shuffle.h"
#include <assert.h>
#include <stdio.h>
static uint32_t state=0x184210fa;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static u128 random_vector(void){u128 v;for(unsigned n=0;n<4;++n)v._u32[n]=random_word();return v;}
static void check(u128 a,u128 b,u128 c)
{
    BuzzGeometryShuffle p=buzz_geometry_shuffle_prepare(c);
    u128 actual=buzz_geometry_shuffle_apply(a,b,p),expected=spu_shufb_ref(a,b,c);
    assert(!memcmp(&actual,&expected,16));
    // A plan belongs to the selector, never to its original source vectors.
    a=random_vector();b=random_vector();
    actual=buzz_geometry_shuffle_apply(a,b,p);expected=spu_shufb_ref(a,b,c);
    assert(!memcmp(&actual,&expected,16));
}
int main(void)
{
    for(unsigned s=0;s<256;++s){u128 c;memset(&c,s,16);check(random_vector(),random_vector(),c);}
    for(unsigned n=0;n<500000;++n)check(random_vector(),random_vector(),random_vector());
    puts("PASS: all 256 selectors and 1000000 random planned applications, independent source reuse");
}
