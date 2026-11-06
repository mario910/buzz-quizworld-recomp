#pragma once
#include "ps3emu/ps3_ftrace.h"

/* Port-private trace IDs. These spans share the SDK clock and capture window.
 * Durations overlap (GPU waits can be inside drain/present): never add them. */
enum {
    BUZZ_FT_RSX_IDLE=100,       /* a: 0 SDK wait, 1 kick, 2 timeout; b: budget ms */
    BUZZ_FT_HOST_PRESENT=101,   /* wraps the host presentation call */
    BUZZ_FT_VK_FENCE=102,       /* a: 1 sync, 2/3 draw context, 4 copy, 5 window */
    BUZZ_FT_FIFO_BEFORE_IDLE=103, /* instant: a=guest put, b=guest get */
    BUZZ_FT_FIFO_AFTER_IDLE=104,
    BUZZ_FT_VK_ACQUIRE=105,
    BUZZ_FT_VK_QUEUE_PRESENT=106,
    BUZZ_FT_FIFO_STOP=107,      /* instant: who=reason, a=get, b=put */
    BUZZ_FT_FIFO_KICK=108,      /* instant: producer signalled the kick event */
    BUZZ_FT_DMA_PUBLICATION=109, /* native tid, address, byte count (4/8 only) */
    BUZZ_FT_FIFO_PARK_WORD=110,  /* address and self-jump word */
    BUZZ_FT_PPU_PUBLICATION=111, /* native tid, address, size; filtered SDK hook */
    BUZZ_FT_DMA_WORD=112,        /* native tid, address, first word after DMA */
    BUZZ_FT_DMA_WORD_HIGH=113,   /* native tid, address+4, second word */
    BUZZ_FT_FIFO_JUMP=114        /* address and non-self jump consumed */
};
/* A diagnostic-only monotonic filter: watch lines observed at a wait, allowing
 * harmless hash collisions. Atomic registration avoids a cross-thread race.
 * It is NOT a notification mechanism and does not alter guest synchronization. */
#ifdef BUZZ_PRODUCER_DEPENDENCY_TRACE
PS3_FT_SHARED uint64_t buzz_publication_lines[256] = {0};
static inline unsigned buzz_publication_bit(uint32_t ea) {
    return ((ea >> 7) ^ (ea >> 21)) & 16383u;
}
static inline void buzz_publication_watch(uint32_t ea) {
    const unsigned bit=buzz_publication_bit(ea);
    __atomic_fetch_or(&buzz_publication_lines[bit>>6],1ull<<(bit&63),__ATOMIC_RELAXED);
}
static inline int buzz_publication_watched(uint32_t ea) {
    const unsigned bit=buzz_publication_bit(ea);
    return (__atomic_load_n(&buzz_publication_lines[bit>>6],__ATOMIC_RELAXED)>>(bit&63))&1;
}
#endif
static inline uint64_t buzz_wait_trace_begin(void) {
    return PS3_FT_ON()?ps3_ft_now():0;
}
static inline void buzz_wait_trace_end(int kind,uint32_t a,uint32_t b,uint64_t start) {
    if(start)ps3_ft_span(kind,0,a,b,start,ps3_ft_now());
}
