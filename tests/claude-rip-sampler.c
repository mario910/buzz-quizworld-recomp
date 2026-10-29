/* Leaf-only diagnostic sampler: suspends each thread of the experiment briefly,
 * records RIP as an image RVA (or '-') plus the thread's cycle delta. Never
 * writes target memory or context. Perturbs scheduling: NOT a benchmark.
 *   claude-rip-sampler.exe <pid> <expected exe path> <milliseconds> */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

typedef struct { DWORD tid; HANDLE handle; ULONG64 cycles; } Thread;

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) return 1;
    DWORD pid = wcstoul(argv[1], NULL, 10);
    DWORD duration = wcstoul(argv[3], NULL, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 1;
    wchar_t image[MAX_PATH]; DWORD length = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, image, &length) || _wcsicmp(image, argv[2])) return 2;
    HMODULE module; DWORD needed;
    if (!EnumProcessModules(process, &module, sizeof module, &needed)) return 3;
    MODULEINFO info;
    if (!GetModuleInformation(process, module, &info, sizeof info)) return 3;
    const DWORD64 base = (DWORD64)info.lpBaseOfDll, limit = base + info.SizeOfImage;
    /* optional argv[4]: .text size (hex, from the .map) -- callers found by
     * the stack heuristic must point into code, not into .rdata pointers */
    const DWORD64 code_limit = argc > 4 ? base + 0x1000 + wcstoull(argv[4], NULL, 16) : limit;
    HMODULE mods[512]; DWORD modbytes = 0; unsigned nmods = 0;
    DWORD64 mlo[512], mhi[512]; char mname[512][64];
    if (EnumProcessModules(process, mods, sizeof mods, &modbytes))
        for (unsigned i = 0; i < modbytes / sizeof(HMODULE) && i < 512; ++i) {
            MODULEINFO mi;
            if (!GetModuleInformation(process, mods[i], &mi, sizeof mi)) continue;
            mlo[nmods] = (DWORD64)mi.lpBaseOfDll; mhi[nmods] = mlo[nmods] + mi.SizeOfImage;
            if (!GetModuleBaseNameA(process, mods[i], mname[nmods], 64)) strcpy(mname[nmods], "?");
            ++nmods;
        }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    Thread threads[128] = {0}; unsigned count = 0; THREADENTRY32 entry = {sizeof entry};
    for (BOOL next = Thread32First(snapshot, &entry); next; next = Thread32Next(snapshot, &entry))
        if (entry.th32OwnerProcessID == pid && count < 128) {
            HANDLE h = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME,
                                  FALSE, entry.th32ThreadID);
            if (h) { threads[count].tid = entry.th32ThreadID; threads[count].handle = h;
                     QueryThreadCycleTime(h, &threads[count].cycles); ++count; }
        }
    CloseHandle(snapshot);
    timeBeginPeriod(1);
    printf("# PID=%lu threads=%u leaf RIP samples, NOT FPS\n", pid, count);
    ULONGLONG start = GetTickCount64(); unsigned rounds = 0;
    while (GetTickCount64() - start < duration) {
        for (unsigned t = 0; t < count; ++t) {
            Thread* th = &threads[t]; ULONG64 now = 0;
            if (!QueryThreadCycleTime(th->handle, &now)) continue;
            ULONG64 delta = now - th->cycles; th->cycles = now;
            if (delta < 200000) continue;            /* idle since last round */
            if (SuspendThread(th->handle) == (DWORD)-1) continue;
            CONTEXT ctx = {0}; ctx.ContextFlags = CONTEXT_CONTROL;
            BOOL ok = GetThreadContext(th->handle, &ctx);
            ResumeThread(th->handle);
            if (!ok) continue;
            if (ctx.Rip >= base && ctx.Rip < limit) printf("S %lu %llx\n", th->tid, (unsigned long long)(ctx.Rip - base));
            else {
                const char* m = "unknown"; DWORD64 moff = 0;
                for (unsigned i = 0; i < nmods; ++i)
                    if (ctx.Rip >= mlo[i] && ctx.Rip < mhi[i]) { m = mname[i]; moff = ctx.Rip - mlo[i]; break; }
                /* Caller: a leaf CRT copy/fill has its return address at [RSP];
                 * other DLL frames (ucrtbase, ...) are found heuristically as
                 * the first of 32 stack slots pointing into our image. */
                DWORD64 slots[32] = {0}; SIZE_T got = 0; DWORD64 ret = 0;
                if (_strnicmp(m, "ntdll", 5) &&
                    ReadProcessMemory(process, (LPCVOID)ctx.Rsp, slots, sizeof slots, &got))
                    for (unsigned k = 0; k < got / sizeof slots[0]; ++k)
                        if (slots[k] >= base + 0x1000 && slots[k] < code_limit) { ret = slots[k]; break; }
                /* -module+offset[@caller]: the offset resolves against the DLL's exports */
                if (ret)
                    printf("S %lu -%s+%llx@%llx\n", th->tid, m, (unsigned long long)moff,
                           (unsigned long long)(ret - base));
                else printf("S %lu -%s+%llx\n", th->tid, m, (unsigned long long)moff);
            }
        }
        ++rounds; Sleep(1);
    }
    timeEndPeriod(1);
    printf("# rounds=%u\n", rounds);
    return 0;
}
