cmake_minimum_required(VERSION 3.31)
# Older Ninja generators can quote DEP_FILE as a literal filename for external
# sources under paths containing parentheses/spaces, silently losing header
# dependencies (Kitware/CMake #26287).
#
# Synthetic unit tests of the PC port's host code (settings, controllers, phone
# buzzers, host overlay/controls, game paths). They need no game files,
# generated code, controllers or desktop input, and build in CI on Windows,
# Linux and macOS.
project(BuzzPortTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The SDK's C sources use C11 atomics (MSVC needs them enabled explicitly).
set(CMAKE_C_STANDARD 11)
if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
endif()
enable_testing()

set(root "${CMAKE_CURRENT_SOURCE_DIR}/..")
include("${root}/cmake/BuzzPhone.cmake")

# The PC settings/controller sources shared by several tests.
add_library(buzz_port_settings STATIC
    ${root}/port/buzzer_input_config.cpp ${root}/port/pc_settings.cpp ${root}/port/pc_settings_file.cpp)
target_include_directories(buzz_port_settings PUBLIC ${root}/port)

function(buzz_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBRARIES;INCLUDES" ${ARGN})
    string(REPLACE "-" "_" target "${name}_test")
    add_executable(${target} ${T_SOURCES})
    target_link_libraries(${target} PRIVATE ${T_LIBRARIES})
    target_include_directories(${target} PRIVATE ${T_INCLUDES})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    add_test(NAME ${name} COMMAND ${target})
endfunction()

buzz_test(phone-sessions SOURCES phone-sessions-test.cpp ${root}/port/phone_sessions.cpp)
buzz_test(phone-server SOURCES phone-server-test.cpp LIBRARIES buzz_phone)
set_tests_properties(phone-server PROPERTIES TIMEOUT 30)
buzz_test(buzzer-hid SOURCES buzzer-hid-test.cpp)
buzz_test(sony-pad SOURCES sony-pad-test.cpp ${root}/port/buzzer_input_config.cpp)
buzz_test(answer-hints SOURCES answer-hints-test.cpp)
buzz_test(pc-settings SOURCES pc-settings-test.cpp LIBRARIES buzz_port_settings)
if(WIN32)
    buzz_test(buzzer-hid-win32 SOURCES buzzer-hid-win32-test.cpp)
    buzz_test(buzzer-host-menu SOURCES buzzer-host-menu-test.cpp LIBRARIES buzz_port_settings)
endif()

# Tests of the PS3Recomp SDK host layer need the ps3recomp submodule.
if(EXISTS "${root}/ps3recomp/libs/system/host_controls.c")
    buzz_test(game-host-paths SOURCES game-host-paths-test.cpp ${root}/ps3recomp/libs/system/cellGame.c
              INCLUDES ${root}/ps3recomp/include ${root}/ps3recomp/libs/system)
    add_library(buzz_host_controls STATIC ${root}/ps3recomp/libs/system/host_controls.c)
    target_include_directories(buzz_host_controls PUBLIC ${root}/ps3recomp/include)
    buzz_test(host-controls SOURCES host-controls-test.cpp LIBRARIES buzz_host_controls)
    buzz_test(host-overlay SOURCES host-overlay-test.cpp ${root}/ps3recomp/libs/system/host_overlay.c
              INCLUDES ${root}/ps3recomp/include)
    add_library(buzz_pc_runtime STATIC ${root}/port/pc_runtime.cpp ${root}/port/pc_texture_upscaler.cpp)
    target_link_libraries(buzz_pc_runtime PUBLIC buzz_port_settings buzz_host_controls buzz_phone)
    buzz_test(pc-runtime SOURCES pc-runtime-test.cpp LIBRARIES buzz_pc_runtime)
endif()
