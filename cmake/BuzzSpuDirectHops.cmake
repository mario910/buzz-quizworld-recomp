# Direct tail calls between lifted SPU fragments (ps3recomp
# runtime/spu/spu_direct_hops.h) for the lifted job sources whose every
# function is a fragment. Sources that include native adapters after
# spu_helpers.h qualify once those headers bracket themselves with
# push_macro/pop_macro("return"). Runtime switch: SPU_DIRECT_HOPS=0.
option(BUZZ_SPU_DIRECT_HOPS "Direct tail calls between lifted SPU job fragments" ${BUZZ_ZEN2_TUNE})
if(NOT BUZZ_SPU_DIRECT_HOPS OR NOT SPU_LIFTED)
    return()
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "BUZZ_SPU_DIRECT_HOPS requires Clang (musttail)")
endif()
set(buzz_direct_hop_jobs "/port/spu/buzz_job_[0-9a-f]+/spu_recomp\\.c$")
set(buzz_direct_hop_native "/analysis/(native-image36|native-image39-kernels|native-video-idct|native-audio-atrac|native-audio-compressor|native-audio-mixer|native-image7)/spu_recomp\\.c$")
set(buzz_direct_hop_modules "/analysis/mstream-modules/[a-z0-9_]+/spu_recomp\\.c$")
set(buzz_direct_hop_count 0)
foreach(buzz_spu_source ${SPU_LIFTED})
    if(buzz_spu_source MATCHES "${buzz_direct_hop_jobs}" OR buzz_spu_source MATCHES "${buzz_direct_hop_native}" OR
       buzz_spu_source MATCHES "${buzz_direct_hop_modules}")
        set_property(SOURCE "${buzz_spu_source}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_DIRECT_HOPS=1)
        math(EXPR buzz_direct_hop_count "${buzz_direct_hop_count}+1")
    endif()
endforeach()
message(STATUS "SPU direct hops: ${buzz_direct_hop_count} lifted source(s)")
