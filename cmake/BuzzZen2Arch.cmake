# Steam Deck build: compile every translation unit -- the lifted PPU and SPU
# code included, which otherwise targets baseline x86-64 / plain AVX2 -- for
# the Deck's Zen 2 CPU. Adds BMI1/2, LZCNT, POPCNT, MOVBE, F16C and FMA.
# Floating-point contraction stays disabled everywhere (-ffp-contract=off is
# on the same command lines); explicit fma()/fmaf() calls are exactly rounded
# with or without the FMA instruction. The resulting executable needs a Zen 2
# or newer CPU, so this is tied to the Deck-only BUZZ_ZEN2_TUNE profile.
option(BUZZ_ZEN2_ARCH "Compile for the Steam Deck CPU (-march=znver2)" ${BUZZ_ZEN2_TUNE})
if(NOT BUZZ_ZEN2_ARCH)
    return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
   NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU)$" OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|GNU)$")
    message(FATAL_ERROR "BUZZ_ZEN2_ARCH requires Linux x86-64 and Clang or GCC")
endif()
target_compile_options(ps3recomp_runtime PRIVATE -march=znver2)
target_compile_options(${PROJECT_NAME} PRIVATE -march=znver2)
message(STATUS "Zen 2 target: -march=znver2 for the runtime and every game translation unit")
