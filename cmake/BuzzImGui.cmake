include(FetchContent)
FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/6d910d5487d11ca567b61c7824b0c78c569d62f0
    URL_HASH SHA256=b715a3226320aaebaea79dc6af6405ae37cdcb479817fddaef12a17d8bd15a3a)
FetchContent_MakeAvailable(imgui)
add_library(buzz_game_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(buzz_game_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(WIN32)
    target_sources(buzz_game_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
    target_link_libraries(buzz_game_imgui PUBLIC d3d12 dxgi d3dcompiler dwmapi)
else()
    target_sources(buzz_game_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)
    target_link_libraries(buzz_game_imgui PUBLIC SDL2::SDL2)
endif()
if(BUZZ_EXPERIMENTAL_VULKAN)
    # The SDK loads libvulkan at run time; ImGui takes its entry points from
    # the backend's instance through ImGui_ImplVulkan_LoadFunctions.
    target_sources(buzz_game_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_include_directories(buzz_game_imgui PUBLIC "${PS3RECOMP_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(buzz_game_imgui PUBLIC VK_NO_PROTOTYPES IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
endif()
