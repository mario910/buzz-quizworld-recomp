/* SPURS job corpus: capture real job executions and replay them offline.
 *
 * Capture (in the running game):
 *   BUZZ_JOB_CORPUS=<dir>          enable, write job_<image>_<seq>.bjc.gz there
 *   BUZZ_JOB_CORPUS_IMAGE=41       image to capture (default 41)
 *   BUZZ_JOB_CORPUS_AFTER_MS=0     start capturing this long after boot
 *   BUZZ_JOB_CORPUS_EVERY=1        capture every Kth job of the image
 *   BUZZ_JOB_CORPUS_MAX=64         stop after N captured jobs
 *   BUZZ_JOB_CORPUS_PERIOD_MS=0    at most one capture per period (spreads a
 *                                  corpus over a whole session)
 * Each file holds the staged entry state (registers + the whole LS), every
 * MFC transfer and atomic the job issued in order (with its bytes), and the
 * exit state (registers, LS, pc/status/stop, outbound mailboxes).
 *
 * Replay (no game boot): BUZZ_REPLAY_CORPUS=<dir or file>. Guest memory is
 * seeded from the recorded GET/GETLLAR bytes (first observation of each
 * byte wins; bytes the job PUT itself are left to the job), the job is run
 * through the same entry path as in the game, and every transfer, atomic and
 * the full exit state are compared. This is the reference any native
 * reimplementation of a job must reproduce byte for byte.
 */
#include "spu_workload.h"
#include "zlib.h"
#include "job_replay_policy.h"
#ifdef BUZZ_NATIVE_IMAGE36
#include "native_image36.h"
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#else
#include <dirent.h>
#include <time.h>
#endif

extern "C" uint8_t* vm_base;

