#include "buzzer_input.h"
#include <SDL.h>
#include <jni.h>
#include <cstdlib>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <sched.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <android/log.h>
#include "android_pause_state.h"
#include "android_cpu_policy.h"
#include "ps3emu/host_controls.h"
extern "C" uint64_t rsx_vulkan_backend_presented_frames(void);
extern "C" int cellSysutilBroadcastEvent(unsigned status,unsigned param);

namespace {
std::mutex surface_mutex;
std::atomic<bool> surface_ready{false};
std::atomic<unsigned> diagnostic_cpu_mask{0};
std::mutex pause_mutex;
buzz_android::PauseState pause_state;
bool pause_published=false;
void publish_pause(bool before) {
    const bool paused=pause_state.paused();
    ps3_host_set_paused(paused);
    // Match the existing PC-menu policy, but only on combined-state changes.
    // Do not send a CLOSE just because the surface resumed beneath an open
    // menu, or send callbacks before a guest has produced its first frame.
    if(pause_published && before!=paused && rsx_vulkan_backend_presented_frames()!=0) {
        const unsigned menu_event=paused?0x0131u:0x0132u;
        const unsigned draw_event=paused?0x0121u:0x0122u;
        if(paused) {cellSysutilBroadcastEvent(menu_event,0);cellSysutilBroadcastEvent(draw_event,0);}
        else {cellSysutilBroadcastEvent(draw_event,0);cellSysutilBroadcastEvent(menu_event,0);}
    }
    if(!pause_published||before!=paused)
        __android_log_print(ANDROID_LOG_INFO,"Buzz","guest_pause=%d visible=%d menu=%d",paused,pause_state.visible,pause_state.menu);
    pause_published=true;
}
// ---- default CPU placement (android_cpu_policy.h) --------------------------
// The mask is applied to the SDL/game thread before the runtime creates its
// workers, which inherit it. Android moves the whole process between cpusets
// when it goes to the background and comes back, and that resets every
// thread's affinity; so on each return the mask is put back on the threads
// that did not exist when it was first applied (Java, UI and binder threads
// that did are never touched).
std::mutex cpu_policy_mutex;
cpu_set_t cpu_policy_set;
bool cpu_policy_active=false;
std::vector<pid_t> cpu_policy_foreign;      // sorted: threads that predate the policy
char cpu_policy_summary[160]="CPU policy: not evaluated";   // for the game's own log

std::vector<pid_t> process_threads() {
    std::vector<pid_t> result;
    if(DIR* directory=opendir("/proc/self/task")) {
        while(const dirent* entry=readdir(directory)) {
            char* end=nullptr;
            const long tid=std::strtol(entry->d_name,&end,10);
            if(end!=entry->d_name && !*end && tid>0)result.push_back(pid_t(tid));
        }
        closedir(directory);
    }
    std::sort(result.begin(),result.end());
    return result;
}
void reapply_cpu_policy() {
    std::lock_guard lock(cpu_policy_mutex);
    if(!cpu_policy_active)return;
    unsigned applied=0;
    for(const pid_t tid:process_threads()) {
        if(std::binary_search(cpu_policy_foreign.begin(),cpu_policy_foreign.end(),tid))continue;
        if(sched_setaffinity(tid,sizeof cpu_policy_set,&cpu_policy_set)==0)++applied;   // a thread may have exited
    }
    __android_log_print(ANDROID_LOG_INFO,"Buzz","CPU policy: placement restored on %u native threads",applied);
}
void visibility(bool visible) {
    bool resumed=false;
    {
        std::lock_guard lock(pause_mutex);
        const bool before=pause_state.paused();
        resumed=visible && !pause_state.visible && pause_published;
        pause_state.visible=visible;
        publish_pause(before);
    }
    if(resumed) {
        // The cpuset move happens around the Activity's resume, not at a point
        // this callback can observe: put the placement back shortly after, and
        // once more in case the move came late.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            reapply_cpu_policy();
            std::this_thread::sleep_for(std::chrono::milliseconds(1600));
            reapply_cpu_policy();
        }).detach();
    }
}
void setting(JNIEnv* env, const char* name, jstring value) {
    const char* text = value ? env->GetStringUTFChars(value, nullptr) : nullptr;
    if (text && *text) setenv(name, text, 1); else unsetenv(name);
    if (text) env->ReleaseStringUTFChars(value, text);
}
}
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeSurfaceReady(JNIEnv*, jclass, jboolean ready) {
    // Freeze new guest/audio work before waiting for an in-flight present.
    // Keep the existing surface-lifetime exclusion intact.
    if(!ready)visibility(false);
    {
        std::lock_guard lock(surface_mutex);
        surface_ready.store(ready, std::memory_order_release);
    }
    if(ready)visibility(true);
}
extern "C" void buzz_android_menu_pause(int paused) {
    std::lock_guard lock(pause_mutex);
    const bool before=pause_state.paused();
    pause_state.menu=paused!=0;
    publish_pause(before);
}
extern "C" int buzz_android_surface_ready() { return surface_ready.load(std::memory_order_acquire); }
extern "C" int buzz_android_surface_begin() {
    surface_mutex.lock();
    if (surface_ready.load(std::memory_order_acquire)) return 1;
    surface_mutex.unlock(); return 0;
}
extern "C" void buzz_android_surface_end() { surface_mutex.unlock(); }
extern "C" JNIEXPORT jlong JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeFrames(JNIEnv*, jclass) {
    return jlong(rsx_vulkan_backend_presented_frames());
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeDiagnosticCpuMask(
    JNIEnv*, jclass, jint mask) {
    // Debug Activity path only. onCreate can still belong to the background
    // cpuset; apply on SDL's foreground thread, before game workers exist.
    if (mask <= 0 || mask > 255) return JNI_FALSE;
    diagnostic_cpu_mask.store(unsigned(mask),std::memory_order_relaxed);
    return JNI_TRUE;
}
extern "C" int buzz_android_apply_diagnostic_affinity() {
    const unsigned mask=diagnostic_cpu_mask.load(std::memory_order_relaxed);
    if(!mask)return 1;
    // SDL may enter main immediately on surfaceChanged, before focus and
    // Android's foreground cpuset transition. Do not measure that startup
    // race as an unavailable-CPU configuration or pin the UI itself.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!surface_ready.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(!surface_ready.load(std::memory_order_acquire))return 0;
    // Only SDL/game descendants inherit the constraint. The independently
    // armed Java watchdog and Android UI retain their original scheduling.
    cpu_set_t allowed{}, selected{};
    if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return 0;
    CPU_ZERO(&selected);
    for (unsigned cpu=0; cpu<8; ++cpu) if (unsigned(mask)&(1u<<cpu)) {
        CPU_SET(cpu,&selected);
    }
    // An inherited thread mask is not the set of CPUs this app may request.
    // Let the kernel enforce online/cpuset restrictions, then reject partial
    // application instead of silently measuring a different set of cores.
    cpu_set_t actual{};
    bool matched=false;
    for(unsigned attempt=0;attempt<100;++attempt) {
        CPU_ZERO(&actual);
        if(sched_setaffinity(0,sizeof selected,&selected)==0 &&
           sched_getaffinity(0,sizeof actual,&actual)==0 && CPU_EQUAL(&selected,&actual)) {
            matched=true;break;
        }
        // Android can still apply its top-app task profile just after focus,
        // overwriting the first affinity request. Retry only before workers.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!matched) {
        sched_setaffinity(0, sizeof allowed, &allowed);
        unsigned observed=0;
        for(unsigned cpu=0;cpu<8;++cpu)if(CPU_ISSET(cpu,&actual))observed|=1u<<cpu;
        __android_log_print(ANDROID_LOG_ERROR,"Buzz","Diagnostic CPU mask 0x%X unavailable on foreground SDL thread (observed 0x%X)",mask,observed);
        return 0;
    }
    __android_log_print(ANDROID_LOG_INFO,"Buzz","Diagnostic native worker affinity: 0x%X",unsigned(mask));
    return 1;
}
// Default placement for ordinary launches. BUZZ_ANDROID_CPU_POLICY=0 keeps
// Android's own scheduling and the default worker count; a diagnostic CPU mask
// replaces it. Never fatal: a device this cannot be applied on plays as before.
extern "C" void buzz_android_apply_cpu_policy() {
    if(const unsigned diagnostic=diagnostic_cpu_mask.load(std::memory_order_relaxed)) {
        std::snprintf(cpu_policy_summary,sizeof cpu_policy_summary,"CPU policy: replaced by diagnostic mask 0x%X",diagnostic);
        return;
    }
    const char* mode=std::getenv("BUZZ_ANDROID_CPU_POLICY");
    if(mode && mode[0]=='0') {
        __android_log_print(ANDROID_LOG_INFO,"Buzz","CPU policy: off (BUZZ_ANDROID_CPU_POLICY=0)");
        std::snprintf(cpu_policy_summary,sizeof cpu_policy_summary,"CPU policy: off (BUZZ_ANDROID_CPU_POLICY=0)");
        return;
    }
    unsigned capacity[64]={};
    size_t count=0;
    for(;count<64;++count) {
        char path[64];
        std::snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu%zu/cpu_capacity",count);
        FILE* file=std::fopen(path,"r");
        if(!file)break;
        unsigned value=0;
        if(std::fscanf(file,"%u",&value)!=1)value=0;
        std::fclose(file);
        capacity[count]=value;
    }
    const auto policy=buzz_android::choose_cpu_policy(capacity,count);
    if(!policy.mask) {
        __android_log_print(ANDROID_LOG_INFO,"Buzz","CPU policy: every core kept (%zu CPUs, no separate performance set)",count);
        std::snprintf(cpu_policy_summary,sizeof cpu_policy_summary,"CPU policy: every core kept (%zu CPUs, no separate performance set)",count);
        return;
    }
    // As for the diagnostic mask: SDL can enter main before the Activity has
    // focus and Android's foreground cpuset; a mask set earlier is overwritten.
    const auto ready_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!surface_ready.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<ready_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cpu_set_t selected;
    CPU_ZERO(&selected);
    for(size_t cpu=0;cpu<count;++cpu)if(policy.mask&(uint64_t(1)<<cpu))CPU_SET(cpu,&selected);
    // Accept a subset: a paused or offline performance core is simply not
    // used until it returns. Require the result to hold for 200 ms, since the
    // foreground task profile can still land just after focus.
    unsigned observed=0,stable=0;
    cpu_set_t admitted;
    CPU_ZERO(&admitted);
    for(unsigned attempt=0;attempt<40 && stable<4;++attempt) {
        cpu_set_t actual;
        CPU_ZERO(&actual);
        bool good=sched_setaffinity(0,sizeof selected,&selected)==0 &&
                  sched_getaffinity(0,sizeof actual,&actual)==0 && CPU_COUNT(&actual)>0;
        observed=0;
        for(size_t cpu=0;good && cpu<count;++cpu) if(CPU_ISSET(cpu,&actual)) {
            if(!CPU_ISSET(cpu,&selected))good=false;
            if(cpu<32)observed|=1u<<cpu;
        }
        stable=good?stable+1:0;
        if(good)admitted=actual;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if(stable<4) {
        __android_log_print(ANDROID_LOG_WARN,"Buzz","CPU policy: mask 0x%llX not accepted (observed 0x%X); every core kept",
            static_cast<unsigned long long>(policy.mask),observed);
        std::snprintf(cpu_policy_summary,sizeof cpu_policy_summary,"CPU policy: mask 0x%llX not accepted (observed 0x%X); every core kept",
            static_cast<unsigned long long>(policy.mask),observed);
        return;
    }
    {
        std::lock_guard lock(cpu_policy_mutex);
        // Keep what the foreground cpuset admitted, not the whole request: the
        // Thor withholds CPU 5 from the app, and forcing it back on after a
        // resume made the game reach 80 C in 40 s instead of running 80+ s.
        cpu_policy_set=admitted;
        cpu_policy_foreign=process_threads();
        const pid_t self=gettid();      // this thread carries the mask to the workers it creates
        cpu_policy_foreign.erase(std::remove(cpu_policy_foreign.begin(),cpu_policy_foreign.end(),self),cpu_policy_foreign.end());
        cpu_policy_active=true;
    }
    // The measured configuration; an explicit worker choice is not replaced.
    if(policy.workers && !std::getenv("BUZZ_ANDROID_JOB_WORKERS_EXPLICIT"))
        setenv("SPURS_JOB_LIST_WORKERS",std::to_string(policy.workers).c_str(),1);
    const char* workers=std::getenv("SPURS_JOB_LIST_WORKERS");
    __android_log_print(ANDROID_LOG_INFO,"Buzz","CPU policy: native threads on performance cores 0x%llX (available now 0x%X), %s list workers",
        static_cast<unsigned long long>(policy.mask),observed,workers?workers:"default");
    std::snprintf(cpu_policy_summary,sizeof cpu_policy_summary,"CPU policy: native threads on performance cores 0x%llX (available now 0x%X), %s list workers",
        static_cast<unsigned long long>(policy.mask),observed,workers?workers:"default");
}
// main() prints this once its log files are open; logcat has it earlier.
extern "C" const char* buzz_android_cpu_policy_summary() { return cpu_policy_summary; }
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeConfigure(
    JNIEnv* env, jclass, jstring hooks, jstring dir, jstring library, jstring status, jstring pending) {
    setting(env, "BUZZ_ANDROID_GPU_HOOKS", hooks);
    setting(env, "BUZZ_ANDROID_GPU_DIR", dir);
    setting(env, "BUZZ_ANDROID_GPU_LIBRARY", library);
    setting(env, "BUZZ_ANDROID_GPU_STATUS", status);
    setting(env, "BUZZ_ANDROID_GPU_PENDING", pending);
    setenv("SDL_VULKAN_LIBRARY", "libbuzz_vulkan.so", 1);
}
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeButtons(
    JNIEnv*, jclass, jint player, jint held, jboolean cancel) {
    buzz_input_touch(unsigned(player), uint8_t(held), cancel);
}
extern "C" JNIEXPORT jint JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativePlayers(JNIEnv*, jclass) {
    return jint(buzz_input_players());
}
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeMenu(JNIEnv*, jclass) {
    // The overlay owns guest pause state. Change it on the SDL event thread.
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = SDLK_ESCAPE;
    event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    SDL_PushEvent(&event);
    event.type = SDL_KEYUP;
    event.key.state = SDL_RELEASED;
    SDL_PushEvent(&event);
}
