"""Pinned native geometry adapters; derived reference code stays private."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba"
DEST = ROOT / "analysis/native-geometry/phase2"
PINS = {"5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"}

def port_include(name):
    # Production adapters compile inside the game target, whose include path
    # has port/; generated source carries no checkout's absolute path.
    return f'#include "{name}"'


def test_include(name):
    # Private test oracles are included from tests/*.cpp without port/ on the
    # include path, so they name the header relative to their own directory.
    return '#include "' + os.path.relpath(ROOT / "port" / name, DEST).replace("\\", "/") + '"'

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
DEHALF = {
    2: (0x00010203,) * 4, 15: (0x70000000,) * 4, 16: (0x38000000,) * 4,
    21: (0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f),
    22: (0x03030303,) * 4, 23: (0x00018080, 0x02038080, 0x04058080, 0x06078080),
    45: (0x80000000,) * 4,
}
POINTERS = {
    14: (0, 15, 0, 15), 16: (0, 0x7fff, 0, 0),
    17: (0x04050607, 0x80808080, 0x0c0d0e0f, 0x80808080),
}
FLOAT3 = {
    15: (0x0c0d0e0f, 0x10111213, 0x14151617, 0x18191a1b),
    17: (0x08090a0b, 0x0c0d0e0f, 0x10111213, 0x14151617),
    18: (0x04050607, 0x08090a0b, 0x0c0d0e0f, 0x10111213),
    21: (0x3f800000,) * 4, 22: (0, 0, 0, 0xffffffff),
}
EMIT = {
    45: (0xfff0,) * 4, 47: (0x00010203,) * 4, 48: (12,) * 4,
    49: (0x10111213, 0x14151617, 0x18191a1b, 0x1c1d1e1f),
    54: (0, 4, 8, 12), 63: (0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f),
}
TRIANGLES = {
    36: (0xf000,) * 4, 37: (0xf00,) * 4, 38: (0xaaaa,) * 4,
    83: (0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f),
    85: (0x03030303,) * 4,
}
COMPACT = {
    36: (0xfffffffb, 0xfffffffe, 0, 0),
    46: (0x0d04051c, 0x1d14150c, 0x0d04051c, 0x1d14150c),
    50: (0x3ff800,) * 4,
    51: (0x08090001, 0x18191011, 0x08090001, 0x18191011),
    45: (0xf000,) * 4, 48: (4,) * 4,
    49: (0x10111213, 0x14151617, 0x18191a1b, 0x1c1d1e1f),
    54: (0, 4, 8, 12), 62: (0x00010203,) * 4,
    63: (0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f),
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
    support = '\n'.join(port_include(name) for name in ("native_geometry_shuffle.h", "native_geometry_ls.h"))
    support += '\nextern void buzz_geometry_run(spu_context*, void (*)(spu_context*), int (*)(spu_context*), unsigned);\n'
    support += 'extern int buzz_geometry_debug;\nextern unsigned buzz_geometry_active;\n'
    support += '#define BUZZ_GEOMETRY_IMPLEMENTED_MASK 6015\n'
    support += constants_guard()
    support += port_include("native_geometry_unit24.h") + "\n"
    support += constants_guard("skin", SKIN)
    support += port_include("native_geometry_skin.h") + "\n"
    support += constants_guard("half", HALF)
    support += port_include("native_geometry_half.h") + "\n"
    support += constants_guard("dehalf", DEHALF)
    support += port_include("native_geometry_half_decode.h") + "\n"
    support += constants_guard("pointers", POINTERS)
    support += port_include("native_geometry_pointers.h") + "\n"
    support += port_include("native_geometry_fill.h") + "\n"
    support += constants_guard("float3", FLOAT3)
    support += port_include("native_geometry_float3.h") + "\n"
    support += constants_guard("emit", EMIT)
    support += port_include("native_geometry_emit.h") + "\n"
    support += constants_guard("triangles", TRIANGLES)
    support += port_include("native_geometry_triangles.h") + "\n"
    support += port_include("native_geometry_indices.h") + "\n"
    support += constants_guard("compact", COMPACT)
    support += port_include("native_geometry_compact.h") + "\n"
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
    bodies = {}
    for pc in ("9148", "92A0", "B880", "B934", "B940", "4AB0", "CAD8", "CB78", "0988", "09E0", "28F0", "97A8", "46FC", "4750", "4808", "47F0", "7A90", "78A8", "5008", "51E0", "50C8", "51C8"):
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
    dehalf_name = "buzz_job_6eba_spu_func_0000CAD8"
    dehalf_setup = f"static void {dehalf_name}_setup(spu_context* ctx) {{{bodies['CAD8']}}}\n"
    def dehalf_functions(tail):
        # Consume each queued branch like spu_drain_call. SPU_RET deliberately
        # leaves the trampoline untouched when returning through a host frame.
        return (f"static __attribute__((noinline)) void {dehalf_name}_reference(spu_context* ctx) {{ do {{ g_spu_trampoline_fn = 0; {dehalf_name}_setup(ctx); g_spu_trampoline_fn = 0; {tail}(ctx); }} while (ctx->pc == 0xCAD8); }}\n"
                f"static __attribute__((noinline)) int {dehalf_name}_native(spu_context* ctx) {{ return buzz_dehalf_native(ctx, {dehalf_name}_reference); }}\n")
    dehalf_wrapper = f"void {dehalf_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 32)) {{ {dehalf_name}_setup(ctx); return; }} buzz_geometry_run(ctx, {dehalf_name}_reference, {dehalf_name}_native, 5); }}"
    source = re.sub(rf"(?ms)^void {dehalf_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: dehalf_setup + dehalf_functions("buzz_job_6eba_spu_func_0000CB78") + dehalf_wrapper, source, count=1)
    pointers_name = "buzz_job_6eba_spu_func_00000988"
    pointers_setup = f"static void {pointers_name}_setup(spu_context* ctx) {{{bodies['0988']}}}\n"
    def pointers_functions(step):
        return (f"static __attribute__((noinline)) void {pointers_name}_reference(spu_context* ctx) {{ do {{ g_spu_trampoline_fn = 0; {pointers_name}_setup(ctx); g_spu_trampoline_fn = 0; {step}(ctx); }} while (ctx->pc == 0x988); }}\n"
                f"static __attribute__((noinline)) int {pointers_name}_native(spu_context* ctx) {{ return buzz_pointers_native(ctx, {pointers_name}_reference); }}\n")
    pointers_wrapper = f"void {pointers_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 1024)) {{ {pointers_name}_setup(ctx); return; }} buzz_geometry_run(ctx, {pointers_name}_reference, {pointers_name}_native, 10); }}"
    source = re.sub(rf"(?ms)^void {pointers_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: pointers_setup + pointers_functions("buzz_job_6eba_spu_func_000009E0") + pointers_wrapper, source, count=1)
    fill_name = "buzz_job_6eba_spu_func_000028F0"
    fill_reference = f"static __attribute__((noinline)) void {fill_name}_reference(spu_context* ctx) {{{bodies['28F0']}}}\n"
    fill_native = f"static __attribute__((noinline)) int {fill_name}_native(spu_context* ctx) {{ return buzz_fill_native(ctx, {fill_name}_reference); }}\n"
    fill_wrapper = f"void {fill_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 4096)) {{ {fill_name}_reference(ctx); return; }} buzz_geometry_run(ctx, {fill_name}_reference, {fill_name}_native, 12); }}"
    source = re.sub(rf"(?ms)^void {fill_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: fill_reference + fill_native + fill_wrapper, source, count=1)
    float3_name = "buzz_job_6eba_spu_func_000097A8"
    float3_reference = f"static __attribute__((noinline)) void {float3_name}_reference(spu_context* ctx) {{{bodies['97A8']}}}\n"
    float3_native = f"static __attribute__((noinline)) int {float3_name}_native(spu_context* ctx) {{ return buzz_float3_native(ctx, {float3_name}_reference); }}\n"
    float3_wrapper = f"void {float3_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 256)) {{ {float3_name}_reference(ctx); return; }} buzz_geometry_run(ctx, {float3_name}_reference, {float3_name}_native, 8); }}"
    source = re.sub(rf"(?ms)^void {float3_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: float3_reference + float3_native + float3_wrapper, source, count=1)
    emit_name = "buzz_job_6eba_spu_func_000046FC"
    emit_setup = f"static void {emit_name}_setup(spu_context* ctx) {{{bodies['46FC']}}}\n"
    def emit_functions(straddled, aligned, step):
        return (f"static __attribute__((noinline)) void {emit_name}_reference(spu_context* ctx) {{ do {{ g_spu_trampoline_fn = 0; {emit_name}_setup(ctx); g_spu_trampoline_fn = 0; if (ctx->pc == 0x4750) {straddled}(ctx); else {aligned}(ctx); g_spu_trampoline_fn = 0; {step}(ctx); }} while (ctx->pc == 0x46FC); }}\n"
                f"static __attribute__((noinline)) int {emit_name}_native(spu_context* ctx) {{ return buzz_emit_native(ctx, {emit_name}_reference); }}\n")
    emit_wrapper = f"void {emit_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 512)) {{ {emit_name}_setup(ctx); return; }} buzz_geometry_run(ctx, {emit_name}_reference, {emit_name}_native, 9); }}"
    source = re.sub(rf"(?ms)^void {emit_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: emit_setup + emit_functions("buzz_job_6eba_spu_func_00004750", "buzz_job_6eba_spu_func_00004808", "buzz_job_6eba_spu_func_000047F0") + emit_wrapper, source, count=1)
    triangles_name = "buzz_job_6eba_spu_func_00007A90"
    triangles_reference = f"static __attribute__((noinline)) void {triangles_name}_reference(spu_context* ctx) {{{bodies['7A90']}}}\n"
    triangles_native = f"static __attribute__((noinline)) int {triangles_name}_native(spu_context* ctx) {{ return buzz_triangles_native(ctx, {triangles_name}_reference); }}\n"
    triangles_wrapper = f"void {triangles_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 8)) {{ {triangles_name}_reference(ctx); return; }} buzz_geometry_run(ctx, {triangles_name}_reference, {triangles_name}_native, 3); }}"
    source = re.sub(rf"(?ms)^void {triangles_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: triangles_reference + triangles_native + triangles_wrapper, source, count=1)
    indices_name = "buzz_job_6eba_spu_func_000078A8"
    indices_reference = f"static __attribute__((noinline)) void {indices_name}_reference(spu_context* ctx) {{{bodies['78A8']}}}\n"
    indices_native = f"static __attribute__((noinline)) int {indices_name}_native(spu_context* ctx) {{ return buzz_index_native(ctx, {indices_name}_reference); }}\n"
    indices_wrapper = f"void {indices_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 64)) {{ {indices_name}_reference(ctx); return; }} buzz_geometry_run(ctx, {indices_name}_reference, {indices_name}_native, 6); }}"
    source = re.sub(rf"(?ms)^void {indices_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: indices_reference + indices_native + indices_wrapper, source, count=1)
    compact_name = "buzz_job_6eba_spu_func_00005008"
    compact_setup = f"static void {compact_name}_setup(spu_context* ctx) {{{bodies['5008']}}}\n"
    def compact_functions(aligned, straddled, step):
        return (f"static __attribute__((noinline)) void {compact_name}_reference(spu_context* ctx) {{ do {{ g_spu_trampoline_fn = 0; {compact_name}_setup(ctx); g_spu_trampoline_fn = 0; if (ctx->pc == 0x51E0) {aligned}(ctx); else {straddled}(ctx); g_spu_trampoline_fn = 0; {step}(ctx); }} while (ctx->pc == 0x5008); }}\n"
                f"static __attribute__((noinline)) int {compact_name}_native(spu_context* ctx) {{ return buzz_compact_native(ctx, {compact_name}_reference); }}\n")
    compact_wrapper = f"void {compact_name}(spu_context* ctx) {{ if (!(buzz_geometry_active & 16)) {{ {compact_name}_setup(ctx); return; }} buzz_geometry_run(ctx, {compact_name}_reference, {compact_name}_native, 4); }}"
    source = re.sub(rf"(?ms)^void {compact_name}\(spu_context\* ctx\) \{{.*?^\}}", lambda m: compact_setup + compact_functions("buzz_job_6eba_spu_func_000051E0", "buzz_job_6eba_spu_func_000050C8", "buzz_job_6eba_spu_func_000051C8") + compact_wrapper, source, count=1)
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "spu_recomp.c").write_text(source)
    (DEST / "spu_recomp.h").write_bytes((SOURCE / "spu_recomp.h").read_bytes())
    seed = "static void unit24_seed_constants(spu_context* ctx) {\n" + constant_declarations() + "\n"
    seed += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in UNIT24) + "\n}\n"
    test_tail = "buzz_job_6eba_spu_func_000092A0_reference"
    tests = constants_guard() + test_include("native_geometry_unit24.h") + "\n"
    tests += f"static void {test_tail}(spu_context* ctx) {{{bodies['92A0']}}}\n" + setup + functions(test_tail)
    (DEST / "test-kernels.inc").write_text(tests + seed)
    skin_tests = constants_guard("skin", SKIN) + test_include("native_geometry_skin.h") + "\n"
    for pc in ("B934", "B940"):
        skin_tests += f"static void buzz_job_6eba_spu_func_0000{pc}_reference(spu_context* ctx) {{{bodies[pc]}}}\n"
    skin_tests += skin_setup + skin_functions("buzz_job_6eba_spu_func_0000B940_reference", "buzz_job_6eba_spu_func_0000B934_reference")
    skin_tests += "static void skin_seed_constants(spu_context* ctx) {\n" + constant_declarations(SKIN) + "\n"
    skin_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in SKIN) + "\n}\n"
    (DEST / "test-skin-kernels.inc").write_text(skin_tests)
    half_tests = constants_guard("half", HALF) + test_include("native_geometry_half.h") + "\n" + half_reference + half_native
    half_tests += "static void half_seed_constants(spu_context* ctx) {\n" + constant_declarations(HALF) + "\n"
    half_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in HALF) + "\n}\n"
    (DEST / "test-half-kernels.inc").write_text(half_tests)
    dehalf_tests = constants_guard("dehalf", DEHALF) + test_include("native_geometry_half_decode.h") + "\n"
    dehalf_tests += f"static void buzz_job_6eba_spu_func_0000CB78_reference(spu_context* ctx) {{{bodies['CB78']}}}\n"
    dehalf_tests += dehalf_setup + dehalf_functions("buzz_job_6eba_spu_func_0000CB78_reference")
    dehalf_tests += "static void dehalf_seed_constants(spu_context* ctx) {\n" + constant_declarations(DEHALF) + "\n"
    dehalf_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in DEHALF) + "\n}\n"
    (DEST / "test-dehalf-kernels.inc").write_text(dehalf_tests)
    pointers_tests = constants_guard("pointers", POINTERS) + test_include("native_geometry_pointers.h") + "\n"
    pointers_tests += f"static void buzz_job_6eba_spu_func_000009E0_reference(spu_context* ctx) {{{bodies['09E0']}}}\n"
    pointers_tests += pointers_setup + pointers_functions("buzz_job_6eba_spu_func_000009E0_reference")
    pointers_tests += "static void pointers_seed_constants(spu_context* ctx) {\n" + constant_declarations(POINTERS) + "\n"
    pointers_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in POINTERS) + "\n}\n"
    (DEST / "test-pointers-kernels.inc").write_text(pointers_tests)
    (DEST / "test-fill-kernels.inc").write_text(test_include("native_geometry_fill.h") + "\n" + fill_reference + fill_native)
    float3_tests = constants_guard("float3", FLOAT3) + test_include("native_geometry_float3.h") + "\n" + float3_reference + float3_native
    float3_tests += "static void float3_seed_constants(spu_context* ctx) {\n" + constant_declarations(FLOAT3) + "\n"
    float3_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in FLOAT3) + "\n}\n"
    (DEST / "test-float3-kernels.inc").write_text(float3_tests)
    emit_tests = constants_guard("emit", EMIT) + test_include("native_geometry_emit.h") + "\n"
    for pc in ("4750", "4808", "47F0"):
        emit_tests += f"static void buzz_job_6eba_spu_func_0000{pc}_reference(spu_context* ctx) {{{bodies[pc]}}}\n"
    emit_tests += emit_setup + emit_functions("buzz_job_6eba_spu_func_00004750_reference", "buzz_job_6eba_spu_func_00004808_reference", "buzz_job_6eba_spu_func_000047F0_reference")
    emit_tests += "static void emit_seed_constants(spu_context* ctx) {\n" + constant_declarations(EMIT) + "\n"
    emit_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in EMIT) + "\n}\n"
    (DEST / "test-emit-kernels.inc").write_text(emit_tests)
    triangles_tests = constants_guard("triangles", TRIANGLES) + test_include("native_geometry_triangles.h") + "\n" + triangles_reference + triangles_native
    triangles_tests += "static void triangles_seed_constants(spu_context* ctx) {\n" + constant_declarations(TRIANGLES) + "\n"
    triangles_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in TRIANGLES) + "\n}\n"
    (DEST / "test-triangles-kernels.inc").write_text(triangles_tests)
    (DEST / "test-indices-kernels.inc").write_text(test_include("native_geometry_indices.h") + "\n" + indices_reference + indices_native)
    compact_tests = constants_guard("compact", COMPACT) + test_include("native_geometry_compact.h") + "\n"
    for pc in ("51E0", "50C8", "51C8"):
        compact_tests += f"static void buzz_job_6eba_spu_func_0000{pc}_reference(spu_context* ctx) {{{bodies[pc]}}}\n"
    compact_tests += compact_setup + compact_functions("buzz_job_6eba_spu_func_000051E0_reference", "buzz_job_6eba_spu_func_000050C8_reference", "buzz_job_6eba_spu_func_000051C8_reference")
    compact_tests += "static void compact_seed_constants(spu_context* ctx) {\n" + constant_declarations(COMPACT) + "\n"
    compact_tests += "\n".join(f"    ctx->gpr[{r}] = constant{r};" for r in COMPACT) + "\n}\n"
    (DEST / "test-compact-kernels.inc").write_text(compact_tests)
    (DEST / "contracts.txt").write_text(digest + "\n9148/92A0: bit 0; AVX2 unit-vector decoder, exact reference final column\nB880/B934/B940: bit 1; AVX2 skinning prefix, exact reference final pair\n4AB0: bit 2; AVX2 half encoder, exact reference final iteration\nCAD8/CB78: bit 5; AVX2 half decoder, exact reference final column\n0988/09E0: bit 10; input pointer table, exact reference final entry\n28F0: bit 12; BE word fill, exact reference final word\n97A8: bit 8; raw float3 decoder, exact reference final group\n46FC/4750/4808/47F0: bit 9; raw float3 output, exact reference final column\n7A90: bit 3; triangle stitching, exact reference final iteration\n78A8: bit 6; index expansion, exact reference final iteration\n5008/51E0/51C8: bit 4; packed 11/11/10 output, exact reference final column\n" + "\n".join(f"{pc}: body SHA256 {hashlib.sha256(body.encode()).hexdigest()}" for pc, body in bodies.items()) + "\n")
    print(f"Generated phase-2 geometry kernels -> {DEST}")


if __name__ == "__main__":
    main()
