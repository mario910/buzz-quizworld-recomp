# Native image 38 (port/native_image38.cpp): the serial chain's small gather
# jobs, fully on the answer-reveal critical path. The lifted entry is renamed
# in its own translation unit and kept as the fallback; the native file takes
# over the registered entry symbol. Pinned to the lifted program it mirrors.
# Runtime switch: BUZZ_IMAGE38=0.
option(BUZZ_IMAGE38_NATIVE "Native image 38 gather job" ON)
if(NOT BUZZ_IMAGE38_NATIVE OR NOT SPU_LIFTED)
    return()
endif()
set(buzz_image38_source "${SPU_DIR}/buzz_job_39d9/spu_recomp.c")
list(FIND SPU_LIFTED "${buzz_image38_source}" buzz_image38_index)
if(buzz_image38_index LESS 0)
    message(STATUS "Native image 38: lifted buzz_job_39d9 not in this build")
    return()
endif()
file(SHA256 "${buzz_image38_source}" buzz_image38_sha)
if(NOT buzz_image38_sha STREQUAL "00fe5f4f232b2824c477a43c478d2c24c32cbfd502fa3421969f056874213474")
    message(WARNING "Native image 38: lifted buzz_job_39d9 changed (${buzz_image38_sha}); native job disabled")
    return()
endif()
set_property(SOURCE "${buzz_image38_source}" APPEND PROPERTY COMPILE_DEFINITIONS
    buzz_job_39d9_spu_func_00000010=buzz_job_39d9_spu_func_00000010_retained)
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native_image38.cpp")
message(STATUS "Native image 38: enabled")
