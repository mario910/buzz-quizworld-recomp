#include "pc_texture_upscaler.h"
#include "ps3emu/host_controls.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <atomic>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

namespace fs=std::filesystem;
namespace buzz_pc {
namespace {
constexpr unsigned max_output_side=4096; // keeps a pack's VRAM sane; the renderer accepts up to 8192
constexpr unsigned give_up_after=5;      // consecutive failures (no Vulkan GPU, missing model, ...)
using Args=std::vector<fs::path::string_type>;
fs::path::string_type arg(const char* s){return fs::path(s).native();}
std::string utf8(const fs::path& p){const auto s=p.u8string();return std::string(s.begin(),s.end());}

// "<16 hex>_<w>x<h>_<2 hex>.png", as the renderer names its dumps.
bool texture_name(const std::string& name,unsigned& w,unsigned& h) {
    unsigned long long hash=0;unsigned fmt=0;int used=0;
    return name.size()>=26&&sscanf(name.c_str(),"%16llx_%ux%u_%2x.png%n",&hash,&w,&h,&fmt,&used)==4&&used==int(name.size());
}

#ifndef _WIN32
// The upscaler running now, killed when the game exits normally. Linux also
// kills it when the game dies any other way (PR_SET_PDEATHSIG in the child).
std::atomic<pid_t> running_child{0};
void kill_running_child() {
    const pid_t pid=running_child.load();
    if(pid>0)kill(pid,SIGKILL);
}
#endif

// Exit code, or -1 when the process cannot start.
int run(const fs::path& exe,const Args& args) {
#ifdef _WIN32
    // Killed with the game: the job closes when the process exits.
    static HANDLE job=[]{
        HANDLE j=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(j&&!SetInformationJobObject(j,JobObjectExtendedLimitInformation,&info,sizeof info)){CloseHandle(j);j=nullptr;}
        return j;
    }();
    std::wstring line=L"\""+exe.native()+L"\"";
    for(const auto& a:args)line+=L" \""+a+L"\"";
    STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
    const std::wstring cwd=exe.parent_path().native();
    if(!CreateProcessW(exe.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS|CREATE_SUSPENDED,
                       nullptr,cwd.empty()?nullptr:cwd.c_str(),&si,&pi))return -1;
    if(job)AssignProcessToJobObject(job,pi.hProcess);
    ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    if(WaitForSingleObject(pi.hProcess,5*60*1000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,INFINITE);}
    DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
    return int(code);
#else
    static const bool registered=std::atexit(kill_running_child)==0;(void)registered;
    // Everything the child needs is built before fork: after it, only
    // async-signal-safe calls until exec.
    std::vector<char*> argv{const_cast<char*>(exe.c_str())};
    for(const auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const std::string cwd=exe.parent_path().native();
#ifdef __linux__
    const pid_t parent=getpid();
#endif
    const pid_t pid=fork();
    if(pid<0)return -1;
    if(pid==0) {
#ifdef __linux__
        // Sent when the forking thread ends, and the worker thread only ends
        // with the game (it returns with no child running). Covers crashes
        // and SIGKILL, which skip atexit. Re-check the parent in case the
        // game died before prctl took effect.
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)!=0||getppid()!=parent)_exit(127);
#endif
        if(!cwd.empty()&&chdir(cwd.c_str())!=0)_exit(127);
        if(nice(5)==-1){}
        execv(exe.c_str(),argv.data());
        _exit(127);
    }
    running_child.store(pid);
    // The same five-minute bound as on Windows.
    int status=0;pid_t done=0;
    for(int waited=0;(done=waitpid(pid,&status,WNOHANG))==0;waited+=100) {
        if(waited>=5*60*1000){kill(pid,SIGKILL);done=waitpid(pid,&status,0);break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    running_child.store(0);
    if(done<0)return -1;
    if(WIFEXITED(status)&&WEXITSTATUS(status)==127)return -1; // could not exec
    return WIFEXITED(status)?WEXITSTATUS(status):1;
#endif
}

// Names of hand-made replacements, which the upscaler leaves alone.
std::set<std::string> hand_made(const fs::path& replace,const fs::path& automatic) {
    std::set<std::string> names;std::error_code ec;
    for(fs::recursive_directory_iterator it(replace,fs::directory_options::skip_permission_denied,ec),end;!ec&&it!=end;it.increment(ec)) {
        if(it->path()==automatic){it.disable_recursion_pending();continue;}
        if(it->is_regular_file(ec))names.insert(it->path().filename().string());
    }
    return names;
}

void worker(fs::path exe,fs::path textures,fs::path::string_type model,unsigned scale) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
    const fs::path dump=textures/"dump",automatic=textures/"replace"/"auto",work=automatic/".work.png";
    const fs::path::string_type scale_arg=arg(std::to_string(scale).c_str());
    std::set<std::string> done; // upscaled, skipped or failed this run
    std::error_code ec;
    fs::create_directories(automatic,ec);
    unsigned made=0,failures=0;
    for(;;)try {
        std::vector<std::string> todo;
        for(fs::directory_iterator it(dump,ec),end;!ec&&it!=end;it.increment(ec)) {
            const std::string name=it->path().filename().string();
            unsigned w=0,h=0;
            if(done.count(name)||!texture_name(name,w,h)||!it->is_regular_file(ec))continue;
            // The renderer writes dumps in place; let one settle before reading it.
            const auto written=it->last_write_time(ec);
            if(ec||fs::file_time_type::clock::now()-written<std::chrono::seconds(2)){ec.clear();continue;}
            // Already upscaled (a previous run), or too large to be worth it.
            if(fs::exists(automatic/name,ec)||w*scale>max_output_side||h*scale>max_output_side){done.insert(name);continue;}
            todo.push_back(name);
        }
        ec.clear();
        if(!todo.empty()) {
            const std::set<std::string> manual=hand_made(textures/"replace",automatic);
            for(const std::string& name:todo) {
                done.insert(name);
                if(manual.count(name))continue;
                fs::remove(work,ec);
                const int code=run(exe,{arg("-i"),(dump/name).native(),arg("-o"),work.native(),arg("-n"),model,arg("-s"),scale_arg,
                                        arg("-m"),(exe.parent_path()/"models").native(),arg("-f"),arg("png"),arg("-j"),arg("1:1:1")});
                if(code==0&&fs::file_size(work,ec)>0&&!ec) {
                    // Renamed into place whole: the renderer never sees a partial file.
                    fs::rename(work,automatic/name,ec);
                    if(!ec){failures=0;++made;ps3_host_texture_pack_changed();continue;}
                }
                ec.clear();fs::remove(work,ec);
                fprintf(stderr,"[upscaler] %s failed (exit %d)\n",name.c_str(),code);
                if(code<0||++failures>=give_up_after) {
                    fprintf(stderr,"[upscaler] stopping after repeated failures; %u textures upscaled this run\n",made);
                    return;
                }
            }
            if(made)fprintf(stderr,"[upscaler] %u textures upscaled this run\n",made);
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }catch(const std::exception& e) {
        // A file name the narrow conversion rejects, a folder vanishing mid-scan.
        fprintf(stderr,"[upscaler] %s\n",e.what());
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}
}

bool start_texture_upscaler(const fs::path& textures) {
    fs::path exe=texture_upscaler_executable(textures);
    fs::path::string_type model=fs::path("realesrgan-x4plus").native();
    unsigned scale=4;
#ifdef _WIN32
    if(const wchar_t* v=_wgetenv(L"BUZZ_TEXTURE_UPSCALER");v&&*v)exe=fs::absolute(fs::path(v));
    if(const wchar_t* v=_wgetenv(L"BUZZ_TEXTURE_UPSCALER_MODEL");v&&*v)model=v;
#else
    if(const char* v=getenv("BUZZ_TEXTURE_UPSCALER");v&&*v)exe=fs::absolute(fs::u8path(v));
    if(const char* v=getenv("BUZZ_TEXTURE_UPSCALER_MODEL");v&&*v)model=v;
#endif
    // The model decides the factor: realesrgan-x4plus(-anime) are 4x only.
    if(const char* v=getenv("BUZZ_TEXTURE_UPSCALER_SCALE");v&&*v)scale=unsigned(strtoul(v,nullptr,10));
    if(scale<2||scale>4){fprintf(stderr,"[upscaler] BUZZ_TEXTURE_UPSCALER_SCALE must be 2-4; automatic upscaling is off\n");return false;}
    std::error_code ec;
    if(!fs::is_regular_file(exe,ec)) {
        fprintf(stderr,"[upscaler] %s not found; automatic upscaling is off\n",utf8(exe).c_str());
        return false;
    }
    fprintf(stderr,"[upscaler] %s, %ux; upscales go to %s\n",utf8(exe).c_str(),scale,utf8(textures/"replace"/"auto").c_str());
    std::thread(worker,exe,textures,model,scale).detach();
    return true;
}
}
