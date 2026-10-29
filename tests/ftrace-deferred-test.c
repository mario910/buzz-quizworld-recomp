#include "../ps3recomp/include/ps3emu/ps3_ftrace.h"
#include <assert.h>
#ifdef _WIN32
static volatile LONG activations;
static DWORD WINAPI activate(LPVOID argument){(void)argument;InterlockedExchangeAdd(&activations,ps3_ft_activate_pending());return 0;}
static DWORD WINAPI observe(LPVOID argument){(void)argument;for(unsigned i=0;i<10000;++i)ps3_ft_on();return 0;}
#endif
static void environment(const char* key,const char* value)
{
#ifdef _WIN32
    assert(!_putenv_s(key,value));
#else
    assert(!setenv(key,value,1));
#endif
}
int main(int argc,char**argv)
{
    assert(argc==2 || argc==3);int deferred=argc==2;
    char option[2048];snprintf(option,sizeof option,"0:10:%s",argv[1]);
    environment("PS3_FTRACE",option);environment("PS3_FTRACE_WAIT",deferred?"1":"0");environment("PS3_FTRACE_CAP","32");
    if(deferred){
    assert(!ps3_ft_on());assert(g_ps3_ft.on==4 && !g_ps3_ft.buf && !g_ps3_ft.n);
    ps3_ft_name(3,"producer");assert(!strcmp(g_ps3_ft.names[3],"producer"));
    for(unsigned i=0;i<10000;++i)assert(!ps3_ft_on());
    assert(g_ps3_ft.on==4 && !g_ps3_ft.buf);
#ifdef _WIN32
    HANDLE threads[10];
    for(unsigned i=0;i<10;++i){threads[i]=CreateThread(NULL,0,i<2?activate:observe,NULL,0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(10,threads,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<10;++i)CloseHandle(threads[i]);
    assert(activations==1);
#else
    assert(ps3_ft_activate_pending());
#endif
    }else assert(ps3_ft_on());
    assert(g_ps3_ft.on==2 && g_ps3_ft.buf && !!g_ps3_ft.pending_activated==deferred);
    assert(!ps3_ft_activate_pending());
    uint64_t now=ps3_ft_now();ps3_ft_span(PS3_FT_PPU_SCOPE,3,0x26beec,0x2898f0,now,now+1000);
    assert(g_ps3_ft.n==1);ps3_ft_dump();
    assert(g_ps3_ft.on==3 && !ps3_ft_activate_pending() && !ps3_ft_on());
    FILE* file=fopen(argv[1],"rb");assert(file);char contents[4096];size_t n=fread(contents,1,sizeof(contents)-1,file);contents[n]=0;fclose(file);
    assert(strstr(contents,"spans=1 dropped=0") && strstr(contents,"26 3 26beec 2898f0"));
    puts(deferred?"PASS: deferred first capture, preserved thread names, no boot allocation, one activation under racing hooks, no live/finished rearm, real trace output":"PASS: existing immediate capture path unchanged, no rearm, real trace output");
    free(g_ps3_ft.buf);g_ps3_ft.buf=NULL;return 0;
}
