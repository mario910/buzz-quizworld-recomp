cmake_minimum_required(VERSION 3.20)
project(BuzzQuizWorldRecomp
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Buzz! Quiz World BCES00647 1.02 experimental port"
)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    # Default: this project is still sitting inside ps3recomp/templates/project/
    get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE)
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

set(RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/recompiled" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
set(PORT_COMPLETION_SOURCE "" CACHE FILEPATH "Experimental native completion adapter TU")
if(PORT_COMPLETION_SOURCE)
    get_filename_component(completion_original "${PORT_COMPLETION_SOURCE}" NAME)
    if(NOT completion_original MATCHES "^ppu_recomp_(013|014|015)\\.cpp$")
        message(FATAL_ERROR "Unknown completion adapter source name")
    endif()
    list(REMOVE_ITEM RECOMP_SOURCES "${RECOMP_DIR}/${completion_original}")
    list(APPEND RECOMP_SOURCES "${PORT_COMPLETION_SOURCE}")
endif()
set(PORT_PRODUCER_PROBE_DIR "" CACHE PATH "Diagnostic PPU producer scope TUs")
if(PORT_PRODUCER_PROBE_DIR)
    # The generated 015 copy already includes the requested completion adapter.
    list(REMOVE_ITEM RECOMP_SOURCES "${PORT_COMPLETION_SOURCE}")
    foreach(producer_name ppu_recomp_015.cpp ppu_recomp_016.cpp)
        set(producer_source "${PORT_PRODUCER_PROBE_DIR}/${producer_name}")
        if(NOT EXISTS "${producer_source}")
            message(FATAL_ERROR "Generate both producer probe TUs first")
        endif()
        list(REMOVE_ITEM RECOMP_SOURCES "${RECOMP_DIR}/${producer_name}")
        list(APPEND RECOMP_SOURCES "${producer_source}")
    endforeach()
endif()
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")
set(PORT_VIDEO_SOURCE "" CACHE FILEPATH "Diagnostic generated video-state probe TU")
if(PORT_VIDEO_SOURCE)
    if(NOT EXISTS "${PORT_VIDEO_SOURCE}")
        message(FATAL_ERROR "Generate video probes first")
    endif()
    list(REMOVE_ITEM RECOMP_SOURCES "${RECOMP_DIR}/ppu_recomp_019.cpp")
    list(APPEND RECOMP_SOURCES "${PORT_VIDEO_SOURCE}")
    get_filename_component(video_probe_directory "${PORT_VIDEO_SOURCE}" DIRECTORY)
    foreach(video_probe_name ppu_recomp_027.cpp ppu_recomp_028.cpp)
        set(video_codec_source "${video_probe_directory}/${video_probe_name}")
        if(NOT EXISTS "${video_codec_source}")
            message(FATAL_ERROR "Generate the worker and codec probe TUs too")
        endif()
        list(REMOVE_ITEM RECOMP_SOURCES "${RECOMP_DIR}/${video_probe_name}")
        list(APPEND RECOMP_SOURCES "${video_codec_source}")
    endforeach()
endif()

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Built from source as a subdirectory rather than imported as a prebuilt
# archive. A static library carries none of its own link requirements, so an
# imported one arrives without SDL2, pthreads, the Metal frameworks or the
# Winsock/XInput imports -- the link then fails on every platform for a
# different reason. add_subdirectory hands all of that over with the target.
if(ANDROID)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "Buzz Android requires arm64-v8a")
    endif()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzAndroidSDL.cmake")
    set(BUZZ_EXPERIMENTAL_VULKAN ON CACHE BOOL "" FORCE)
    set(BUZZ_STANDALONE ON CACHE BOOL "" FORCE)
    set(BUZZ_DISC_COMPLETION ON CACHE BOOL "" FORCE)
