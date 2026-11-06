#!/bin/sh
# Run the generated Image 36 leaf against the exact reference under QEMU.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
workspace=${1:?Usage: sh tests/scripts/test-image36-arm64.sh PREPARED_WORKSPACE}
workspace=$(CDPATH='' cd -- "$workspace" && pwd)
out=$(mktemp -d)
trap 'rm -f -- "$out/reference.o" "$out/kernel.o" "$out/oracle.o" "$out/test.o" "$out/owner.o" "$out/test"; rmdir -- "$out"' EXIT
cd "$workspace"
python3 "$root/tools/codegen/generate-image36-native.py" --workspace "$workspace"
for name in reference kernel oracle; do
    flag=
    if [ "$name" = kernel ]; then flag=-DBUZZ_IMAGE36_NEON; fi
    aarch64-linux-gnu-gcc -DSPU_LS_DEBUG_HOOKS=0 -std=gnu17 -O2 -frounding-math -ffp-contract=off \
        -I"$root/port" -Ips3recomp/runtime/spu $flag -c "analysis/native-image36/${name}_06d8.c" -o "$out/$name.o"
done
aarch64-linux-gnu-gcc -std=gnu17 -O2 -I"$root/port" -Ips3recomp/runtime/spu \
    -c "$root/tests/image36-native-test.c" -o "$out/test.o"
aarch64-linux-gnu-g++ -std=gnu++17 -O2 -I"$root/port" -Ips3recomp/runtime/spu \
    -c "$root/port/native_image36.cpp" -o "$out/owner.o"
aarch64-linux-gnu-g++ "$out"/*.o -o "$out/test"
qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test"
