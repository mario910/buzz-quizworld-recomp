#!/bin/bash
# AVX2/FMA host required. No game assets needed for arithmetic/routing tests.
# Optional full-state replay: PREPARED_WORKSPACE IMAGE7_CAPTURE_DIRECTORY.
set -euo pipefail
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
if (( $# != 0 && $# != 2 )); then
    echo "Usage: bash $0 [PREPARED_WORKSPACE IMAGE7_CAPTURE_DIRECTORY]" >&2
    exit 2
fi
out=${IMAGE7_TEST_OUT:-$(mktemp -d)}
mkdir -p -- "$out"
out=$(CDPATH='' cd -- "$out" && pwd)
if [[ -z ${IMAGE7_TEST_OUT:-} ]]; then trap 'rm -rf -- "$out"' EXIT; fi
cc=${CC:-clang}
cxx=${CXX:-clang++}
flags=(-mavx2 -mfma -fno-strict-aliasing -fwrapv -frounding-math -ffp-contract=off
       -DSPU_LS_DEBUG_HOOKS=0 -I"$root/port" -I"$root/ps3recomp/runtime/spu")
"$cxx" -std=c++17 -O3 "${flags[@]}" "$root/tests/image7-simd-color-test.cpp" -o "$out/color"
"$out/color"
"$cxx" -std=c++17 -O2 "${flags[@]}" -DBUZZ_IMAGE7_SIMD_AVX2=1 \
    "$root/tests/image7-simd-routing-test.cpp" "$root/port/native_image7.cpp" -o "$out/routing"
BUZZ_IMAGE7=1 BUZZ_IMAGE7_SIMD=1 "$out/routing"
BUZZ_IMAGE7=1 BUZZ_IMAGE7_SIMD=0 "$out/routing"
env -u BUZZ_IMAGE7_SIMD BUZZ_IMAGE7=1 "$out/routing"
"$cxx" -std=c++17 -O2 "${flags[@]}" -USPU_LS_DEBUG_HOOKS -DSPU_LS_DEBUG_HOOKS=1 \
    "$root/tests/image7-simd-disabled-test.cpp" -o "$out/probed"
"$out/probed"
"$cxx" -std=c++17 -O2 "${flags[@]}" -mno-avx2 -mno-fma \
    "$root/tests/image7-simd-disabled-test.cpp" -o "$out/isa-fallback"
"$out/isa-fallback"
if (( $# == 0 )); then
    echo "Captured-state comparison skipped: supply workspace and capture directory."
    exit 0
fi
workspace=$(CDPATH='' cd -- "$1" && pwd)
captures=$(CDPATH='' cd -- "$2" && pwd)
# Generate under the test output directory; never rewrite a prepared build.
mkdir -p "$out/generated/port"
ln -sfn "$workspace/port/spu" "$out/generated/port/spu"
python3 "$root/tools/codegen/generate-image7-native.py" --workspace "$out/generated"
generated="$out/generated/analysis/native-image7"
flags+=(-I"$generated")
"$cc" -std=gnu17 -O3 "${flags[@]}" -DSPU_NATIVE_RTZ=1 -DSPU_RTZ_RESULT_GUARD=1 \
    -include "$root/port/spu_shuffle.h" -c "$generated/kernel_8638.c" -o "$out/kernel.o"
"$cc" -std=gnu17 -O2 "${flags[@]}" -c "$generated/oracle_8638.c" -o "$out/oracle.o"
"$cxx" -std=c++17 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "${flags[@]}" -DBUZZ_IMAGE7_SIMD_AVX2=1 "$root/tests/image7-simd-test.cpp" \
    "$root/port/native_image7.cpp" "$root/port/native_image7_simd.cpp" \
    "$out/kernel.o" "$out/oracle.o" -o "$out/states"
BUZZ_IMAGE7=1 BUZZ_IMAGE7_SIMD=1 "$out/states" "$captures"