endif()
option(BUZZ_EXPERIMENTAL_VULKAN "Developer-only Vulkan game build; keep outside build-windows" OFF)
if(BUZZ_EXPERIMENTAL_VULKAN)
    if(NOT WIN32 AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Buzz Vulkan requires Windows, Linux or Android")
    endif()
    if(CMAKE_BINARY_DIR MATCHES "[/\\]build-windows$")
        message(FATAL_ERROR "Use build-vulkan-windows: do not replace the tested D3D12 build")
    endif()
    set(PS3RECOMP_RSX_VULKAN ON CACHE BOOL "" FORCE)
    set(PS3RECOMP_FETCH_VULKAN_DEPS ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzSpuPureLoop.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzWindowsTimer.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzRendererCpu.cmake")
if(ANDROID)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzAndroidFiber.cmake")
endif()
if((ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND BUZZ_EXPERIMENTAL_VULKAN)
    find_package(SDL2 REQUIRED)
    include("../cmake/BuzzLinuxVulkan.cmake")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzVulkanUpload.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzRendererWait.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzRendererStatePackets.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzFifoReady.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzFifoParkWake.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzFifoMethods.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzPpuWordWait.cmake")

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
set(PORT_PREPARATION_EXECUTABLE "" CACHE FILEPATH "BuzzPrepare host executable for HLE generation")
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    if(PORT_PREPARATION_EXECUTABLE)
        if(NOT EXISTS "${PORT_PREPARATION_EXECUTABLE}")
            message(FATAL_ERROR "BuzzPrepare executable missing: ${PORT_PREPARATION_EXECUTABLE}")
        endif()
        set(HLE_NID_COMMAND "${PORT_PREPARATION_EXECUTABLE}"
            --generate-hle-nids "${PS3RECOMP_DIR}" --out "${HLE_NID_TABLE}")
        set(HLE_NID_GENERATOR_FILES "${PORT_PREPARATION_EXECUTABLE}")
    else()
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(HLE_NID_COMMAND "${Python3_EXECUTABLE}" "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
            --out "${HLE_NID_TABLE}" --all)
        set(HLE_NID_GENERATOR_FILES "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
            "${PS3RECOMP_DIR}/tools/nid_database.py")
    endif()
    # The generator discovers handlers from library definitions. Rebuild when
    # an existing module changes, and reconfigure when a new module is added.
    file(GLOB_RECURSE HLE_NID_SOURCES CONFIGURE_DEPENDS
        "${PS3RECOMP_DIR}/libs/*.c")
    # A removed input cannot make the output older than a remaining dependency.
    # Track the list itself so deletions also remove stale handler references.
    set(HLE_NID_SOURCE_LIST "${CMAKE_CURRENT_BINARY_DIR}/hle_nid_sources.txt")
    file(GENERATE OUTPUT "${HLE_NID_SOURCE_LIST}" CONTENT "${HLE_NID_GENERATOR_FILES}\n${HLE_NID_SOURCES}\n")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND ${HLE_NID_COMMAND}
        DEPENDS ${HLE_NID_GENERATOR_FILES} ${HLE_NID_SOURCES} "${HLE_NID_SOURCE_LIST}"
        VERBATIM
        COMMENT "Generating the HLE NID -> handler table")
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)
set(PORT_SPU_ISA "sse2" CACHE STRING "Lifted x86 SPU helpers: sse2, sse41, avx2 or experimental avx512")
set_property(CACHE PORT_SPU_ISA PROPERTY STRINGS sse2 sse41 avx2 avx512)
if(NOT PORT_SPU_ISA MATCHES "^(sse2|sse41|avx2|avx512)$")
    message(FATAL_ERROR "Unknown PORT_SPU_ISA=${PORT_SPU_ISA}")
endif()

set(PORT_SPU_INPUTS "" CACHE STRING
    "Input files to scan for all embedded SPU ELF programs (semicolon-separated decrypted ELF/PRX/data paths)")
set(SPU_SOURCES "")
set(PORT_IMAGE39_SOURCE "" CACHE FILEPATH "Experimental checked arithmetic for image 39; empty uses reference")
set(PORT_IMAGE40_SOURCE "" CACHE FILEPATH "Experimental checked arithmetic for image 40; empty uses reference")
set(PORT_IMAGE7_SOURCE "" CACHE FILEPATH "Image 7 lift with the native-float 0x8638 kernel (tools/codegen/generate-image7-native.py)")
set(PORT_VIDEO_IDCT_SOURCE "" CACHE FILEPATH "EU 1.00 Sofdec native transform adapter (generate-video-idct.py)")
option(PORT_IMAGE7_COMPARE "Compare Image 7 against its exact oracle on Android" OFF)
set(PORT_IMAGE36_SOURCE "" CACHE FILEPATH "Image36 lift with scoped native 0x06D8 SIMD (tools/codegen/generate-image36-native.py)")
set(PORT_MSTREAM_DIR "" CACHE PATH "Lifted runtime-loaded MultiStream modules (tools/codegen/generate-mstream-modules.py); empty interprets them")
set(PORT_GEOMETRY_SOURCE "" CACHE FILEPATH "Experimental generated geometry lift; empty uses reference")
set(geometry_phase2_mask 0)
set(geometry_job_audit_available 0)
set(geometry_job_native_setup_available 0)
set(animation_job_available 0)
set(native_geometry "${SPU_DIR}/buzz_job_6eba/spu_recomp.c")
if(PORT_ENABLE_SPU AND PORT_SPU_INPUTS)
    include("${PS3RECOMP_DIR}/cmake/PS3RecompSPU.cmake")
    ps3recomp_generate_spu("${CMAKE_CURRENT_BINARY_DIR}/generated_spu" ${PORT_SPU_INPUTS})
    list(APPEND SPU_SOURCES ${SPU_GENERATED_SOURCES}
         "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
elseif(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(PORT_IMAGE39_SOURCE)
        if(NOT EXISTS "${PORT_IMAGE39_SOURCE}" OR (NOT ANDROID AND NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$"))
            message(FATAL_ERROR "Image 39 experiment requires generated source and Android ARM64 or checked AVX2/FMA")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/buzz_job_c0bc/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE39_SOURCE}")
    endif()
    if(PORT_IMAGE40_SOURCE)
        if(NOT EXISTS "${PORT_IMAGE40_SOURCE}" OR (NOT ANDROID AND NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$"))
            message(FATAL_ERROR "Image 40 experiment requires generated source and Android ARM64 or checked AVX2/FMA")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/buzz_job_abe1/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE40_SOURCE}")
        file(READ "${PORT_IMAGE40_SOURCE}" animation_adapter_source)
        if(animation_adapter_source MATCHES "#define BUZZ_ANIMATION_JOB_DRIVER_ABI 1")
            set(animation_job_available 1)
        endif()
    endif()
    if(PORT_IMAGE36_SOURCE)
        get_filename_component(image36_dir "${PORT_IMAGE36_SOURCE}" DIRECTORY)
        set(image36_kernel "${image36_dir}/kernel_06d8.c")
        if(NOT EXISTS "${PORT_IMAGE36_SOURCE}" OR NOT EXISTS "${image36_kernel}" OR (NOT ANDROID AND NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$"))
            message(FATAL_ERROR "Image36 leaf requires generated source and Android ARM64 or checked AVX2/FMA")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/buzz_job_14b4/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE36_SOURCE}" "${image36_kernel}")
    endif()
    if(PORT_IMAGE7_SOURCE)
        get_filename_component(image7_dir "${PORT_IMAGE7_SOURCE}" DIRECTORY)
        set(image7_kernel "${image7_dir}/kernel_8638.c")
        if(NOT EXISTS "${PORT_IMAGE7_SOURCE}" OR NOT EXISTS "${image7_kernel}" OR (NOT ANDROID AND NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$"))
            message(FATAL_ERROR "Image 7 kernel requires generated source and Android ARM64 or checked AVX2/FMA")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/spu_0006_at_007E7500/spu_recomp.c")
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/spu_0006_at_007FF780/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE7_SOURCE}" "${image7_kernel}")
    endif()
    if(PORT_VIDEO_IDCT_SOURCE)
        get_filename_component(video_idct_dir "${PORT_VIDEO_IDCT_SOURCE}" DIRECTORY)
        set(video_idct_kernel "${video_idct_dir}/kernel.c")
        if(NOT EXISTS "${PORT_VIDEO_IDCT_SOURCE}" OR NOT EXISTS "${video_idct_kernel}" OR (NOT ANDROID AND NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$"))
            message(FATAL_ERROR "Video IDCT requires a generated adapter and ARM64 or AVX2/FMA")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/spu_0007_at_0080A080/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_VIDEO_IDCT_SOURCE}" "${video_idct_kernel}")
    endif()
    if(PORT_MSTREAM_DIR)
        file(GLOB mstream_lifted "${PORT_MSTREAM_DIR}/*/spu_recomp.c")
        if(NOT EXISTS "${PORT_MSTREAM_DIR}/mstream_register.c" OR NOT mstream_lifted)
            message(FATAL_ERROR "Run tools/codegen/generate-mstream-modules.py first")
        endif()
        list(APPEND SPU_LIFTED ${mstream_lifted} "${PORT_MSTREAM_DIR}/mstream_register.c")
    endif()
    if(PORT_GEOMETRY_SOURCE)
        if(NOT EXISTS "${PORT_GEOMETRY_SOURCE}")
            message(FATAL_ERROR "Generate the experimental geometry lift first")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${native_geometry}")
        set(native_geometry "${PORT_GEOMETRY_SOURCE}")
        file(READ "${native_geometry}" geometry_adapter_source)
        if(geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_IMPLEMENTED_MASK ([0-9]+)")
            set(geometry_phase2_mask "${CMAKE_MATCH_1}")
        endif()
        if(geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE 1")
            set(geometry_job_audit_available 1)
        endif()
        if(geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE 1")
            if(NOT geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_JOB_DRIVER_ABI 48")
                message(FATAL_ERROR "Regenerate the phase-3 driver adapter: its native routing protocol changed")
            endif()
            set(geometry_job_native_setup_available 1)
        endif()
        if(geometry_adapter_source MATCHES "#include \"spu_neon_rtz.h\"")
            if(NOT ANDROID OR geometry_adapter_source MATCHES "spu_df|spu_fesd|spu_frds")
                message(FATAL_ERROR "Image 41 NEON requires Android ARM64 and single-precision SPU code")
            endif()
            set(geometry_arm_neon ON)
            if(geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_LS_DRIVER 1")
                set(geometry_arm_driver ON)
            endif()
        elseif(geometry_adapter_source MATCHES "native_geometry_unit24.h" AND
               (ANDROID OR NOT PORT_NATIVE_SPU_FLOAT OR NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$"))
            message(FATAL_ERROR "Use the ARM64 geometry adapter on Android or AVX2/FMA with PORT_NATIVE_SPU_FLOAT on desktop")
        endif()
        list(APPEND SPU_LIFTED "${native_geometry}")
        message(STATUS "Experimental geometry: ${native_geometry}")
    endif()
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        list(APPEND SPU_SOURCES ${SPU_LIFTED}
             "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c jobmods_register.c taskset_policy_bytes.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

set(PORT_HOST_PROFILE_DIR "" CACHE PATH "Generated, hash-bound host adapters for a reviewed executable family")
foreach(host_name stubs boot_trace frontend_policy video_queue edge_decompress)
    set(host_${host_name} "${CMAKE_CURRENT_SOURCE_DIR}/${host_name}.cpp")
    if(PORT_HOST_PROFILE_DIR)
        set(host_${host_name} "${PORT_HOST_PROFILE_DIR}/${host_name}.cpp")
        if(NOT EXISTS "${host_${host_name}}")
            message(FATAL_ERROR "Missing generated host adapter: ${host_name}")
        endif()
    endif()
endforeach()
set(game_sources
    main.cpp
    cpu_isa_gate.cpp
    ${host_stubs}
    virtual_buzzer.cpp
    buzzer_input_config.cpp
    buzzer_input_win32.cpp
    buzzer_hid_win32.cpp
    pc_settings.cpp
    pc_runtime.cpp
    frame_limit.cpp
    pc_settings_file.cpp
    pc_overlay.cpp
    pc_texture_upscaler.cpp
    ../third_party/qrcodegen/qrcodegen.cpp
    ${host_boot_trace}
    ${host_frontend_policy}
    video_probe.cpp
    ${host_video_queue}
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)
if(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${game_sources})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME main)
    target_link_libraries(${PROJECT_NAME} PRIVATE log)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzAndroidGpu.cmake")
    target_sources(${PROJECT_NAME} PRIVATE android_controls.cpp)
    target_link_libraries(${PROJECT_NAME} PRIVATE buzz_vulkan)
else()
    add_executable(${PROJECT_NAME} ${game_sources})
endif()
if(BUZZ_EXPERIMENTAL_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BUZZ_EXPERIMENTAL_VULKAN=1)
endif()

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route SPU_IB_DISPATCH through the musttail path.
#   -O2                 -- the target-wide -O1 exists for the huge PPU TUs; the
#                          SPU sources are small and decode-hot, so they get
#                          full optimization. The last -O on the line wins.
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT)
    if(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(PORT_SPU_OPT "-O2" CACHE STRING "Optimization level of the lifted SPU TUs")
        set(spu_flags ${PORT_SPU_OPT})
        if(MSVC)
            list(APPEND spu_flags /clang:-ffp-contract=off)
        else()
            list(APPEND spu_flags -ffp-contract=off)
        endif()
        if(PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
            list(APPEND spu_flags -mavx2)
            set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_AVX2=1)
            if(PORT_SPU_ISA STREQUAL "avx512")
                list(APPEND spu_flags -mavx512f -mavx512bw -mavx512vl)
                set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_AVX512=1)
                set_property(SOURCE cpu_isa_gate.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_AVX512=1)
            endif()
        elseif(PORT_SPU_ISA STREQUAL "sse41")
            list(APPEND spu_flags -msse4.1)
            set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_SSE41=1)
        endif()
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "${spu_flags}")
    endif()
endif()

# Lifted images re-read the env-armed LS probe globals on every LS access
# (see SPU_LS_DEBUG_HOOKS in spu_context.h). OFF drops those probes from the
# lifted TUs only; the runtime keeps its own.
set(spu_probe_default ON)
if(BUZZ_STANDALONE)
    set(spu_probe_default OFF)
endif()
option(PORT_SPU_LS_DEBUG_HOOKS "Keep SPU_LS_WATCH/SPU_SMC_WATCH probes in lifted SPU images" ${spu_probe_default})
if(SPU_SOURCES AND NOT PORT_SPU_LS_DEBUG_HOOKS)
    set_property(SOURCE ${SPU_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS SPU_LS_DEBUG_HOOKS=0)
endif()

# Use single-instruction word shifts/rotates on ARM64 and SSSE3+ x86. Keep
# the SDK checkout immutable; include its helpers before remapping.
if(SPU_SOURCES)
    if(MSVC)
        set_property(SOURCE ${SPU_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/spu_shuffle.h")
    else()
        set_property(SOURCE ${SPU_SOURCES} APPEND PROPERTY COMPILE_OPTIONS
            -include "${CMAKE_CURRENT_SOURCE_DIR}/spu_shuffle.h")
    endif()
endif()

option(PORT_NATIVE_SPU_FLOAT "Native RZ/FMA for the audited single-precision geometry image only" OFF)
option(PORT_RTZ_RESULT_GUARD "Experimental result classification instead of operand exponent checks; same exact lane repair" OFF)
if(PORT_RTZ_RESULT_GUARD)
    set(result_guard_sources "${native_geometry}")
    if(PORT_IMAGE40_SOURCE)
        list(APPEND result_guard_sources "${PORT_IMAGE40_SOURCE}")
    endif()
    if(PORT_IMAGE39_SOURCE)
        list(APPEND result_guard_sources "${PORT_IMAGE39_SOURCE}")
    endif()
    set_property(SOURCE ${result_guard_sources} APPEND PROPERTY COMPILE_DEFINITIONS SPU_RTZ_RESULT_GUARD=1)
endif()
set(spurs_job_source "${PS3RECOMP_DIR}/runtime/spu/spurs_job.c")
if(ANDROID AND (PORT_IMAGE39_SOURCE OR PORT_IMAGE40_SOURCE OR geometry_arm_neon))
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzAndroidJobRtz.cmake")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzSpursExecutionTrace.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzSpuJobReturn.cmake")
if(geometry_arm_neon)
    set_property(SOURCE "${native_geometry}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    set_property(SOURCE "${spurs_job_source}" TARGET_DIRECTORY ps3recomp_runtime
                 APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ_IMAGE=41)
    set_property(SOURCE native_geometry.cpp runtime_defaults.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_GEOMETRY_ARM_NEON=1)
endif()
option(BUZZ_IMAGE39_NATIVE_REQUEST "Use the admitted EU100 native descriptor-cache request driver" OFF)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzImage39Bounds.cmake")
if(BUZZ_IMAGE39_NATIVE_REQUEST)
    if(NOT PORT_IMAGE39_SOURCE)
        message(FATAL_ERROR "Native image39 requests require the generated EU100 image39 adapter")
    endif()
    file(READ "${PORT_IMAGE39_SOURCE}" image39_request_source)
    if(NOT image39_request_source MATCHES "#define BUZZ_IMAGE39_REQUEST_PROFILE 100")
        message(FATAL_ERROR "Regenerate the pinned EU100 image39 adapter before enabling native requests")
    endif()
endif()
if(PORT_IMAGE39_SOURCE)
    set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_DEFINITIONS
        "BUZZ_IMAGE39_NATIVE_REQUEST=$<BOOL:${BUZZ_IMAGE39_NATIVE_REQUEST}>")
    if(PORT_SPU_INPUTS)
        message(FATAL_ERROR "Image 39 adapter requires the pinned pre-generated SPU images")
    endif()
    if(NOT ANDROID)
        set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    endif()
    if(MSVC)
        set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
    set_property(SOURCE "${spurs_job_source}" TARGET_DIRECTORY ps3recomp_runtime
                 APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ_IMAGE39=39)
    if(ANDROID)
        set_property(SOURCE runtime_defaults.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_ANDROID_IMAGE39_NEON=1)
    else()
        set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
    endif()
endif()
if(PORT_IMAGE40_SOURCE)
    if(PORT_SPU_INPUTS)
        message(FATAL_ERROR "Image 40 adapter requires the pinned pre-generated SPU images")
    endif()
    if(NOT ANDROID)
        set_property(SOURCE "${PORT_IMAGE40_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    endif()
    if(MSVC)
        set_property(SOURCE "${PORT_IMAGE40_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${PORT_IMAGE40_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
    set_property(SOURCE "${spurs_job_source}" TARGET_DIRECTORY ps3recomp_runtime
                 APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ_EXTRA_IMAGE=40)
    if(NOT ANDROID)
        set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
    endif()
endif()
if(PORT_NATIVE_SPU_FLOAT)
    if(NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
        message(FATAL_ERROR "Native SPU float requires the checked AVX2/FMA build")
    endif()
    file(READ "${native_geometry}" geometry_source)
    if(geometry_source MATCHES "spu_df|spu_fesd|spu_frds")
        message(FATAL_ERROR "Geometry image now contains double-precision operations: RTZ mode is not safe")
    endif()
    set_property(SOURCE "${native_geometry}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1)
    set_property(SOURCE "${native_geometry}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    if(MSVC)
        set_property(SOURCE "${native_geometry}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math)
    else()
        set_property(SOURCE "${native_geometry}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math)
    endif()
    set_property(SOURCE "${PS3RECOMP_DIR}/runtime/spu/spurs_job.c" TARGET_DIRECTORY ps3recomp_runtime
                 APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ_IMAGE=41)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
endif()

# app.rc carries the icon, the version block and the manifest, and needs a
# resource compiler. Listing it as a source on a host that has none is not a
# link error, it is a "no rule to make" for a file nothing can consume.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"        # win32_compat.h and friends
    "${PS3RECOMP_DIR}/libs/video"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzPhone.cmake")
target_link_libraries(${PROJECT_NAME} PRIVATE buzz_phone)

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zlib"
                 "${CMAKE_BINARY_DIR}/zlib" EXCLUDE_FROM_ALL)
option(BUZZ_STANDALONE "Build for the prepared distribution layout" OFF)
option(BUZZ_DISC_COMPLETION "Enable the validated original disc completion mode" OFF)
if(BUZZ_STANDALONE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BUZZ_STANDALONE=1
        BUZZ_DISC_COMPLETION=$<BOOL:${BUZZ_DISC_COMPLETION}>)
endif()
target_sources(${PROJECT_NAME} PRIVATE ${host_edge_decompress} job_corpus.cpp native_stagef.cpp native_geometry.cpp native_geometry_job.cpp native_animation_runtime.cpp runtime_defaults.cpp)
if(NOT ANDROID OR geometry_arm_driver)
    target_sources(${PROJECT_NAME} PRIVATE native_geometry_job_flow.cpp)
endif()
if(geometry_arm_driver)
    option(PORT_GEOMETRY_HOST_NEON "Use the native host SoA geometry pipeline on ARM64" OFF)
    set_property(SOURCE native_geometry_job.cpp native_geometry_job_flow.cpp runtime_defaults.cpp
                 APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_GEOMETRY_LS_DRIVER=1)
    if(PORT_GEOMETRY_HOST_NEON)
        if(NOT geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_HOST_NEON 1")
            message(FATAL_ERROR "Generate the full ARM64 host adapter with --arm64-host")
        endif()
        set_property(SOURCE native_geometry_job.cpp native_geometry_job_flow.cpp
                     APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_GEOMETRY_HOST_NEON=1)
    elseif(geometry_adapter_source MATCHES "#define BUZZ_GEOMETRY_HOST_NEON 1")
        message(FATAL_ERROR "The full ARM64 adapter requires PORT_GEOMETRY_HOST_NEON")
    endif()
    set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
endif()
set(native_job_default_candidate OFF)
option(PORT_OPTIMIZE_PPU_HOT_PATHS "Optimize PPU bridge and profiled job-poll TU" ON)
if(geometry_job_native_setup_available AND animation_job_available AND
   BUZZ_EXPERIMENTAL_VULKAN AND PORT_NATIVE_SPU_FLOAT AND PORT_RTZ_RESULT_GUARD AND PORT_SPU_ISA STREQUAL "avx2" AND
   PORT_COMPLETION_SOURCE AND PORT_IMAGE7_SOURCE AND PORT_IMAGE36_SOURCE AND PORT_IMAGE39_SOURCE AND PORT_MSTREAM_DIR AND
   PORT_OPTIMIZE_PPU_HOT_PATHS AND PORT_SPU_OPT STREQUAL "-O2" AND NOT PORT_SPU_LS_DEBUG_HOOKS AND
   NOT PORT_VIDEO_SOURCE AND NOT PORT_PRODUCER_PROBE_DIR)
    set(native_job_default_candidate ON)
endif()
option(PORT_NATIVE_JOB_DEFAULTS "Embed the approved native/Vulkan runtime profile in the qualified EXE" ${native_job_default_candidate})
if(PORT_NATIVE_JOB_DEFAULTS AND NOT native_job_default_candidate)
    message(FATAL_ERROR "Embedded native job defaults require both reviewed adapters and the qualified Vulkan/AVX2/RTZ build")
endif()
set(PORT_NATIVE_JOB_PROFILE "eu100-vulkan-native-v1" CACHE STRING "Identity of the reviewed disc native runtime profile")
if(NOT PORT_NATIVE_JOB_PROFILE MATCHES "^(eu100|us100)-vulkan-native-v1$")
    message(FATAL_ERROR "Unknown native runtime profile identity")
endif()
if(PORT_NATIVE_JOB_DEFAULTS)
    file(READ "${PORT_COMPLETION_SOURCE}" native_completion_adapter)
    if(PORT_NATIVE_JOB_PROFILE STREQUAL "us100-vulkan-native-v1")
        if(NOT native_completion_adapter MATCHES "#define BUZZ_COMPLETION_US100 1")
            message(FATAL_ERROR "US native defaults require the pinned US PPU completion adapter")
        endif()
    elseif(NOT native_completion_adapter MATCHES "#define BUZZ_COMPLETION_DISC100 1" OR
           native_completion_adapter MATCHES "#define BUZZ_COMPLETION_US100 1")
        message(FATAL_ERROR "EU native defaults require the pinned EU PPU completion adapter")
    endif()
endif()
set_property(SOURCE native_geometry_job.cpp native_animation_runtime.cpp APPEND PROPERTY COMPILE_DEFINITIONS "BUZZ_NATIVE_JOB_DEFAULT=$<BOOL:${PORT_NATIVE_JOB_DEFAULTS}>")
set_property(SOURCE runtime_defaults.cpp APPEND PROPERTY COMPILE_DEFINITIONS "BUZZ_RUNTIME_DEFAULTS=$<BOOL:${PORT_NATIVE_JOB_DEFAULTS}>" "BUZZ_NATIVE_RUNTIME_PROFILE=\"${PORT_NATIVE_JOB_PROFILE}\"")
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzSpursWaits.cmake")
if(animation_job_available)
    if(NOT PORT_NATIVE_SPU_FLOAT OR NOT PORT_RTZ_RESULT_GUARD OR NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
        message(FATAL_ERROR "Native animation job requires checked AVX2/FMA, native SPU RTZ and result guard")
    endif()
    set_property(SOURCE native_animation_runtime.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_ANIMATION_JOB_AVAILABLE=1 SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
    set_property(SOURCE native_animation_runtime.cpp APPEND PROPERTY COMPILE_OPTIONS "${spu_flags}" -mfma)
    if(MSVC)
        set_property(SOURCE native_animation_runtime.cpp APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math)
    else()
        set_property(SOURCE native_animation_runtime.cpp APPEND PROPERTY COMPILE_OPTIONS -frounding-math)
    endif()
endif()
set_property(SOURCE native_geometry.cpp APPEND PROPERTY COMPILE_DEFINITIONS "BUZZ_GEOMETRY_AVAILABLE=${geometry_phase2_mask}")
set_property(SOURCE native_geometry_job.cpp APPEND PROPERTY COMPILE_DEFINITIONS "BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE=${geometry_job_audit_available}")
set_property(SOURCE native_geometry_job.cpp APPEND PROPERTY COMPILE_DEFINITIONS "BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE=${geometry_job_native_setup_available}")
option(BUZZ_GEOMETRY_INDEX_SEQUENTIAL "Bulk-validated sequential geometry index decoding" OFF)
option(BUZZ_GEOMETRY_DEFERRED_CLIP "Defer exceptional geometry projection repair to one block check" OFF)
option(BUZZ_GEOMETRY_NATIVE_RECIPROCAL "Use native SIMD reciprocal division for ordinary geometry projection (relaxed intermediate values)" OFF)
option(BUZZ_GEOMETRY_BOUNDED_SKIN "Use native bounded palette blending and weighted transforms" OFF)
option(BUZZ_GEOMETRY_COARSE_CLIP "Reject offscreen geometry jobs and admit bounded native projection" OFF)
option(BUZZ_GEOMETRY_LAZY_ATTRIBUTES "Defer rigid geometry attributes until triangles survive culling" OFF)
option(BUZZ_GEOMETRY_INDEX_BATCHED "Eight-lane packed geometry delta decoding" OFF)
option(BUZZ_GEOMETRY_INDEX_CACHE "Content-verified cache of decoded geometry indices" OFF)
option(BUZZ_GEOMETRY_HOST_COMMANDS "Build and publish geometry command packets with host integer control" OFF)
option(BUZZ_GEOMETRY_ZERO_VERTEX_PADDING "Zero unindexed vertex alignment bytes instead of evaluating a padding vertex" OFF)
# Explicitly reproduce the compaction path present in the qualified Windows
# executable. Other platforms keep their existing source default.
option(BUZZ_GEOMETRY_CULL_COMPACT "Eight-lane geometry output compaction" ${WIN32})
set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "BUZZ_GEOMETRY_DEFERRED_CLIP=$<BOOL:${BUZZ_GEOMETRY_DEFERRED_CLIP}>"
    "BUZZ_GEOMETRY_NATIVE_RECIPROCAL=$<BOOL:${BUZZ_GEOMETRY_NATIVE_RECIPROCAL}>"
    "BUZZ_GEOMETRY_BOUNDED_SKIN=$<BOOL:${BUZZ_GEOMETRY_BOUNDED_SKIN}>"
    "BUZZ_GEOMETRY_COARSE_CLIP=$<BOOL:${BUZZ_GEOMETRY_COARSE_CLIP}>"
    "BUZZ_GEOMETRY_LAZY_ATTRIBUTES=$<BOOL:${BUZZ_GEOMETRY_LAZY_ATTRIBUTES}>"
    "BUZZ_GEOMETRY_INDEX_SEQUENTIAL=$<BOOL:${BUZZ_GEOMETRY_INDEX_SEQUENTIAL}>"
    "BUZZ_GEOMETRY_INDEX_BATCHED=$<BOOL:${BUZZ_GEOMETRY_INDEX_BATCHED}>"
    "BUZZ_GEOMETRY_INDEX_CACHE=$<BOOL:${BUZZ_GEOMETRY_INDEX_CACHE}>"
    "BUZZ_GEOMETRY_HOST_COMMANDS=$<BOOL:${BUZZ_GEOMETRY_HOST_COMMANDS}>"
    "BUZZ_GEOMETRY_ZERO_VERTEX_PADDING=$<BOOL:${BUZZ_GEOMETRY_ZERO_VERTEX_PADDING}>"
    "BUZZ_GEOMETRY_CULL_COMPACT=$<BOOL:${BUZZ_GEOMETRY_CULL_COMPACT}>")
if(geometry_job_native_setup_available OR
   (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND BUZZ_STANDALONE AND PORT_SPU_ISA STREQUAL "avx2"))
    # The native setup uses the same u128 integer helpers as its lifted
    # callees. Keep their reviewed ISA/optimization flags; the PPU-wide /O1
    # and baseline ISA would turn byte shuffles into scalar loops here.
    set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_OPTIONS "${spu_flags}")
    if(PORT_NATIVE_SPU_FLOAT)
        set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1)
        set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_OPTIONS -mfma)
        if(MSVC)
            set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math)
        else()
            set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_OPTIONS -frounding-math)
        endif()
    endif()
    if(PORT_RTZ_RESULT_GUARD)
        set_property(SOURCE native_geometry_job_flow.cpp APPEND PROPERTY COMPILE_DEFINITIONS SPU_RTZ_RESULT_GUARD=1)
    endif()
endif()
if(PORT_IMAGE36_SOURCE)
    target_sources(${PROJECT_NAME} PRIVATE native_image36.cpp)
    if(ANDROID)
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_IMAGE36_NEON=1)
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
        set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
    endif()
    set_property(SOURCE job_corpus.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_NATIVE_IMAGE36=1)
    if(MSVC)
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
endif()
if(PORT_VIDEO_IDCT_SOURCE)
    target_sources(${PROJECT_NAME} PRIVATE native_video_idct.cpp)
    set_property(SOURCE "${video_idct_kernel}" APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT ANDROID)
        set_property(SOURCE "${video_idct_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
        set_property(SOURCE "${video_idct_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    endif()
    if(MSVC)
        set_property(SOURCE "${video_idct_kernel}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${video_idct_kernel}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
endif()
if(PORT_IMAGE7_SOURCE)
    target_sources(${PROJECT_NAME} PRIVATE native_image7.cpp)
    if(ANDROID AND PORT_IMAGE7_COMPARE)
        set_property(SOURCE native_image7.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_IMAGE7_COMPARE_DEFAULT=1)
    endif()
    # Only the kernel TU: native RTZ helpers + result guard, built for the
    # environment buzz_image7_run sets around each call.
    if(ANDROID)
        set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_IMAGE7_NEON=1)
        set_property(SOURCE "${image7_kernel}" APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
    else()
        set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
        set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    endif()
    if(MSVC)
        set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
endif()
set_source_files_properties(${host_edge_decompress} job_corpus.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zlib;${CMAKE_BINARY_DIR}/zlib")
target_link_libraries(${PROJECT_NAME} PRIVATE $<LINK_ONLY:zlibstatic>)

# The runtime is C++, and on Apple also Objective-C. Letting CMake infer the
# link language from the generated C would leave the C++ standard library out.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Windows system import libraries. dbghelp symbolizes crash-handler addresses
# against the PDB, user32 and gdi32 are what the backend's window and its GDI
# present are made of, and winmm is timeBeginPeriod. None of the four names
# anything a POSIX linker can find; the D3D12 backend's own imports come in
# through the #pragma comment(lib) in libs/video/rsx_d3d12_backend.c.
if(WIN32 OR BUZZ_EXPERIMENTAL_VULKAN)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzImGui.cmake")
    target_link_libraries(${PROJECT_NAME} PRIVATE buzz_game_imgui)
endif()
if(WIN32)
    # Keep native symbol addresses for resolving sampler RVAs. This changes
    # only the link, not the very large generated translation units.
    target_link_options(${PROJECT_NAME} PRIVATE /MAP)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dbghelp
        user32
        hid
        gdi32
        winmm
    )
endif()

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
        # The lift spells PPC arithmetic as signed C arithmetic, which must wrap;
        # without this, clang exploits overflow UB (e.g. folds "x*x < 0" to false).
        -fwrapv
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /wd4100 /wd4244 /wd4267
        /Zc:__cplusplus         # report correct __cplusplus value
        # ppu_recomp.h defines PPU_FENCE, the lowering of the guest's
        # sync/lwsync/eieio/isync, and in C that is <stdatomic.h>. The lifter
        # writes C++ by default, but --single-file writes ppu_recomp.c, and a
        # port that took it would not compile without this. The runtime's own
        # build already carries the same flag for the same reason.
        /experimental:c11atomics
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - /bigobj on MSVC. A single lifted file routinely passes the 65,536-section
#     limit of the default object format.
if(MSVC)
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
if(PORT_OPTIMIZE_PPU_HOT_PATHS)
    # /O1 was selected for compilation cost of the enormous generated TUs,
    # not for the small dispatch/VM bridge used by every guest memory store.
    # Keep wrap/no-alias/FP flags unchanged; do not edit generated sources.
    if(MSVC)
        set(ppu_hot_flag /O2)
    else()
        set(ppu_hot_flag -O2)
    endif()
    set_property(SOURCE ${PS3RECOMP_SCAFFOLD} APPEND PROPERTY COMPILE_OPTIONS ${ppu_hot_flag})
    # The shared runtime is small compared with generated PPU translations.
    # Optimize DMA, SPURS scheduling and vertex decode on every platform.
    target_compile_options(ps3recomp_runtime PRIVATE ${ppu_hot_flag})
    # Native sampling repeatedly finds the job-completion poll/select loops
    # at 0x26A8A0 / 0x26BEEC in this Quiz World 1.02 translation unit.
    if(PORT_COMPLETION_SOURCE)
        get_filename_component(ppu_hot_chunk "${PORT_COMPLETION_SOURCE}" NAME)
    elseif(PORT_HOST_PROFILE_DIR)
        set(ppu_hot_chunk ppu_recomp_013.cpp)
    else()
        set(ppu_hot_chunk ppu_recomp_015.cpp)
    endif()
    set_property(SOURCE "${RECOMP_DIR}/${ppu_hot_chunk}"
                 APPEND PROPERTY COMPILE_OPTIONS ${ppu_hot_flag})
    if(PORT_COMPLETION_SOURCE)
        set_property(SOURCE "${PORT_COMPLETION_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS ${ppu_hot_flag})
    endif()
    if(PORT_PRODUCER_PROBE_DIR)
        # Preserve the normal source-specific optimization level of each TU.
        set_property(SOURCE "${PORT_PRODUCER_PROBE_DIR}/ppu_recomp_015.cpp" APPEND PROPERTY COMPILE_OPTIONS ${ppu_hot_flag})
    endif()
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
