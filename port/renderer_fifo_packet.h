#ifndef BUZZ_RENDERER_FIFO_PACKET_H
#define BUZZ_RENDERER_FIFO_PACKET_H
#include <stdint.h>

/* Only subchannel-zero packets without FIFO-bridge side effects. The two
 * existing method consumers still execute every word, in their original order.
 * GET_REPORT has extra bridge work; fences handled by those consumers remain
 * ordinary calls, including submit-and-wait and the subsequent label write. */
static inline int buzz_fifo_packet_eligible(uint32_t subchannel, uint32_t type,
                                           uint32_t method, uint32_t count) {
    if (subchannel || (type != 0 && type != 2) || !count || count > 2047 ||
        (method & 3) || method < 0x200 || method >= 0x2000) return 0;
    const uint32_t last = method + (type == 0 ? (count - 1) * 4 : 0);
    return last < 0x2000 && !(method <= 0x1800 && last >= 0x1800);
}
#endif