namespace {

constexpr uint32_t kMagic = 0x434A5A42; /* "BZJC" */
constexpr uint32_t kVersion = 1;

struct ExitState {
    uint32_t pc, status, stop_code, mbox_valid, mbox, intr_valid, intr;
};

struct Recording {
    spu_context* ctx = nullptr;
    int image = 0;
    uint32_t job_ea = 0, seq = 0;
    std::vector<uint8_t> entry;   /* gpr + ls */
    std::vector<uint8_t> records; /* kind, lsa, ea, size, bytes */
    uint32_t count = 0;
};

template <class T> void put(std::vector<uint8_t>& out, const T& value)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

void put_bytes(std::vector<uint8_t>& out, const void* data, size_t size)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

void put_state(std::vector<uint8_t>& out, const spu_context* ctx)
{
    put_bytes(out, ctx->gpr, sizeof(ctx->gpr));
    put_bytes(out, ctx->ls, SPU_LS_SIZE);
}

ExitState exit_of(const spu_context* ctx)
{
    ExitState e{};
    e.pc = ctx->pc;
    e.status = ctx->status;
    e.stop_code = ctx->stop_code;
    e.mbox_valid = spu_channel_has_data(&ctx->ch_out_mbox) ? 1u : 0u;
    e.mbox = e.mbox_valid ? ctx->ch_out_mbox.value : 0;
    e.intr_valid = spu_channel_has_data(&ctx->ch_out_intr_mbox) ? 1u : 0u;
    e.intr = e.intr_valid ? ctx->ch_out_intr_mbox.value : 0;
    return e;
}

uint64_t now_ms()
{
#ifdef _WIN32
    return GetTickCount64();
#else
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}

/* ---- capture ------------------------------------------------------------ */

std::string g_dir;
int g_image = 41;
uint64_t g_start_ms = 0;
uint32_t g_every = 1, g_max = 64;
uint64_t g_period_ms = 0;
std::atomic<uint64_t> g_last_ms{0};
std::atomic<uint32_t> g_seen{0}, g_taken{0};
/* DMA may complete on another thread than the one running the job, so the
 * observer finds the recording by context, not by thread. */
constexpr int kSlots = 8;
std::atomic<Recording*> g_slots[kSlots];
thread_local Recording* t_recording = nullptr;

void observe(spu_context* ctx, uint32_t kind, uint32_t lsa, uint32_t ea, uint32_t size,
             const void* data)
{
    for (auto& slot : g_slots) {
        Recording* r = slot.load(std::memory_order_acquire);
        if (!r || r->ctx != ctx) continue;
        put(r->records, kind);
        put(r->records, lsa);
        put(r->records, ea);
        put(r->records, size);
        put_bytes(r->records, data, size);
        r->count++;
        return;
    }
}

void capture_begin(spu_context* ctx, int image, uint32_t job_ea)
{
    if (image != g_image || now_ms() < g_start_ms) return;
    if (g_period_ms) {
        const uint64_t now = now_ms();
        uint64_t last = g_last_ms.load(std::memory_order_relaxed);
        if (now - last < g_period_ms || !g_last_ms.compare_exchange_strong(last, now)) return;
    }
    uint32_t seen = g_seen.fetch_add(1, std::memory_order_relaxed);
    if (seen % g_every) return;
    uint32_t seq = g_taken.fetch_add(1, std::memory_order_relaxed);
    if (seq >= g_max) return;
    Recording* r = new Recording;
    r->ctx = ctx;
    r->image = image;
    r->job_ea = job_ea;
    r->seq = seq;
    put_state(r->entry, ctx);
    for (auto& slot : g_slots) {
        Recording* expected = nullptr;
        if (slot.compare_exchange_strong(expected, r, std::memory_order_acq_rel)) {
            t_recording = r;
            return;
        }
    }
    delete r;
}

void capture_end(spu_context* ctx, int, uint32_t)
{
    Recording* r = t_recording;
    if (!r || r->ctx != ctx) return;
    t_recording = nullptr;
    for (auto& slot : g_slots) {
        Recording* expected = r;
        slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    char name[64];
    snprintf(name, sizeof name, "/job_%d_%04u.bjc.gz", r->image, r->seq);
    std::string path = g_dir + name;
    gzFile f = gzopen(path.c_str(), "wb1");
    if (f) {
        std::vector<uint8_t> head;
        put(head, kMagic);
        put(head, kVersion);
        put(head, r->image);
        put(head, r->job_ea);
        put(head, r->seq);
        put(head, (uint32_t)SPU_LS_SIZE);
        gzwrite(f, head.data(), (unsigned)head.size());
        gzwrite(f, r->entry.data(), (unsigned)r->entry.size());
        gzwrite(f, &r->count, sizeof r->count);
        if (!r->records.empty()) gzwrite(f, r->records.data(), (unsigned)r->records.size());
        std::vector<uint8_t> tail;
        put_state(tail, ctx);
        put(tail, exit_of(ctx));
        gzwrite(f, tail.data(), (unsigned)tail.size());
        gzclose(f);
    }
    if (r->seq < 4 || r->seq + 1 == g_max)
        fprintf(stderr, "[job-corpus] wrote %s (%u transfers)\n", path.c_str(), r->count);
    delete r;
}

struct CorpusInit {
    CorpusInit()
    {
        const char* dir = getenv("BUZZ_JOB_CORPUS");
        if (!dir || !dir[0] || getenv("BUZZ_REPLAY_CORPUS")) return;
        g_dir = dir;
        if (const char* e = getenv("BUZZ_JOB_CORPUS_IMAGE")) g_image = atoi(e);
        if (const char* e = getenv("BUZZ_JOB_CORPUS_EVERY")) g_every = (uint32_t)atoi(e);
        if (const char* e = getenv("BUZZ_JOB_CORPUS_MAX")) g_max = (uint32_t)atoi(e);
        if (!g_every) g_every = 1;
        if (const char* e = getenv("BUZZ_JOB_CORPUS_PERIOD_MS")) g_period_ms = strtoull(e, nullptr, 10);
        uint64_t after = 0;
        if (const char* e = getenv("BUZZ_JOB_CORPUS_AFTER_MS")) after = strtoull(e, nullptr, 10);
        g_start_ms = now_ms() + after;
#ifdef _WIN32
        CreateDirectoryA(g_dir.c_str(), nullptr);
#endif
        g_spu_dma_observer = observe;
        g_spurs_job_capture_begin = capture_begin;
        g_spurs_job_capture_end = capture_end;
        fprintf(stderr, "[job-corpus] capturing image %d every %u max %u after %llu ms -> %s\n",
                g_image, g_every, g_max, (unsigned long long)after, g_dir.c_str());
    }
} g_corpus_init;

/* ---- replay ------------------------------------------------------------- */

struct Transfer {
    uint32_t kind, lsa, ea, size;
    size_t offset; /* into the owning byte vector */
};

struct Job {
    int image = 0;
    uint32_t job_ea = 0, seq = 0;
    std::vector<uint8_t> entry, exit_state, data;
    std::vector<Transfer> transfers;
    ExitState exit{};
};

bool read_exact(gzFile f, void* out, size_t size)
{
    return gzread(f, out, (unsigned)size) == (int)size;
}

bool load_job(const std::string& path, Job& job)
{
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t magic = 0, version = 0, ls_size = 0, count = 0;
    bool ok = read_exact(f, &magic, 4) && magic == kMagic && read_exact(f, &version, 4) &&
              version == kVersion && read_exact(f, &job.image, 4) &&
              read_exact(f, &job.job_ea, 4) && read_exact(f, &job.seq, 4) &&
              read_exact(f, &ls_size, 4) && ls_size == SPU_LS_SIZE;
    const size_t state = sizeof(((spu_context*)nullptr)->gpr) + SPU_LS_SIZE;
    if (ok) {
        job.entry.resize(state);
        ok = read_exact(f, job.entry.data(), state) && read_exact(f, &count, 4);
    }
    for (uint32_t i = 0; ok && i < count; i++) {
        Transfer t{};
        ok = read_exact(f, &t.kind, 4) && read_exact(f, &t.lsa, 4) && read_exact(f, &t.ea, 4) &&
             read_exact(f, &t.size, 4) && t.size <= 0x10000;
        if (!ok) break;
        t.offset = job.data.size();
        job.data.resize(job.data.size() + t.size);
        ok = read_exact(f, job.data.data() + t.offset, t.size);
        job.transfers.push_back(t);
    }
    if (ok) {
        job.exit_state.resize(state);
        ok = read_exact(f, job.exit_state.data(), state) && read_exact(f, &job.exit, sizeof job.exit);
    }
    gzclose(f);
    return ok;
}

const char* kind_name(uint32_t kind)
{
    static const char* names[] = {"?", "GET", "PUT", "GETLLAR", "PUTLLC", "PUTLLC-fail", "PUTLLUC"};
    return kind < 7 ? names[kind] : "?";
}

bool is_read(uint32_t kind) { return kind == 1 || kind == 3; }

std::vector<uint8_t> g_replay_data;
std::vector<Transfer> g_replay;
spu_context* g_replay_ctx = nullptr;
std::vector<uint32_t> g_steps; /* BUZZ_REPLAY_STEPS: the step order of a job */
bool g_trace_steps = false;

void replay_step(spu_context* ctx, uint32_t pc)
{
    if (ctx == g_replay_ctx) g_steps.push_back(pc);
}
double g_run_seconds[2];  /* culled (one transfer), drawn */
unsigned g_runs[2];

double seconds_now()
{
#ifdef _WIN32
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart / (double)f.QuadPart;
#else
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

void replay_observe(spu_context* ctx, uint32_t kind, uint32_t lsa, uint32_t ea, uint32_t size,
                    const void* data)
{
    if (ctx != g_replay_ctx) return;
    Transfer t{kind, lsa, ea, size, g_replay_data.size()};
    put_bytes(g_replay_data, data, size);
    g_replay.push_back(t);
}

/* Returns 0 exact, 1 output mismatch, 2 only input divergence (a byte read
 * twice changed under the job in the game: a concurrent writer). */
struct Seed {
    uint32_t ea;
    const uint8_t* bytes;
};

/* Guest bytes to restore before every run: the first time the job observed
 * a byte decides it. A run changes them (its atomic advances the ring pointer
 * it read), so they are re-applied for each repeat. */
std::vector<Seed> seeds_of(const Job& job)
{
    std::vector<Seed> seeds;
    for (size_t n = 0; n < job.transfers.size(); n++) {
        const Transfer& t = job.transfers[n];
        if (!is_read(t.kind)) continue;
        const uint8_t* bytes = job.data.data() + t.offset;
        for (uint32_t i = 0; i < t.size; i++) {
            const uint32_t ea = t.ea + i;
            bool seen = false;
            for (size_t m = 0; m < n && !seen; m++) {
                const Transfer& u = job.transfers[m];
                seen = u.kind != 5 && ea - u.ea < u.size;
            }
            if (!seen) seeds.push_back({ea, bytes + i});
        }
    }
    return seeds;
}

int replay_job(const std::string& path, const Job& job, const std::vector<Seed>& seeds,
               spu_context* ctx, bool report_input_diffs)
{
    for (const Seed& s : seeds) vm_base[s.ea] = *s.bytes;
    spu_lifted_entry_fn entry = spu_job_entry_for_image(job.image);
    if (!entry) {
        fprintf(stderr, "[replay] %s: image %d is not registered\n", path.c_str(), job.image);
        return 1;
    }
    spu_context_init(ctx, 0);
    ctx->image_id = job.image;
    memcpy(ctx->gpr, job.entry.data(), sizeof(ctx->gpr));
    memcpy(ctx->ls, job.entry.data() + sizeof(ctx->gpr), SPU_LS_SIZE);
    g_replay.clear();
    g_replay_data.clear();
    g_replay_ctx = ctx;
    g_steps.clear();
    g_spu_step_observer = g_trace_steps ? replay_step : nullptr;
    double start = seconds_now();
    spurs_job_run_prepared(entry, job.image, ctx);
    const int drawn = job.transfers.size() > 1;
    g_run_seconds[drawn] += seconds_now() - start;
    g_runs[drawn]++;
    g_replay_ctx = nullptr;
    g_spu_step_observer = nullptr;
    if (g_trace_steps) {
        fprintf(stderr, "[steps] %s %zu:", path.c_str(), g_steps.size());
        for (uint32_t pc : g_steps) fprintf(stderr, " %X", pc);
        fprintf(stderr, "\n");
        g_trace_steps = false; /* first run of the job only */
    }

    int verdict = 0;
    unsigned input_diffs = 0;
    size_t n = job.transfers.size() < g_replay.size() ? job.transfers.size() : g_replay.size();
    for (size_t i = 0; i < n; i++) {
        const Transfer& a = job.transfers[i];
        const Transfer& b = g_replay[i];
        bool same_shape = a.kind == b.kind && a.lsa == b.lsa && a.ea == b.ea && a.size == b.size;
        bool same_bytes = same_shape && !memcmp(job.data.data() + a.offset,
                                                g_replay_data.data() + b.offset, a.size);
        if (same_bytes) continue;
        if (same_shape && is_read(a.kind)) { input_diffs++; continue; }
        fprintf(stderr, "[replay] %s: transfer %zu differs: game %s lsa=%05X ea=%08X size=%u,"
                        " replay %s lsa=%05X ea=%08X size=%u\n", path.c_str(), i,
                kind_name(a.kind), a.lsa, a.ea, a.size, kind_name(b.kind), b.lsa, b.ea, b.size);
        if (same_shape) {
            const uint8_t* g = job.data.data() + a.offset;
            const uint8_t* r = g_replay_data.data() + b.offset;
            uint32_t first = 0;
            while (g[first] == r[first]) first++;
            fprintf(stderr, "[replay]   first differing byte +%u:", first);
            for (uint32_t k = first & ~15u; k < a.size && k < (first & ~15u) + 32; k++)
                fprintf(stderr, "%s%02X/%02X", k % 4 ? "" : " ", g[k], r[k]);
            fprintf(stderr, "\n");
        }
        return 1;
    }
    if (job.transfers.size() != g_replay.size()) {
        fprintf(stderr, "[replay] %s: %zu transfers in the game, %zu in replay\n", path.c_str(),
                job.transfers.size(), g_replay.size());
        return 1;
    }
    /* BUZZ_REPLAY_CONTRACT=1: a native stage may leave dead scratch (LS the
     * job never sends, registers it never reads) different; compare only what
     * leaves the job: transfers (above) and pc/status/stop/mailboxes (below). */
    static const bool contract = getenv("BUZZ_REPLAY_CONTRACT") != nullptr;
    if (!contract && memcmp(ctx->gpr, job.exit_state.data(), sizeof(ctx->gpr))) {
        fprintf(stderr, "[replay] %s: exit registers differ\n", path.c_str());
        verdict = 1;
    }
    const uint8_t* ls = job.exit_state.data() + sizeof(ctx->gpr);
    if (!contract && memcmp(ctx->ls, ls, SPU_LS_SIZE)) {
        uint32_t first = 0, count = 0;
        for (uint32_t i = SPU_LS_SIZE; i-- > 0;)
            if (ctx->ls[i] != ls[i]) { first = i; count++; }
        fprintf(stderr, "[replay] %s: exit LS differs in %u bytes, first at %05X\n",
                path.c_str(), count, first);
        verdict = 1;
    }
    ExitState e = exit_of(ctx);
    if (memcmp(&e, &job.exit, sizeof e)) {
        fprintf(stderr, "[replay] %s: exit pc/status/stop/mailbox differ (%05X/%X/%X vs %05X/%X/%X)\n",
                path.c_str(), e.pc, e.status, e.stop_code, job.exit.pc, job.exit.status,
                job.exit.stop_code);
        verdict = 1;
    }
    if (!verdict && input_diffs) {
        if (report_input_diffs)
            fprintf(stderr, "[replay] %s: %u reads differ from the captured input\n",
                    path.c_str(), input_diffs);
        return 2;
    }
    return verdict;
}

std::vector<std::string> corpus_files(const std::string& where)
{
    std::vector<std::string> files;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(where.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return {where};
    WIN32_FIND_DATAA found;
    HANDLE h = FindFirstFileA((where + "\\*.bjc.gz").c_str(), &found);
    if (h == INVALID_HANDLE_VALUE) return files;
    do files.push_back(where + "\\" + found.cFileName);
    while (FindNextFileA(h, &found));
    FindClose(h);
#else
    DIR* d = opendir(where.c_str());
    if (!d) return {where};
    while (dirent* e = readdir(d))
        if (strstr(e->d_name, ".bjc.gz")) files.push_back(where + "/" + e->d_name);
    closedir(d);
#endif
    return files;
}

} // namespace

extern "C" int buzz_job_corpus_replay(const char* where);
extern "C" void buzz_stagef_report(void);

/* BUZZ_REPLAY_CORPUS: called from main once guest memory exists. Returns the
 * process exit code: 1 on output/state mismatch; read-input divergence is
 * reported separately and is NOT a claim of fully deterministic replay. */
extern "C" int buzz_job_corpus_replay(const char* where)
{
#ifdef _WIN32
    auto* ctx = static_cast<spu_context*>(_aligned_malloc(sizeof(spu_context), 16));
#else
    auto* ctx = static_cast<spu_context*>(aligned_alloc(16, sizeof(spu_context)));
#endif
    g_spu_dma_observer = replay_observe;
    /* BUZZ_REPLAY_REPEAT=N: run every job N times (each run is compared) and
     * report the mean host time of the lifted job, culled vs drawn. */
    unsigned repeat = 1;
    if (const char* e = getenv("BUZZ_REPLAY_REPEAT")) repeat = (unsigned)atoi(e);
    if (!repeat) repeat = 1;
    unsigned exact = 0, divergent = 0, failed = 0;
    struct Timing { double seconds = 0; unsigned runs = 0, jobs = 0; } timing[3][2];
    size_t transfers = 0;
    for (const std::string& path : corpus_files(where)) {
        Job job;
        if (!load_job(path, job)) {
            fprintf(stderr, "[replay] %s: unreadable\n", path.c_str());
            failed++;
            continue;
        }
        transfers += job.transfers.size();
        g_trace_steps = getenv("BUZZ_REPLAY_STEPS") != nullptr;
        const std::vector<Seed> seeds = seeds_of(job);
        const int drawn = job.transfers.size() > 1;
        const double seconds_before = g_run_seconds[drawn];
        const unsigned runs_before = g_runs[drawn];
        const int r = buzz_replay_repeat_checked(repeat, [&](unsigned pass) {
            return replay_job(path, job, seeds, ctx, pass == 0);
        });
        Timing& t = timing[r][drawn];
        t.seconds += g_run_seconds[drawn] - seconds_before;
        t.runs += g_runs[drawn] - runs_before;
        ++t.jobs;
        (r == 0 ? exact : r == 2 ? divergent : failed)++;
    }
    g_spu_dma_observer = nullptr;
    buzz_stagef_report();
#ifdef BUZZ_NATIVE_IMAGE36
    buzz_image36_report();
    if (buzz_image36_mismatches()) ++failed;
#endif
    fprintf(stderr, "[replay] %u exact, %u with concurrent input changes, %u FAILED (%zu transfers)\n",
            exact, divergent, failed, transfers);
    for (int drawn = 0; drawn < 2; drawn++)
        if (g_runs[drawn])
            fprintf(stderr, "[replay] %s jobs: %u runs, %.2f us per job\n",
                    drawn ? "drawn" : "fully culled", g_runs[drawn],
                    g_run_seconds[drawn] * 1e6 / g_runs[drawn]);
    const char* cohort_names[] = {"exact-input", "FAILED", "input-divergent"};
    for (int cohort = 0; cohort < 3; ++cohort)
        for (int drawn = 0; drawn < 2; ++drawn) {
            const Timing& t = timing[cohort][drawn];
            if (t.runs)
                fprintf(stderr, "[replay] timing %s %s: %u jobs, %u runs, %.2f us per job\n",
                        cohort_names[cohort], drawn ? "drawn" : "fully culled",
                        t.jobs, t.runs, t.seconds * 1e6 / t.runs);
        }
#ifdef _WIN32
    _aligned_free(ctx);
#else
    free(ctx);
#endif
    return failed ? 1 : 0;
}
