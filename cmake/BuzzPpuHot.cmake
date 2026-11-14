# Steam Deck critical path: the PPU side of the frame is serial (main thread and
# one pool worker alternate with the SPURS chain), so its lifted code counts
# one-for-one in frame time.
#   - PPU_INLINE_STORES: inline guest-store fast path of ppu_recomp.h in every
#     lifted PPU TU (runtime switch: PPU_NO_INLINE_STORES=1).
#   - -O2 for the few TUs that hold ~95% of the sampled PPU time (menu and
#     question rounds); the rest keep the target-wide -O1 for build time.
# Measured on the Deck (2026-11-14): calls disappear from the profile, but PPU
# thread time and FPS do not move (menu 36.4 vs 36.7); kept as an opt-in.
option(BUZZ_PPU_HOT "Inline PPU stores and -O2 for the profiled hot lifted PPU TUs" OFF)
if(NOT BUZZ_PPU_HOT)
    return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|GNU)$")
    message(FATAL_ERROR "BUZZ_PPU_HOT requires Linux and Clang or GCC")
endif()
file(READ "${RECOMP_DIR}/ppu_recomp.h" buzz_ppu_header)
if(NOT buzz_ppu_header MATCHES "PPU_INLINE_STORES")
    message(FATAL_ERROR "ppu_recomp.h predates the inline store fast path; lift again with the current ppu_lifter.py")
endif()
set(buzz_ppu_hot_pattern "ppu_recomp_(012|013|014|015|016|017|018|019|025|026|027|028|029|030)\\.cpp$")
set(BUZZ_PPU_HOT_OPT "-O2" CACHE STRING "Optimization level of the hot lifted PPU TUs")
foreach(buzz_ppu_source ${RECOMP_SOURCES})
    if(buzz_ppu_source MATCHES "ppu_recomp_[0-9]+\\.cpp$")
        set_property(SOURCE "${buzz_ppu_source}" APPEND PROPERTY COMPILE_DEFINITIONS PPU_INLINE_STORES=1)
        if(buzz_ppu_source MATCHES "${buzz_ppu_hot_pattern}")
            set_property(SOURCE "${buzz_ppu_source}" APPEND PROPERTY COMPILE_OPTIONS ${BUZZ_PPU_HOT_OPT} -mtune=znver2)
        endif()
    endif()
endforeach()
