#include <adrenotools/driver.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <string>
#include <unistd.h>

namespace {
std::once_flag initialized;
void* loader = nullptr;
using GetProc = void* (*)(void*, const char*);
GetProc get_proc = nullptr;
void status(const char* value) {
    __android_log_print(ANDROID_LOG_INFO, "BuzzGPU", "%s", value);
    if (const char* path = getenv("BUZZ_ANDROID_GPU_STATUS"))
        if (FILE* file = fopen(path, "w")) { fputs(value, file); fclose(file); }
}
void open_loader() {
    const char* dir = getenv("BUZZ_ANDROID_GPU_DIR");
    const char* name = getenv("BUZZ_ANDROID_GPU_LIBRARY");
    const char* hooks = getenv("BUZZ_ANDROID_GPU_HOOKS");
    if (dir && name && hooks) {
        const std::string path = std::string(dir) + name;
        if (access(path.c_str(), R_OK) == 0)
            loader = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL,
                ADRENOTOOLS_DRIVER_CUSTOM, nullptr, hooks, dir, name, nullptr, nullptr);
        if (loader) status("Wybrano sterownik z paczki ZIP");
        else status("Błąd paczki — używam sterownika systemowego");
    }
    if (!loader) {
        loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!dir) status("Sterownik systemowy");
    }
    if (loader) get_proc = reinterpret_cast<GetProc>(dlsym(loader, "vkGetInstanceProcAddr"));
    if (!get_proc) status("Nie można uruchomić Vulkan");
}
}
// SDL and RSX both use this entry point; never mix Vulkan loader instances.
extern "C" __attribute__((visibility("default"))) void* vkGetInstanceProcAddr(void* instance, const char* name) {
    std::call_once(initialized, open_loader);
    return get_proc ? get_proc(instance, name) : nullptr;
}
extern "C" void buzz_android_gpu_ready() {
    if (const char* marker = getenv("BUZZ_ANDROID_GPU_PENDING")) unlink(marker);
}
extern "C" void buzz_android_gpu_device(const char* name) {
    __android_log_print(ANDROID_LOG_INFO, "BuzzGPU", "GPU: %s", name);
    if (const char* path = getenv("BUZZ_ANDROID_GPU_STATUS"))
        if (FILE* file = fopen(path, "a")) { fprintf(file, "\nGPU: %s", name); fclose(file); }
}
