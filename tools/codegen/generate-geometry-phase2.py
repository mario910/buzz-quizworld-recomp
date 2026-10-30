"""Pinned native geometry adapters; derived reference code stays private."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba"
DEST = ROOT / "analysis/native-geometry/phase2"
PINS = {"5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"}
# Initialized by 0x7FD8 and tables D1D0..D220/DA20 in the pinned lift.
# Restrict the specialization, rather than changing the meaning of an SPU op.
UNIT24 = {
    45: (0x00010203,) * 4,
    85: (0, 0x3f800000, 0x3f800000, 0),
    86: (0xf0, 0x3ff, 0x3ff, 0),
    87: (0xfffffffc, 0xfffffffa, 0xfffffffc, 0),
    88: (0x7fffffff, 0x7fffffff, 0x7fffffff, 0),
    89: (0, 0, 0, 0x0c0d0e0f),
    96: (0x08080808, 0x08080808, 0x08080808, 0),
    97: (0x80000000, 0x80000000, 0x80000000, 0xffffffff),
    101: (0xc6353150,) * 4, 102: (0x46352e7c,) * 4,
    108: (0x03030303,) * 4,
    109: (0x04050607, 0x08090a0b, 0x14151617, 0x18191a1b),
    110: (0x3f800000,) * 4,
    112: (0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f),
}
SKIN = {
    38: (0x00010203,) * 4,
    39: (0x08090a0b, 0x00010203, 0x04050607, 0x0c0d0e0f),
    43: (0x04050607, 0x08090a0b, 0x00010203, 0x0c0d0e0f),
    51: (0xffffffff, 0xffffffff, 0xffffffff, 0), 55: (0x3f800000,) * 4,
    77: (0x1c1d1e1f, 0x04050607, 0x08090a0b, 0x80808080),
    81: (0x80808080, 0x0c0d0e0f, 0x1c1d1e1f, 0x80808080),
    87: (0x0c0d0e0f,) * 4, 88: (0x08090a0b,) * 4, 89: (0x04050607,) * 4,
    90: (0x3b808080,) * 4,
    94: (0x80808008, 0x8080800a, 0x8080800c, 0x8080800e),
    95: (0x80808000, 0x80808002, 0x80808004, 0x80808006),
}
HALF = {
    33: (0x00010405, 0x08090c0d, 0x10111415, 0x18191c1d),
    37: (0x80008000,) * 4, 38: (0x38000000,) * 4,
    56: (0x47804780,) * 4, 57: (0x38003800,) * 4, 58: (0x7bff7bff,) * 4,
}


def constant_declarations(values=UNIT24):
    return "\n".join(f"    u128 constant{r}; " + " ".join(f"constant{r}._u32[{i}] = {w:#x}u;" for i, w in enumerate(words)) for r, words in values.items())


def constants_guard(name="unit24", values=UNIT24):
    guard = f"static int buzz_{name}_constants_match(const spu_context* ctx) {{\n" + constant_declarations(values) + "\n"
    guard += "    __m128i mismatch = _mm_setzero_si128();\n"
    guard += "\n".join(f"    mismatch = _mm_or_si128(mismatch, _mm_xor_si128(_mm_loadu_si128((const __m128i*)&ctx->gpr[{r}]), _mm_loadu_si128((const __m128i*)&constant{r})));" for r in values)
    return guard + "\n    return _mm_testz_si128(mismatch, mismatch);\n}\n"


def main():
    raw = (SOURCE / "spu_recomp.c").read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest not in PINS:
        raise SystemExit("Unreviewed phase-2 lift; audit before adding a pin")
    subprocess.run([sys.executable, str(ROOT / "tools/codegen/generate-geometry-stagef.py")], check=True)
    source = (ROOT / "analysis/stagef-geometry/spu_recomp.c").read_text()
    support = '\n'.join(f'#include "{(ROOT / "port" / name).as_posix()}"' for name in ("native_geometry_shuffle.h", "native_geometry_ls.h"))
    support += '\nextern void buzz_geometry_run(spu_context*, void (*)(spu_context*), int (*)(spu_context*), unsigned);\n'
    support += 'extern int buzz_geometry_debug;\nextern unsigned buzz_geometry_active;\n'
    support += '#define BUZZ_GEOMETRY_IMPLEMENTED_MASK 7\n'
    support += constants_guard()
    support += f'#include "{(ROOT / "port/native_geometry_unit24.h").as_posix()}"\n'
    support += constants_guard("skin", SKIN)
    support += f'#include "{(ROOT / "port/native_geometry_skin.h").as_posix()}"\n'
    support += constants_guard("half", HALF)
    support += f'#include "{(ROOT / "port/native_geometry_half.h").as_posix()}"\n'
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
    bodies = {}
    for pc in ("9148", "92A0", "B880", "B934", "B940", "4AB0"):
        name = "buzz_job_6eba_spu_func_0000" + pc
        pattern = rf"(?ms)^void {name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        match = re.search(pattern, source)
        if not match: raise SystemExit(f"Missing reviewed block {pc}")
        body = match[1]
        if set(re.findall(r"ctx->(\w+)", body)) - {"gpr", "pc"} or re.search(r"spu_(?:wrch|rdch|stop|drain|halt)", body):
            raise SystemExit(f"Unreviewed side effect in {pc}")
        bodies[pc] = body
    name = "buzz_job_6eba_spu_func_00009148"
    setup = f"static void {name}_setup(spu_context* ctx) {{{bodies['9148']}}}\n"
    def functions(tail):
        return (f"static __attribute__((noinline)) void {name}_reference(spu_context* ctx) {{ do {{ {name}_setup(ctx); {tail}(ctx); }} while (ctx->pc == 0x9148); }}\n"
                f"static __attribute__((noinline)) int {name}_native(spu_context* ctx) {{ return buzz_unit24_native(ctx, {name}_setup, {tail}); }}\n")
    wrapper = f"void {name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 1)) {{ {name}_setup(ctx); return; }} buzz_geometry_run(ctx, {name}_reference, {name}_native, 0); }}"
    source = re.sub(rf"(?ms)^void {name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: setup + functions("buzz_job_6eba_spu_func_000092A0") + wrapper, source, count=1)
    skin_name = "buzz_job_6eba_spu_func_0000B880"
    skin_setup = f"static void {skin_name}_setup(spu_context* ctx) {{{bodies['B880']}}}\n"
    def skin_functions(tail, step):
        return (f"static __attribute__((noinline)) void {skin_name}_reference(spu_context* ctx) {{ {skin_name}_setup(ctx); {tail}(ctx); while (ctx->pc == 0xB934) {{ {step}(ctx); {tail}(ctx); }} }}\n"
                f"static __attribute__((noinline)) int {skin_name}_native(spu_context* ctx) {{ return buzz_skin_native(ctx, {skin_name}_setup, {tail}); }}\n")
    skin_wrapper = f"void {skin_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 2)) {{ {skin_name}_setup(ctx); return; }} buzz_geometry_run(ctx, {skin_name}_reference, {skin_name}_native, 1); }}"
    source = re.sub(rf"(?ms)^void {skin_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: skin_setup + skin_functions("buzz_job_6eba_spu_func_0000B940", "buzz_job_6eba_spu_func_0000B934") + skin_wrapper, source, count=1)
    half_name = "buzz_job_6eba_spu_func_00004AB0"
    half_reference = f"static __attribute__((noinline)) void {half_name}_reference(spu_context* ctx) {{{bodies['4AB0']}}}\n"
    half_native = f"static __attribute__((noinline)) int {half_name}_native(spu_context* ctx) {{ return buzz_half_native(ctx, {half_name}_reference); }}\n"
    half_wrapper = f"void {half_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 4)) {{ {half_name}_reference(ctx); return; }} buzz_geometry_run(ctx, {half_name}_reference, {half_name}_native, 2); }}"
    source = re.sub(rf"(?ms)^void {half_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: half_reference + half_native + half_wrapper, source, count=1)
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "spu_recomp.c").write_text(source)
    (DEST / "spu_recomp.h").write_bytes((SOURCE / "spu_recomp.h").read_bytes())
    seed = "static void unit24_seed_constants(spu_context* ctx) {\n" + constant_declarations() + "\n"
    seed += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in UNIT24) + "\n}\n"
    test_tail = "buzz_job_6eba_spu_func_000092A0_reference"
    tests = constants_guard() + f'#include "{(ROOT / "port/native_geometry_unit24.h").as_posix()}"\n'
    tests += f"static void {test_tail}(spu_context* ctx) {{{bodies['92A0']}}}\n" + setup + functions(test_tail)
    (DEST / "test-kernels.inc").write_text(tests + seed)
    skin_tests = constants_guard("skin", SKIN) + f'#include "{(ROOT / "port/native_geometry_skin.h").as_posix()}"\n'
    for pc in ("B934", "B940"):
        skin_tests += f"static void buzz_job_6eba_spu_func_0000{pc}_reference(spu_context* ctx) {{{bodies[pc]}}}\n"
    skin_tests += skin_setup + skin_functions("buzz_job_6eba_spu_func_0000B940_reference", "buzz_job_6eba_spu_func_0000B934_reference")
    skin_tests += "static void skin_seed_constants(spu_context* ctx) {\n" + constant_declarations(SKIN) + "\n"
    skin_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in SKIN) + "\n}\n"
    (DEST / "test-skin-kernels.inc").write_text(skin_tests)
    half_tests = constants_guard("half", HALF) + f'#include "{(ROOT / "port/native_geometry_half.h").as_posix()}"\n' + half_reference + half_native
    half_tests += "static void half_seed_constants(spu_context* ctx) {\n" + constant_declarations(HALF) + "\n"
    half_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in HALF) + "\n}\n"
    (DEST / "test-half-kernels.inc").write_text(half_tests)
    (DEST / "contracts.txt").write_text(digest + "\n9148/92A0: bit 0; AVX2 unit-vector decoder, exact reference final column\nB880/B934/B940: bit 1; AVX2 skinning prefix, exact reference final pair\n4AB0: bit 2; AVX2 half encoder, exact reference final iteration\n" + "\n".join(f"{pc}: body SHA256 {hashlib.sha256(body.encode()).hexdigest()}" for pc, body in bodies.items()) + "\n")
    print(f"Generated phase-2 unit24, skin and half groups -> {DEST}")


if __name__ == "__main__":
    main()
