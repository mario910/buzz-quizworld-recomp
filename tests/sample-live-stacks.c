/* Bounded read-only debugger sampling of this experiment. StackWalk64 uses
 * real x64 unwind metadata, not a heuristic search for pointers on the stack.
 * Sampling perturbs scheduling: never use this output as an FPS benchmark. */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
typedef struct {DWORD tid;HANDLE handle;ULONG64 cycles;} Thread;
int wmain(int argc,wchar_t**argv)
{
    if(argc!=3){fputs("usage: sample-live-stacks PID EXACT_EXE_PATH\n",stderr);return 1;}
    DWORD pid=wcstoul(argv[1],NULL,10);if(!pid)return 1;
    const wchar_t* basename=wcsrchr(argv[2],L'\\');
    if(!basename || _wcsicmp(basename+1,L"BuzzQuizWorldRecomp.exe")){fputs("Not experiment EXE\n",stderr);return 1;}
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!process)return 1;
    wchar_t image[32768];DWORD length=32768;
    if(!QueryFullProcessImageNameW(process,0,image,&length) || _wcsicmp(image,argv[2])){
        fputs("PID/path mismatch; no threads touched\n",stderr);CloseHandle(process);return 1;}
    HMODULE module;DWORD needed;MODULEINFO info;
    if(!EnumProcessModules(process,&module,sizeof module,&needed) || !GetModuleInformation(process,module,&info,sizeof info)){
        CloseHandle(process);return 1;}
    DWORD64 base=(DWORD64)info.lpBaseOfDll,limit=base+info.SizeOfImage;
    SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    if(!SymInitialize(process,".",TRUE)){CloseHandle(process);return 1;}
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snapshot==INVALID_HANDLE_VALUE){SymCleanup(process);CloseHandle(process);return 1;}
    Thread threads[128]={0};unsigned count=0;THREADENTRY32 entry={sizeof entry};
    for(BOOL next=Thread32First(snapshot,&entry);next;next=Thread32Next(snapshot,&entry))if(entry.th32OwnerProcessID==pid && count<128){
        HANDLE h=OpenThread(THREAD_QUERY_INFORMATION|THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);
        if(h){threads[count].tid=entry.th32ThreadID;threads[count].handle=h;QueryThreadCycleTime(h,&threads[count].cycles);++count;}}
    CloseHandle(snapshot);
    printf("# PID=%lu image_size=%lX threads=%u; diagnostic sampling, NOT FPS\n",pid,info.SizeOfImage,count);
    ULONGLONG start=GetTickCount64();unsigned rounds=0,failures=0;
    while(GetTickCount64()-start<8000){
        for(unsigned t=0;t<count;++t){
            Thread* thread=&threads[t];ULONG64 now=0;if(!QueryThreadCycleTime(thread->handle,&now))continue;
            ULONG64 delta=now-thread->cycles;thread->cycles=now;
            if(SuspendThread(thread->handle)==(DWORD)-1)continue;
            DWORD64 addresses[8]={0};unsigned depth=0;
            /* SEH finally guarantees resume even if DbgHelp raises. No writes
             * to the target context or memory are performed. */
            __try {
                CONTEXT ctx={0};ctx.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
                if(GetThreadContext(thread->handle,&ctx)){
                    addresses[depth++]=ctx.Rip;STACKFRAME64 frame={0};
                    frame.AddrPC.Offset=ctx.Rip;frame.AddrPC.Mode=AddrModeFlat;
                    frame.AddrStack.Offset=ctx.Rsp;frame.AddrStack.Mode=AddrModeFlat;
                    frame.AddrFrame.Offset=ctx.Rbp;frame.AddrFrame.Mode=AddrModeFlat;
                    for(unsigned steps=0;depth<8 && steps<16 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread->handle,&frame,&ctx,NULL,SymFunctionTableAccess64,SymGetModuleBase64,NULL);++steps){
                        if(!frame.AddrPC.Offset)break;
                        if(frame.AddrPC.Offset!=addresses[depth-1])addresses[depth++]=frame.AddrPC.Offset;
                    }
                }
            } __finally {if(ResumeThread(thread->handle)==(DWORD)-1)++failures;}
            if(depth){printf("S %lu %llu",thread->tid,(unsigned long long)delta);
                for(unsigned n=0;n<depth;++n)if(addresses[n]>=base && addresses[n]<limit)printf(" %llx",(unsigned long long)(addresses[n]-base));else printf(" -");
                putchar('\n');}
        }
        ++rounds;Sleep(50);
    }
    printf("# rounds=%u elapsed_ms=%llu resume_failures=%u\n",rounds,(unsigned long long)(GetTickCount64()-start),failures);
    for(unsigned t=0;t<count;++t)CloseHandle(threads[t].handle);
    SymCleanup(process);CloseHandle(process);return failures?1:0;
}
