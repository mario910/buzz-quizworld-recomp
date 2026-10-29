/* Baseline-x86 startup gate: SPU registration constructors run before main,
 * so checking only main would be too late for an optional ISA build. */
#ifdef BUZZ_SPU_AVX512
#include "cpu_isa.h"
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif
#ifndef BUZZ_AVX512_GATE_CHECK
#define BUZZ_AVX512_GATE_CHECK() buzz_avx512_host_supported()
#endif
static void buzz_early_isa_gate(void)
{
    if(!BUZZ_AVX512_GATE_CHECK()){
        std::fputs("AVX512F/BW/VL or OS register state unavailable; no lifted SPU constructors executed. Rebuild with -SpuIsa avx2.\n",stderr);
#ifdef _WIN32
        ExitProcess(2);
#else
        std::_Exit(2);
#endif
    }
}
#ifdef _WIN32
/* XCT sorts before the ordinary Clang/MSVC XCU constructor section. Keep
 * this TU baseline and force retention under /OPT:REF. Covered by an actual
 * negative-gate subprocess test with a sentinel XCU constructor. */
#pragma section(".CRT$XCT",read)
extern "C" __declspec(allocate(".CRT$XCT")) void (*const buzz_cpu_isa_initializer)(void)=buzz_early_isa_gate;
#pragma comment(linker,"/include:buzz_cpu_isa_initializer")
#else
__attribute__((constructor(101))) static void buzz_early_isa_constructor(void){buzz_early_isa_gate();}
#endif
#endif
