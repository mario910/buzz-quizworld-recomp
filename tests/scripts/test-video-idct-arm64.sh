#!/bin/sh
# Full decoder-leaf state comparison against exact SPU arithmetic.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
workspace=${1:?Usage: sh tests/scripts/test-video-idct-arm64.sh PREPARED_WORKSPACE}
workspace=$(CDPATH='' cd -- "$workspace" && pwd)
out=$(mktemp -d)
trap 'rm -f -- "$out/kernel.o" "$out/oracle.o" "$out/test"; rmdir -- "$out"' EXIT
cd "$workspace"
python3 "$root/tools/codegen/generate-video-idct.py" --workspace "$workspace"
for name in kernel oracle; do
    aarch64-linux-gnu-gcc -O2 -frounding-math -ffp-contract=off -DSPU_LS_DEBUG_HOOKS=0 \
        -I"$root/port" -Ips3recomp/runtime/spu -c "analysis/native-video-idct/$name.c" -o "$out/$name.o"
done
aarch64-linux-gnu-g++ -O2 -frounding-math -ffp-contract=off -I"$root/port" -Ips3recomp/runtime/spu \
    "$root/tests/video-idct-test.cpp" "$out/kernel.o" "$out/oracle.o" -o "$out/test"
qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test"
