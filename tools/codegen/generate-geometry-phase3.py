"""Build the pinned whole-driver audit and native outer-driver adapter."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "analysis/native-geometry/phase3"
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PARENT_PIN = "0cb78a3d3170bf81921021199fcd2144272858635224f3135208f76c76113d99"


def main():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != PIN:
        raise SystemExit("Unreviewed image-41 lift")
    parent_generator = ROOT / "tools/codegen/generate-geometry-phase2.py"
    if hashlib.sha256(parent_generator.read_bytes()).hexdigest() != PARENT_PIN:
        raise SystemExit("Unreviewed phase-2 adapter; audit before updating the parent pin")
    subprocess.run([sys.executable, str(parent_generator)], check=True)
    parent = ROOT / "analysis/native-geometry/phase2"
    source = (parent / "spu_recomp.c").read_text()
    if "#define BUZZ_GEOMETRY_IMPLEMENTED_MASK 6015" not in source:
        raise SystemExit("Expected all eleven validated phase-2 groups")
    counts = {}
    for old, new in (("spu_wrch", "buzz_geometry_job_wrch"),
                     ("spu_rdch", "buzz_geometry_job_rdch"),
                     ("spu_rchcnt", "buzz_geometry_job_rchcnt")):
        source, counts[old] = re.subn(rf"\b{old}\(", new + "(", source)
        if not counts[old]:
            raise SystemExit(f"Missing channel sites: {old}")
    source = re.sub(r"\bSPU_IB_DISPATCH\b", "buzz_geometry_job_indirect", source)
    source = source.replace("g_spu_trampoline_fn = spu_indirect_branch;",
                            "g_spu_trampoline_fn = buzz_geometry_job_indirect_target(ctx);")
    # Original interrupt thunks branch to r2+8, sometimes a lexical suffix
    # omitted from the lift's public entry table. Compile those exact suffixes
    # too, so strict BF8 replay never falls through to interpreted instructions.
    interior_entries = {0xde8: (0xde0, "        ctx->gpr[6] = spu_ilhu"),
                        0xee8: (0xee0, "        ctx->gpr[47] = spu_ila"),
                        0xf48: (0xf40, "        ctx->gpr[81] = spu_il"),
                        0x111c: (0x1114, "        ctx->gpr[11] = spu_il"),
                        0x1240: (0x1238, "        { ctx->pc = 0x1160"),
                        0x12c0: (0x12b8, "        { ctx->pc = 0x1018")}
    extra_functions = ""
    extra_table = ""
    for pc, (parent_pc, marker) in interior_entries.items():
        parent_name = f"buzz_job_6eba_spu_func_{parent_pc:08X}"
        match = re.search(rf"(?ms)^void {parent_name}\(spu_context\* ctx\) \{{(.*?)^\}}", source)
        if not match or match[1].count(marker) != 1:
            raise SystemExit(f"Unreviewed list/blend interior {pc:x}")
        suffix = match[1][match[1].index(marker):]
        extra_name = f"buzz_job_6eba_spu_func_{pc:08X}"
        if re.search(rf"\bvoid {extra_name}\(", source):
            raise SystemExit(f"Duplicate interior {pc:x}")
        extra_functions += f"static void {extra_name}(spu_context* ctx) {{\n{suffix}}}\n"
        extra_table += f'    {{ 0x{pc:08X}u, {extra_name}, "{extra_name}" }},\n'
    table_marker = "static const spu_func_entry spu_function_table[] = {"
    if source.count(table_marker) != 1:
        raise SystemExit("Unreviewed image-41 function table")
    source = source.replace(table_marker, extra_functions + "\n" + table_marker + "\n" + extra_table, 1)
    list_name = "buzz_job_6eba_spu_func_00000BF8"
    list_pattern = rf"(?ms)^void {list_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(list_pattern, source))) != 1:
        raise SystemExit("Expected one pinned list/blend root")
    list_library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in (0x7118, 0x7df8, 0x2780, 0x2c00, 0xd04))
    source = re.sub(list_pattern, lambda m:
        f"static const buzz_geometry_blend_list_functions {list_name}_library = {{{list_library}}};\n"
        f"static void {list_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {list_name}(spu_context* ctx) {{ buzz_geometry_job_blend_lists(ctx, {list_name}_reference, &{list_name}_library); }}",
        source, count=1)
    for command_pc, hook in ((0x32c8, "prepare_commands"), (0x27a0, "publish_commands")):
        command_name = f"buzz_job_6eba_spu_func_{command_pc:08X}"
        command_pattern = rf"(?ms)^void {command_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(command_pattern, source))) != 1:
            raise SystemExit("Expected one pinned command entry")
        source = re.sub(command_pattern, lambda m:
            f"static void {command_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {command_name}(spu_context* ctx) {{ buzz_geometry_job_{hook}(ctx, {command_name}_reference); }}",
            source, count=1)
    run_name = "buzz_job_6eba_spu_func_00007118"
    run_pattern = rf"(?ms)^void {run_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(run_pattern, source))) != 1:
        raise SystemExit("Expected one pinned run-list root")
    source = re.sub(run_pattern, lambda m:
        f"static void {run_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {run_name}(spu_context* ctx) {{ buzz_geometry_job_run_lists(ctx, {run_name}_reference, buzz_job_6eba_spu_func_00007298); }}",
        source, count=1)
    for command_pc, reset in ((0x20, 1), (0x100, 0)):
        command_name = f"buzz_job_6eba_spu_func_{command_pc:08X}"
        pattern = rf"(?ms)^void {command_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(pattern, source))) != 1:
            raise SystemExit("Expected one pinned binding/reset entry")
        source = re.sub(pattern, lambda m:
            f"static void {command_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {command_name}(spu_context* ctx) {{ buzz_geometry_job_command_bindings(ctx, {command_name}_reference, {reset}); }}",
            source, count=1)
    for command_pc, hook in ((0x2e8, "emit_position"), (0x12f0, "emit_full")):
        command_name = f"buzz_job_6eba_spu_func_{command_pc:08X}"
        pattern = rf"(?ms)^void {command_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(pattern, source))) != 1:
            raise SystemExit("Expected one pinned command emitter entry")
        command_library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in (0x32c8, 0x100, 0x20, 0x27a0))
        source = re.sub(pattern, lambda m:
            f"static const buzz_geometry_command_functions {command_name}_library = {{{command_library}}};\n"
            f"static void {command_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {command_name}(spu_context* ctx) {{ buzz_geometry_job_{hook}(ctx, {command_name}_reference, &{command_name}_library); }}",
            source, count=1)
    for streams, pc in enumerate((0xbd4c, 0xbe20, 0xbf7c), 1):
        direction_name = f"buzz_job_6eba_spu_func_{pc:08X}"
        direction_pattern = rf"(?ms)^void {direction_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(direction_pattern, source))) != 1:
            raise SystemExit("Expected one pinned skin direction entry")
        source = re.sub(direction_pattern, lambda m:
            f"static void {direction_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {direction_name}(spu_context* ctx) {{ buzz_geometry_job_skin_directions(ctx, {direction_name}_reference, buzz_job_6eba_spu_func_0000B868, {streams}); }}",
            source, count=1)
    weighted_name = "buzz_job_6eba_spu_func_0000B880"
    weighted_pattern = rf"(?ms)^void {weighted_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(weighted_pattern, source))) != 1:
        raise SystemExit("Expected one pinned phase-2 weighted skin entry")
    source = re.sub(weighted_pattern, lambda m:
        f"static void {weighted_name}_phase2(spu_context* ctx) {{{m[1]}}}\n"
        f"void {weighted_name}(spu_context* ctx) {{ buzz_geometry_job_weighted_skin(ctx, {weighted_name}_phase2, {weighted_name}_reference, buzz_job_6eba_spu_func_0000B4C0, buzz_job_6eba_spu_func_0000BB58); }}",
        source, count=1)
    # Separate the retained rigid data suffix from B348's native clamp/branch.
    rigid_name = "buzz_job_6eba_spu_func_0000B348"
    rigid_match = re.search(rf"(?ms)^void {rigid_name}\(spu_context\* ctx\) \{{(.*?)^\}}", source)
    rigid_marker = "        ctx->gpr[27] = spu_ilhu(0x8000);"
    if not rigid_match or rigid_match[1].count(rigid_marker) != 1:
        raise SystemExit("Unreviewed rigid data suffix")
    rigid_suffix = rigid_match[1][rigid_match[1].index(rigid_marker):]
    rigid_prefix = rigid_match[1][:rigid_match[1].index(rigid_marker)]
    source = source[:rigid_match.start()] + f"void {rigid_name}(spu_context* ctx) {{{rigid_prefix}        {rigid_name}_data(ctx);\n}}" + source[rigid_match.end():]
    flow_name = "buzz_job_6eba_spu_func_0000AF18"
    flow_pattern = rf"(?ms)^void {flow_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(flow_pattern, source))) != 1:
        raise SystemExit("Expected one pinned AF18 root")
    flow_library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in (0x2750,0x2778,0x2780,0x2790,0x2738,0xb880))
    direction_library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in (0xbd4c,0xbe20,0xbf7c,0xc14c,0xc280,0xb5a0))
    source = re.sub(flow_pattern, lambda m:
        f"static void {rigid_name}_data_reference(spu_context* ctx) {{{rigid_suffix}}}\n"
        f"static void {rigid_name}_data(spu_context* ctx) {{ buzz_geometry_job_rigid_skin(ctx, {rigid_name}_data_reference, buzz_job_6eba_spu_func_0000B4C0, buzz_job_6eba_spu_func_0000BB58); }}\n"
        f"static const buzz_geometry_skin_flow_functions {flow_name}_library = {{{flow_library}, {rigid_name}_data, {{{direction_library}}}, buzz_job_6eba_spu_func_0000B4C0, buzz_job_6eba_spu_func_0000B868}};\n"
        f"static void {flow_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {flow_name}(spu_context* ctx) {{ buzz_geometry_job_skin_flow(ctx, {flow_name}_reference, &{flow_name}_library); }}",
        source, count=1)
    skin_name = "buzz_job_6eba_spu_func_000009F8"
    skin_pattern = rf"(?ms)^void {skin_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(skin_pattern, source))) != 1:
        raise SystemExit("Expected one pinned skin/rigid controller")
    skin_library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in (0x2750, 0x2778, 0x2780, 0xc518, 0x2c00, 0xaf18))
    source = re.sub(skin_pattern, lambda m:
        f"static const buzz_geometry_skin_dispatch_functions {skin_name}_library = {{{skin_library}}};\n"
        f"static void {skin_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {skin_name}(spu_context* ctx) {{ buzz_geometry_job_skin_dispatch(ctx, {skin_name}_reference, &{skin_name}_library); }}",
        source, count=1)
    stream_name = "buzz_job_6eba_spu_func_000093F0"
    stream_pattern = rf"(?ms)^void {stream_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(stream_pattern, source))) != 1:
        raise SystemExit("Expected one pinned attribute stream entry")
    source = re.sub(stream_pattern, lambda m:
        f"static void {stream_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {stream_name}(spu_context* ctx) {{ buzz_geometry_job_decode_custom_stream(ctx, {stream_name}_reference, buzz_job_6eba_spu_func_00007DF8); }}",
        source, count=1)
    attribute_name = "buzz_job_6eba_spu_func_0000AC68"
    attribute_pattern = rf"(?ms)^void {attribute_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(attribute_pattern, source))) != 1:
        raise SystemExit("Expected one pinned attribute root")
    source = re.sub(attribute_pattern, lambda m:
        f"static const buzz_geometry_attribute_functions {attribute_name}_library = {{{stream_name}, buzz_job_6eba_spu_func_000040C8}};\n"
        f"static void {attribute_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {attribute_name}(spu_context* ctx) {{ buzz_geometry_job_decode_attributes(ctx, {attribute_name}_reference, &{attribute_name}_library); }}",
        source, count=1)
    color_name = "buzz_job_6eba_spu_func_000089B0"
    color_pattern = rf"(?ms)^void {color_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(color_pattern, source))) != 1:
        raise SystemExit("Expected one pinned U8 color codec entry")
    source = re.sub(color_pattern, lambda m:
        f"static void {color_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {color_name}(spu_context* ctx) {{ buzz_geometry_job_decode_color(ctx, {color_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    unit_name = "buzz_job_6eba_spu_func_00009114"
    unit_pattern = rf"(?ms)^void {unit_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(unit_pattern, source))) != 1:
        raise SystemExit("Expected one pinned unit-vector codec entry")
    source = re.sub(unit_pattern, lambda m:
        f"static void {unit_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {unit_name}(spu_context* ctx) {{ buzz_geometry_job_decode_unit_vectors(ctx, {unit_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    half_name = "buzz_job_6eba_spu_func_00008378"
    half_pattern = rf"(?ms)^void {half_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(half_pattern, source))) != 1:
        raise SystemExit("Expected one pinned half-stream codec entry")
    source = re.sub(half_pattern, lambda m:
        f"static void {half_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {half_name}(spu_context* ctx) {{ buzz_geometry_job_decode_half_stream(ctx, {half_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    float32_name = "buzz_job_6eba_spu_func_000087CC"
    float32_pattern = rf"(?ms)^void {float32_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(float32_pattern, source))) != 1:
        raise SystemExit("Expected one pinned float32-stream codec entry")
    source = re.sub(float32_pattern, lambda m:
        f"static void {float32_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {float32_name}(spu_context* ctx) {{ buzz_geometry_job_decode_float32_stream(ctx, {float32_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    fixed_name = "buzz_job_6eba_spu_func_00009058"
    fixed_pattern = rf"(?ms)^void {fixed_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(fixed_pattern, source))) != 1:
        raise SystemExit("Expected one pinned fixed-stream codec entry")
    source = re.sub(fixed_pattern, lambda m:
        f"static void {fixed_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {fixed_name}(spu_context* ctx) {{ buzz_geometry_job_decode_fixed_stream(ctx, {fixed_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    snorm_name = "buzz_job_6eba_spu_func_000085E8"
    snorm_pattern = rf"(?ms)^void {snorm_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(snorm_pattern, source))) != 1:
        raise SystemExit("Expected one pinned snorm-stream codec entry")
    source = re.sub(snorm_pattern, lambda m:
        f"static void {snorm_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {snorm_name}(spu_context* ctx) {{ buzz_geometry_job_decode_snorm_stream(ctx, {snorm_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    i16_name = "buzz_job_6eba_spu_func_00008B94"
    i16_pattern = rf"(?ms)^void {i16_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(i16_pattern, source))) != 1:
        raise SystemExit("Expected one pinned i16-stream codec entry")
    source = re.sub(i16_pattern, lambda m:
        f"static void {i16_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {i16_name}(spu_context* ctx) {{ buzz_geometry_job_decode_i16_stream(ctx, {i16_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    cmp_name = "buzz_job_6eba_spu_func_00008D34"
    cmp_pattern = rf"(?ms)^void {cmp_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(cmp_pattern, source))) != 1:
        raise SystemExit("Expected one pinned cmp-stream codec entry")
    source = re.sub(cmp_pattern, lambda m:
        f"static void {cmp_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {cmp_name}(spu_context* ctx) {{ buzz_geometry_job_decode_cmp_stream(ctx, {cmp_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    u8_integer_name = "buzz_job_6eba_spu_func_00008EA8"
    u8_integer_pattern = rf"(?ms)^void {u8_integer_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(u8_integer_pattern, source))) != 1:
        raise SystemExit("Expected one pinned u8_integer-stream codec entry")
    source = re.sub(u8_integer_pattern, lambda m:
        f"static void {u8_integer_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {u8_integer_name}(spu_context* ctx) {{ buzz_geometry_job_decode_u8_integer_stream(ctx, {u8_integer_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    blend_name = "buzz_job_6eba_spu_func_0000C750"
    blend_pattern = rf"(?ms)^void {blend_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(blend_pattern, source))) != 1:
        raise SystemExit("Expected one pinned weighted-scatter root")
    source = re.sub(blend_pattern, lambda m:
        f"static void {blend_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {blend_name}(spu_context* ctx) {{ buzz_geometry_job_blend_scatter(ctx, {blend_name}_reference, buzz_job_6eba_spu_func_0000C7A8); }}",
        source, count=1)
    descriptor_name = "buzz_job_6eba_spu_func_00007DF8"
    descriptor_pattern = rf"(?ms)^void {descriptor_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(descriptor_pattern, source))) != 1:
        raise SystemExit("Expected one pinned descriptor decoder entry")
    codec_functions = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in
        (0x85e8,0x87cc,0x8378,0x89b0,0x8b94,0x8d34,0x8ea8,0x9058,0x9114))
    source = re.sub(descriptor_pattern, lambda m:
        f"static const buzz_geometry_descriptor_functions {descriptor_name}_library = {{{{{codec_functions}}}, buzz_job_6eba_spu_func_0000C750, buzz_job_6eba_spu_func_000083C8, buzz_geometry_job_descriptor_dispatched}};\n"
        f"static void {descriptor_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {descriptor_name}(spu_context* ctx) {{ buzz_geometry_job_decode_descriptors(ctx, {descriptor_name}_reference, &{descriptor_name}_library); }}",
        source,count=1)
    name = "buzz_job_6eba_spu_func_00001C18"
    # Retain the public library wrappers and their per-kernel switches. No
    # original main-driver block is a continuation of the native path.
    entries = (0xac68, 0xbf8, 0x9f8, 0xaea0, 0x6608, 0x2b80, 0x37c0,
               0x2dc0, 0x54e8, 0x35f0, 0x12f0, 0x2e8, 0x32c8, 0x27a0)
    library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in entries)
    pattern = rf"(?ms)^void {name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    matches = list(re.finditer(pattern, source))
    if len(matches) != 1:
        raise SystemExit("Expected one pinned whole-driver root")
    source = re.sub(pattern, lambda m:
                    f"static const buzz_geometry_job_functions {name}_library = {{{library}}};\n"
                    f"static void {name}_native(spu_context* ctx) {{ "
                    f"buzz_geometry_job_native_start(ctx, &{name}_library); }}\n"
                    f"static void {name}_reference(spu_context* ctx) {{{m[1]}}}\n"
                    f"void {name}(spu_context* ctx) {{ "
                    f"buzz_geometry_job_run(ctx, {name}_reference, {name}_native, "
                    f"buzz_geometry_job_native_eligible); }}", source, count=1)
    support = f'#include "{(ROOT / "port/native_geometry_job.h").as_posix()}"\n'
    support += "#define BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE 1\n"
    support += "#define BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE 1\n"
    support += "#define BUZZ_GEOMETRY_JOB_DRIVER_ABI 23\n"
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "spu_recomp.c").write_text(source)
    (DEST / "spu_recomp.h").write_bytes((parent / "spu_recomp.h").read_bytes())
    print(f"Phase-3 reference/shadow adapter -> {DEST}; wrapped sites {counts}")


if __name__ == "__main__":
    main()
