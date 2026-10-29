#include "../ps3recomp/runtime/spu/spurs_job_pool.h"
#include "../ps3recomp/runtime/platform/win32_compat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
static _Thread_local uint32_t spu;
void spurs_job_release_thread_context(void) {}
void spurs_job_set_spu_num(uint32_t value) { spu = value; }
uint32_t spurs_job_get_spu_num(void) { return spu; }
static _Thread_local int submitter;
static atomic_uint caller_jobs;
static atomic_uint active, peak, completed, seen[128], lane_active[6], used_lanes;
static atomic_uint groups_active[2], groups_overlap;
static void job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    assert(spu >= 1 && spu <= 4);
    if (submitter) atomic_fetch_add(&caller_jobs, 1);
    assert(!spurs_job_pool_run(0x55, 5, 4, 0x1000, 1, 256, 0xabc, 5, job));
    assert(chain == 0xabc && stride == 256 && ea == 0x1000 + (unsigned)(index - 5) * stride);
    assert(atomic_fetch_add(&lane_active[spu], 1) == 0);
    atomic_fetch_or(&used_lanes, 1u << spu);
    unsigned n = atomic_fetch_add(&active, 1) + 1, old = atomic_load(&peak);
    while (old < n && !atomic_compare_exchange_weak(&peak, &old, n)) {}
    Sleep((index & 3) + 1); /* intentionally nonuniform completion order */
    assert(atomic_fetch_add(&seen[index], 1) == 0);
    atomic_fetch_sub(&active, 1);
    atomic_fetch_sub(&lane_active[spu], 1);
    atomic_fetch_add(&completed, 1);
}
static void reset(void)
{
    for (unsigned n = 0; n < 128; ++n) atomic_store(&seen[n], 0);
    atomic_store(&completed, 0);
}
static void ranged_job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    assert(chain == 0xabc && index >= 5 && index < 17);
    assert(spu >= 1 && spu <= 4);
    assert(atomic_fetch_add(&lane_active[spu], 1) == 0);
    if (index < 8) assert(stride == 64 && ea == 0x8000 + (unsigned)(index-5)*64);
    else if (index < 10) assert(stride == 256 && ea == 0x12000 + (unsigned)(index-8)*256);
    else assert(stride == 128 && ea == 0x18000 + (unsigned)(index-10)*128);
    Sleep((index & 3) + 1);
    assert(atomic_fetch_add(&seen[index], 1) == 0);
    atomic_fetch_sub(&lane_active[spu], 1);
    atomic_fetch_add(&completed, 1);
}
static void simultaneous_job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    unsigned group = chain - 0xabc;
    assert(group < 2 && index >= (int)(group * 16) && index < (int)(group * 16 + 8));
    assert(ea == 0x1000 + (unsigned)(index - (int)group * 16) * stride);
    assert(atomic_fetch_add(&lane_active[spu], 1) == 0);
    atomic_fetch_add(&groups_active[group], 1);
    if (atomic_load(&groups_active[1 - group])) atomic_store(&groups_overlap, 1);
    Sleep((index & 15) == 7 ? 50 : 1);
    assert(atomic_fetch_add(&seen[index], 1) == 0);
    atomic_fetch_sub(&lane_active[spu], 1);
    atomic_fetch_sub(&groups_active[group], 1);
    atomic_fetch_add(&completed, 1);
}
static DWORD WINAPI submit(LPVOID unused)
{
    unsigned group = (unsigned)(uintptr_t)unused;
    submitter = 1; spu = 99;
    assert(spurs_job_pool_run(0x55, 5, 4, 0x1000, 8, 256, 0xabc + group,
                              (int)group * 16, simultaneous_job));
    assert(spu == 99);
    return 0;
}
static uint32_t burst_outputs[32];
static void burst_job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    assert(chain == 0xabc && index >= 0 && index < 32);
    assert(stride == 128 && ea == 0x4000u + (unsigned)index * stride);
    assert(spu >= 1 && spu <= 4);
    assert(atomic_fetch_add(&lane_active[spu], 1) == 0);
    assert(atomic_fetch_add(&seen[index], 1) == 0);
    burst_outputs[index] = 0x12340000u ^ (unsigned)index;
    atomic_fetch_sub(&lane_active[spu], 1);
    atomic_fetch_add(&completed, 1);
}
/* Stage sequences: stage k+1's ranges are computed from outputs stage k
 * wrote, by the continuation, after the whole stage returned. */
