#pragma once
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <cstddef>
// Preserve the Windows fixtures' guard-page checks on POSIX hosts.
using DWORD = unsigned long;
constexpr int MEM_RESERVE=0,MEM_COMMIT=0,MEM_RELEASE=0;
constexpr int PAGE_READWRITE=PROT_READ|PROT_WRITE,PAGE_NOACCESS=PROT_NONE;
struct SYSTEM_INFO { unsigned dwPageSize; };
inline void GetSystemInfo(SYSTEM_INFO* p){p->dwPageSize=(unsigned)sysconf(_SC_PAGESIZE);}
inline size_t& geometry_test_allocation_size(){static size_t n;return n;}
inline void* VirtualAlloc(void*,size_t n,int,int){void* p=mmap(nullptr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);geometry_test_allocation_size()=n;return p==MAP_FAILED?nullptr:p;}
inline bool VirtualProtect(void* p,size_t n,int prot,DWORD*){return mprotect(p,n,prot)==0;}
inline bool VirtualFree(void* p,size_t,int){return munmap(p,geometry_test_allocation_size())==0;}
#endif
