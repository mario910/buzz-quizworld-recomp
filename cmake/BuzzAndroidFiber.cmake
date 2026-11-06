# Bionic provides the signal context structure. The game also needs the
# cooperative context switching functions used by the SDK's fiber scheduler.
enable_language(ASM)
include(FetchContent)
FetchContent_Declare(buzz_ucontext
    URL https://codeload.github.com/kaniini/libucontext/tar.gz/49e671dd52ff6791295d8161ad3b6da7dc5f6f9d
    URL_HASH SHA256=c84f57298ed4bce278c31ed8e5c0b3da6f9bd8bc45c9dfb08656fe797bdc2c47)
FetchContent_MakeAvailable(buzz_ucontext)
set(ucontext_arm "${buzz_ucontext_SOURCE_DIR}/arch/aarch64")
set(ucontext_common "${buzz_ucontext_SOURCE_DIR}/arch/common")
add_library(buzz_android_fiber STATIC
    "${ucontext_arm}/getcontext.S" "${ucontext_arm}/makecontext.c"
    "${ucontext_arm}/setcontext.S" "${ucontext_arm}/swapcontext.S" "${ucontext_arm}/trampoline.c")
target_include_directories(buzz_android_fiber
    PUBLIC "${buzz_ucontext_SOURCE_DIR}/include" "${ucontext_common}/include"
    PRIVATE "${ucontext_arm}" "${ucontext_common}")
# Clang ignores the trampoline's GCC optimize attribute.
target_compile_options(buzz_android_fiber PRIVATE "$<$<COMPILE_LANGUAGE:C>:-fomit-frame-pointer>")
target_link_libraries(ps3recomp_runtime PRIVATE buzz_android_fiber)
set_property(SOURCE "${PS3RECOMP_DIR}/libs/spurs/cellFiber.c" TARGET_DIRECTORY ps3recomp_runtime
    APPEND PROPERTY COMPILE_OPTIONS -include "${CMAKE_CURRENT_LIST_DIR}/BuzzAndroidFiber.h")
set_property(SOURCE "${PS3RECOMP_DIR}/runtime/platform/win32_compat.c" TARGET_DIRECTORY ps3recomp_runtime
    APPEND PROPERTY COMPILE_OPTIONS -include "${CMAKE_CURRENT_LIST_DIR}/BuzzAndroidThreads.h")
