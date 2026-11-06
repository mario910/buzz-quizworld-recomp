// EXE-owned defaults, installed before constructors cache getenv() options.
#include <cstdio>
#include <cstdlib>

#ifndef BUZZ_RUNTIME_DEFAULTS
#define BUZZ_RUNTIME_DEFAULTS 0
#endif

namespace {
struct Default { const char* name; const char* value; };
constexpr Default defaults[] = {
#define X(name, value) {#name, value},
#if defined(BUZZ_STANDALONE) && !BUZZ_RUNTIME_DEFAULTS
#define BUZZ_COMMON_DEFAULTS_ONLY
#endif
#include "runtime_defaults.inc"
#if defined(BUZZ_SPURS_IDLE_SPIN_US) && BUZZ_SPURS_IDLE_SPIN_US >= 0
#define BUZZ_SPURS_STRINGIFY_IMPL(value) #value
#define BUZZ_SPURS_STRINGIFY(value) BUZZ_SPURS_STRINGIFY_IMPL(value)
// Install before the SDK caches its options. Explicit environment overrides
// still win independently for workers and the command-chain waiter.
X(SPURS_POOL_SPIN_US, BUZZ_SPURS_STRINGIFY(BUZZ_SPURS_IDLE_SPIN_US))
X(SPURS_JTS_SPIN_US, BUZZ_SPURS_STRINGIFY(BUZZ_SPURS_IDLE_SPIN_US))
#undef BUZZ_SPURS_STRINGIFY
#undef BUZZ_SPURS_STRINGIFY_IMPL
#endif
#ifdef BUZZ_STANDALONE
X(PS3_TITLE, "Buzz! Quiz World")
X(PAD_NO_KEYBOARD, "1")
X(BUZZ_OFFLINE, "1")
X(BUZZ_HIDE_PSN_STORE, "1")
X(RSX_SHADER_CACHE_DIR, "shader-cache")
#ifdef BUZZ_ANDROID_IMAGE39_NEON
X(SPURS_NATIVE_IMAGE39_FP, "1")
#endif
#ifdef BUZZ_GEOMETRY_ARM_NEON
X(BUZZ_STAGEF, "1")
X(BUZZ_STAGEF_KERNELS, "6")
#endif
#ifdef BUZZ_GEOMETRY_LS_DRIVER
X(BUZZ_GEOMETRY_JOB, "1")
X(BUZZ_GEOMETRY_JOB_LAYOUT, "1")
X(BUZZ_GEOMETRY_JOB_PUBLISH, "1")
X(BUZZ_GEOMETRY_JOB_VERTEX, "1")
#endif
#if BUZZ_DISC_COMPLETION && !BUZZ_RUNTIME_DEFAULTS
X(BUZZ_NATIVE_COMPLETION, "2")
#endif
#endif
#undef X
};

int install_defaults() {
#if BUZZ_RUNTIME_DEFAULTS || defined(BUZZ_STANDALONE)
    for (const auto& option : defaults) {
        if (std::getenv(option.name)) continue;
#ifdef _WIN32
        if (_putenv_s(option.name, option.value)) return 1;
#else
        if (setenv(option.name, option.value, 0)) return 1;
#endif
    }
#endif
    return 0;
}

#ifdef _MSC_VER
// MSVC exe_common.inl initializes the narrow environment in .CRT$XIAA.
// XIB follows that initialization and precedes ALL .CRT$XC* C++ constructors
// (including clang __attribute__((constructor)) in the linked runtime).
#pragma section(".CRT$XIB", long, read)
}
extern "C" {
__declspec(allocate(".CRT$XIB")) int (__cdecl* buzz_runtime_defaults_initializer)() = install_defaults;
}
// /OPT:REF must retain this otherwise unreferenced initializer.
#pragma comment(linker, "/include:buzz_runtime_defaults_initializer")
namespace {
#else
__attribute__((constructor(101))) void initialize() {
    if (install_defaults()) std::abort();
}
#endif
}

extern "C" void buzz_print_runtime_defaults() {
    std::printf("profile=%s\n", BUZZ_RUNTIME_DEFAULTS ? "eu100-vulkan-native-v1" : "none");
    for (const auto& option : defaults) {
        const char* value = std::getenv(option.name);
        std::printf("%s=%s\n", option.name, value ? value : "<unset>");
    }
}
