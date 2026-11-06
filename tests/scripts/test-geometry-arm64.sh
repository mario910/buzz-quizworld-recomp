#!/bin/sh
# Run the phase-2 geometry fixtures against the exact lift under QEMU.
set -eu
cd "$(dirname "$0")/../.."
python3 tools/codegen/generate-geometry-phase2.py
out=$(mktemp -d)
trap 'rm -f -- "$out/test" "$out/stagef.o" "$out/oracle.o" "$out/drain.c" "$out/drain.o" "$out/spu_recomp.c" "$out/spu_recomp.h"; rmdir -- "$out"' EXIT
for kernel in fill float3 triangles emit indices pointers half half-decode compact skin phase2; do
    aarch64-linux-gnu-g++ -std=gnu++17 -O2 -frounding-math -ffp-contract=off \
        -include initializer_list -Ips3recomp/include -Ips3recomp/runtime/spu \
        "tests/geometry-$kernel-test.cpp" -o "$out/test"
    qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test"
done
aarch64-linux-gnu-gcc -std=gnu17 -O2 -Ips3recomp/include -Ips3recomp/runtime/spu \
    tests/geometry-shuffle-plan-test.c -o "$out/test"
qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test"

python3 tools/codegen/generate-geometry-phase2.py --arm64
for name in spu_recomp.c spu_recomp.h; do
    sed -e 's/buzz_job_6eba_spu_/oracle_/g' -e 's/buzz_job_6eba_register/oracle_register/g' \
        "port/spu/buzz_job_6eba/$name" > "$out/$name"
done
stagef_cc() {
    aarch64-linux-gnu-gcc -O2 -ffunction-sections -fdata-sections \
        -DSPU_LS_DEBUG_HOOKS=0 -Iport -Ips3recomp/include -Ips3recomp/runtime/spu "$@"
}
stagef_cc -frounding-math -ffp-contract=off -c analysis/native-geometry/arm64/spu_recomp.c -o "$out/stagef.o"
stagef_cc -DSPU_SCALAR_HELPERS -c "$out/spu_recomp.c" -o "$out/oracle.o"
cat ps3recomp/runtime/spu/spu_drain.c port/spu_pure_loop_runtime.inc > "$out/drain.c"
stagef_cc -Dspu_drain_call=sdk_drain_call_for_test -Dspu_img_restore=sdk_img_restore_for_test -c "$out/drain.c" -o "$out/drain.o"
stagef_cc -DTEST_SPU_PURE_LOOPS -Wl,--gc-sections tests/stagef-arm64-test.c "$out/stagef.o" "$out/oracle.o" "$out/drain.o" -o "$out/test"
qemu-aarch64 -L /usr/aarch64-linux-gnu "$out/test" analysis/raw-reference/buzz_job_6eba.bin
