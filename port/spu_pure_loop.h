#pragma once
#include "spu_context.h"
#ifdef __cplusplus
extern "C" {
#endif
// For audited private-LS loops with no enabled interrupt sources or channel/DMA
// operations. Zero preserves the ordinary dispatch at every block boundary.
unsigned buzz_spu_pure_loop_budget(const spu_context* ctx, uint32_t back_pc, uint32_t body_pc);
void buzz_spu_pure_loop_hops(spu_context* ctx, uint32_t back_pc, uint32_t body_pc, unsigned pairs);
#ifdef __cplusplus
}
#endif
