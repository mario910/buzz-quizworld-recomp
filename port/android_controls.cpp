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
#include <android/log.h>
#include "android_pause_state.h"
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
void visibility(bool visible) {
    std::lock_guard lock(pause_mutex);
    const bool before=pause_state.paused();
    pause_state.visible=visible;
    publish_pause(before);
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
