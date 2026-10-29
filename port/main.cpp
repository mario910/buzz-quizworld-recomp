/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"
#include "ps3emu/ps3_ftrace.h"
#ifdef BUZZ_SPU_AVX512
#include "cpu_isa.h"
#endif
#if defined(BUZZ_SPU_NATIVE_RTZ) && defined(_WIN32)
#include <intrin.h>
#endif

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void ps3_sampler_start(void);

#ifdef _WIN32
extern "C" uint32_t ppu_prof_resolve_host(void*);
extern "C" uint32_t g_last_hle_nid;
extern "C" const char* g_last_hle_name;
static void buzz_crash_module(void* address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(address), &module)) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, sizeof(path));
        fprintf(stderr, " module=%s+0x%llX", path,
                (unsigned long long)((uintptr_t)address - (uintptr_t)module));
    }
}
static LONG WINAPI buzz_crash_report(EXCEPTION_POINTERS* exception)
{
    const auto* record = exception->ExceptionRecord;
    fprintf(stderr, "\n[buzz-crash] code=%08lX host=%p guest-function=%08X\n",
            record->ExceptionCode, record->ExceptionAddress,
            ppu_prof_resolve_host(record->ExceptionAddress));
    fprintf(stderr, "[buzz-crash] last HLE=%08X (%s)\n", g_last_hle_nid,
            g_last_hle_name ? g_last_hle_name : "unknown");
    fprintf(stderr, "[buzz-crash] fault");
    buzz_crash_module(record->ExceptionAddress);
    fprintf(stderr, "\n");
    if (record->NumberParameters >= 2)
        fprintf(stderr, "[buzz-crash] access=%llu address=%llX\n",
                (unsigned long long)record->ExceptionInformation[0],
                (unsigned long long)record->ExceptionInformation[1]);
    void* frames[24];
    const auto count = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    for (unsigned i = 0; i < count; ++i) {
        fprintf(stderr, "[buzz-crash] #%u host=%p guest-function=%08X",
                i, frames[i], ppu_prof_resolve_host(frames[i]));
        buzz_crash_module(frames[i]);
        fprintf(stderr, "\n");
    }
    fflush(stdout);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#ifndef _WIN32
#include <sys/mman.h>   /* mmap: the guest arena, see alloc_guest_vm */
#endif

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);
void     buzz_register_overrides(void);
void     buzz_edge_stats(unsigned*, unsigned long long*);
int      buzz_job_corpus_replay(const char* where);
void     buzz_spurs_debug_snapshot(void);
void     buzz_buzzer_stats(void);
int      ppu_prof_snapshot(int, unsigned*, unsigned*, const char**);
void     ppu_report_guest_lrs(void);
const char* ps3_hle_inflight_name(unsigned);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
void     cellGcm_fifo_kick_wait(unsigned ms); /* interruptible render-worker idle */
unsigned cellGcm_flip_request_count(void);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(PS3RECOMP_RSX_VULKAN)
extern "C" int  rsx_vulkan_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_vulkan_backend_present(void);
extern "C" int  rsx_vulkan_backend_pump_messages(void);
#  define rsx_backend_init    rsx_vulkan_backend_init
#  define rsx_backend_present rsx_vulkan_backend_present
#  define rsx_backend_pump    rsx_vulkan_backend_pump_messages
#  define RSX_BACKEND_NAME    "Vulkan"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

