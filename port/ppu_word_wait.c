#include "ppu_word_wait.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"

#ifdef BUZZ_PPU_WORD_WAIT_TRACE
#include "ps3emu/ps3_ftrace.h"
#endif

enum { BUZZ_PPU_WAITERS = 16 };
typedef struct {
    uint8_t* memory;
    uint32_t address;
    int active;
    CONDITION_VARIABLE wake;
} BuzzPpuWaiter;
static BuzzPpuWaiter buzz_ppu_waiters[BUZZ_PPU_WAITERS];
static SRWLOCK buzz_ppu_wait_lock = SRWLOCK_INIT;
static uint64_t buzz_ppu_wait_lines;

static uint64_t buzz_ppu_line_bit(uint32_t ea)
{
    return 1ull << (((ea >> 7) ^ (ea >> 17)) & 63);
}
static void buzz_ppu_update_filter(void)
{
    uint64_t bits = 0;
    for (unsigned n = 0; n < BUZZ_PPU_WAITERS; ++n)
        if (buzz_ppu_waiters[n].active) bits |= buzz_ppu_line_bit(buzz_ppu_waiters[n].address);
    /* Store -> load ordering with the producer's copy -> fence -> filter
     * load: an unobserved registration must see the already published word. */
    __atomic_store_n(&buzz_ppu_wait_lines, bits, __ATOMIC_SEQ_CST);
}

int buzz_ppu_word_wait(uint8_t* memory, uint32_t address, uint32_t caller)
{
    if (!memory || (address & 3) || address < 0x10000u || address >= 0xF0000000u) return 0;
    uint32_t* word = (uint32_t*)(memory + address);
    if (!__atomic_load_n(word, __ATOMIC_ACQUIRE)) return 1;
#ifdef BUZZ_PPU_WORD_WAIT_TRACE
    const uint64_t start = PS3_FT_ON() ? ps3_ft_now() : 0;
#else
    (void)caller;
#endif
    AcquireSRWLockExclusive(&buzz_ppu_wait_lock);
    unsigned slot = 0;
    while (slot < BUZZ_PPU_WAITERS && buzz_ppu_waiters[slot].active) ++slot;
    if (slot == BUZZ_PPU_WAITERS) {
        ReleaseSRWLockExclusive(&buzz_ppu_wait_lock);
        return 0;
    }
    BuzzPpuWaiter* w = &buzz_ppu_waiters[slot];
    w->memory = memory; w->address = address; w->active = 1;
    buzz_ppu_update_filter();
#if !defined(__x86_64__) && !defined(_M_X64)
    /* Explicit StoreLoad barrier on weakly ordered hosts, paired with the
     * producer's post-copy fence. On x86-64 the seq-cst filter store already
     * provides this ordering. Do not rely on release/acquire code generation
     * alone for the registration versus guest-memory observation handshake. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
    /* Register, recheck and park under the same lock used by notification.
     * Even if the writer is uninstrumented, the two-ms bound returns control
     * to the guest. No spin and no repeated syscall while the word is busy. */
    if (__atomic_load_n(word, __ATOMIC_ACQUIRE))
        SleepConditionVariableSRW(&w->wake, &buzz_ppu_wait_lock, 2, 0);
    w->active = 0;
    buzz_ppu_update_filter();
    ReleaseSRWLockExclusive(&buzz_ppu_wait_lock);
#ifdef BUZZ_PPU_WORD_WAIT_TRACE
    if (start) ps3_ft_span(115, GetCurrentThreadId(), address, caller, start, ps3_ft_now());
#endif
    return 1;
}

void buzz_ppu_word_dma_written(uint32_t address, uint32_t size)
{
    if (!size) return;
    const uint64_t bits = __atomic_load_n(&buzz_ppu_wait_lines, __ATOMIC_ACQUIRE);
    if (!bits) return;
    const uint64_t lo = address, hi = lo + size;
    /* Never wrap a guest range into address zero. A large transfer can skip
     * the coarse filter instead of scanning millions of cache lines. */
    int maybe = size >= 8192;
    for (uint64_t line = lo & ~127ull; !maybe && line < hi; line += 128)
        maybe = (bits & buzz_ppu_line_bit((uint32_t)line)) != 0;
    if (!maybe) return;
    AcquireSRWLockExclusive(&buzz_ppu_wait_lock);
    for (unsigned n = 0; n < BUZZ_PPU_WAITERS; ++n) {
        BuzzPpuWaiter* w = &buzz_ppu_waiters[n];
        if (!w->active || (uint64_t)w->address >= hi || (uint64_t)w->address + 4 <= lo) continue;
        const uint32_t value = __atomic_load_n((uint32_t*)(w->memory+w->address), __ATOMIC_ACQUIRE);
#ifdef BUZZ_PPU_WORD_WAIT_TRACE
        if (PS3_FT_ON()) {
            const uint64_t now = ps3_ft_now();
            ps3_ft_span(116, GetCurrentThreadId(), w->address, __builtin_bswap32(value), now, now);
        }
#endif
        if (!value) WakeConditionVariable(&w->wake);
    }
    ReleaseSRWLockExclusive(&buzz_ppu_wait_lock);
}