enum { STAGES = 40 };
typedef struct {
    unsigned stage, stages, calls, slow;
    atomic_uint done[STAGES];
    uint32_t out[STAGES][24];
} Sequence;
static Sequence* sequences[2];
static unsigned stage_jobs(unsigned stage) { return 1 + stage * 7 % 23; }
static void stage_job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    Sequence* s = sequences[chain - 0x700];
    const unsigned stage = ea >> 16, slot = (ea & 0xffff) / 64;
    assert(stride == 64 && stage < s->stages && slot < stage_jobs(stage));
    assert(index == (int)(stage * 100 + slot));
    assert(spu >= 1 && spu <= 4);
    assert(atomic_fetch_add(&lane_active[spu], 1) == 0);
    /* barrier: the whole previous stage finished before any job of this one */
    if (stage) assert(atomic_load(&s->done[stage - 1]) == stage_jobs(stage - 1));
    assert(atomic_load(&s->done[stage]) < stage_jobs(stage));
    if ((index & 7) == 3) Sleep(1);
    s->out[stage][slot] = stage * 1000 + slot;
    atomic_fetch_sub(&lane_active[spu], 1);
    atomic_fetch_add(&s->done[stage], 1);
}
static unsigned next_stage(void* context, SpursPoolRange out[32])
{
    Sequence* s = context;
    assert(spu >= 1 && spu <= 4); /* on the last completer, a pool worker */
    ++s->calls;
    const unsigned stage = s->stage;
    assert(atomic_load(&s->done[stage]) == stage_jobs(stage));
    for (unsigned slot = 0; slot < stage_jobs(stage); ++slot)
        assert(s->out[stage][slot] == stage * 1000 + slot);
    if (s->slow) Sleep(2); /* the owner must keep waiting meanwhile */
    if (stage + 1 == s->stages) return 0;
    /* derive the next stage from what this one produced */
    const unsigned next = s->out[stage][0] / 1000 + 1, count = stage_jobs(next);
    s->stage = next;
    /* split into two ranges to cover multi-range stages */
    const unsigned head = count > 1 ? count / 2 : count;
    out[0] = (SpursPoolRange){next << 16, 64, head, (int)(next * 100)};
    if (head == count) return 1;
    out[1] = (SpursPoolRange){(next << 16) + head * 64, 64, count - head, (int)(next * 100 + head)};
    return 2;
}
static void run_sequence(Sequence* s, unsigned which, unsigned stages, unsigned slow)
{
    memset(s, 0, sizeof *s);
    s->stages = stages; s->slow = slow;
    sequences[which] = s;
    SpursPoolRange first = {0, 64, stage_jobs(0), 0};
    assert(spurs_job_pool_run_sequence(0x55, 5, 4, &first, 1, 0x700 + which, stage_job,
                                       next_stage, s));
    /* returned only after the final stage and the final continuation */
    assert(s->calls == stages && s->stage == stages - 1);
    for (unsigned k = 0; k < stages; ++k) assert(atomic_load(&s->done[k]) == stage_jobs(k));
}
static Sequence sequence_a, sequence_b;
static atomic_uint tiny_rounds;
static void tiny_job(uint32_t ea, int index, uint32_t stride, uint32_t chain)
{
    assert(ea == 0x4000 && !index && stride == 128 && chain == 0xabc);
    assert(spu >= 1 && spu <= 4);
    assert(atomic_fetch_add(&lane_active[spu], 1) == 0);
    atomic_fetch_sub(&lane_active[spu], 1);
}
static DWORD WINAPI sequence_thread(LPVOID unused)
{
    (void)unused;
    submitter = 1; spu = 99;
    for (unsigned round = 0; round < 10; ++round) run_sequence(&sequence_b, 1, STAGES, 1);
    assert(spu == 99);
    return 0;
}
static DWORD WINAPI tiny_thread(LPVOID unused)
{
    (void)unused;
    submitter = 1; spu = 99;
    /* each finishing tiny batch wakes every waiting owner: spurious wakes for
     * the sequence owner between its stages */
    while (atomic_load(&tiny_rounds) < 3000) {
        assert(spurs_job_pool_run(0x55, 5, 4, 0x4000, 1, 128, 0xabc, 0, tiny_job));
        atomic_fetch_add(&tiny_rounds, 1);
    }
    return 0;
}
int main(void)
{
    submitter = 1; spu = 99;
    assert(!spurs_job_pool_run(0x55, 4, 4, 0x1000, 8, 256, 0xabc, 5, job));
    assert(!spurs_job_pool_run(0x55, 5, 0, 0x1000, 8, 256, 0xabc, 5, job));
    assert(!spurs_job_pool_run(0x55, 5, 4, 0xfffffff0u, 8, 256, 0xabc, 5, job));
    for (unsigned round = 0; round < 20; ++round) {
        reset();
        assert(spurs_job_pool_run(0x55, 5, 4, 0x1000, 64, 256, 0xabc, 5, job));
        assert(spu == 99);
        assert(atomic_load(&active) == 0 && atomic_load(&completed) == 64);
        for (unsigned n = 5; n < 69; ++n) assert(atomic_load(&seen[n]) == 1);
    }
    assert(atomic_load(&peak) > 1 && atomic_load(&peak) <= 4);
    assert(atomic_load(&used_lanes) == 0x1e);
    const char* help = getenv("SPURS_POOL_CALLER_HELP");
    assert((atomic_load(&caller_jobs) != 0) == (help && help[0] && help[0] != '0'));
    assert(!spurs_job_pool_run(0x66, 5, 4, 0x1000, 1, 256, 0xabc, 5, job));
    /* Simultaneous chain callers serialize admission; neither can return
     * before its own full list, and per-SPU resources cannot overlap. */
    reset();
    HANDLE one = CreateThread(NULL, 0, submit, NULL, 0, NULL);
    HANDLE two = CreateThread(NULL, 0, submit, (LPVOID)(uintptr_t)1, 0, NULL);
    assert(one && two);
    assert(WaitForSingleObject(one, 10000) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(two, 10000) == WAIT_OBJECT_0);
    CloseHandle(one); CloseHandle(two);
    assert(atomic_load(&completed) == 16);
    assert(atomic_load(&groups_overlap));
    SpursPoolRange ranges[] = {{0x8000,64,3,5},{0x12000,256,2,8},{0x18000,128,7,10}};
    for (unsigned round = 0; round < 20; ++round) {
        reset();
        assert(spurs_job_pool_run_ranges(0x55,5,4,ranges,3,0xabc,ranged_job));
        assert(spu == 99 && atomic_load(&completed) == 12);
        for (unsigned i = 5; i < 17; ++i) assert(atomic_load(&seen[i]) == 1);
    }
    /* Reuse the submitting stack through 2000 tiny bursts, including one-job
     * batches. A last-worker handoff must never retain that stack or return
     * before the real outputs are published. SPU lane ownership still holds. */
    for (unsigned round = 0; round < 2000; ++round) {
        unsigned count = 1 + round % 17;
        reset(); memset(burst_outputs, 0, sizeof burst_outputs);
        assert(spurs_job_pool_run(0x55,5,4,0x4000,count,128,0xabc,0,burst_job));
        assert(atomic_load(&completed) == count && spu == 99);
        for (unsigned i = 0; i < count; ++i) {
            assert(atomic_load(&seen[i]) == 1);
            assert(burst_outputs[i] == (0x12340000u ^ i));
        }
    }
    /* Sequences: alone, a one-stage sequence, then concurrently with a second
     * sequence owner (fairness, lane exclusivity) and a stream of tiny
     * batches (spurious done wakes while continuations run unlocked). */
    const int sequences_on = !(help && help[0] && help[0] != '0'); /* caller-help ignores them */
    for (unsigned round = 0; sequences_on && round < 50; ++round)
        run_sequence(&sequence_a, 0, STAGES, 0);
    if (sequences_on) {
        run_sequence(&sequence_a, 0, 1, 0);
        HANDLE seq = CreateThread(NULL, 0, sequence_thread, NULL, 0, NULL);
        HANDLE tiny = CreateThread(NULL, 0, tiny_thread, NULL, 0, NULL);
        assert(seq && tiny);
        for (unsigned round = 0; round < 10; ++round) run_sequence(&sequence_a, 0, STAGES, 1);
        assert(WaitForSingleObject(seq, 60000) == WAIT_OBJECT_0);
        assert(WaitForSingleObject(tiny, 60000) == WAIT_OBJECT_0);
        CloseHandle(seq); CloseHandle(tiny);
    }
    {
        SpursPoolRange bad = {0, 64, 0, 0};
        assert(!spurs_job_pool_run_sequence(0x55, 5, 4, &bad, 1, 0x700, stage_job,
                                            next_stage, &sequence_a));
    }
    assert(!spurs_job_pool_run_ranges(0x55,5,4,NULL,3,0xabc,ranged_job));
    assert(!spurs_job_pool_run_ranges(0x55,5,4,ranges,33,0xabc,ranged_job));
    ranges[2].first_index = -1;
    assert(!spurs_job_pool_run_ranges(0x55,5,4,ranges,3,0xabc,ranged_job));
    ranges[2].first_index = 10; ranges[2].first = 0xfffffff0u;
    assert(!spurs_job_pool_run_ranges(0x55,5,4,ranges,3,0xabc,ranged_job));
    spurs_job_pool_shutdown();
    reset();
    assert(spurs_job_pool_run(0x66, 5, 2, 0x1000, 8, 256, 0xabc, 5, job));
    assert(atomic_load(&completed) == 8);
    spurs_job_pool_shutdown();
    /* restart with a different worker count, sequences included */
    if (sequences_on) {
        static Sequence restarted;
        memset(&restarted, 0, sizeof restarted);
        restarted.stages = 12; sequences[0] = &restarted;
        SpursPoolRange first = {0, 64, stage_jobs(0), 0};
        assert(spurs_job_pool_run_sequence(0x77, 5, 2, &first, 1, 0x700, stage_job,
                                           next_stage, &restarted));
        assert(restarted.calls == 12);
        spurs_job_pool_shutdown();
    }
    puts("SPURS pool: unique SPU IDs, exactly-once work, full-list barrier, bounds, restart PASS");
}
