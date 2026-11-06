#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A wake is only a hint. The caller always returns to the original guest
 * predicate; timeout, unrelated writers and full capacity never invent work. */
int buzz_ppu_word_wait(uint8_t* memory, uint32_t address, uint32_t caller);
/* Called after the SDK's post-DMA sequentially consistent fence. */
void buzz_ppu_word_dma_written(uint32_t address, uint32_t size);
#ifdef __cplusplus
}
#endif
