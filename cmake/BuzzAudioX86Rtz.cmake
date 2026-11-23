# Experimental scoped checked audio arithmetic. Generated leaf kernels save
# and restore MXCSR; the rest of each SPU image retains its original helpers.
option(BUZZ_AUDIO_X86_RTZ "Use generated scoped checked x86 audio kernels" OFF)
if(BUZZ_AUDIO_X86_RTZ)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
       NOT CMAKE_C_COMPILER_ID MATCHES "^(Clang|GNU)$" OR
       NOT PORT_SPU_ISA MATCHES "^(avx2|avx512)$" OR
       NOT PORT_AUDIO_COMPRESSOR_SOURCE OR NOT PORT_AUDIO_MIXER_SOURCE OR NOT PORT_AUDIO_ATRAC_SOURCE)
        message(FATAL_ERROR "BUZZ_AUDIO_X86_RTZ requires checked x86 AVX2/FMA and all generated audio adapters")
    endif()
endif()
foreach(audio_source IN ITEMS "${PORT_AUDIO_COMPRESSOR_SOURCE}" "${PORT_AUDIO_MIXER_SOURCE}" "${PORT_AUDIO_ATRAC_SOURCE}")
    if(NOT audio_source)
        continue()
    endif()
    get_filename_component(audio_directory "${audio_source}" DIRECTORY)
    set(audio_kernel "${audio_directory}/kernel.c")
    file(READ "${audio_kernel}" audio_kernel_text)
    if(audio_kernel_text MATCHES "#define SPU_NATIVE_RTZ 1")
        if(NOT BUZZ_AUDIO_X86_RTZ)
            message(FATAL_ERROR "Regenerate ordinary audio kernels or enable BUZZ_AUDIO_X86_RTZ explicitly")
        endif()
        # Definitions must precede the forced spu_shuffle.h include.
        set_property(SOURCE "${audio_kernel}" APPEND PROPERTY COMPILE_DEFINITIONS SPU_NATIVE_RTZ=1 SPU_RTZ_RESULT_GUARD=1)
        if(MSVC)
            set_property(SOURCE "${audio_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma /clang:-frounding-math /clang:-ffp-contract=off)
        else()
            set_property(SOURCE "${audio_kernel}" APPEND PROPERTY COMPILE_OPTIONS -mfma -frounding-math -ffp-contract=off)
        endif()
    elseif(BUZZ_AUDIO_X86_RTZ)
        message(FATAL_ERROR "Regenerate all audio kernels with generate-audio-compressor.py --x86-rtz")
    endif()
endforeach()
