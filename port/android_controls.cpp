#include "buzzer_input.h"
#include <SDL.h>
#include <jni.h>
#include <cstdlib>
#include <string>
#include <atomic>
#include <mutex>

namespace {
std::mutex surface_mutex;
std::atomic<bool> surface_ready{false};
void setting(JNIEnv* env, const char* name, jstring value) {
    const char* text = value ? env->GetStringUTFChars(value, nullptr) : nullptr;
    if (text && *text) setenv(name, text, 1); else unsetenv(name);
    if (text) env->ReleaseStringUTFChars(value, text);
}
}
extern "C" JNIEXPORT void JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeSurfaceReady(JNIEnv*, jclass, jboolean ready) {
    std::lock_guard lock(surface_mutex);
    surface_ready.store(ready, std::memory_order_release);
}
extern "C" int buzz_android_surface_ready() { return surface_ready.load(std::memory_order_acquire); }
extern "C" int buzz_android_surface_begin() {
    surface_mutex.lock();
    if (surface_ready.load(std::memory_order_acquire)) return 1;
    surface_mutex.unlock(); return 0;
}
extern "C" void buzz_android_surface_end() { surface_mutex.unlock(); }
extern "C" uint64_t rsx_vulkan_backend_presented_frames(void);
extern "C" JNIEXPORT jlong JNICALL Java_org_buzzrecomp_game_BuzzActivity_nativeFrames(JNIEnv*, jclass) {
    return jlong(rsx_vulkan_backend_presented_frames());
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
