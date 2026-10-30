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
option(BUZZ_EXPERIMENTAL_VULKAN "Developer-only Vulkan game build; keep outside build-windows" OFF)
if(BUZZ_EXPERIMENTAL_VULKAN)
    if(NOT WIN32)
        message(FATAL_ERROR "Buzz Vulkan bring-up currently targets Windows; Linux title integration is separate")
    endif()
    if(CMAKE_BINARY_DIR MATCHES "[/\\]build-windows$")
        message(FATAL_ERROR "Use build-vulkan-windows: do not replace the tested D3D12 build")
    endif()
    set(PS3RECOMP_RSX_VULKAN ON CACHE BOOL "" FORCE)
    set(PS3RECOMP_FETCH_VULKAN_DEPS ON CACHE BOOL "" FORCE)
endif()
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    # The generator discovers handlers from library definitions. Rebuild when
    # an existing module changes, and reconfigure when a new module is added.
    file(GLOB_RECURSE HLE_NID_SOURCES CONFIGURE_DEPENDS
        "${PS3RECOMP_DIR}/libs/*.c")
    # A removed input cannot make the output older than a remaining dependency.
    # Track the list itself so deletions also remove stale handler references.
    set(HLE_NID_SOURCE_LIST "${CMAKE_CURRENT_BINARY_DIR}/hle_nid_sources.txt")
    file(GENERATE OUTPUT "${HLE_NID_SOURCE_LIST}" CONTENT "${HLE_NID_SOURCES}\n")
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
                ${HLE_NID_SOURCES} "${HLE_NID_SOURCE_LIST}"
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
set(PORT_IMAGE36_SOURCE "" CACHE FILEPATH "Image36 lift with scoped native 0x06D8 SIMD (tools/codegen/generate-image36-native.py)")
set(PORT_MSTREAM_DIR "" CACHE PATH "Lifted runtime-loaded MultiStream modules (tools/codegen/generate-mstream-modules.py); empty interprets them")
set(PORT_GEOMETRY_SOURCE "" CACHE FILEPATH "Experimental generated geometry lift; empty uses reference")
set(native_geometry "${SPU_DIR}/buzz_job_6eba/spu_recomp.c")
if(PORT_ENABLE_SPU AND PORT_SPU_INPUTS)
    include("${PS3RECOMP_DIR}/cmake/PS3RecompSPU.cmake")
    ps3recomp_generate_spu("${CMAKE_CURRENT_BINARY_DIR}/generated_spu" ${PORT_SPU_INPUTS})
    list(APPEND SPU_SOURCES ${SPU_GENERATED_SOURCES}
         "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
elseif(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(PORT_IMAGE39_SOURCE)
        if(NOT EXISTS "${PORT_IMAGE39_SOURCE}" OR NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
            message(FATAL_ERROR "Image 39 experiment requires generated source and checked AVX2/FMA build")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/buzz_job_c0bc/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE39_SOURCE}")
    endif()
    if(PORT_IMAGE40_SOURCE)
        if(NOT EXISTS "${PORT_IMAGE40_SOURCE}" OR NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
            message(FATAL_ERROR "Image 40 experiment requires generated source and checked AVX2/FMA build")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/buzz_job_abe1/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE40_SOURCE}")
    endif()
    if(PORT_IMAGE36_SOURCE)
        get_filename_component(image36_dir "${PORT_IMAGE36_SOURCE}" DIRECTORY)
        set(image36_kernel "${image36_dir}/kernel_06d8.c")
        if(NOT EXISTS "${PORT_IMAGE36_SOURCE}" OR NOT EXISTS "${image36_kernel}" OR NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
            message(FATAL_ERROR "Image36 leaf requires generated source and checked AVX2/FMA build")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/buzz_job_14b4/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE36_SOURCE}" "${image36_kernel}")
    endif()
    if(PORT_IMAGE7_SOURCE)
        get_filename_component(image7_dir "${PORT_IMAGE7_SOURCE}" DIRECTORY)
        set(image7_kernel "${image7_dir}/kernel_8638.c")
        if(NOT EXISTS "${PORT_IMAGE7_SOURCE}" OR NOT EXISTS "${image7_kernel}" OR NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$")
            message(FATAL_ERROR "Image 7 kernel requires tools/codegen/generate-image7-native.py output and an AVX2/FMA build")
        endif()
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/spu_0006_at_007E7500/spu_recomp.c")
        list(REMOVE_ITEM SPU_LIFTED "${SPU_DIR}/spu_0006_at_007FF780/spu_recomp.c")
        list(APPEND SPU_LIFTED "${PORT_IMAGE7_SOURCE}" "${image7_kernel}")
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
add_executable(${PROJECT_NAME}
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
option(PORT_SPU_LS_DEBUG_HOOKS "Keep SPU_LS_WATCH/SPU_SMC_WATCH probes in lifted SPU images" ON)
if(SPU_SOURCES AND NOT PORT_SPU_LS_DEBUG_HOOKS)
    set_property(SOURCE ${SPU_SOURCES} APPEND PROPERTY COMPILE_DEFINITIONS SPU_LS_DEBUG_HOOKS=0)
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
if(PORT_IMAGE39_SOURCE)
    if(PORT_SPU_INPUTS)
        message(FATAL_ERROR "Image 39 adapter requires the pinned pre-generated SPU images")
    endif()
    set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    if(MSVC)
        set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${PORT_IMAGE39_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
    set_property(SOURCE "${PS3RECOMP_DIR}/runtime/spu/spurs_job.c" TARGET_DIRECTORY ps3recomp_runtime
                 APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ_IMAGE39=39)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
endif()
if(PORT_IMAGE40_SOURCE)
    if(PORT_SPU_INPUTS)
        message(FATAL_ERROR "Image 40 adapter requires the pinned pre-generated SPU images")
    endif()
    set_property(SOURCE "${PORT_IMAGE40_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    if(MSVC)
        set_property(SOURCE "${PORT_IMAGE40_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${PORT_IMAGE40_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
    set_property(SOURCE "${PS3RECOMP_DIR}/runtime/spu/spurs_job.c" TARGET_DIRECTORY ps3recomp_runtime
                 APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ_EXTRA_IMAGE=40)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
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
target_sources(${PROJECT_NAME} PRIVATE ${host_edge_decompress} job_corpus.cpp native_stagef.cpp)
if(PORT_IMAGE36_SOURCE)
    target_sources(${PROJECT_NAME} PRIVATE native_image36.cpp)
    set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
    set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
    set_property(SOURCE job_corpus.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_NATIVE_IMAGE36=1)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS BUZZ_SPU_NATIVE_RTZ=1)
    if(MSVC)
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_OPTIONS /clang:-frounding-math /clang:-ffp-contract=off)
    else()
        set_property(SOURCE "${image36_kernel}" APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
    endif()
endif()
if(PORT_IMAGE7_SOURCE)
    target_sources(${PROJECT_NAME} PRIVATE native_image7.cpp)
    # Only the kernel TU: native RTZ helpers + result guard, built for the
    # environment buzz_image7_run sets around each call.
    set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
    set_property(SOURCE "${image7_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma)
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
if(WIN32)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/BuzzImGui.cmake")
    target_link_libraries(${PROJECT_NAME} PRIVATE buzz_game_imgui)
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
option(PORT_OPTIMIZE_PPU_HOT_PATHS "Optimize PPU bridge and profiled job-poll TU" ON)
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
