#!/bin/sh
# Exact complete-state comparisons. No game/firmware files enter the repo.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
workspace=${1:?Usage: sh tests/scripts/test-audio-dsp-host.sh PREPARED_EU100_WORKSPACE}
workspace=$(CDPATH='' cd -- "$workspace" && pwd)
out=$(mktemp -d)
trap 'rm -f -- "$out/test" "$out/wrapper" "$out/atrac"; rmdir -- "$out"' EXIT
python3 "$root/tools/codegen/generate-audio-compressor.py" --workspace "$workspace" --include-mixer --include-atrac
"${CC:-cc}" -O2 -g -ffp-contract=off -DSPU_LS_DEBUG_HOOKS=0 -DSPU_LS_FAST=1 \
    -fsanitize=address,undefined -I"$workspace/ps3recomp/runtime/spu" -I"$workspace/ps3recomp/include" \
    "$workspace/analysis/native-audio-compressor/kernel.c" \
    "$workspace/analysis/native-audio-compressor/oracle.c" \
    "$workspace/analysis/native-audio-compressor/baseline.c" \
    "$workspace/analysis/native-audio-mixer/kernel.c" \
    "$workspace/analysis/native-audio-mixer/oracle.c" \
    "$workspace/analysis/native-audio-mixer/baseline.c" \
    "$root/tests/audio-compressor-test.c" -lm -o "$out/test"
"$out/test"
"${CC:-cc}" -O2 -g -ffp-contract=off -DSPU_LS_DEBUG_HOOKS=0 -DSPU_LS_FAST=1 \
    -fsanitize=address,undefined -I"$workspace/ps3recomp/runtime/spu" -I"$workspace/ps3recomp/include" \
    "$workspace/analysis/native-audio-atrac/kernel.c" "$workspace/analysis/native-audio-atrac/oracle.c" \
    "$root/tests/audio-atrac-test.c" -lm -o "$out/atrac"
"$out/atrac"
"${CXX:-c++}" -O2 -g -fsanitize=address,undefined \
    -I"$workspace/ps3recomp/runtime/spu" -I"$workspace/ps3recomp/include" \
    "$root/tests/audio-dsp-wrapper-test.cpp" "$root/port/native_audio_compressor.cpp" -o "$out/wrapper"
for mode in 0 1 2; do "$out/wrapper" "$mode"; done
