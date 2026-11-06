include(FetchContent)
# Includes the pinned linkernsbypass submodule.
FetchContent_Declare(adrenotools
    GIT_REPOSITORY https://github.com/bylaws/libadrenotools.git
    GIT_TAG 8fae8ce254dfc1344527e05301e43f37dea2df80
    GIT_SUBMODULES lib/linkernsbypass GIT_SUBMODULES_RECURSE TRUE)
set(saved_shared_libs "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(adrenotools)
set(BUILD_SHARED_LIBS "${saved_shared_libs}")
add_library(buzz_vulkan SHARED "${CMAKE_CURRENT_LIST_DIR}/../port/android_gpu_driver.cpp")
target_link_libraries(buzz_vulkan PRIVATE adrenotools dl log)
# Never link the hooks into the application's namespace: their libc entry
# points must only interpose inside adrenotools' isolated driver namespace.
# Gradle explicitly packages the shared targets below.
add_dependencies(buzz_vulkan main_hook file_redirect_hook gsl_alloc_hook)
target_link_options(buzz_vulkan PRIVATE "-Wl,-z,max-page-size=16384")
# liblinkernsbypass holds function-pointer variables named after linker APIs.
# They must never interpose on Android's real functions in the parent namespace.
target_link_options(buzz_vulkan PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/BuzzAndroidGpu.exports")
set_property(TARGET buzz_vulkan APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/BuzzAndroidGpu.exports")
