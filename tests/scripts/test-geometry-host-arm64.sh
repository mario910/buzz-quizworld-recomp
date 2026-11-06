#!/bin/sh
# Run in a prepared workspace containing the pinned private oracle fixtures.
# Arguments: the complete 19508-entry image41 corpus and an ARM64 libz.a.
set -eu
cd "$(dirname "$0")/../.."
corpus=${1:?Provide the complete image41 corpus directory}
zlib=${2:?Provide an AArch64 Linux libz.a archive}
out=$(mktemp -d)
trap 'rm -f -- "$out/test"; rmdir -- "$out"' EXIT
for name in unit-vectors half-input input-fields output streams input-job skin-job clip-job cull-job index-job vertex-job; do
    aarch64-linux-gnu-g++ -std=gnu++17 -O2 -UNDEBUG -D_Thread_local=thread_local \
        -frounding-math -ffp-contract=off -Ips3recomp/include -Ips3recomp/runtime/spu \
        -Ithird_party/zlib "tests/geometry-host-$name-test.cpp" "$zlib" -o "$out/test"
    case "$name" in
        *-job) qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test" "$corpus" ;;
        *) qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test" ;;
    esac
done
