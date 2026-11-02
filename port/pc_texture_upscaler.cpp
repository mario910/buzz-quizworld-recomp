#include "pc_texture_upscaler.h"
#include "ps3emu/host_controls.h"
#include <algorithm>
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
// Default largest upscale side. 4x of the menu's 130 eligible textures is
// ~2 GB of RGBA with mips at 4096 and ~445 MB at 2048: the 1024-pixel
// textures are a third of the count and three quarters of the memory, and
// already the sharpest. BUZZ_TEXTURE_UPSCALER_MAX_SIDE raises it (to the
// renderer's 8192 limit) for GPUs with memory to spare.
constexpr unsigned default_max_side=2048;
constexpr unsigned give_up_after=5;      // consecutive failed batches (no Vulkan GPU, missing model, ...)
// One upscaler run per batch: loading the model costs more than a small
// texture (2.1 s for one 128x128 file, 3.5 s for ten). Bounded by count and
// by input pixels, so a batch of large textures still finishes in a minute
// or two and new screens get their turn.
constexpr size_t batch_files=32;
constexpr unsigned long long batch_pixels=4ull<<20;
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

// Exit code, or -1 when the process cannot start. Killed after timeout_ms.
int run(const fs::path& exe,const Args& args,unsigned timeout_ms) {
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
    if(WaitForSingleObject(pi.hProcess,timeout_ms)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,INFINITE);}
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
    int status=0;pid_t done=0;
    for(unsigned waited=0;(done=waitpid(pid,&status,WNOHANG))==0;waited+=100) {
        if(waited>=timeout_ms){kill(pid,SIGKILL);done=waitpid(pid,&status,0);break;}
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

void worker(fs::path exe,fs::path textures,fs::path::string_type model,unsigned scale,unsigned max_output_side) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
    // Dot-named folders: the renderer's scan of replace/ skips them, so it
    // never sees a staged input or a half-written output.
    const fs::path dump=textures/"dump",automatic=textures/"replace"/"auto",
                   batch_in=automatic/".batch-in",batch_out=automatic/".batch-out";
    const fs::path::string_type scale_arg=arg(std::to_string(scale).c_str());
    std::set<std::string> done; // upscaled, skipped or failed this run
    std::error_code ec;
    fs::create_directories(automatic,ec);
    unsigned made=0,failures=0;
    for(;;)try {
        std::vector<std::pair<unsigned long long,std::string>> todo; // input pixels, name
        for(fs::directory_iterator it(dump,ec),end;!ec&&it!=end;it.increment(ec)) {
            const std::string name=it->path().filename().string();
            unsigned w=0,h=0;
            if(done.count(name)||!texture_name(name,w,h)||!it->is_regular_file(ec))continue;
            // The renderer writes dumps in place; let one settle before reading it.
            const auto written=it->last_write_time(ec);
            if(ec||fs::file_time_type::clock::now()-written<std::chrono::seconds(2)){ec.clear();continue;}
            // Already upscaled (a previous run), or too large to be worth it.
            if(fs::exists(automatic/name,ec)||w*scale>max_output_side||h*scale>max_output_side){done.insert(name);continue;}
            todo.push_back({(unsigned long long)w*h,name});
        }
        ec.clear();
        if(!todo.empty()) {
            const std::set<std::string> manual=hand_made(textures/"replace",automatic);
            // Small first: the most textures on screen soonest.
            std::sort(todo.begin(),todo.end());
            std::vector<std::string> batch;unsigned long long pixels=0;
            fs::remove_all(batch_in,ec);fs::remove_all(batch_out,ec);
            fs::create_directories(batch_in,ec);fs::create_directories(batch_out,ec);
            for(const auto& [size,name]:todo) {
                if(batch.size()==batch_files||(!batch.empty()&&pixels+size>batch_pixels))break;
                done.insert(name);
                if(manual.count(name))continue;
                // A hard link when the folders share a volume, else a copy.
                fs::create_hard_link(dump/name,batch_in/name,ec);
                if(ec){ec.clear();fs::copy_file(dump/name,batch_in/name,ec);}
                if(ec){ec.clear();continue;}
                batch.push_back(name);pixels+=size;
            }
            if(!batch.empty()) {
                // Two minutes, plus one per megapixel of input: generous for
                // a slow GPU, still bounded if the upscaler hangs.
                const unsigned timeout=unsigned(120000+pixels*60000/(1u<<20));
                const int code=run(exe,{arg("-i"),batch_in.native(),arg("-o"),batch_out.native(),arg("-n"),model,arg("-s"),scale_arg,
                                        arg("-m"),(exe.parent_path()/"models").native(),arg("-f"),arg("png"),arg("-j"),arg("1:2:2")},timeout);
                unsigned moved=0;
                for(const std::string& name:batch) {
                    // Renamed into place whole: the renderer never sees a partial file.
                    if(fs::file_size(batch_out/name,ec)>0&&!ec){fs::rename(batch_out/name,automatic/name,ec);if(!ec)++moved;}
                    if(ec){ec.clear();fprintf(stderr,"[upscaler] %s failed (exit %d)\n",name.c_str(),code);}
                }
                fs::remove_all(batch_in,ec);fs::remove_all(batch_out,ec);ec.clear();
                made+=moved;
                if(moved)ps3_host_texture_pack_changed();
                fprintf(stderr,"[upscaler] %u of %zu upscaled (%u this run)\n",moved,batch.size(),made);
                if(moved)failures=0;
                else if(code<0||++failures>=give_up_after) {
                    fprintf(stderr,"[upscaler] stopping after repeated failures; %u textures upscaled this run\n",made);
                    return;
                }
                continue; // more may be waiting: no pause between batches
            }
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
    unsigned max_side=default_max_side;
    if(const char* v=getenv("BUZZ_TEXTURE_UPSCALER_MAX_SIDE");v&&*v)max_side=unsigned(strtoul(v,nullptr,10));
    if(max_side<64||max_side>8192){fprintf(stderr,"[upscaler] BUZZ_TEXTURE_UPSCALER_MAX_SIDE must be 64-8192; automatic upscaling is off\n");return false;}
    std::error_code ec;
    if(!fs::is_regular_file(exe,ec)) {
        fprintf(stderr,"[upscaler] %s not found; automatic upscaling is off\n",utf8(exe).c_str());
        return false;
    }
    fprintf(stderr,"[upscaler] %s, %ux up to %u pixels; upscales go to %s\n",utf8(exe).c_str(),scale,max_side,utf8(textures/"replace"/"auto").c_str());
    std::thread(worker,exe,textures,model,scale,max_side).detach();
    return true;
}
}
