#include "core.h"
#include <SDL3/SDL.h>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace buzz_launcher {
bool executable_running(const fs::path& exe) {
    const auto target=fs::weakly_canonical(exe);
#ifdef _WIN32
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check running game processes");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(Process32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szExeFile,exe.filename().c_str()))continue;
        auto p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!p)continue;
        std::array<wchar_t,32768> name{};DWORD count=DWORD(name.size());
        if(QueryFullProcessImageNameW(p,0,name.data(),&count)) {std::error_code ec;auto image=fs::weakly_canonical(fs::path(name.data()),ec);if(!ec&&_wcsicmp(image.c_str(),target.c_str())==0)found=true;}
        CloseHandle(p);if(found)break;
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return found;
#else
    for(auto& entry:fs::directory_iterator("/proc")) {
        auto id=entry.path().filename().string();if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos)continue;
        std::error_code ec;auto image=fs::read_symlink(entry.path()/"exe",ec);
        if(!ec&&image==target)return true;
    }
    return false;
#endif
}
Environment current_environment() {
    Environment result;auto env=SDL_CreateEnvironment(true);
    if(!env)throw std::runtime_error(SDL_GetError());
    auto variables=SDL_GetEnvironmentVariables(env);
    if(variables) {
        for(char** p=variables;*p;++p) {
            std::string entry=*p;auto equal=entry.find('=');
            if(equal!=std::string::npos&&equal)result[entry.substr(0,equal)]=entry.substr(equal+1);
        }
        SDL_free(variables);
    }
    SDL_DestroyEnvironment(env);return result;
}
Child::~Child() {
    if(process_)SDL_DestroyProcess(static_cast<SDL_Process*>(process_));
    if(out_)SDL_CloseIO(static_cast<SDL_IOStream*>(out_));
    if(err_)SDL_CloseIO(static_cast<SDL_IOStream*>(err_));
}
void Child::start(const LaunchPlan& p) {
    if(process_)throw std::runtime_error("A game process is already tracked by this launcher");
    auto environment=SDL_CreateEnvironment(false);auto props=SDL_CreateProperties();
    auto fail=[&] {std::string error=SDL_GetError();if(props)SDL_DestroyProperties(props);if(environment)SDL_DestroyEnvironment(environment);throw std::runtime_error(error);};
    if(!environment||!props)fail();
    for(auto& [k,v]:p.environment)if(!SDL_SetEnvironmentVariable(environment,k.c_str(),v.c_str(),true))fail();
    auto exe=utf8(p.executable),elf=utf8(p.elf),cwd=utf8(p.cwd);
    const char* args[]={exe.c_str(),elf.c_str(),nullptr};
    auto out=SDL_IOFromFile(utf8(p.stdout_path).c_str(),"wb"),err=SDL_IOFromFile(utf8(p.stderr_path).c_str(),"wb");
    if(!out||!err) { if(out)SDL_CloseIO(out);if(err)SDL_CloseIO(err);fail(); }
    out_=out;err_=err;
    if(!SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,const_cast<char**>(args)) ||
       !SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,environment) ||
       !SDL_SetStringProperty(props,SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,cwd.c_str()) ||
       !SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_REDIRECT) ||
       !SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_POINTER,out) ||
       !SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,SDL_PROCESS_STDIO_REDIRECT) ||
       !SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_POINTER,err))fail();
    // SDL uses CreateProcess/posix_spawn directly: arguments are not shell text.
    // Do not detach: keep status reporting, but never kill a child on UI exit.
    process_=SDL_CreateProcessWithProperties(props);
    if(!process_)fail();
    SDL_DestroyProperties(props);SDL_DestroyEnvironment(environment);
}
bool Child::running(int* exit_code) {
    if(!process_)return false;
    return !SDL_WaitProcess(static_cast<SDL_Process*>(process_),false,exit_code);
}
void Child::stop_owned() {
    if(process_&&running()) {
        SDL_KillProcess(static_cast<SDL_Process*>(process_),true);
        SDL_WaitProcess(static_cast<SDL_Process*>(process_),true,nullptr);
    }
}
}
