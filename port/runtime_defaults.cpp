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
#include "runtime_defaults.inc"
#undef X
};

int install_defaults() {
#if BUZZ_RUNTIME_DEFAULTS
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
