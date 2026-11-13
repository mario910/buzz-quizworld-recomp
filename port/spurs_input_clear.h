/* SPURS staging only: omit zero stores to bytes the input copies overwrite.
 * Input list traversal, copies, padding and all bytes at job entry are kept.
 * Never use this for a local store visible to a running SPU. */
#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t next, end;
    int deferred;
} buzz_spurs_input_clear;

static inline buzz_spurs_input_clear buzz_spurs_clear_regions(
    uint8_t* ls, uint32_t binary, uint32_t io, uint32_t io_bytes,
    uint32_t region_end, int zero_mode, int enabled)
{
    buzz_spurs_input_clear plan = {0, 0, 0};
    if (zero_mode != 1 || region_end <= binary) return plan;
    const uint32_t limit = 256u * 1024u;
    const uint64_t aligned = ((uint64_t)io_bytes + 1023u) & ~UINT64_C(1023);
    const uint64_t io_end = (uint64_t)io + aligned;
    if (enabled && binary <= io && io <= region_end && region_end <= limit &&
        aligned && io_end <= region_end) {
        memset(ls + binary, 0, io - binary);
        memset(ls + (uint32_t)io_end, 0, region_end - (uint32_t)io_end);
        plan.next = io;
        plan.end = (uint32_t)io_end;
        plan.deferred = 1;
    } else {
        /* Unproven/wrapped layouts retain the original bounded clear. */
        memset(ls + binary, 0, (region_end < limit ? region_end : limit) - binary);
    }
    return plan;
}

static inline void buzz_spurs_clear_input_gap(
    uint8_t* ls, buzz_spurs_input_clear* plan, uint32_t destination, uint32_t bytes)
{
    if (!plan->deferred) return;
    const uint32_t start = destination < plan->end ? destination : plan->end;
    if (plan->next < start) memset(ls + plan->next, 0, start - plan->next);
    const uint64_t end = (uint64_t)destination + bytes;
    plan->next = end < plan->end ? (uint32_t)end : plan->end;
}

static inline void buzz_spurs_clear_input_tail(uint8_t* ls, buzz_spurs_input_clear* plan)
{
    if (plan->deferred && plan->next < plan->end)
        memset(ls + plan->next, 0, plan->end - plan->next);
}
