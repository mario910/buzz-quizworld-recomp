#!/bin/sh
# Run the outer-driver, output layout, and DMA publication fixtures under QEMU.
set -eu
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
trap 'rm -f -- "$out/test"; rmdir -- "$out"' EXIT

for fixture in driver publish output; do
    python3 "tools/codegen/generate-geometry-$fixture-test.py"
done

compile() {
    aarch64-linux-gnu-g++ -std=gnu++17 -D_Thread_local=thread_local -O2 \
        -frounding-math -ffp-contract=off -Ips3recomp/include -Ips3recomp/runtime/spu "$@" -o "$out/test"
    qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test"
}
compile -DBUZZ_GEOMETRY_LS_DRIVER=1 tests/geometry-driver-flow-test.cpp port/native_geometry_job_flow.cpp
compile tests/geometry-publish-test.cpp
compile tests/geometry-output-test.cpp