static void present_guest_frame(void)
{
    rsx_backend_present();
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

static DWORD WINAPI frame_clock(LPVOID)
{
    SetThreadDescription(GetCurrentThread(), L"RSX worker (FIFO + D3D12)");
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "Buzz! Quiz World";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");

    unsigned  last_flip = 0;
    ULONGLONG next_tick = GetTickCount64();
    ULONGLONG last_report = next_tick;
    const char* trace_trigger=getenv("BUZZ_TRACE_TRIGGER");
    ULONGLONG next_trace_check=0;
    unsigned idle_ms = 4;
    if (const char* e = getenv("BUZZ_GCM_IDLE_MS")) {
        char* end = nullptr;
        unsigned long value = strtoul(e, &end, 10);
        if (end != e && !*end && value <= 16) idle_ms = (unsigned)value;
    }
    fprintf(stderr, "[rsx] worker idle timeout=%u ms (FIFO kick enabled)\n", idle_ms);

    for (;;) {
        cellGcm_fifo_kick_wait(idle_ms);
        ULONGLONG now = GetTickCount64();
        if(trace_trigger && now>=next_trace_check &&
           __atomic_load_n(&g_ps3_ft.on,__ATOMIC_ACQUIRE)==4){
            next_trace_check=now+250;
            FILE* trigger=fopen(trace_trigger,"rb");
            if(trigger){fclose(trigger);
                if(ps3_ft_activate_pending())fprintf(stderr,"[ftrace] first capture activation requested by marker\n");}
        }
        if (now - last_report >= 5000) {
            unsigned jobs = 0;
            unsigned long long bytes = 0;
            buzz_edge_stats(&jobs, &bytes);
            buzz_buzzer_stats();
            static bool captured = false;
            if (!captured) { captured = true; buzz_spurs_debug_snapshot(); }
            static unsigned reported_flip=~0u,stalled_reports=0;
            const unsigned flip=cellGcm_flip_request_count();
            stalled_reports=flip==reported_flip?stalled_reports+1:0;
            reported_flip=flip;
            if(getenv("BUZZ_BOOT_TRACE") && stalled_reports==2){
                buzz_spurs_debug_snapshot();
                ppu_report_guest_lrs();
                for(unsigned t=0;t<64;++t)if(const char* name=ps3_hle_inflight_name(t))
                    fprintf(stderr,"[boot-hle] tid=%u in=%s\n",t,name);
                for(int slot=0;slot<64;++slot){
                    unsigned tid=0,pc=0;const char* name=nullptr;
                    if(ppu_prof_snapshot(slot,&tid,&pc,&name))
                        fprintf(stderr,"[boot-thread] tid=%u pc=%08X name=%s\n",tid,pc,name?name:"");
                }
            }
            fprintf(stderr, "[buzz-progress] decompressed-jobs=%u bytes=%llu flips=%u presented=%u\n",
                    jobs, bytes, cellGcm_flip_request_count(), ppu_boot_frames_presented());
            /* Redirected UCRT streams otherwise issue thousands of tiny pipe
             * writes per second. Keep logs bounded-latency without paying
             * that cost for every diagnostic character. */
            fflush(stdout);
            fflush(stderr);
            last_report = now;
        }

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += 16;             /* ~60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            if (rsx_backend_pump() != 0) {
                /* The guest entry runs on another thread and does not return
                 * when its host window disappears. Merely disabling RSX left
                 * the entire PPU/SPURS workload alive invisibly. Match the
                 * runner's existing sys_process_exit lifetime: closing this
                 * experimental host window ends this process as well. */
                fprintf(stderr,"[host] Game window closed; exiting experimental process.\n");
                exit(0);
            }
            /* Present on a guest flip. A present on a fixed clock can catch the
             * drain mid-frame and flash a partial one. Before the first flip
             * present freely, so the window is not blank during boot. */
            unsigned fc = cellGcm_flip_request_count();
            if (fc != last_flip) {
                present_guest_frame();
                last_flip = fc;
            } else if (fc == 0) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is mapped up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It stops below the 0xE0000000 mark, so ppu_vm_size arms the
     * loader's bounds check for what is left.
     *
     * mmap, not calloc: vm_commit() hands vm_base + addr to mprotect, which
     * takes page-aligned addresses only. glibc serves an allocation this big
     * from its own mmap chunk and returns a pointer 16 bytes past the page
     * start, so every commit failed with EINVAL and sys_memory_allocate
     * answered ENOMEM to every call. mmap's base is page-aligned and zeroed. */
    void* p = mmap(NULL, 0xE0000000u, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = (p == MAP_FAILED) ? nullptr : (uint8_t*)p;
    ppu_vm_size = 0xE0000000u;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

int main(int argc, char** argv)
{
    /* Configure streams before their first use. Buffers must outlive all
     * guest/worker threads, including normal exit's final flush. A forced
     * kill may lose the tail; live crash debugging can opt out explicitly. */
    static char stdout_buffer[64 * 1024];
    static char stderr_buffer[64 * 1024];
    const char* unbuffered = getenv("BUZZ_UNBUFFERED_LOG");
    const bool live_logs = unbuffered && strcmp(unbuffered, "1") == 0;
    setvbuf(stdout, live_logs ? nullptr : stdout_buffer,
            live_logs ? _IONBF : _IOFBF, sizeof stdout_buffer);
    setvbuf(stderr, live_logs ? nullptr : stderr_buffer,
            live_logs ? _IONBF : _IOFBF, sizeof stderr_buffer);
#if defined(BUZZ_SPU_AVX2) || defined(BUZZ_SPU_SSE41)
    // This TU remains baseline x86: reject incompatible CPUs before entering
    // lifted SPU code. The test includes OS AVX register-state support.
#ifndef _WIN32
    __builtin_cpu_init();
#endif
#ifdef BUZZ_SPU_AVX2
#ifdef _WIN32
    const bool spu_cpu_ok = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE);
#else
    const bool spu_cpu_ok = __builtin_cpu_supports("avx2");
#endif
    if (!spu_cpu_ok) {
        fprintf(stderr, "This build needs AVX2. Rebuild with -SpuIsa sse2.\n");
        return 2;
    }
    fprintf(stderr, "[boot] lifted SPU helpers: AVX2 (CPU/OS support checked)\n");
#ifdef BUZZ_SPU_AVX512
    if(!buzz_avx512_host_supported()){
        fprintf(stderr,"This experimental build needs AVX512F/BW/VL and OS ZMM/opmask state. Rebuild with -SpuIsa avx2.\n");
        return 2;
    }
    fprintf(stderr,"[boot] experimental lifted SPU codegen: AVX512F/BW/VL (CPU + XCR0 checked)\n");
#endif
#ifdef BUZZ_SPU_NATIVE_RTZ
#ifdef _WIN32
    int feature_regs[4]; __cpuid(feature_regs,1);
    const bool fma_ok = (feature_regs[2] & (1 << 12)) != 0;
#else
    const bool fma_ok = __builtin_cpu_supports("fma");
#endif
    if (!fma_ok) { fprintf(stderr,"Native SPU arithmetic needs FMA. Rebuild without -NativeSpuFloat and -NativeImage40.\n"); return 2; }
    fprintf(stderr,"[boot] audited SPU arithmetic: FMA checked, exact fallback, image-scoped MXCSR\n");
#endif
#else
#ifdef _WIN32
    const bool spu_cpu_ok = IsProcessorFeaturePresent(PF_SSE4_1_INSTRUCTIONS_AVAILABLE);
#else
    const bool spu_cpu_ok = __builtin_cpu_supports("sse4.1");
#endif
    if (!spu_cpu_ok) {
        fprintf(stderr, "This build needs SSE4.1. Rebuild with -SpuIsa sse2.\n");
        return 2;
    }
    fprintf(stderr, "[boot] lifted SPU helpers: SSE4.1\n");
#endif
#endif
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(buzz_crash_report);
#endif
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
#endif

    printf("=== Buzz! Quiz World experimental PS3Recomp runner ===\n");
    fprintf(stderr, "[boot] diagnostic logs: %s\n",
            live_logs ? "unbuffered (debug override)" : "64 KiB buffers, 5 s flush");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    /* Offline job corpus replay (port/job_corpus.cpp): no game boot. */
    if (const char* corpus = getenv("BUZZ_REPLAY_CORPUS"))
        return buzz_job_corpus_replay(corpus);

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    lv2_init_syscalls();     /* the lv2 syscall table */
    buzz_register_overrides();

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);
    fflush(stdout);
    fflush(stderr);

    ps3_sampler_start(); /* opt-in PS3_SAMPLE; includes lifted SPU host map */
    int rc = ppu_run(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
