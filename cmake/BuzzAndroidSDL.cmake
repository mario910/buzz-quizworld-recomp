# Use the same SDL release for the Java activity and the native runtime.
set(BUZZ_ANDROID_SDL_SOURCE "" CACHE PATH "SDL 2.32.10 source checkout")
if(NOT EXISTS "${BUZZ_ANDROID_SDL_SOURCE}/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java")
    message(FATAL_ERROR "Set BUZZ_ANDROID_SDL_SOURCE to the SDL 2.32.10 source directory")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${BUZZ_ANDROID_SDL_SOURCE}" "${CMAKE_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)

# The SDK uses distro-style SDL2 header names.
set(sdl_compat "${CMAKE_CURRENT_BINARY_DIR}/sdl-compat")
file(WRITE "${sdl_compat}/SDL2/SDL.h" "#include <SDL.h>\n")
file(WRITE "${sdl_compat}/SDL2/SDL_vulkan.h" "#include <SDL_vulkan.h>\n")
target_include_directories(SDL2 INTERFACE "${sdl_compat}")

# The SDK uses find_package(SDL2). Point it at the target already built above.
file(WRITE "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/SDL2Config.cmake" "set(SDL2_FOUND TRUE)\n")
