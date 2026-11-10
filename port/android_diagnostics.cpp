// Opt-in bounded debug runs only. No handler/timer/thread on ordinary play.
// Samples instruction pointers without unwinding or allocating in a signal
// handler; normal Android perf access is restricted on some retail devices.
#include <jni.h>
#include <android/log.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string>
#include "rsx_shader_spirv.h"

namespace {
struct Sample { uint64_t pc; uint32_t tid, reserved; };
static_assert(sizeof(Sample)==16);
volatile sig_atomic_t sample_pipe=-1;
void sample_pc(int, siginfo_t*, void* opaque) {
    const int saved=errno;
    const auto* context=static_cast<const ucontext_t*>(opaque);
    const Sample sample{context->uc_mcontext.pc, uint32_t(syscall(SYS_gettid)),0};
    // One atomic non-blocking pipe write; full buffers drop samples.
    if(sample_pipe>=0)(void)write(int(sample_pipe),&sample,sizeof sample);
    errno=saved;
}
void snapshot_maps(const std::string& path) {
    int in=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);
    int out=open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(in>=0&&out>=0) {
        char buffer[4096];ssize_t n;size_t total=0;
        while(total<1024*1024&&(n=read(in,buffer,sizeof buffer))>0) {
            (void)write(out,buffer,size_t(n));total+=size_t(n);
        }
    }
    if(in>=0)close(in);if(out>=0)close(out);
}
}
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeDiagnosticSkipIntro(
    JNIEnv*,jclass) {
    // Use the desktop diagnostic's startup-only policy. Media calls are
    // restored at the first menu; ordinary Android launches are unchanged.
    setenv("BUZZ_NO_FMV","1",1);
}
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeDiagnosticAudioMode(
    JNIEnv*,jclass,jint mode) {
    if(mode>=0 && mode<=2) {
        const char value[2]={char('0'+mode),0};
        setenv("BUZZ_AUDIO_COMPRESSOR",value,1);
    }
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeDiagnosticSamples(
    JNIEnv* env,jclass,jstring file) {
    if(sample_pipe>=0||!file)return JNI_FALSE;
    struct sigaction previous{};
    struct itimerval current{};
    if(sigaction(SIGPROF,nullptr,&previous)||getitimer(ITIMER_PROF,&current)||
       (previous.sa_handler!=SIG_DFL&&previous.sa_handler!=SIG_IGN)||
       current.it_value.tv_sec||current.it_value.tv_usec)return JNI_FALSE;
    const char* chars=env->GetStringUTFChars(file,nullptr);
    if(!chars)return JNI_FALSE;
    const std::string path(chars);
    env->ReleaseStringUTFChars(file,chars);
    int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return JNI_FALSE;
    int pipefd[2];
    if(pipe2(pipefd,O_NONBLOCK|O_CLOEXEC)){close(fd);return JNI_FALSE;}
    Dl_info image{};
    dladdr(reinterpret_cast<void*>(&sample_pc),&image);
    const char magic[8]={'B','Z','W','P','C','0','0','1'};
    const uint64_t base=reinterpret_cast<uintptr_t>(image.dli_fbase);
    if(write(fd,magic,8)!=8||write(fd,&base,8)!=8){close(fd);close(pipefd[0]);close(pipefd[1]);return JNI_FALSE;}
    snapshot_maps(path+".maps");
    struct sigaction action{};
    action.sa_sigaction=sample_pc;action.sa_flags=SA_SIGINFO|SA_RESTART;sigemptyset(&action.sa_mask);
    sample_pipe=pipefd[1];
    if(sigaction(SIGPROF,&action,nullptr)){sample_pipe=-1;close(fd);close(pipefd[0]);close(pipefd[1]);return JNI_FALSE;}
    // Polling is wall-clock bounded too, independent of the game's frames.
    try {
        std::thread([fd,readfd=pipefd[0],path] {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            auto maps_due=std::chrono::steady_clock::now();
            auto stats_due=maps_due+std::chrono::seconds(5);
            while(std::chrono::steady_clock::now()<end) {
                pollfd event{readfd,POLLIN,0};
                if(poll(&event,1,100)>0) {
                    Sample buffer[256];ssize_t n=read(readfd,buffer,sizeof buffer);
                    if(n>0)(void)write(fd,buffer,size_t(n));
                }
                if(std::chrono::steady_clock::now()>=maps_due) {
                    snapshot_maps(path+".maps");
                    maps_due=std::chrono::steady_clock::now()+std::chrono::seconds(1);
                }
                if(std::chrono::steady_clock::now()>=stats_due) {
                    RsxSpirvCacheStats stats{};
                    rsx_hlsl_to_spirv_cache_stats(&stats);
                    std::fprintf(stderr,"[spirv-cache] diagnostic translations=%llu disk_hits=%llu memory_hits=%llu\n",
                        (unsigned long long)stats.translations,(unsigned long long)stats.disk_hits,
                        (unsigned long long)stats.memory_hits);
                    stats_due=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                }
            }
            // Descriptors stay valid until process exit if a pending SIGPROF
            // races this diagnostic timeout. No recycled-fd writes.
        }).detach();
    } catch(...) {
        sample_pipe=-1;sigaction(SIGPROF,&previous,nullptr);
        close(fd);close(pipefd[0]);close(pipefd[1]);return JNI_FALSE;
    }
    struct itimerval timer{};
    timer.it_value.tv_usec=10000;timer.it_interval.tv_usec=10000;
    if(setitimer(ITIMER_PROF,&timer,nullptr))return JNI_FALSE;
    setenv("AUDIO_RATE","1",1);
    __android_log_print(ANDROID_LOG_INFO,"Buzz","Diagnostic PC sampling armed (100 CPU-Hz)");
    return JNI_TRUE;
}
