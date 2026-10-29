#define BUZZ_SPU_AVX512
#ifdef TEST_REJECT_ISA
#define BUZZ_AVX512_GATE_CHECK() 0
#else
#define BUZZ_AVX512_GATE_CHECK() 1
#endif
#include "../port/cpu_isa_gate.cpp"
#include <cassert>
static int sentinel;
__attribute__((constructor)) static void optional_isa_constructor(void)
{
#ifdef TEST_REJECT_ISA
    /* Reaching this before rejection is a test failure, even on an AVX512 host. */
    ExitProcess(42);
#else
    sentinel=1;
#endif
}
int main(){assert(sentinel==1);std::puts("PASS: baseline ISA gate runs before ordinary SPU-style constructors");return 0;}
